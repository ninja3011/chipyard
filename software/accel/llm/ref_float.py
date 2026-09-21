#!/usr/bin/env python3
"""Float32 numpy reference of the llama2.c forward pass on the ORIGINAL
weights -- ground truth for the C/int8 port and for quantization damage."""
import sys, numpy as np
from quantize import read_model

def rms(x, w): return w * (x / np.sqrt((x * x).mean() + 1e-5))
def silu(x): return x / (1 + np.exp(-x))

class Ref:
    def __init__(self, path):
        self.cfg, self.w = read_model(path); c = self.cfg
        self.hs = c['dim'] // c['heads']; self.kvd = c['kv_heads'] * self.hs
        self.k = np.zeros((c['layers'], c['seq'], self.kvd), np.float32); self.v = self.k.copy()
    def step(self, tok, pos):
        c, w, hs = self.cfg, self.w, self.hs; x = w['embed'][tok].copy()
        for l in range(c['layers']):
            xb = rms(x, w['rms_att'][l])
            q = w['wq'][l] @ xb; k = w['wk'][l] @ xb; v = w['wv'][l] @ xb
            for i in range(0, c['dim'], 2):
                hd = (i % hs) // 2; fcr, fci = w['rope_cos'][pos, hd], w['rope_sin'][pos, hd]
                q[i], q[i+1] = q[i]*fcr - q[i+1]*fci, q[i]*fci + q[i+1]*fcr
                if i < self.kvd: k[i], k[i+1] = k[i]*fcr - k[i+1]*fci, k[i]*fci + k[i+1]*fcr
            self.k[l, pos] = k; self.v[l, pos] = v
            out = np.zeros(c['dim'], np.float32)
            for h in range(c['heads']):
                kvh = h // (c['heads'] // c['kv_heads'])
                K = self.k[l, :pos+1, kvh*hs:(kvh+1)*hs]; V = self.v[l, :pos+1, kvh*hs:(kvh+1)*hs]
                s = K @ q[h*hs:(h+1)*hs] / np.sqrt(hs); s = np.exp(s - s.max()); s /= s.sum()
                out[h*hs:(h+1)*hs] = s @ V
            x = x + w['wo'][l] @ out
            xb = rms(x, w['rms_ffn'][l])
            hb = silu(w['w1'][l] @ xb) * (w['w3'][l] @ xb)
            x = x + w['w2'][l] @ hb
        x = rms(x, w['rms_final'])
        return w['embed'] @ x

if __name__ == '__main__':
    r = Ref(sys.argv[1]); n = int(sys.argv[2]); tok = 1; toks = [1]; am = []
    for pos in range(n):
        lg = r.step(tok, pos); tok = int(lg.argmax()); am.append(tok); toks.append(tok)
    print(' '.join(map(str, toks[:-1]))); print(' '.join(map(str, am)))
