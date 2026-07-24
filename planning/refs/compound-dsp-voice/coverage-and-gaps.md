# Coverage and honest gaps - Bionic Lester profiling -> unit

Companion to `findings.md` (what we measured), `synthesis.md` (the model brief),
`capture-matrix.md` (the plan). This doc is the **honest ledger of what we know,
what we assumed, and what we never tested**, so the unit is built with eyes open.

Read this before trusting the POC or scoping the unit. Nothing here is a reason not
to start building - it is the map of where the model is load-bearing vs guessing.

## Confidence tiers

### TIER 1 - Measured, high confidence
- A and B are the same circuit (matched 0.2 dB / 0 Hz / THD 0.08%) at ONE setting.
- Mode shapes on the MULTI out (HP / AP / Notch / hidden) at cutoff-noon / res-down.
- Aliasing HI = a steep ~5 kHz HF rolloff that bites only HF-active modes.
- Gain is a per-channel input VCA; the A->B normal taps pre-Gain-A.
- Clock law N~25 (clock = cutoff x 25) - from ONE measurement (9 oc image spacing).
- Distortion transfer on the LP out: clean -> soft knee (~-12) -> hard clip 22-27%
  odd-harmonic (H3) - output saturates ~-20 dBFS.
- Resonance Q law on BP single-clk: ~1.1 (ccw) -> ~35 (cw), no self-osc single-clk.
- Self-osc EXISTS at clk-both + divergent cutoffs + res>~4 oc (does NOT single-clk).
- Self-osc BREATHES: 15 s capture, adjacent-frame spectral corr 0.78, carrier +/-26%.
- Mode reshapes ALL outputs and shifts the self-osc ~5 kHz, but leaves the clock RATE
  and passive cutoff unchanged (alias-image hold test) -> mode = SVF feedback topology.
- Cutoffs weakly cross-couple (~6% inverse) even in single-source mode (one test).

### TIER 2 - Inferred / modeled, NOT experimentally isolated
These fit the data but were never proven as THE mechanism. The POC picks one.
- **What "clk = both" actually is.** findings.md contains an unresolved tension:
  line 49 "clk-both is a clock-BLEND, NOT fast AM/XOR modulation of the signal" vs
  line 54 "appears to XOR the two clock oscillators." The POC v5 uses a THIRD model -
  **two cores cascaded, each on its own clock, + a shared resonance loop** - because
  it is what makes the comb track f_B and unifies the behaviors. It reproduces the
  observations (comb tracks f_B, 2x at convergence, self-osc at divergence) but we
  have NOT experimentally distinguished cascade vs XOR vs blend. This is the single
  biggest "modeled not measured" call.
- **Self-osc = shared resonance loop** (POC) vs **clock feedthrough self-sustaining**
  (findings.md line 184). Both explain "needs both clocks + high res." Not isolated.
- **Comb spacing = f_B.** Confirmed at only TWO low-cutB points (fB 909->861,
  1101->1002). Not swept across the range; detector fumbles wide spacings.
- **N = 25 fixed.** One data point. Not confirmed that N is constant across cutoff
  (could vary; the low end is aliasing-confounded and unreliable below ~200 Hz).
- **Cross-coupling 6%.** One reversible test; direction/magnitude not mapped.

### TIER 3 - Partially measured, SNR- or drift-limited
- Cutoff-vs-knob curve: structure solid, absolute Hz unreliable below ~200 Hz
  (near the -78 dBFS floor; needs a hotter send + BP-center tracking).
- The 16-corner clk-both map: extremes (ccw/cw) only, ONE instance each, not a
  full 3x3 divergence grid; the interior transition boundaries are interpolated.
- Aliasing image frequencies/levels: read off ess deconvolution, not a dedicated
  `shf` fold-back sweep at low cutoff.
- "Cutoff noon" drifts ~5 dB between repatches - some cross-take comparisons carry
  that uncertainty.

## COMPLETELY UNTESTED (never captured or explored)

Enumerated against the capture matrix - these are honest zeros, not light coverage:

