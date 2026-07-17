#!/usr/bin/env python3
"""Measure THD + harmonic breakdown of a captured 1 kHz sine.

Use during loopback calibration to establish the chain's OWN distortion floor:
feed cal_1k_hot.wav through the loop (no DUT), capture the return, run this on it.
Harmonics should sit far below the fundamental (good converters/301 -> < -70 dB);
that floor bounds how small a Lester nonlinearity you can attribute to the module.

    ./thd.py return.wav            # assumes ~1000 Hz fundamental
    ./thd.py return.wav 1000
"""
import sys
import numpy as np
from scipy.io import wavfile

path = sys.argv[1]
f0 = float(sys.argv[2]) if len(sys.argv) > 2 else 1000.0

fs, x = wavfile.read(path)
if x.ndim > 1:
    x = x[:, 0]
x = x.astype(np.float64)
x /= (np.abs(x).max() + 1e-12)

# window the steady middle to avoid edges
n = len(x)
x = x[n // 4: 3 * n // 4]
w = np.hanning(len(x))
X = np.abs(np.fft.rfft(x * w))
freqs = np.fft.rfftfreq(len(x), 1.0 / fs)

def band_energy(fc):
    # sum a few bins around fc (window leakage)
    lo, hi = fc - 15.0, fc + 15.0
    m = (freqs >= lo) & (freqs <= hi)
    return np.sqrt(np.sum(X[m] ** 2))

fund = band_energy(f0)
harm = []
for k in range(2, 11):
    fk = k * f0
    if fk >= fs / 2:
        break
    e = band_energy(fk)
    harm.append((k, fk, e))

thd = np.sqrt(sum(e ** 2 for _, _, e in harm)) / (fund + 1e-12)
print(f"file      : {path}")
print(f"fundamental {f0:.0f} Hz")
print(f"THD       : {thd*100:.4f} %   ({20*np.log10(thd+1e-12):.1f} dB)")
print("harmonics (dB below fundamental):")
for k, fk, e in harm:
    print(f"  H{k:<2d} {fk:6.0f} Hz  {20*np.log10(e/(fund+1e-12)+1e-12):7.1f} dB")
