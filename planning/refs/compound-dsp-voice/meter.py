#!/usr/bin/env python3
"""Live level meter on the MOTU Line5 return. One continuous parec stream
(explicit target, immune to WirePlumber's default shuffling), RMS/peak in dBFS
updated ~5x/s with peak-hold and a clip flag. Clean Ctrl-C.

    ./meter.py               # Line5 (default)
    SRC=<node> ./meter.py    # override source node

Robust replacement for `parec | ecasignalview` (ncurses-in-a-pipe wrecks the
terminal on SIGINT). The return is mono duplicated to stereo, so channel 1 is
metered; both read the same.
"""
import os, sys, subprocess
import numpy as np

FS = 48000
WIN = int(0.2 * FS)                       # 200 ms analysis window
SRC = os.environ.get(
    "SRC", "alsa_input.usb-MOTU_M4_M4MA0617JK-00.HiFi__Line5__source")

p = subprocess.Popen(
    ["parec", "-d", SRC, "--rate=48000", "--channels=2",
     "--format=float32le", "--no-remix"],
    stdout=subprocess.PIPE)

need = WIN * 2 * 4                         # frames * channels * bytes(float32)
hold = -120.0
buf = b""
print(f"metering {SRC}\n(Ctrl-C to stop)")
try:
    while True:
        while len(buf) < need:
            d = p.stdout.read(need - len(buf))
            if not d:
                raise KeyboardInterrupt
            buf += d
        a = np.frombuffer(buf[:need], dtype=np.float32).reshape(-1, 2)
        buf = buf[need:]
        ch = a[:, 0]
        rms = 20 * np.log10(np.sqrt(np.mean(ch ** 2)) + 1e-20)
        pk = 20 * np.log10(np.max(np.abs(ch)) + 1e-20)
        hold = max(hold, pk)
        flag = "  CLIP" if pk > -0.1 else ""
        sys.stdout.write(
            f"\rRMS {rms:7.2f}   Peak {pk:7.2f}   Hold {hold:7.2f} dBFS{flag}   ")
        sys.stdout.flush()
except KeyboardInterrupt:
    pass
finally:
    p.terminate()
    print()
