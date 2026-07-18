#!/usr/bin/env python3
"""ESS-sweep -> magnitude response for the capture matrix.

Regularized deconvolution against the ideal exponential sweep (same params as
gen_excitation.py's ess): H(f) = Y conj(R) / (|R|^2 + lambda), then time-gate the
linear impulse response (drops the harmonic-distortion products, which land at
earlier times) and FFT it. The pre-roll delay in the capture only adds linear
phase, so it does not affect the magnitude.

    ./measure.py raw/bl_C_A_hp-lo-a_m+lp.wav [more.wav ...]

Prints a comparative 1/3-octave magnitude table (L channel = the multi/mode out;
R channel = the reference tap, usually LP) plus detected features per column.
Column labels come from the config token in the filename (bl_C_A_<mode>-...).
"""
import sys, subprocess, os
import numpy as np

FS = 48000


def ideal_ess(f1=20.0, f2=20000.0, dur=8.0):
    N = int(dur * FS); t = np.arange(N) / FS; L = dur / np.log(f2 / f1)
    x = np.sin(2 * np.pi * f1 * L * (np.exp(t / L) - 1.0))
    n = int(0.01 * FS); x[:n] *= np.linspace(0, 1, n); x[-n:] *= np.linspace(1, 0, n)
    return x


def read_wav(path):
    raw = subprocess.run(
        ["sox", path, "-t", "raw", "-e", "float", "-b", "32", "-r", str(FS), "-c", "2", "-"],
        capture_output=True).stdout
    x = np.frombuffer(raw, dtype=np.float32)
    return x[:len(x) // 2 * 2].reshape(-1, 2)


def impulse_response(rec_ch, ref):
    N = 1 << int(np.ceil(np.log2(len(rec_ch) + len(ref))))
    R = np.fft.rfft(ref, N)
    Y = np.fft.rfft(rec_ch, N)
    lam = 1e-3 * np.max(np.abs(R) ** 2)
    H = Y * np.conj(R) / (np.abs(R) ** 2 + lam)
    h = np.fft.irfft(H)
    # linear IR = largest-magnitude peak; gate -5..+95 ms around it
    pk = int(np.argmax(np.abs(h)))
    a = max(0, pk - int(0.005 * FS)); b = min(len(h), pk + int(0.095 * FS))
    seg = h[a:b] * np.hanning(b - a)
    return seg


def mag_bins(seg, edges):
    Nf = 1 << int(np.ceil(np.log2(len(seg) * 4)))
    S = np.abs(np.fft.rfft(seg, Nf)); f = np.fft.rfftfreq(Nf, 1 / FS)
    out = []
    for lo, hi in zip(edges[:-1], edges[1:]):
        m = (f >= lo) & (f < hi)
        out.append(20 * np.log10(np.sqrt(np.mean(S[m] ** 2)) + 1e-20) if m.any() else np.nan)
    return np.array(out)


def label(path):
    toks = os.path.basename(path).split("_")
    return toks[3].split("-")[0] if len(toks) > 3 else os.path.basename(path)


def features(centers, db):
    # Ignore bins in the post-cliff noise floor (below -80 dB re peak); the
    # switched-cap ceiling drops the response there and it is not a real feature.
    valid = db > (np.nanmax(db) - 80)
    pb = np.nanmax(db); i_pb = int(np.nanargmax(db))
    corner = None
    for i in range(i_pb, len(db)):
        if not valid[i]: break
        if db[i] <= pb - 3: corner = centers[i]; break
    corner_lo = None
    for i in range(i_pb, -1, -1):
        if not valid[i]: break
        if db[i] <= pb - 3: corner_lo = centers[i]; break
    # switched-cap ceiling = first bin (above the passband peak) that falls off cliff
    ceiling = None
    for i in range(i_pb, len(db)):
        if not valid[i]: ceiling = centers[i]; break
    # deepest local dip (notch) that recovers on both sides, within the valid band
    notch = None
    for i in range(1, len(db) - 1):
        if not (valid[i - 1] and valid[i] and valid[i + 1]): continue
        if db[i] < db[i - 1] and db[i] < db[i + 1] and db[i] < pb - 12:
            if notch is None or db[i] < notch[1]: notch = (centers[i], db[i])
    return pb, corner_lo, corner, notch, ceiling


ref = ideal_ess()
edges = np.array([31.25 * 2 ** (k / 3) for k in range(31)])   # ~31 Hz .. ~18 kHz, 1/3 oct
centers = np.sqrt(edges[:-1] * edges[1:])

cols = []
for path in sys.argv[1:]:
    rec = read_wav(path)
    seg = impulse_response(rec[:, 0], ref)
    db = mag_bins(seg, edges)
    db = db - np.nanmax(db)
    cols.append((label(path), db, features(centers, db + 0)))

hdr = "  freq   " + "".join(f"{c[0]:>8}" for c in cols)
print(hdr); print("  " + "-" * (len(hdr) - 2))
for r, fc in enumerate(centers):
    row = f"  {fc:6.0f}  " + "".join(f"{c[1][r]:8.1f}" if not np.isnan(c[1][r]) else f"{'':>8}" for c in cols)
    print(row)
print("\nfeatures (L channel, magnitude re passband peak):")
for name, db, (pb, clo, chi, notch, ceil) in cols:
    nn = f"notch~{notch[0]:.0f}Hz({notch[1]:.0f}dB)" if notch else "no notch"
    cl = f"SC-ceiling~{ceil:.0f}Hz" if ceil else ""
    print(f"  {name:>5}: corner_lo={clo and round(clo)} corner_hi={chi and round(chi)}  {nn}  {cl}")
