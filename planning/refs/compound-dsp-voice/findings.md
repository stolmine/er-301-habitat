# Compound DSP voice - profiling findings (running log)

DUT: Industrial Music Electronics "Bionic Lester" dual switched-capacitor filter.
Level chain + noise fingerprint: `calibration.md`. Plan: `capture-matrix.md`.
Analysis: `measure.py` (ESS Farina deconvolution, validated against the known LP).

## Confirmed

### Filters A/B
- **A and B are the same circuit** - matched to 0.2 dB RMS, 0.0 Hz, THD within
  0.08% (leg check, cutoff cw / gain noon / res down). Plan to **model one filter**
  and reuse; Phase B confirms across more settings.
- Stereo capture is valid (converter legs + filters match to 0.2 dB).

### Mode shapes (multi output, cutoff noon, res down, alias LO)
- **HP**: high-pass, -3 dB corner ~2.2 kHz, passes to the SC ceiling.
- **AP**: flat magnitude (true allpass; action is in phase).
- **Notch**: deep band-reject, ~2.2 kHz, -31 dB, recovers both sides.
- **Hidden (6th mode)**: a **brighter, shallower notch** - dip at ~2.2 kHz but
  only -25 dB, and recovers higher than any mode toward the ceiling (most HF
  passthrough). Reads as **notch + partial high-pass**. Genuinely distinct.
- LP (12 dB/oct) and BP are dedicated hard outs; the mode toggle only moves the
  multi out. All modes share the corner/notch frequency (same SVF core).

### Aliasing (LO/HI, shared)
- **HI adds a steep ~5 kHz HF rolloff**, below the ~7 kHz SC ceiling. It only
  bites the HF-active modes: HP (narrows to a band ~4.5 kHz), Notch (notch gone ->
  steep rolloff), Hidden (notch + HF lift gone). **AP is barely affected.**
- **LO = HF-rich / bright** (more aliasing-prone content up to the ceiling).
- Aliasing does **not** move the SC ceiling (that is set by cutoff/clock).

### Clock source (A/B/both, shared)
- **Dormant at baseline** (single-A, res down, both cutoffs matched): clk-A vs
  clk-both identical to <0.5 dB. Its real action needs **resonance up, cutoffs
  offset, and monitoring B** (user hypothesis) -> deferred to Phase X.

### Cutoff (per channel; switched-cap clock)
- Tuning range **~30 Hz to ~5 kHz**, roughly exponential, **saturating past
  3 o'clock** (3 oc ~= cw, corner ~4.8 kHz).
- **SC clock ceiling** (hard cliff = clock Nyquist) ~6.5-7 kHz for 9 oc..cw;
  **collapses to ~500 Hz at min cutoff**.
- **Switched-cap ALIASING, strongest at low cutoff**: the low clock's images fold
  into the audible band. At 9 oc the filter is tuned to ~30 Hz but there are
  clock images at ~0.8 / 1.6 / 3.2 kHz only 25-36 dB down. This is the module's
  signature and a core modeling target.
- A slight **resonant bump at the corner even at res-min** (SC peaking near clock).

### Distortion (input amps)
- **~0.5% THD, H2-dominant** (H2 -46 dB, H3 -51 dB) already at **noon gain**, LP
  out, cutoff cw. Grows with gain; character is mode-dependent (per spec).
  Full map in Phase D.

## Measurement caveats / refinements needed
- **Low-cutoff outputs are very quiet** (near the -78 dBFS floor). Use a **hotter
  send** for the low-cutoff / aliasing captures, or the deconvolution is marginal.
- **Auto corner/ceiling extraction is unreliable** where aliasing folds in and
  where normalization catches a spurious peak. Trust the deconvolved curves (eye)
  over the auto-features at low cutoff.
- **Precise cutoff law**: track the **BP center** with adequate SNR (cleaner than
  the LP corner), or use stepped sines.
- **"Cutoff noon" drifts between repatches** - reset carefully, or avoid changing
  cutoff mid-set. (Caught a ~5 dB corner shift during a BP repatch.)

## Remaining plan (refined)
1. **Cutoff law (clean)**: hotter send, BP-center tracking across the 5 positions.
2. **Aliasing deep-dive**: `shf` probe at low cutoff; map image frequencies/levels
   vs cutoff and vs alias LO/HI.
3. **Resonance (Phase K-res)**: Q + near-corner peaking vs the shared res knob.
4. **Distortion (Phase D)**: gain sweeps per mode; distortion-vs-input-level.
5. **Phase X**: clk-src (res up, offset cutoffs, monitor B), series mode pairs,
   normalling (audio + cutoff-CV).
6. **Phase B**: confirm A/B match holds across settings.
