import sys, numpy as np
import ref_float as R
from quantize import quant_rows

def make_qmv(mode):
    cache = {}
    def qmv(W, x):
        key = id(W)
        if key not in cache:
            rows, k = W.shape; q, s = quant_rows(W, rows, k); cache[key] = (q.astype(np.int32), s)
        q, s = cache[key]
        if mode == 'w8a8':
            sx = np.abs(x).max() / 127.0 or 1.0
            xq = np.clip(np.rint(x / sx), -127, 127).astype(np.int32)
            return (q @ xq).astype(np.float64) * s * sx
        return (q * s[:, None]) @ x   # w8 only, float activations
    return qmv

class QRef(R.Ref):
    pass

def run(mode, ref_tokens):
    r = R.Ref('stories260K.bin'); qmv = make_qmv(mode)
    w = r.w
    class W:  # wrap matrices so `@` goes through qmv
        def __init__(s, m): s.m = m
        def __matmul__(s, x): return qmv(s.m, x)
    for key in ['wq','wk','wv','wo','w1','w2','w3']:
        w[key] = [W(m) for m in w[key]]
    w['embed_m'] = W(w['embed'])
    embed = w['embed']
    orig_step = r.step
    out = []
    import types
    # patch final classifier: r.step uses w['embed'] @ x; replace with wrapper but keep row lookup
    class E:
        def __init__(s): pass
        def __getitem__(s, i): return embed[i]
        def __matmul__(s, x): return qmv(embed, x)
    w['embed'] = E()
    for pos, t in enumerate(ref_tokens):
        lg = r.step(t, pos); out.append(int(np.argmax(lg)))
    return out

toks = list(map(int, open('/tmp/ref_out.txt').read().splitlines()[0].split()))
ref = list(map(int, open('/tmp/ref_out.txt').read().splitlines()[1].split()))
for mode in ['w8', 'w8a8']:
    o = run(mode, toks)
    print(mode, 'agreement', sum(a == b for a, b in zip(o, ref)), '/', len(ref))
