# Tessera: clean-room implementation of the firmware-walk findings

Plan of record, 2026-07-20. Source findings live in
`~/Downloads/Trinity2_0_Firmware/re/findings-*.md` (deliberately out of git, next to the
binary). Ledger item `hab:trinity-fm-unit`. Current shipped state: **2.8.3.57**.

**Clean-room rule.** Implement *mechanism* read from the binary. Do not transcribe tables or
code verbatim. The empirical rig (`validate_grid.py` over `data/timbre2.jsonl`, 1138 clean
cells) stays the arbiter — every stage below reports a grid-match delta before it is adopted.

## Architectural note, read first

The hardware BLOCK engine is **two wavetable oscillators through ONE amplitude envelope**.
Tessera is an **additive modal engine: 14 modes, each with its own exponential envelope**.
These are different topologies. The measured two-class decay split (tone ~0.93-1.0, sidebands
~0.35) is explicit in our model and must be *emergent* in the hardware's.

Decision taken: keep the modal architecture and port the envelope **shape** (cubed linear
ramp), not the topology. Stage 3 is therefore a test of whether shape transfers across
topologies. A faithful single-envelope prototype remains a possible future fork if Stage 3
disappoints; it would answer whether the modal engine is the right abstraction at all.

## Not implementing — both eliminated by the walk

- **Parabolic sine.** Belongs to ORB and CLANG only. BLOCK and NEON use lookup tables. Adding
  it would inject harmonics the modelled hardware does not produce.
- **filter_N.** It is the EQ knob (CC 74), a post-clipper 2-pole TPT SVF, and it is provably
  neutral at the `eq=64` our clean map pins. Nothing to model for this corpus.

---

## Stage 1 — Grit scales dec_time above 0.75. HIGHEST CONFIDENCE

**Mechanism (READ, exact):** in the `grit > 0.75` zone only, grit scales the decay-time
parameter by `(1 - (grit - 0.75)*4)`, reaching exactly zero at grit = 1.0.

**Replaces:** the tau-ceiling harmonic sum at `Tessera.cpp:287-293`, which was already refuted
empirically. So this removes a known-wrong mechanism *and* installs a measured-exact one.

```
gN = ccGrit / 127
if (gN > 0.75) tauEff = tauC * max(1 - (gN - 0.75)*4, kTauFloor)
else           tauEff = tauC
```

**Design notes:**
- A floor is required: `k` reaches 0, and `expf(-1/(0*sr))` is a division by zero. Propose
  `kTauFloor` such that tau bottoms out around 1 ms.
- The tone collapsing to near-silence at max grit is **faithful, not a bug** — the noise path
  has its own envelope and survives, which is precisely the "just noise" behaviour the manual
  describes above 75%.
- This also settles the long-open tau-ceiling-vs-damping-rate question: it is neither.

**Independent corroboration:** this law is an exact match for what we recorded by ear as
"amp-env shortening past ~0.75 (808 snare)" back in the BLOCK profiling pass.

**Cost:** block-rate scalar. Free on am335x.

**Acceptance:** grid-match delta >= 0, and `decay_fund` / `decay_up` not worse. Watch high-grit
cells specifically.

---

## Stage 2 — Grit regime boundaries, and a likely wrong noise bed

**Mechanism (READ, partial):** the depth cascade has four zones with breakpoints at
**0.1 / 0.5 / 0.75** (CC 12.7 / 63.5 / 95.25). Recovered formulas: depth 0 below 0.1;
`(g - 0.1)*10*X` in the mid zone; a hardcoded `9.0*X` in the top zone.

**Ours today:** `noiseMix` onset at CC 25, transitions at CC 110 and CC 115; kappa from a
16-entry measured LUT.

**Two separate things to change, and they carry different risk:**

1. **Zone boundaries for FM depth.** Our dead zone (first three kappa entries ~0, i.e. CC 0-16)
   already brackets the firmware's 0.1 = CC 12.7 well. Keep the *measured kappa magnitudes*,
   move the *boundaries* to the exact values. Low risk.

2. **The noise bed below 0.75 is probably a modelling fudge.** Grit is noise-FM, not a noise
   bed — that is now confirmed at instruction level. The only genuine noise mixing in the
   firmware is the mix-out above 0.75. Our model blends noise from CC 25 upward, which likely
   compensates for FM depth we were not rendering. **Test removing the noise bed below 0.75
   entirely.** If grid match holds or improves, we have removed a fudge.

