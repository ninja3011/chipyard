// Portable, libc-free INT8 transformer inference (llama2.c architecture:
// RMSNorm, RoPE, grouped-query attention, SwiGLU FFN, shared classifier).
// Weights are the per-row-quantized blob from quantize.py. The ONLY backend
// switch is mv_acc(): a plain-C integer matvec on the host (to validate the
// port and the quantization) or the 8x8 tile engine (USE_ACCEL) on the board.
// Everything else -- norms, RoPE, softmax, attention, sampling -- runs on the
// core in float, identically on both.
#ifndef LLM_CORE_H
#define LLM_CORE_H
#include <stdint.h>
#include <stdint.h>

/* Model selection: default TinyStories-260K; -DMODEL_15M for TinyStories-15M.
 * Every dimension is a multiple of 8 (the tile edge) after padding. */
#ifdef MODEL_15M
#include "stories15M_i8_sections.h"
#define DIM     288
#define HID_P   768
#define LAYERS  6
#define HEADS   6
#define NKV     6
#define HS      48           /* head size = DIM / HEADS */
#define KVD     288          /* NKV * HS */
#define VOCAB   32000
#define MAXPOS  128
#define ATT_SCALE 0.14433757f /* 1/sqrt(48) */
#ifndef LORA_CLIP
#define LORA_CLIP 1.0f        /* grad-norm clip */
#endif
#define LORA_ADAM 1          /* plain SGD is unstable at this scale */
#else
#include "stories260K_i8_sections.h"
#define DIM     64
#define HID_P   176
#define LAYERS  5
#define HEADS   8
#define NKV     4
#define HS      8            /* head size = DIM / HEADS */
#define KVD     32           /* NKV * HS */
#define VOCAB   512
#define MAXPOS  256
#define ATT_SCALE 0.35355339f /* 1/sqrt(8) */
#ifndef LORA_CLIP
#define LORA_CLIP 1e30f       /* effectively off: 260K trains fine without it */
#endif
#endif

#ifdef USE_ACCEL
#include "../int8_gemm.h"
#endif

typedef struct {
  const float *rms_att, *rms_ffn, *rms_final, *rope_cos, *rope_sin;
  const uint32_t *tok_off; const char *tok_data;
  const int8_t *embed_q; const float *embed_s;
  const int8_t *wq, *wk, *wv, *wo, *w1, *w3, *w2;
  const float *sq, *sk, *sv, *so, *s1, *s3, *s2;
} Model;

static inline const void *sec(const uint8_t *blob, int i) {
  const uint32_t *t = (const uint32_t *)blob + 16;
  return blob + t[2 * i];
}
static void model_init(Model *m, const uint8_t *blob) {
  m->rms_att = sec(blob, SEC_RMS_ATT); m->rms_ffn = sec(blob, SEC_RMS_FFN);
  m->rms_final = sec(blob, SEC_RMS_FINAL);
  m->rope_cos = sec(blob, SEC_ROPE_COS); m->rope_sin = sec(blob, SEC_ROPE_SIN);
  m->tok_off = sec(blob, SEC_TOK_OFF); m->tok_data = sec(blob, SEC_TOK_DATA);
  m->embed_q = sec(blob, SEC_EMBED_Q); m->embed_s = sec(blob, SEC_EMBED_S);
  m->wq = sec(blob, SEC_WQ_Q); m->sq = sec(blob, SEC_WQ_S);
  m->wk = sec(blob, SEC_WK_Q); m->sk = sec(blob, SEC_WK_S);
  m->wv = sec(blob, SEC_WV_Q); m->sv = sec(blob, SEC_WV_S);
  m->wo = sec(blob, SEC_WO_Q); m->so = sec(blob, SEC_WO_S);
  m->w1 = sec(blob, SEC_W1_Q); m->s1 = sec(blob, SEC_W1_S);
  m->w3 = sec(blob, SEC_W3_Q); m->s3 = sec(blob, SEC_W3_S);
  m->w2 = sec(blob, SEC_W2_Q); m->s2 = sec(blob, SEC_W2_S);
}

