#!/usr/bin/env python3
"""Timestamped serial capture. usage: serial_log.py <device> <plain_log>
Writes the raw text to <plain_log> (same as `cat`) and [seconds_since_start, text] JSON lines to
<plain_log minus .log>.ts.jsonl, so the real timing can be replayed into a video later."""
import os, sys, time, json, signal
dev, plain = sys.argv[1], sys.argv[2]
ts = plain[:-4] + '.ts.jsonl' if plain.endswith('.log') else plain + '.ts.jsonl'
fd = os.open(dev, os.O_RDONLY | os.O_NOCTTY); t0 = time.time()
signal.signal(signal.SIGTERM, lambda *a: sys.exit(0))
with open(plain, 'ab', buffering=0) as p, open(ts, 'a') as j:
    while True:
        b = os.read(fd, 4096)
        if not b: time.sleep(0.01); continue
        p.write(b); j.write(json.dumps([round(time.time() - t0, 3), b.decode('latin1')]) + '\n'); j.flush()
