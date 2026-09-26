#!/usr/bin/env python3
"""Talk to the chip over the FT232R serial console.
  interactive:  python3 chat_terminal.py          (Ctrl-C to quit; type at the '>' prompt)
  one-shot:     python3 chat_terminal.py --once "prompt or /command"   (prints the reply, returns at the next '> ')
The chip echoes what you type and prints its reply; it computes slowly (~1.5 tokens/s), so replies stream in."""
import os, sys, glob, time, select, termios, tty, subprocess
dev = os.path.realpath(glob.glob('/dev/serial/by-id/*FT232R*')[0])
subprocess.run(['stty', '-F', dev, '115200', 'cs8', '-cstopb', '-parenb', 'raw', '-echo', 'clocal'], check=True)
fd = os.open(dev, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
def drain(t=0.3):
    out = b''; end = time.time() + t
    while time.time() < end:
        r, _, _ = select.select([fd], [], [], 0.05)
        if r:
            try: out += os.read(fd, 4096); end = time.time() + t
            except BlockingIOError: pass
    return out
def send(s):
    for ch in s: os.write(fd, ch.encode()); time.sleep(0.012)
    os.write(fd, b'\r')
if len(sys.argv) > 2 and sys.argv[1] == '--once':
    drain(0.3); send(sys.argv[2]); buf = b''; t0 = time.time()
    while time.time() - t0 < 900:
        buf += drain(0.5)
        if buf.rstrip().endswith(b'>') and b'\n' in buf and len(buf) > len(sys.argv[2]) + 2: break
    sys.stdout.write(buf.decode('latin1').replace('\r', '')); sys.exit(0)
old = termios.tcgetattr(sys.stdin); tty.setcbreak(sys.stdin.fileno())
try:
    os.write(fd, b'\r')
    while True:
        r, _, _ = select.select([fd, sys.stdin], [], [])
        if fd in r:
            try: sys.stdout.write(os.read(fd, 4096).decode('latin1')); sys.stdout.flush()
            except BlockingIOError: pass
        if sys.stdin in r:
            ch = os.read(sys.stdin.fileno(), 1)
            if ch == b'\x03': break
            os.write(fd, ch)
finally:
    termios.tcsetattr(sys.stdin, termios.TCSADRAIN, old)