// ---- state -----------------------------------------------------------------
static float x_[DIM], xb_[DIM], xb2_[DIM], q_[DIM], k_[KVD], v_[KVD];
static float hb_[HID_P], hb2_[HID_P], att_[MAXPOS], logits_[VOCAB];
static float kcache_[LAYERS][MAXPOS][KVD], vcache_[LAYERS][MAXPOS][KVD];
// Engine operand/result buffers: B is [K][8] (only column 0 used -- a
// matvec is a matmul with N=1 padded to the 8-wide tile), C is [O][8] int32
// (O up to VOCAB for the classifier, hence VOCAB*8 -- HID_P*8 overflowed).
static int8_t  xb_q[HID_P * 8] __attribute__((aligned(8)));
static int32_t acc_[VOCAB * 8] __attribute__((aligned(8)));
static uint64_t g_mv_calls;
#ifdef USE_ACCEL
static int g_use_accel = 1;   /* runtime: 1 = tile engine, 0 = plain C on the core */
#endif

static float my_expf(float x) {
  if (x < -87.f) return 0.f;
  if (x > 88.f) x = 88.f;
  float t = x * 1.44269504f;
  int n = (int)(t + (t >= 0 ? 0.5f : -0.5f));
  float r = x - n * 0.693145751953125f - n * 1.42860677e-06f;
  float p = 1.f + r * (1.f + r * (0.5f + r * (0.16666667f + r * (0.041666668f +
            r * (0.008333334f + r * 0.0013888889f)))));
  union { float f; uint32_t u; } s; s.u = (uint32_t)(n + 127) << 23;
  return p * s.f;
}

// y[O] = (W[O][K] int8 * scale_row) . x[K]; x is quantized per-tensor here.
#ifndef DBG
#define DBG(c) ((void)0)
#endif
static void mv(const int8_t *W, const float *ws, int O, int K, const float *x, float *y) {
  float mx = 0.f;
  for (int i = 0; i < K; i++) { float a = x[i] < 0 ? -x[i] : x[i]; if (a > mx) mx = a; }
  DBG('a');
  float sx = mx > 0.f ? mx / 127.f : 1.f, inv = 1.f / sx;
  for (int i = 0; i < K; i++) {
    float v = x[i] * inv;
    xb_q[i * 8] = (int8_t)(int)(v + (v >= 0 ? 0.5f : -0.5f));
  }
#ifdef USE_ACCEL
  if (g_use_accel) {
    DBG('g');
    acc_gemm(O, 8, K, W, K, 0, xb_q, 8, 0, acc_, 8, 0, 0, 0);
    DBG('G');
  } else
#endif
  {
    for (int o = 0; o < O; o++) {
      int32_t s = 0;
      for (int k = 0; k < K; k++) s += (int32_t)W[o * K + k] * (int32_t)xb_q[k * 8];
      acc_[o * 8] = s;
    }
  }
  for (int o = 0; o < O; o++) y[o] = (float)acc_[o * 8] * ws[o] * sx;
  g_mv_calls++;
}

static void rmsnorm(float *o, const float *x, const float *w, int n) {
  float ss = 0.f;
  for (int i = 0; i < n; i++) ss += x[i] * x[i];
  ss = 1.0f / __builtin_sqrtf(ss / n + 1e-5f);
  for (int i = 0; i < n; i++) o[i] = w[i] * (ss * x[i]);
}


// ---- LoRA on the LAST layer's FFN down-projection (w2) ---------------------
// y = W2 hb + lora_scale * B (A hb): A is [R][HID_P], B is [DIM][R]. W2 stays
// frozen INT8 on the engine; only A and B (float) are trained.
#define LORA_R 8
static int   g_lora_on = 0;
static float lora_A[LORA_R][HID_P], lora_B[DIM][LORA_R];
static float lora_scale = 2.0f;                 /* alpha / r = 16 / 8 */
static float hb_last_[HID_P], xpre_[DIM], ah_[LORA_R];
#ifdef LORA_ADAM
static float mA_[LORA_R][HID_P], vA_[LORA_R][HID_P], mB_[DIM][LORA_R], vB_[DIM][LORA_R];
static float adam_pb1 = 1.f, adam_pb2 = 1.f;
#endif