**Open discrepancy to respect, not paper over:** we measured "additive noise takes over above
CC 115"; the firmware boundary is CC 95.25. The manual describes above-75% as a *gradual*
mix-out, so the transition plausibly starts at 95 and completes near 115-127, making our hard
switch a midpoint fit. The zone formulas were also only partially recovered — `(g-0.1)*10`
reaches 1.0 at g=0.2 and the 0.2-0.5 behaviour was not read. **Treat as partial. Grid-validate;
do not blind-replace.**

**Acceptance:** grid delta >= 0 with the noise bed removed below 0.75. If it regresses, the bed
is carrying real signal and the FM depth is under-rendered — which is itself a useful finding.

---

## Stage 3 — Per-mode cubed linear ramp, and no attack

**Mechanism (READ):** the hardware envelope jumps to 1.0 in a single instruction — the attack
ramp path exists but is **dead code**, its rate constant exactly 0.0. The stored level then
decrements **linearly**, and the audio path uses **`level^3`**.

**Port the shape, per mode:**

```
// was:  env[m] *= mdecay[m];              // exp(-1/(tau*sr))
ramp[m] -= rate[m];                        // linear decrement
if (ramp[m] < 0) ramp[m] = 0;
e = ramp[m]*ramp[m]*ramp[m];               // cubed at output
```

and drop the 2 ms one-pole `mAtkEnv` (`Tessera.cpp:269-270, 416`).

**Three subtleties that will bite if missed:**

1. **`winAvg` becomes invalid.** The measured-to-initial amplitude correction at
   `Tessera.cpp:184-188` assumes an *exponential* decay: `W(t) = (t/T)(1 - e^(-T/t))`. A cubed
   linear ramp has a different window average and `W` must be re-derived for it, or the
   amplitude table is being corrected by the wrong law. This correction was worth +6 points of
   grid match when it was added, so getting it wrong will silently cost that back.
2. **`tau` -> ramp duration mapping.** Our fitted decay laws produce exponential time
   constants. A cubed ramp reaching zero at time T is not the same object. Decide the
   equivalence explicitly (match the -60 dB point, or match RMS over the analysis window) and
   write down which was chosen. `kTauR[]` per-mode ratios become duration ratios.
3. **Dropping the attack risks the flatness regression.** The 2 ms attack took signed flatness
   error from +20.9% to +0.4%. Removing it may hand that back — the firmware says the hardware
   has no attack, so if flatness degrades, something *else* softens the hardware onset.
   `smooth_enable_N` is the standing suspect and no code reading it exists anywhere in the DSP
   corpus. **Run the grid before and after, and treat a flatness regression as evidence about
   `smooth_enable_N`, not as a reason to reinstate the fudge.**

**Cost:** roughly a wash on am335x (one multiply either way; the cube is two multiplies but
only at output, and the linear decrement is an add). Removing the attack one-pole is a small
saving.

**Acceptance:** grid delta reported honestly per metric. This is the stage most likely to
regress; a negative result here is a real finding about topology, not a failure.

---

## Deferred

- **Polynomial sine for NEON.** Cortex-A8 has no gather load, so the 14-mode LUT bank cannot
  vectorise. A high-order polynomial *matching our LUT's -97 dB* (explicitly NOT the hardware's
  -64 dB approximation) would make the bank NEON-able 4-wide. Deferred until the fidelity work
  settles, so grid deltas stay attributable to one thing at a time.
- **Character's register.** With s3 = Grit and s2 = Shape, nothing in the recovered mapping is
  Character. It may be `s1` (undetermined) or it may reach the engine another way. Our fitted
  CC 78 dead zone stands unchallenged again.
- **`e_hi` +26%.** Both suspects eliminated. No identified mechanism. Open question — not a
  tuning target.
- **The EQ confound.** The original 1143-capture corpus never sends CC 74, and the per-mode
  amplitude table was regression-fitted against it. Worth settling whether that knob sat inside
  the CC 57-70 dead zone before trusting the fitted amplitudes.

## Sequencing

Stages are independent and land separately, each with its own version bump and grid run:
1 (2.8.3.58) -> 2 (2.8.3.59) -> 3 (2.8.3.60). Both arches every build
(`make spreadsheet ARCH=linux` and `ARCH=am335x`), linux copied to `~/.od/rear/`, version
bumped every dev build or the hardware runs a stale extraction.
