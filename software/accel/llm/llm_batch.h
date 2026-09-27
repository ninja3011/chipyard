// Batched forward pass: push up to 8 KNOWN tokens through the model together (prompt prefill / teacher-forced
// training), so the engine's tile uses all 8 of its columns instead of 1. Each weight tile is loaded once for 8
// tokens instead of 8 times. Per-token arithmetic is unchanged (per-vector activation scales, same accumulation
// order), so the last position's logits are bit-identical to running forward() token by token.
// Include after llm_core.h. Inference only: ignores LoRA.
#ifndef LLM_BATCH_H
#define LLM_BATCH_H
#define NB 8
static float bx_[NB][DIM], bxb_[NB][DIM], bxb2_[NB][DIM], bq_[NB][DIM], bk_[NB][KVD], bv_[NB][KVD], bhb_[NB][HID_P], bhb2_[NB][HID_P];
static int8_t bxq_[HID_P * 8] __attribute__((aligned(8)));

// y[c][o] = (W[o][:] . x[c][:]) for c < nb; x rows have stride xs, y rows stride ys
static void mv_batch(const int8_t *W, const float *ws, int O, int K, int nb, const float *x, int xs, float *y, int ys) {
  float sx[NB];
  for (int c = 0; c < NB; c++) {
    if (c >= nb) { for (int k = 0; k < K; k++) bxq_[k * 8 + c] = 0; sx[c] = 1.f; continue; }
    const float *xc = x + c * xs; float mx = 0.f;
    for (int i = 0; i < K; i++) { float a = xc[i] < 0 ? -xc[i] : xc[i]; if (a > mx) mx = a; }
    float s = mx > 0.f ? mx / 127.f : 1.f, inv = 1.f / s; sx[c] = s;
    for (int i = 0; i < K; i++) { float v = xc[i] * inv; bxq_[i * 8 + c] = (int8_t)(int)(v + (v >= 0 ? 0.5f : -0.5f)); }
  }
#ifdef USE_ACCEL
  if (g_use_accel) {
    acc_gemm(O, 8, K, W, K, 0, bxq_, 8, 0, acc_, 8, 0, 0, 0);
  } else
#endif
  {
    for (int o = 0; o < O; o++) for (int c = 0; c < nb; c++) {
      int32_t s = 0; for (int k = 0; k < K; k++) s += (int32_t)W[o * K + k] * (int32_t)bxq_[k * 8 + c];
      acc_[o * 8 + c] = s;
    }
  }
  for (int c = 0; c < nb; c++) for (int o = 0; o < O; o++) y[c * ys + o] = (float)acc_[o * 8 + c] * ws[o] * sx[c];
}

// tokens[0..nb-1] at positions pos0..pos0+nb-1 (nb <= 8). Leaves the LAST token's logits in logits_.
static void forward_batch_ex(const Model *m, const int *tokens, int nb, int pos0, int want_logits) {
  for (int b = 0; b < nb; b++) for (int i = 0; i < DIM; i++) bx_[b][i] = (float)m->embed_q[tokens[b] * DIM + i] * m->embed_s[tokens[b]];
  for (int l = 0; l < LAYERS; l++) {
    for (int b = 0; b < nb; b++) rmsnorm(bxb_[b], bx_[b], m->rms_att + l * DIM, DIM);
    mv_batch(m->wq + l * DIM * DIM, m->sq + l * DIM, DIM, DIM, nb, bxb_[0], DIM, bq_[0], DIM);
    mv_batch(m->wk + l * KVD * DIM, m->sk + l * KVD, KVD, DIM, nb, bxb_[0], DIM, bk_[0], KVD);
    mv_batch(m->wv + l * KVD * DIM, m->sv + l * KVD, KVD, DIM, nb, bxb_[0], DIM, bv_[0], KVD);
    for (int b = 0; b < nb; b++) {
      int pos = pos0 + b;
      for (int i = 0; i < DIM; i += 2) {
        int hd = (i % HS) / 2;
        float fcr = m->rope_cos[pos * (HS / 2) + hd], fci = m->rope_sin[pos * (HS / 2) + hd];
        float a = bq_[b][i], c = bq_[b][i + 1]; bq_[b][i] = a * fcr - c * fci; bq_[b][i + 1] = a * fci + c * fcr;
        if (i < KVD) { a = bk_[b][i]; c = bk_[b][i + 1]; bk_[b][i] = a * fcr - c * fci; bk_[b][i + 1] = a * fci + c * fcr; }
      }
      for (int i = 0; i < KVD; i++) { kcache_[l][pos][i] = bk_[b][i]; vcache_[l][pos][i] = bv_[b][i]; }
    }
    for (int b = 0; b < nb; b++) {          /* attention: every position's keys were written above */
      int pos = pos0 + b;
      for (int h = 0; h < HEADS; h++) {
        const float *qh = bq_[b] + h * HS; int kvh = h / (HEADS / NKV); float mx = -1e30f;
        for (int t = 0; t <= pos; t++) {
          const float *kk = &kcache_[l][t][kvh * HS]; float s = 0.f;
          for (int i = 0; i < HS; i++) s += qh[i] * kk[i];
          s *= ATT_SCALE; att_[t] = s; if (s > mx) mx = s;
        }
        float sum = 0.f;
        for (int t = 0; t <= pos; t++) { att_[t] = my_expf(att_[t] - mx); sum += att_[t]; }
        float inv = 1.f / sum; float *out = bxb2_[b] + h * HS;
        for (int i = 0; i < HS; i++) out[i] = 0.f;
        for (int t = 0; t <= pos; t++) { const float *vv = &vcache_[l][t][kvh * HS]; float a = att_[t] * inv; for (int i = 0; i < HS; i++) out[i] += a * vv[i]; }
      }
    }
    mv_batch(m->wo + l * DIM * DIM, m->so + l * DIM, DIM, DIM, nb, bxb2_[0], DIM, bxb_[0], DIM);
    for (int b = 0; b < nb; b++) for (int i = 0; i < DIM; i++) bx_[b][i] += bxb_[b][i];
    for (int b = 0; b < nb; b++) rmsnorm(bxb_[b], bx_[b], m->rms_ffn + l * DIM, DIM);
    mv_batch(m->w1 + l * HID_P * DIM, m->s1 + l * HID_P, HID_P, DIM, nb, bxb_[0], DIM, bhb_[0], HID_P);
    mv_batch(m->w3 + l * HID_P * DIM, m->s3 + l * HID_P, HID_P, DIM, nb, bxb_[0], DIM, bhb2_[0], HID_P);
    for (int b = 0; b < nb; b++) for (int i = 0; i < HID_P; i++) bhb_[b][i] = (bhb_[b][i] / (1.f + my_expf(-bhb_[b][i]))) * bhb2_[b][i];
    mv_batch(m->w2 + l * DIM * HID_P, m->s2 + l * DIM, DIM, HID_P, nb, bhb_[0], HID_P, bxb_[0], DIM);
    for (int b = 0; b < nb; b++) for (int i = 0; i < DIM; i++) bx_[b][i] += bxb_[b][i];
  }
  if (!want_logits) return;      /* the output classifier is ~1/5 of a forward pass: skip it for non-final chunks */
  /* only the last position needs logits */
  for (int i = 0; i < DIM; i++) x_[i] = bx_[nb - 1][i];
  rmsnorm(x_, x_, m->rms_final, DIM);
  mv(m->embed_q, m->embed_s, VOCAB, DIM, x_, logits_);
}
static inline void forward_batch(const Model *m, const int *tokens, int nb, int pos0) { forward_batch_ex(m, tokens, nb, pos0, 1); }
#endif
