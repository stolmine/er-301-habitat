#!/usr/bin/env python3
"""Render POC demos: tonal sweep, drums, impulse/trigger train through the
systemic model at varying settings. Produces montage WAVs in out/.
Each montage plays the source through the SAME 6 settings so they compare."""
import os, numpy as np
from scipy.io import wavfile
import model as M

FS = M.FS
HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "out"); os.makedirs(OUT, exist_ok=True)
rng = np.random.default_rng(1)


# ---------- sources ----------
def src_sweep(dur=2.6):
    n = int(dur * FS); t = np.arange(n) / FS
    f1, f2 = 50.0, 4000.0; L = dur / np.log(f2 / f1)
    s = np.sin(2 * np.pi * f1 * L * (np.exp(t / L) - 1))
    fade = int(0.01 * FS); s[:fade] *= np.linspace(0, 1, fade); s[-fade:] *= np.linspace(1, 0, fade)
    return 0.7 * s


def _env(n, a, d):
    e = np.ones(n); ai = int(a * FS); di = int(d * FS)
    e[:ai] = np.linspace(0, 1, ai); e[ai:ai + di] = np.exp(-np.linspace(0, 6, di))
    e[ai + di:] = 0; return e


def _kick(dur=0.35):
    n = int(dur * FS); t = np.arange(n) / FS
    f = 120 * np.exp(-t * 35) + 50
    s = np.sin(2 * np.pi * np.cumsum(f) / FS) * np.exp(-t * 9)
    s[:20] += np.linspace(1, 0, 20)  # click
    return s * 0.9


def _snare(dur=0.25):
    n = int(dur * FS); t = np.arange(n) / FS
    tone = np.sin(2 * np.pi * 185 * t) * np.exp(-t * 18) * 0.5
    noise = rng.standard_normal(n) * np.exp(-t * 14)
    return (tone + noise) * 0.6


def _hat(dur=0.06):
    n = int(dur * FS); t = np.arange(n) / FS
    return rng.standard_normal(n) * np.exp(-t * 60) * 0.4


def src_drums(dur=2.6):
    n = int(dur * FS); x = np.zeros(n)
    step = 0.125  # 16ths at 120 bpm-ish
    def put(sig, at):
        i = int(at * FS); m = min(len(sig), n - i); x[i:i + m] += sig[:m]
    beats = int(dur / step)
    for b in range(beats):
        at = b * step
        if b % 4 == 0: put(_kick(), at)
        if b % 8 == 4: put(_snare(), at)
        put(_hat(), at)
        if b % 8 == 6: put(_kick(), at)
    return np.clip(x, -1, 1) * 0.8


def src_impulses(dur=2.6):
    n = int(dur * FS); x = np.zeros(n)
    for at in np.arange(0.15, dur, 0.32):
        i = int(at * FS)
        x[i:i + 8] += np.hanning(16)[:8] * (0.9 if int(at * 3) % 2 else 0.6)  # short bursts/triggers
    return x


# ---------- settings ----------
SETTINGS = [
    ("clean-bp",        dict(cutA=.5, res=.10, gain=1, clksrc=0, mode=1, alias=0)),
    ("resonant",        dict(cutA=.5, res=.88, gain=1, clksrc=0, mode=1, alias=0)),
    ("low-cut alias",   dict(cutA=.20, res=.45, gain=1, clksrc=0, mode=1, alias=0)),
    ("overdrive",       dict(cutA=.5, res=.30, gain=6, clksrc=0, mode=1, alias=0)),
    ("dual-clk comb",   dict(cutA=.45, cutB=.06, res=.35, gain=1.5, clksrc=2, mode=1, alias=0)),
    ("dual-clk selfosc",dict(cutA=.2, cutB=.9, res=.96, gain=1.5, clksrc=2, mode=1, alias=0)),
]


def norm(y, peak=0.5):
    p = np.max(np.abs(y)) + 1e-9
    return y * min(peak / p, 12.0)


def montage(src, name):
    gap = np.zeros(int(0.25 * FS))
    parts = []
    for tag, s in SETTINGS:
        y = M.render(src, **s)
        y = norm(y)
        parts.append(y); parts.append(gap)
        print(f"  {name}/{tag}: peak {20*np.log10(np.max(np.abs(y))+1e-9):.1f} dB, "
              f"rms {20*np.log10(np.sqrt(np.mean(y**2))+1e-9):.1f} dB")
    out = np.concatenate(parts)
    out = np.clip(out, -1, 1)
    path = os.path.join(OUT, f"poc_{name}.wav")
    wavfile.write(path, FS, (out * 32767).astype(np.int16))
    print(f"-> {path}  ({len(out)/FS:.1f}s)")


if __name__ == "__main__":
    print("settings order:", ", ".join(t for t, _ in SETTINGS))
    for name, fn in [("sweep", src_sweep), ("drums", src_drums), ("impulses", src_impulses)]:
        print(f"\n[{name}]")
        montage(fn(), name)
