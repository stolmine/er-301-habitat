#!/usr/bin/env python3
"""Cheapest possible falsification of the lane-shear hypothesis.

NO SORTING. Split into N bands with a filterbank, circularly rotate each band by
an amount proportional to band index, sum. If pure differential displacement
across correlated lanes does not produce the dislocation we are after, then the
sort will not either and the problem is elsewhere.

Filterbank, not FFT, deliberately: within a band the signal is narrowband, so
splices land near band-local zero crossings and the click problem mostly
evaporates. FFT would drag in phase management for no gain here.
"""
import numpy as np
from scipy.signal import butter, sosfilt, sosfiltfilt

SR = 48000


def bands(x, n=16, lo=40.0, hi=16000.0, sr=SR, zerophase=True):
    """Split into n log-spaced bands as DIFFERENCES OF LOWPASSES, so the bank
    telescopes and re-sums to the input exactly.

    A bank of independent Butterworth bandpasses does NOT sum flat - measured
    -27 dB reassembly error, i.e. ~4% - and that matters here rather than being
    a nicety: the whole point of content-derived intervals is that sub-threshold
    regions pass through UNTOUCHED, and they cannot be untouched if the bank
    itself colors them. Zero-phase so displacement is the only thing under test.
    """
    edges = np.geomspace(lo, hi, n + 1)
    f = sosfiltfilt if zerophase else sosfilt
    lps = []
    for e in edges[1:-1]:
        lps.append(f(butter(4, e / (sr / 2), btype='low', output='sos'), x))
    out = [lps[0]]
    for i in range(1, len(lps)):
        out.append(lps[i] - lps[i - 1])
    out.append(x - lps[-1])
    return np.array(out), edges


def shear(x, n=16, max_shift_ms=60.0, mode='linear', sr=SR):
    """Rotate band k by an amount proportional to k. Circular, so it is a pure
    permutation of each lane in time: no gain, no pitch, no repetition, and the
    per-band long-term spectrum is exactly invariant."""
    B, edges = bands(x, n=n, sr=sr)
    out = np.zeros(len(x))
    shifts = []
    for k in range(n):
        if mode == 'linear':
            frac = k / max(1, n - 1)
        elif mode == 'alternating':          # zig-zag, a sharper tear
            frac = (k % 2) * 2 - 1
            frac = frac * (k / max(1, n - 1))
        else:                                 # 'random' but fixed
            frac = np.random.default_rng(k).uniform(-1, 1)
        s = int(frac * max_shift_ms * 1e-3 * sr)
        shifts.append(s)
        out += np.roll(B[k], s)
    return out, np.array(shifts), edges


def reassembly_error(x, n=16, sr=SR):
    """Sanity: with zero shift the bank must re-sum to the input."""
    B, _ = bands(x, n=n, sr=sr)
    y = B.sum(axis=0)
    m = slice(sr // 2, -sr // 2)
    return 20 * np.log10(np.sqrt(np.mean((y[m] - x[m]) ** 2)) /
                         (np.sqrt(np.mean(x[m] ** 2)) + 1e-30))


def spectrum_invariance(x, y, sr=SR):
    """A permutation preserves the long-term spectrum. Measure how well."""
    m = slice(sr // 2, -sr // 2)
    fx = np.abs(np.fft.rfft(x[m] * np.hanning(len(x[m]))))
    fy = np.abs(np.fft.rfft(y[m] * np.hanning(len(y[m]))))
    f = np.fft.rfftfreq(len(x[m]), 1 / sr)
    band = (f > 50) & (f < 16000) & (fx > fx.max() * 1e-4)
    d = 20 * np.log10((fy[band] + 1e-12) / (fx[band] + 1e-12))
    return float(np.median(d)), float(np.percentile(np.abs(d), 95))


def write(path, x, sr=SR):
    from scipy.io import wavfile
    p = np.max(np.abs(x)) + 1e-9
    wavfile.write(path, sr, (x / max(1.0, p) * 0.9 * 32767).astype(np.int16))
