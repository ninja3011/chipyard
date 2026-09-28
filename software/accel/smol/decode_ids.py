#!/usr/bin/env python3
"""Decode SmolLM token ids (stdin or argv) using the real tokenizer, for reading host-test output."""
import sys
from tokenizers import Tokenizer
tk = Tokenizer.from_file('hf/tokenizer.json')
text = sys.stdin.read() if len(sys.argv) < 2 else ' '.join(sys.argv[1:])
for line in text.strip().split('\n'):
    tag, *rest = line.split(':', 1) if ':' in line[:8] else ('', line)
    ids = [int(x) for x in (rest[0] if rest else line).split() if x.strip('-').isdigit()]
    ids = [i for i in ids if i not in (0, 2)]
    print((tag + ':' if tag else '') + repr(tk.decode(ids)))