static float my_logf(float x) {   /* natural log, for loss reporting only */
  union { float f; uint32_t u; } v; v.f = x;
  int e = (int)((v.u >> 23) & 255) - 127;
  v.u = (v.u & 0x7fffff) | 0x3f800000;
  float m = v.f, t = (m - 1.f) / (m + 1.f), t2 = t * t;
  float l = 2.f * t * (1.f + t2 * (1.f / 3 + t2 * (0.2f + t2 * (1.f / 7 + t2 * (1.f / 9)))));
  return l + e * 0.6931472f;
}

// One transformer step; leaves next-token logits in logits_.
static void forward(const Model *m, int token, int pos) {
  DBG('F');
  for (int i = 0; i < DIM; i++) x_[i] = (float)m->embed_q[token * DIM + i] * m->embed_s[token];
  DBG('E');
  for (int l = 0; l < LAYERS; l++) {
    rmsnorm(xb_, x_, m->rms_att + l * DIM, DIM);
    DBG('N');
    mv(m->wq + l * DIM * DIM, m->sq + l * DIM, DIM, DIM, xb_, q_);
    mv(m->wk + l * KVD * DIM, m->sk + l * KVD, KVD, DIM, xb_, k_);
    mv(m->wv + l * KVD * DIM, m->sv + l * KVD, KVD, DIM, xb_, v_);
    for (int i = 0; i < DIM; i += 2) {
      int hd = (i % HS) / 2;
      float fcr = m->rope_cos[pos * (HS / 2) + hd], fci = m->rope_sin[pos * (HS / 2) + hd];
      float a = q_[i], b = q_[i + 1];
      q_[i] = a * fcr - b * fci; q_[i + 1] = a * fci + b * fcr;
      if (i < KVD) {
        a = k_[i]; b = k_[i + 1];
        k_[i] = a * fcr - b * fci; k_[i + 1] = a * fci + b * fcr;
      }
    }
    for (int i = 0; i < KVD; i++) { kcache_[l][pos][i] = k_[i]; vcache_[l][pos][i] = v_[i]; }
    for (int h = 0; h < HEADS; h++) {
      const float *qh = q_ + h * HS; int kvh = h / (HEADS / NKV);
      float mx = -1e30f;
      for (int t = 0; t <= pos; t++) {
        const float *kk = &kcache_[l][t][kvh * HS];
        float s = 0.f;
        for (int i = 0; i < HS; i++) s += qh[i] * kk[i];
        s *= ATT_SCALE;
        att_[t] = s; if (s > mx) mx = s;
      }
      float sum = 0.f;
      for (int t = 0; t <= pos; t++) { att_[t] = my_expf(att_[t] - mx); sum += att_[t]; }
      float inv = 1.f / sum;
      float *out = xb2_ + h * HS;
      for (int i = 0; i < HS; i++) out[i] = 0.f;
      for (int t = 0; t <= pos; t++) {
        const float *vv = &vcache_[l][t][kvh * HS]; float a = att_[t] * inv;
        for (int i = 0; i < HS; i++) out[i] += a * vv[i];
      }
    }
    mv(m->wo + l * DIM * DIM, m->so + l * DIM, DIM, DIM, xb2_, xb_);
    for (int i = 0; i < DIM; i++) x_[i] += xb_[i];
    rmsnorm(xb_, x_, m->rms_ffn + l * DIM, DIM);
    mv(m->w1 + l * HID_P * DIM, m->s1 + l * HID_P, HID_P, DIM, xb_, hb_);
    mv(m->w3 + l * HID_P * DIM, m->s3 + l * HID_P, HID_P, DIM, xb_, hb2_);
    for (int i = 0; i < HID_P; i++) hb_[i] = (hb_[i] / (1.f + my_expf(-hb_[i]))) * hb2_[i];
    mv(m->w2 + l * DIM * HID_P, m->s2 + l * DIM, DIM, HID_P, hb_, xb_);
    if (l == LAYERS - 1) {
      for (int i = 0; i < HID_P; i++) hb_last_[i] = hb_[i];
      if (g_lora_on) {
        for (int r = 0; r < LORA_R; r++) {
          float a = 0.f; for (int j = 0; j < HID_P; j++) a += lora_A[r][j] * hb_[j];
          ah_[r] = a;
        }
        for (int i = 0; i < DIM; i++) {
          float d = 0.f; for (int r = 0; r < LORA_R; r++) d += lora_B[i][r] * ah_[r];
          xb_[i] += lora_scale * d;
        }
      }
    }
    for (int i = 0; i < DIM; i++) x_[i] += xb_[i];
  }
  for (int i = 0; i < DIM; i++) xpre_[i] = x_[i];
  rmsnorm(x_, x_, m->rms_final, DIM);
  mv(m->embed_q, m->embed_s, VOCAB, DIM, x_, logits_);
}

