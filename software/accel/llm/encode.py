#!/usr/bin/env python3
"""llama2.c-style BPE encoder (greedy best-score merges) for tok512.bin.
Usage: encode.py "text" -> prints token ids (BOS first)."""
import sys
from quantize import read_tokenizer

def encode(text, toks, scores, bos=True):
    vocab = {t: i for i, t in enumerate(toks)}
    ids = [1] if bos else []
    if text: ids.append(vocab[b' '])                     # dummy prefix space
    for ch in text.encode('utf-8'):
        c = bytes([ch])
        ids.append(vocab[c] if c in vocab else ch + 3)   # byte fallback <0xXX>
    while True:
        best, bi, bid = -1e30, -1, -1
        for i in range(len(ids) - 1):
            if ids[i] < 3 and False: continue
            s = toks[ids[i]] + toks[ids[i + 1]]
            if s in vocab and scores[vocab[s]] > best: best, bi, bid = scores[vocab[s]], i, vocab[s]
        if bi < 0: break
        ids[bi:bi + 2] = [bid]
    return ids

def decode(ids, toks):
    out = b''; prev = 1
    for t in ids:
        if t == 1: prev = t; continue
        p = toks[t]
        if prev == 1 and p.startswith(b' '): p = p[1:]
        if len(p) == 6 and p.startswith(b'<0x') and p.endswith(b'>'): p = bytes([int(p[3:5], 16)])
        out += p; prev = t
    return out.decode('utf-8', 'replace')

if __name__ == '__main__':
    toks, scores = read_tokenizer(__import__('os').environ.get('TOK','tok512.bin'))
    ids = encode(sys.argv[1], toks, scores)
    print(' '.join(map(str, ids))); print(decode(ids, toks), file=sys.stderr)
