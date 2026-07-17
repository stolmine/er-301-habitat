#!/usr/bin/env python3
# Inventory the Plenum wardrobe mapping: how does the 5-D control space
# (Size, Decay, Bass, Damp, Weave; Mix excluded) map to effect activations?
# Are the effects orthogonal (diverse combinations) or correlated (clustered)?
# Is "drama" evenly spread across states?
import numpy as np

rng = np.random.default_rng(0)
N = 200_000
S  = rng.random(N)   # Size
D  = rng.random(N)   # Decay
B  = rng.random(N)   # Bass
M  = rng.random(N)   # Damp
W  = rng.random(N)   # Weave
clip = lambda x: np.clip(x, 0.0, 1.0)

def report(name, acts, drama):
    print(f"\n===== {name} =====")
    A = np.stack(list(acts.values()))          # (k, N)
    keys = list(acts.keys())
    # per-effect mean activation + fraction "off" (<0.05)
    print("  effect         mean   frac<0.05")
    for k, v in acts.items():
        print(f"    {k:11s} {v.mean():6.3f}   {np.mean(v<0.05):5.2f}")
    # inter-effect correlation (want LOW off-diagonal = orthogonal)
    C = np.corrcoef(A)
    off = C[np.triu_indices(len(keys), 1)]
    print(f"  inter-effect |corr|: mean {np.abs(off).mean():.3f}  max {np.abs(off).max():.3f}")
    # drama spread: want ~uniform (deciles evenly climbing, low mass at 0)
    dec = np.percentile(drama, np.arange(0, 101, 10))
    print(f"  drama deciles: " + " ".join(f"{x:.2f}" for x in dec))
    print(f"  drama: mean {drama.mean():.3f} std {drama.std():.3f} "
          f"frac<0.1 {np.mean(drama<0.1):.2f} frac>0.7 {np.mean(drama>0.7):.2f}")

# ---- CURRENT (2.8.3.16) ----
mass = 0.5*(S+D)
ferocity = D*(1-W)
bright = clip(0.5+B-M)
wardPresence = clip(ferocity + mass*0.3 - 0.15)
cur = {
    "fold":  wardPresence*ferocity,
    "crush": wardPresence*mass,
    "ring":  wardPresence*ferocity*0.7,
    "comb":  wardPresence*ferocity*0.6,
}
report("CURRENT 2.8.3.16", cur, wardPresence)

# ---- REDESIGN A: one distinct control axis per effect (orthogonal) ----
# each effect driven by its own control -> decorrelated; drama = balanced mean
a_fold   = D               # energy
a_crush  = S               # bigness -> aggressive SR reduction
a_ring   = clip(1-W)       # sparseness -> clang
a_muffle = M               # darkness -> lowpass
a_drive  = B               # weight -> asymmetric drive
# comb from a mid-favoring combo to fill coverage (bell around mid settings)
a_comb   = clip(1 - 2*np.abs(mass-0.5))    # peaks at mass~0.5
red = {"fold":a_fold,"crush":a_crush,"ring":a_ring,
       "muffle":a_muffle,"drive":a_drive,"comb":a_comb}
dramaA = np.mean(np.stack(list(red.values())), axis=0)
report("REDESIGN A (per-control, mean-drama)", red, dramaA)

# ---- REDESIGN B: same axes, drama = max (so any pushed axis = dramatic) ----
dramaB = np.max(np.stack(list(red.values())), axis=0)
report("REDESIGN B (per-control, max-drama)", red, dramaB)

# ---- REDESIGN C: directional ramps (calm end = off) -> clean corner + spread
ramp = lambda x, lo, hi: clip((x-lo)/(hi-lo))
c_fold   = ramp(D, 0.45, 1.0)          # long decay
c_crush  = ramp(S, 0.4, 1.0)           # big size (aggressive SR reduction)
c_ring   = ramp(1-W, 0.45, 1.0)        # sparse weave
c_muffle = ramp(M, 0.45, 1.0)          # dark
c_drive  = ramp(B, 0.5, 1.0)           # heavy bass
c_comb   = ramp(ferocity, 0.12, 0.8)   # long+sparse resonators
redC = {"fold":c_fold,"crush":c_crush,"ring":c_ring,
        "muffle":c_muffle,"drive":c_drive,"comb":c_comb}
dramaC = np.mean(np.stack(list(redC.values())), axis=0)
report("REDESIGN C (directional ramps, mean-drama)", redC, dramaC)

# default patch (S.5 D.6 B.5 M.3 W1) drama under C:
d0 = np.mean([ramp(0.6,0.45,1),ramp(0.5,0.4,1),ramp(1-1,0.45,1),
              ramp(0.3,0.45,1),ramp(0.5,0.5,1),ramp(0.6*(1-1),0.12,0.8)])
print(f"\n  [C] default-patch drama = {d0:.3f} (want low = clean-ish default)")