static int argmax_logits(void) {
  int b = 0; for (int i = 1; i < VOCAB; i++) if (logits_[i] > logits_[b]) b = i; return b;
}
static uint32_t rng_state = 1;
static float rng_f(void) {
  rng_state ^= rng_state << 13; rng_state ^= rng_state >> 17; rng_state ^= rng_state << 5;
  return (rng_state >> 8) / 16777216.0f;
}
static int sample_logits(float temp) {
  if (temp <= 0.f) return argmax_logits();
  float mx = logits_[0]; for (int i = 1; i < VOCAB; i++) if (logits_[i] > mx) mx = logits_[i];
  float sum = 0.f;
  for (int i = 0; i < VOCAB; i++) { logits_[i] = my_expf((logits_[i] - mx) / temp); sum += logits_[i]; }
  float r = rng_f() * sum, c = 0.f;
  for (int i = 0; i < VOCAB; i++) { c += logits_[i]; if (r < c) return i; }
  return VOCAB - 1;
}


// ---- LoRA training step -----------------------------------------------------
static int8_t  vq_b[VOCAB * 8] __attribute__((aligned(8)));   /* [t][8], col 0 used */
static int32_t dacc_[DIM * 8] __attribute__((aligned(8)));

static void lora_init(void) {
  for (int r = 0; r < LORA_R; r++) for (int j = 0; j < HID_P; j++)
    lora_A[r][j] = (rng_f() * 2.f - 1.f) * 0.075f;   /* ~ 1/sqrt(fan_in) */
  for (int i = 0; i < DIM; i++) for (int r = 0; r < LORA_R; r++) lora_B[i][r] = 0.f;
#ifdef LORA_ADAM
  for (int r = 0; r < LORA_R; r++) for (int j = 0; j < HID_P; j++) { mA_[r][j] = 0.f; vA_[r][j] = 0.f; }
  for (int i = 0; i < DIM; i++) for (int r = 0; r < LORA_R; r++) { mB_[i][r] = 0.f; vB_[i][r] = 0.f; }
  adam_pb1 = 1.f; adam_pb2 = 1.f;
#endif
  g_lora_on = 1;
}