1. **Distortion mode-dependence (Phase D-gain, ~60 planned, ~0 done).** The spec's
   headline claim - "distortion character changes with mode" - is UNTESTED. We only
   measured overdrive on the LP out. AP/HP/Notch/hidden overdrive, and symmetry-vs-
   mode, are unmeasured. The POC's distortion is a generic cubic softclip, not fit.
2. **Series routing (Phase X-series/X-series-od, ~14 planned, 0 done).** A-out -> B-in
   mode pairs (LP-HP, BP-BP, N-BP, hid-LP, ...) and series overdrive: never captured.
   The POC's "both" is a cascade but was NOT validated against real series takes.
3. **Normalling (Phase X-norm, 6 planned, ~1 partial).** The A-audio->B-input and
   A-cutoff-CV->B-cutoff normals: only the Gain-A-normal point was tested.
4. **CV response - ALL of it.** Cutoff CV, resonance CV, gain CV, the cutoff-CV
   normal: the module is voltage-controlled and we measured KNOBS ONLY. No CV scaling,
   no v/oct tracking of the self-osc (playing it as an oscillator), no CV-rate limits.
5. **A/B match across settings (Phase B, ~12 planned, 1 done).** Match confirmed at
   ONE setting; drift across cutoff/res/mode is assumed, not verified.
6. **Full Phase C config survey (24 configs, ~partial).** Mode shapes done at one
   knob setting on single-clk; the clk=both x mode x alias grid is not systematic.
7. **DC3/DC4 intermod.** Gain/drive through the combs, and input-frequency-x-volume
   through the combs/osc: flagged, never run.
8. **The clock waveform itself.** We infer a square wave; never scoped. Duty cycle,
   edge shape, actual feedthrough spectrum: unknown.
9. **The ~5 kHz anti-alias cliff shape.** Modeled as crude FIR smoothing; the real
   filter order/shape is unmeasured.
10. **Thermal / warmup drift, unit-to-unit variation.** One unit, one session-pair.
11. **Resonance as a tuned oscillator.** Whether the self-osc tracks 1 v/oct well
    enough to play melodically (a common use) is untested.
12. **Extreme/edge knob interactions** beyond the sampled corners (the matrix's
    "we do NOT sweep everything everywhere" - the un-swept interior is unknown).

## POC-model gaps (the model itself, unvalidated)

- **NO NULL TEST.** The POC has never been compared sample- or spectrum-wise against
  an actual recording. It is validated ONLY against descriptive metrics (comb
  spacing, morph-corr, RMS gating). The capture set exists; the null test does not.
  This is the highest-value next step for model fidelity.
- Converged self-osc currently reads LOUDER than divergent; hardware relationship
  not confirmed (may need a divergence-dependent loop gain).
- Mode self-osc shift is qualitative (centroid moves) but not fit to the measured
  hidden~4.7k -> HP~9.7k ~5 kHz shift.
- Breathing morph-corr ~0.70 vs measured 0.78 - close, not matched.
- Aliasing (HI) and distortion shapes are generic, not fit to the measured curves.
- Cross-coupling, drift depth (5%), feedthrough (3%) are plausible constants, not fit.

## Observability gap (process honesty)

- `states.tsv` (full-state logging) covers only the LAST 9 captures. The earlier ~70
  captures' exact knob states live in `findings.md` PROSE only - reconstructable but
  not machine-checkable. Drift between those takes is not fully auditable.
- ~82 captures done of a planned ~180 (~45%), front-loaded onto the clk=both / self-
  osc / mode payoff and light on distortion, routing, CV, and A/B.

## What this means for the unit

The model is strong enough to build a UNIT THAT SOUNDS LIKE THE FAMILY and nails the
prized behaviors (aliasing grit, clock combs, breathing self-osc, mode reshaping).
It is NOT yet validated to be faithful in the untested regimes (distortion-by-mode,
series/normalling, CV, exact tuning). Build the systemic skeleton now; treat Tier 2/3
constants as tunable and the untested list as the validation backlog once we can null-
test against the captures. The unit should EXPOSE the emergent mechanisms, not bake in
the specific measured numbers, per `synthesis.md`.
