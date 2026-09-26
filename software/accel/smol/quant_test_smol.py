#!/usr/bin/env python3
"""How much does W8A8 (per-row int8 weights, per-vector int8 activations = what the engine does) hurt SmolLM2?
Teacher-forced: feed the float model's own reply, compare next-token argmax; also print greedy text."""
import sys, numpy as np, ref_smol as R
from tokenizers import Tokenizer
sys.path.insert(0, '../llm'); from quantize import quant_rows
tk = Tokenizer.from_file('hf/tokenizer.json'); W = R.load()
def make_qmv(act='vector', wq=True):
    cache = {}
    def qmv(w, x):
        k = id(w)
        if k not in cache: q, s = quant_rows(w, *w.shape); cache[k] = (q.astype(np.float64), s.astype(np.float64))
        q, s = cache[k]
        if act == 'float': return (q * s[:, None]) @ x
        sx = np.abs(x).max() / 127.0 or 1.0; xq = np.clip(np.rint(x / sx), -127, 127)
        return (q @ xq) * s * sx
    return qmv
prompts = ["What is the capital of France?", "Write one sentence about a robot.", "Explain what a matrix multiplication is."]
sysmsg = "You are a helpful AI assistant named SmolLM, trained by Hugging Face"
for user in prompts:
    p = f"<|im_start|>system\n{sysmsg}<|im_end|>\n<|im_start|>user\n{user}<|im_end|>\n<|im_start|>assistant\n"; ids = tk.encode(p).ids
    rf = R.Ref(W); pos = 0
    for t in ids: lg = rf.step(t, pos); pos += 1
    out = []
    for _ in range(40):
        t = int(lg.argmax())
        if t == 2: break
        out.append(t); lg = rf.step(t, pos); pos += 1
    seq = ids + out
    for mode in ['float-act', 'vector-int8']:
        qmv = make_qmv('float' if mode == 'float-act' else 'vector'); rq = R.Ref(W); agree = 0; gen = []
        for i, t in enumerate(seq[:-1]):
            lg = rq.step(t, i, mv=qmv)
            if i >= len(ids) - 1: agree += int(lg.argmax()) == seq[i + 1]
        print(f'[{mode:12s}] next-token agreement with float on {len(out)+1} reply positions: {agree}/{len(out)+1}   prompt: {user[:40]!r}', flush=True)
    print('   float reply:', tk.decode(out)[:110].replace('\n', ' '))