// Call right after forward(): backprop next-token loss to the adapter
// (through the tied classifier and the final RMSNorm) and take one SGD step.
// Returns the cross-entropy loss for this position.
static float lora_step(const Model *m, int target, float lr) {
  float mx = logits_[0]; for (int i = 1; i < VOCAB; i++) if (logits_[i] > mx) mx = logits_[i];
  float sum = 0.f;
  for (int i = 0; i < VOCAB; i++) { logits_[i] = my_expf(logits_[i] - mx); sum += logits_[i]; }
  float loss = -my_logf(logits_[target] / sum), inv = 1.f / sum;
  // dxf = Wcls^T . dlogits. The classifier's per-row (per-token) scale sits on
  // the SUM index of this transposed product, so fold it into the vector first.
  float vmax = 0.f; static float v[VOCAB];
  for (int t = 0; t < VOCAB; t++) {
    float dl = logits_[t] * inv - (t == target ? 1.f : 0.f);
    v[t] = dl * m->embed_s[t]; float a = v[t] < 0 ? -v[t] : v[t]; if (a > vmax) vmax = a;
  }
  float sv = vmax > 0.f ? vmax / 127.f : 1.f, isv = 1.f / sv;
  for (int t = 0; t < VOCAB; t++) { float q = v[t] * isv; vq_b[t * 8] = (int8_t)(int)(q + (q >= 0 ? 0.5f : -0.5f)); }
#ifdef USE_ACCEL
  if (g_use_accel) {
    acc_gemm(DIM, 8, VOCAB, m->embed_q, DIM, 1, vq_b, 8, 0, dacc_, 8, 0, 0, 0);
  } else
#endif
  {
    for (int j = 0; j < DIM; j++) {
      int32_t a = 0; for (int t = 0; t < VOCAB; t++) a += (int32_t)m->embed_q[t * DIM + j] * (int32_t)vq_b[t * 8];
      dacc_[j * 8] = a;
    }
  }
  float dxf[DIM], g[DIM], dx[DIM];
  for (int j = 0; j < DIM; j++) dxf[j] = (float)dacc_[j * 8] * sv;
  // final RMSNorm backward: xf = w * x * r,  r = (mean(x^2)+eps)^-1/2
  float ss = 0.f; for (int j = 0; j < DIM; j++) ss += xpre_[j] * xpre_[j];
  float r = 1.0f / __builtin_sqrtf(ss / DIM + 1e-5f), dot = 0.f;
  for (int j = 0; j < DIM; j++) { g[j] = dxf[j] * m->rms_final[j]; dot += g[j] * xpre_[j]; }
  for (int j = 0; j < DIM; j++) dx[j] = r * g[j] - r * r * r * xpre_[j] * dot / DIM;
  // adapter gradients: dB = s dx ah^T ; dah = s B^T dx ; dA = dah hb^T
  float dah[LORA_R];
  for (int q = 0; q < LORA_R; q++) { float a = 0.f; for (int i = 0; i < DIM; i++) a += lora_B[i][q] * dx[i]; dah[q] = lora_scale * a; }
  // global gradient norm -> clip scale (dB = s*dx*ah^T, dA = dah*hb^T)
  float n2 = 0.f, dxx = 0.f, ahh = 0.f, dahh = 0.f, hbb = 0.f;
  for (int i = 0; i < DIM; i++) dxx += dx[i] * dx[i];
  for (int q = 0; q < LORA_R; q++) { ahh += ah_[q] * ah_[q]; dahh += dah[q] * dah[q]; }
  for (int j = 0; j < HID_P; j++) hbb += hb_last_[j] * hb_last_[j];
  n2 = lora_scale * lora_scale * dxx * ahh + dahh * hbb;
  float cs = 1.f;
  if (n2 > LORA_CLIP * LORA_CLIP) cs = LORA_CLIP / __builtin_sqrtf(n2);
#ifdef LORA_ADAM
  if (lr > 0.f) {
    adam_pb1 *= 0.9f; adam_pb2 *= 0.999f;
    float c1 = 1.f / (1.f - adam_pb1), c2 = 1.f / (1.f - adam_pb2);
    for (int i = 0; i < DIM; i++) for (int q = 0; q < LORA_R; q++) {
      float g = cs * lora_scale * dx[i] * ah_[q];
      mB_[i][q] = 0.9f * mB_[i][q] + 0.1f * g; vB_[i][q] = 0.999f * vB_[i][q] + 0.001f * g * g;
      lora_B[i][q] -= lr * (mB_[i][q] * c1) / (__builtin_sqrtf(vB_[i][q] * c2) + 1e-8f);
    }
    for (int q = 0; q < LORA_R; q++) for (int j = 0; j < HID_P; j++) {
      float g = cs * dah[q] * hb_last_[j];
      mA_[q][j] = 0.9f * mA_[q][j] + 0.1f * g; vA_[q][j] = 0.999f * vA_[q][j] + 0.001f * g * g;
      lora_A[q][j] -= lr * (mA_[q][j] * c1) / (__builtin_sqrtf(vA_[q][j] * c2) + 1e-8f);
    }
  }
#else
  float lrc = lr * cs;
  for (int i = 0; i < DIM; i++) for (int q = 0; q < LORA_R; q++) lora_B[i][q] -= lrc * lora_scale * dx[i] * ah_[q];
  for (int q = 0; q < LORA_R; q++) for (int j = 0; j < HID_P; j++) lora_A[q][j] -= lrc * dah[q] * hb_last_[j];
#endif
  return loss;
}

