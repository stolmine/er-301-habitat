#!/usr/bin/env python3
# Tune the Rauschen "Cellular" per-instance emergent FREQUENCY mapping. Mirrors
# the C++ caHashU / caGridVal / caVNoise EXACTLY (uint32 semantics) so the
# distribution we tune here is the one the unit actually produces.
# Goal: no V/Oct; freq emerges from (X, Y, per-instance seed); LOW weighting
# (~50% of cases below 220Hz); upward excursions driven by Y.
import numpy as np

M32 = 0xFFFFFFFF
def hashU(a):
    a &= M32
    a ^= a >> 16; a = (a * 0x7feb352d) & M32
    a ^= a >> 15; a = (a * 0x846ca68b) & M32
    a ^= a >> 16
    return a & M32

def gridVal(i, j, seed):
    h = hashU((i * 374761393 + j * 668265263 + seed) & M32)
    return (h >> 8) * (1.0 / 16777216.0)

def vnoise(x, y, seed):
    G = 3.0
    fx, fy = x * G, y * G
    i, j = int(fx), int(fy)
    tx, ty = fx - i, fy - j
    tx = tx * tx * (3 - 2 * tx); ty = ty * ty * (3 - 2 * ty)
    v00 = gridVal(i, j, seed);     v10 = gridVal(i + 1, j, seed)
    v01 = gridVal(i, j + 1, seed); v11 = gridVal(i + 1, j + 1, seed)
    a = v00 + tx * (v10 - v00); b = v01 + tx * (v11 - v01)
    return a + ty * (b - a)

def instSeed(k):
    return hashU((0x9E3779B9 + k * 0x85EBCA6B) & M32)

# sample: 240 instances x 24x24 (X,Y) grid
KS = range(240)
XY = np.linspace(0.0, 1.0, 24)
samples = []  # (nF, px, py)
for k in KS:
    sd = (instSeed(k) + 505) & M32
    for px in XY:
        for py in XY:
            samples.append((vnoise(px, py, sd), px, py))
S = np.array(samples)
nF, PX, PY = S[:, 0], S[:, 1], S[:, 2]
print(f"caVNoise field: mean={nF.mean():.3f} std={nF.std():.3f} "
      f"min={nF.min():.3f} max={nF.max():.3f} (roughly-uniform check)")

def report(name, Fref, A, B, floor=20.0, ceil=12000.0):
    octExp = A * nF + B * (PY * PY)
    f = Fref * np.power(2.0, octExp)
    f = np.clip(f, floor, ceil)
    below = np.mean(f < 220.0)
    loY = f[PY < 0.34]; hiY = f[PY > 0.66]
    print(f"\n{name}: Fref={Fref} A={A} B={B}")
    print(f"  P(f<220Hz) = {below*100:.1f}%   (target ~50%)")
    print(f"  percentiles Hz: p5={np.percentile(f,5):.0f} p25={np.percentile(f,25):.0f} "
          f"p50={np.percentile(f,50):.0f} p75={np.percentile(f,75):.0f} "
          f"p95={np.percentile(f,95):.0f} max={f.max():.0f}")
    print(f"  Y-excursion: median @loY(Y<.34)={np.median(loY):.0f}Hz  "
          f"median @hiY(Y>.66)={np.median(hiY):.0f}Hz  "
          f"P(f<220)@loY={np.mean(loY<220)*100:.0f}% @hiY={np.mean(hiY<220)*100:.0f}%")

none = None
# candidates: Fref sets the floor; A = X/Y field weight; B = Y-squared upward lift
report("cand1", 40.0, 2.2, 3.4)
report("cand2", 45.0, 2.0, 3.2)
report("cand3", 40.0, 1.8, 3.6)
report("cand4", 38.0, 2.4, 3.0)
report("cand5", 45.0, 2.4, 3.6)

print("\n--- refinement around cand5 ---")
report("r1", 50.0, 2.4, 3.6)
report("r2", 48.0, 2.5, 3.7)
report("r3", 50.0, 2.5, 3.7)
report("r4", 52.0, 2.4, 3.6)
