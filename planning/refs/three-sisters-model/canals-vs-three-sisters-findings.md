# Canals vs. Three Sisters — Findings

Comparison of the `canals` clone (`stolmine/er-301-habitat`, `mods/*/Canals.cpp`
+ `SistersSvf.h`) against the Three Sisters topology derived from the official
Whimsical Raps technical maps and the validated ZDF model (`three_sisters_svf.c`,
`ts_model.py`).

## Summary

The SVF core is the same family we used — Simper/Cytomic trapezoidal ZDF
(`hp = (in − r·s1 − g·s1 − s2)·h`, double-integrator state update). The skeleton
matches. Divergences are concentrated in three areas: **frequency-coefficient
accuracy, Q-placement (which stages resonate), and the nonlinear / self-oscillation
regime.** Crossover routing, CENTRE dual-resonance, the FREQ±SPAN exponential
structure, and the ER-301 v/oct scaling are already correct.

## Differences, ranked by impact

### 1. Frequency coefficient drops π → everything tuned ~3.14× flat
`SistersSvf::setFreqQ` uses `g = f*(1 + f*f*0.333333)` (Taylor of tan(x)). The
comment claims it matches stmlib `FREQUENCY_DIRTY`, but stmlib is
`f*(π + 0.3736·π³·f²)` — the leading π is missing.

Measured (Canals SVF, bp peak vs requested cutoff):

| requested | Canals peak | error | correct prewarp |
|-----------|-------------|-------|-----------------|
| 100 Hz    | 31.5 Hz     | 3.18× low | 100.3 Hz |
| 261 Hz    | 82.8 Hz     | 3.15× low | 260.7 Hz |
| 1000 Hz   | 318.6 Hz    | 3.14× low | 999.8 Hz |
| 4000 Hz   | 1272.9 Hz   | 3.14× low | 3999.8 Hz |

Relative octaves still track (ratios survive), so it behaves like a filter, but
absolute pitch and v/oct calibration are off by π. The cubic coefficient
(0.333 vs ~11.6) also degrades HF tracking near Nyquist. Fix: `g = tan(π·f)` (or
the full stmlib dirty approximation with the π term restored).

### 2. LOW/HIGH use two resonant stages; the real unit uses one
Canals sets `q` on both stages of every block
(`low1.setFreqQ(lowF,q); low2.setFreqQ(lowF,q)`, same for hi1/hi2). The official
block diagrams put resonance on **SVF1 only** for LOW/HIGH, with SVF2 a fixed
non-resonant 2-pole (≈ Butterworth). Result: LOW/HIGH over-resonate — compound
double-peak and altered self-osc — instead of one resonant core feeding a plain
2-pole. CENTRE (both stages resonant) is correct. Fix: fixed ~0.707 Q on
`low2`/`hi2`; keep resonance on `ctr1`/`ctr2`.

### 3. No true self-oscillation
`r = 1/q` is always > 0 and `softClip` only attenuates (a loss, never injects
energy), so the core strictly decays — at q=100 it rings long but won't sustain.
The real unit self-oscillates (sine "choir," ping-to-drone, CENTRE→SPAN 2-op FM).
Same trap encountered in our own model; fix is the slightly-negative-damping path
(`svf_tick_nl`, validated at 0.013% THD).

### 4. High-Q distorts where the real unit stays clean
`softClip` triggers at |state| > 2; at high Q the bp/lp states swing to ~Q×input
and slam the clipper, so the *cleanest* setting on the real unit becomes the
dirtiest on Canals. The low-distortion resonance signature is not reproduced.
Fix: move saturation into the resonance feedback term (tanh on bp in the loop)
rather than hard-clipping the integrator states.

### 5. Anti-resonance is generic dry/wet, not the topology notch
Canals does `out*(1−a) + (dry−out)*a` (input-minus-output spectral subtraction).
The real !Q taps the genuine complementary SVF output — LOW↔SVF1 hp, HIGH↔SVF1 lp,
CENTRE↔SVF1 lp + SVF2 hp — for true 180° notch cancellation. The approximation
gives a different notch depth and phase.

### 6. Control-rate only, one v/oct sample per frame
Reads `voct[0]` once per frame and gates coefficient recompute on a change
threshold → no audio-rate FM, no per-sample pitch, zipper risk on fast moves, and
the 2-op FM self-patch is impossible. No coefficient smoothing.

### 7. Single in / single out vs 4×4
Real Three Sisters has LOW/CENTRE/HIGH/ALL ins *and* outs (parallel, simultaneous)
— the spectral-mixer / three-source-crossfade / independent-band paradigm. Canals
has one input and an Output knob crossfading LOW→CENTRE→HIGH→ALL. Reasonable v1
scope cut, but the multi-source use case is gone.

### 8. Minor: HIGH formant stage order swapped
Canals does lp→hp; the real HIGH block keeps SVF1 always highpass (hp→lp).
Identical linear BP magnitude (the stages commute), but it differs once saturation
is in the loop and changes which tap is the complement for anti-resonance.

## Already correct
- Crossover routing taps: LOW lp→lp, CENTRE hp→lp, HIGH hp→hp
- CENTRE dual-resonance (both SVFs resonant), in both modes
- FREQ±SPAN exponential structure (semitone-based, `SemitonesToRatio`)
- ER-301 v/oct scaling (1.0 = 10 octaves = 120 semitones)
- Formant taps, except HIGH stage order (see #8)

## Fix priority
1, 2 are objective correctness (pitch + which stages resonate) → highest.
3, 4 recover the defining clean-resonance / self-osc behavior.
5, 6 refine character and modulation bandwidth.
7 is a deliberate scope decision, not a bug.