// One pass over a token sequence (teacher forcing); returns mean loss.
static float lora_epoch(const Model *m, const int *toks, int n, float lr) {
  float tot = 0.f;
  for (int i = 0; i + 1 < n; i++) { forward(m, toks[i], i); tot += lora_step(m, toks[i + 1], lr); }
  return tot / (n - 1);
}

// Decode one token to text via callback (strips the leading space after BOS,
// expands raw-byte tokens like <0x0A>).
typedef void (*emit_fn)(const char *s, int n);
static void decode_token(const Model *m, int prev, int tok, emit_fn emit) {
  const char *p = m->tok_data + m->tok_off[tok]; int n = (int)(m->tok_off[tok + 1] - m->tok_off[tok]);
  if (prev == 1 && n > 0 && p[0] == ' ') { p++; n--; }
  if (n == 6 && p[0] == '<' && p[1] == '0' && p[2] == 'x' && p[5] == '>') {
    int hi = p[3] <= '9' ? p[3] - '0' : (p[3] & 7) + 9, lo = p[4] <= '9' ? p[4] - '0' : (p[4] & 7) + 9;
    char c = (char)(hi * 16 + lo); emit(&c, 1); return;
  }
  emit(p, n);
}

// ---- Clean-ending generation -------------------------------------------------
// Greedy decoding of a model fine-tuned on a short text has no learned "stop"
// (an EOS target was tried and is not learnable through this rank-8 last-layer
// adapter), so it loops after the memorized text. Instead: buffer tokens one
// SENTENCE at a time, print a sentence only once it is complete, and stop at
// the first sentence identical to one already printed, at EOS, or once
// max_tokens is reached (finishing the current sentence first, hard cap 2x).
#define GEN_MAXTOK 512
static int piece_ends_sentence(const Model *m, int tok) {
  int n = (int)(m->tok_off[tok + 1] - m->tok_off[tok]);
  if (n <= 0) return 0;
  char c = m->tok_data[m->tok_off[tok] + n - 1];
  return c == '.' || c == '!' || c == '?';
}
static void gen_sentences(const Model *m, int max_tokens, emit_fn emit) {
  static int all[GEN_MAXTOK];          /* tokens of every sentence already printed */
  static int starts[GEN_MAXTOK / 2];   /* start index of each printed sentence */
  int nall = 0, nsent = 0, tok = 1, prev = 1, cur = 0, pos = 0;
  int sent[GEN_MAXTOK]; int sent_prev[GEN_MAXTOK];
  for (;;) {
    forward(m, tok, pos++); prev = tok; tok = argmax_logits();
    if (tok == 1 || tok == 2) break;
    sent[cur] = tok; sent_prev[cur] = prev; cur++;
    int end = piece_ends_sentence(m, tok);
    if (end || pos >= 2 * max_tokens || cur >= GEN_MAXTOK / 2) {
      int dup = 0;
      for (int k = 0; k < nsent && !dup; k++) {
        int a = starts[k], b = (k + 1 < nsent ? starts[k + 1] : nall);
        if (b - a == cur) { dup = 1; for (int i = 0; i < cur; i++) if (all[a + i] != sent[i]) { dup = 0; break; } }
      }
      if (dup) break;
      for (int i = 0; i < cur; i++) { decode_token(m, sent_prev[i], sent[i], emit); all[nall++] = sent[i]; }
      starts[nsent++] = nall - cur; cur = 0;
      if (pos >= max_tokens || nall + 2 >= GEN_MAXTOK) break;
    }
    if (pos >= 2 * max_tokens) break;
  }
  emit("\n", 1);
}
#endif
