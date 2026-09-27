#!/usr/bin/env python3
"""Chat with SmolLM2-135M running on the FPGA (INT8 tile engine), over the FT232R serial console.
The PC tokenizes and decodes (byte-level BPE); the chip runs the transformer and returns token ids.
  interactive:  python3 smol_chat.py [--temp 0] [--maxnew 60]
  one question: python3 smol_chat.py --once "What is the capital of France?" [--record chat.ts.jsonl]
--record saves [seconds, text] events of exactly what is shown, for replay_to_video.py."""
import os, sys, glob, time, json, select, subprocess, argparse, re
from tokenizers import Tokenizer
ap = argparse.ArgumentParser(); ap.add_argument('--once'); ap.add_argument('--check', action='store_true'); ap.add_argument('--turns', nargs='+'); ap.add_argument('--record'); ap.add_argument('--temp', type=float, default=0.0)
ap.add_argument('--maxnew', type=int, default=60); ap.add_argument('--rep', type=float, default=1.15); ap.add_argument('--system', default='You are a helpful assistant.'); a = ap.parse_args()
HERE = os.path.dirname(os.path.abspath(__file__)); tk = Tokenizer.from_file(f'{HERE}/hf/tokenizer.json')
dev = os.path.realpath(glob.glob('/dev/serial/by-id/*FT232R*')[0])
subprocess.run(['stty', '-F', dev, '115200', 'cs8', '-cstopb', '-parenb', 'raw', '-echo', 'clocal'], check=True)
fd = os.open(dev, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK); t0 = time.time(); rec = open(a.record, 'w') if a.record else None
def show(s):
    sys.stdout.write(s); sys.stdout.flush()
    if rec: rec.write(json.dumps([round(time.time() - t0, 3), s]) + '\n'); rec.flush()
def read_until(pred, timeout):
    buf = b''; end = time.time() + timeout
    while time.time() < end:
        r, _, _ = select.select([fd], [], [], 0.2)
        if r:
            try: buf += os.read(fd, 4096)
            except BlockingIOError: pass
            if pred(buf): return buf
    return buf
def send(line):
    for ch in line + '\n': os.write(fd, ch.encode()); time.sleep(0.004)
os.write(fd, b'\r'); read_until(lambda b: b.rstrip().endswith(b'>'), 5)
send('R'); read_until(lambda b: b'reset' in b, 10)
first = True
def ask(user):
    global first
    text = (f"<|im_start|>system\n{a.system}<|im_end|>\n" if first else "\n") + f"<|im_start|>user\n{user}<|im_end|>\n<|im_start|>assistant\n"
    first = False; ids = tk.encode(text, add_special_tokens=False).ids
    send(f"G {a.maxnew} {int(a.temp * 10)} {int(a.rep * 100)} " + ' '.join(map(str, ids)))
    out = []; shown = ''; buf = b''; done = False; ts = time.time()
    while not done:
        r, _, _ = select.select([fd], [], [], 0.5)
        if not r: continue
        try: buf += os.read(fd, 4096)
        except BlockingIOError: continue
        s = buf.decode('latin1')
        while True:
            m = re.match(r'\s*(\d+) ', s)
            if m:
                tid = int(m.group(1)); s = s[m.end():]; buf = s.encode('latin1')
                if tid in (0, 2): continue
                out.append(tid); txt = tk.decode(out)
                if txt.startswith(shown): show(txt[len(shown):]); shown = txt
                continue
            if 'END' in s and s.rstrip().endswith('>'): done = True
            break
    stats = re.search(r'END (\d+) (\d+)', s); show('\n')
    if stats: show(f"[{stats.group(1)} tokens, {int(stats.group(2))/50e6:.1f} s on the chip, {int(stats.group(1))*50e6/max(1,int(stats.group(2))):.2f} tok/s]\n")
if a.check:
    send('C'); b = read_until(lambda b: b'CHECK' in b and b.rstrip().endswith(b'>'), 900); show(b.decode('latin1').replace('\r', '').strip() + '\n')
elif a.turns:
    for q in a.turns: show(f"> {q}\n"); ask(q)
elif a.once: show(f"> {a.once}\n"); ask(a.once)
else:
    try:
        while True:
            u = input('> ').strip()
            if u: ask(u)
    except (EOFError, KeyboardInterrupt): pass
