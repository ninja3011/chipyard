#!/usr/bin/env python3
"""Type a line into the chip's serial console (FT232R), one character at a time.
usage: chip_say.py "text" [--dev /dev/ttyUSBn]   (device auto-detected via /dev/serial/by-id)"""
import os, sys, time, glob
args = [a for a in sys.argv[1:] if not a.startswith('--')]
dev = os.path.realpath(glob.glob('/dev/serial/by-id/*FT232R*')[0])
fd = os.open(dev, os.O_WRONLY | os.O_NOCTTY)
for ch in args[0]:
    os.write(fd, ch.encode()); time.sleep(0.012)
os.write(fd, b'\r'); os.close(fd)
