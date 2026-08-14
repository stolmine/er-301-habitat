#!/usr/bin/env python3
"""Offline prototype for the kaleidoscope slicer (planning/kaleidoscope-unit.md).

Settles the SOUND questions before any C++ exists: fold semantics, mirror
alternation, and the Prism transposition law. Renders WAVs to listen to and
prints measurements so the structure can be verified, not just assumed.

Model (per the design doc):
  Window  - the shard: loop start + loop length
  Fold    - N reflections filling the SAME loop duration, so the period is
            constant and the macro is not an accidental tempo control.
            Odd folds play reversed: that is the mirror, and it is free.
  Prism   - fold k is transposed by k * interval, by RESAMPLING (artifact-free,
            preserves wave shape). Duration mismatch is absorbed by the slot:
            transposed up repeats inside its slot, down truncates.
"""
import numpy as np
from scipy.io import wavfile

SR = 48000


# ---------------------------------------------------------------- source ----
def make_source(seconds=4.0, sr=SR):
    """A phrase with distinct pitched events plus transients, so that slicing
    and reordering are audible rather than a wash."""
    n = int(seconds * sr)
    t = np.arange(n) / sr
    out = np.zeros(n)
    # eight pitched events walking up a scale
    semis = [0, 3, 5, 7, 10, 12, 7, 3]
    ev = n // len(semis)
    for i, s in enumerate(semis):
        f = 220.0 * 2 ** (s / 12.0)
        seg = np.arange(ev) / sr
        env = np.exp(-seg * 6.0)
        body = (np.sin(2 * np.pi * f * seg)
                + 0.5 * np.sin(2 * np.pi * 2 * f * seg)
                + 0.25 * np.sin(2 * np.pi * 3 * f * seg))
        click = np.exp(-seg * 900.0) * np.random.default_rng(i).normal(0, 1, ev) * 0.6
        out[i * ev:(i + 1) * ev] = (body * env * 0.3 + click)
    return out / (np.max(np.abs(out)) + 1e-9) * 0.8


# ----------------------------------------------------------------- engine ----
def read_resampled(buf, pos, count, ratio, reverse=False):
    """Linear-interpolated read of `count` output samples at `ratio` speed,
    wrapping inside the shard so an up-transposed fold repeats to fill its slot
    instead of leaving a gap."""
    L = len(buf)
    if L < 2:
        return np.zeros(count)
    idx = (pos + np.arange(count) * ratio) % L
    if reverse:
        idx = (L - 1) - idx
    i0 = np.floor(idx).astype(np.int64)
    frac = idx - i0
    i1 = (i0 + 1) % L
    return buf[i0] * (1 - frac) + buf[i1] * frac


def kaleidoscope(src, start=0.0, length=1.0, folds=4, prism=0.0,
                 xfade_ms=3.0, sr=SR, reps=4):
    """Render `reps` loop periods. Returns (audio, per-fold debug info)."""
    s0 = int(start * sr)
    Lp = int(length * sr)                 # loop period, CONSTANT in folds
    slot = Lp // folds                    # each fold's time slot
    shard = src[s0:s0 + slot]             # the shard shrinks as folds rise
    if len(shard) < 2:
        return np.zeros(Lp * reps), []

    xf = max(1, int(xfade_ms * sr / 1000.0))
    xf = min(xf, slot // 4)
    out = np.zeros(Lp * reps + slot + xf)
    info = []

    for r in range(reps):
        for f in range(folds):
            ratio = 2 ** (f * prism / 12.0)
            rev = (f % 2 == 1)                     # mirror
            seg = read_resampled(shard, 0.0, slot + xf, ratio, reverse=rev)
            # equal-power edges so boundaries do not click
            w = np.ones(len(seg))
            ramp = np.arange(xf) / xf
            w[:xf] = np.sin(ramp * np.pi / 2) ** 1.0
            w[-xf:] = np.cos(ramp * np.pi / 2) ** 1.0
            base = r * Lp + f * slot
            out[base:base + len(seg)] += seg * w
            if r == 0:
                info.append(dict(fold=f, ratio=ratio, reversed=rev,
                                 slot=slot, start=base))
    return out[:Lp * reps], info


# ------------------------------------------------------------- measurement ----
def f0_of(x, sr=SR):
    """Autocorrelation pitch estimate, for verifying the Prism law."""
    x = x - x.mean()
    if np.max(np.abs(x)) < 1e-6:
        return 0.0
    ac = np.correlate(x, x, 'full')[len(x) - 1:]
    lo, hi = int(sr / 2000), int(sr / 50)
    if hi >= len(ac):
        return 0.0
    lag = int(np.argmax(ac[lo:hi])) + lo
    return sr / lag if lag else 0.0


def max_step(x):
    """Largest sample-to-sample jump: the click detector."""
    return float(np.max(np.abs(np.diff(x)))) if len(x) > 1 else 0.0


def write(path, x, sr=SR):
    peak = np.max(np.abs(x)) + 1e-9
    wavfile.write(path, sr, (x / max(1.0, peak) * 0.9 * 32767).astype(np.int16))
