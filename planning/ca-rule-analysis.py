#!/usr/bin/env python3
# Rank all 256 elementary CA rules by sonic "interestingness" so Vivary's Rule
# control can favor the lively (Wolfram class III/IV) rules instead of the dull
# (class I/II) ones. Metrics from both a single-cell seed (Vivary's reseed
# state) and a random seed (steady running texture).
import numpy as np
rng = np.random.default_rng(0)

def lut(rule):
    return np.array([(rule >> b) & 1 for b in range(8)], dtype=np.uint8)

def run(rule, L, G, seed):
    lu = lut(rule); c = seed.copy(); rows = [c.copy()]
    for _ in range(G):
        l = np.roll(c, 1); r = np.roll(c, -1)
        c = lu[(l << 2) | (c << 1) | r]
        rows.append(c.copy())
    return np.array(rows)

def score_rule(rows, L):
    last = rows[-1]
    died = (last.sum() == 0 or last.sum() == L)
    change = np.mean(rows[1:] != rows[:-1])
    half = rows[len(rows) // 2:]
    variety = len(set(map(tuple, half))) / len(half)  # distinct rows (non-periodic)
    # spatial entropy: mean row entropy (balanced/structured rows, not all-same)
    p = np.clip(rows.mean(1), 1e-6, 1 - 1e-6)
    sent = np.mean(-(p * np.log2(p) + (1 - p) * np.log2(1 - p)))
    # COMPLEXITY = structured rows (entropy) that keep renewing (variety).
    # Period-2 flippers get killed by variety; class III/IV rules score high.
    interest = sent * variety * (0.0 if died else 1.0)
    return interest, change, variety, died

L, G = 128, 160
res = []
for rule in range(256):
    # single-cell seed (Vivary's reseed)
    s1 = np.zeros(L, np.uint8); s1[L // 2] = 1
    i1, ch1, v1, d1 = score_rule(run(rule, L, G, s1), L)
    # random seed (steady texture)
    sr = (rng.random(L) < 0.5).astype(np.uint8)
    ir, chr_, vr, dr = score_rule(run(rule, L, G, sr), L)
    res.append((0.5 * i1 + 0.5 * ir, rule, ch1, d1, chr_, dr))

res.sort(reverse=True)
print("TOP 40 rules by interestingness (single-cell + random averaged)")
print("rk rule  score  chg(1cell) died1  chg(rand) diedR")
for i, (sc, rule, ch1, d1, chr_, dr) in enumerate(res[:40]):
    print(f"{i+1:2d} {rule:3d}   {sc:.3f}   {ch1:.2f}      {int(d1)}     {chr_:.2f}      {int(dr)}")

live = [r for r in res if r[0] > 0.15]
print(f"\n{len(live)}/256 rules score > 0.15 (lively). "
      f"single-cell deaths: {sum(1 for r in res if r[3])}/256")
print("curated 32 (interesting, spread):",
      sorted(r[1] for r in res[:32]))
