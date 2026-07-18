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

### Gain staging / normalling (tested)
- **Gain A is A's own input amp** (per-filter), not a between-filters stage.
  Dropping Gain A noon->ccw left B's output unchanged (-33.2 vs -33.1 dBFS) when
  B is fed via the A->B audio normal. So the **normal taps the raw IN A signal
  PRE-Gain-A**; each filter has its own input gain.
- Implication for **series** (A out -> B in patched): B's input = A's output, so
  **Gain B is the effective between-filters drive** there. On the normal/parallel
  path, the two are independent.

### Clock source (A/B/both, shared) - MECHANISM CONFIRMED
- **CLK SRC selects which channel's clock tunes the filters.** Shown by monitoring
  B with offset cutoffs (A noon ~3.4 kHz, B 3 oc ~4.8 kHz) and resonance up:
  - **clk-A**: B's resonant peak @ **3.4 kHz** (A's clock drives B).
  - **clk-B**: B's peak @ **4.8 kHz** (B's own clock).
  - **clk-both**: B's peak ~4.5 kHz (near its own) with a broad resonance **smear**
    (ess deconvolution shows time-variance = slow clock drift wandering the peak).
    A sustained 1 kHz tone through B comes out **clean** (sidebands >70 dB down) ->
    clk-both is a **clock-blend, NOT fast AM/XOR modulation** of the signal.
- **Dormant at matched cutoffs + res down** (baseline): clk-A vs clk-both identical.
  Needs offset cutoffs + res up to show (the user's hypothesis, confirmed).

#### CLK=both dual-clock detail (instrumentation, monitoring B, A=noon fixed)
- **The "both" mode appears to XOR the two clock oscillators** and feed the result
  as the SC clock. Evidence (Cutoff-B divergence sweep, res 3 oc):
  - **f_B << f_A** (B ccw): B-BP becomes a **dense ~800 Hz-spaced COMB** of
    resonances (crest 6.5) - XOR sidebands (carrier f_A, spacing ~f_B) folded in-band.
  - **f_B rising** (B 9 oc): comb spreads -> single blended peak ~1.7 kHz.
  - **f_B = f_A** (converged): single peak at ~**4.8 kHz ≈ 2× f_A** (XOR of two equal
    square waves -> 2x). crest 3.9.
  - **f_B >= f_A** (B 3 oc, cw): single peak ~4.8 kHz (sidebands spread wide).
  - Comb spikiness (crest) tracks divergence: 6.5 -> 4.9 -> 3.9.
- **Model implication**: two clock oscillators (f_A, f_B from the two cutoffs);
  in "both" mode XOR/combine them and drive the SC clock with the result -> comb
  filtering that sweeps from dense (large downward divergence) to single-peak
  (convergence/upward). A ring-mod/comb generator, not simple detune.
- **DUAL-CLOCK SELF-OSCILLATION (corner discovery, contradicts spec).** At
  **A ccw / B cw / res cw / alias LO / clk both** (divergent clocks, max res) the
  module **self-oscillates with NO input**: -16 dBFS out, a **~74 Hz-spaced comb**
  of tones (74, 340, 414, 487 Hz). The spec's "does not self-oscillate" holds only
  for the resonance control on a single clock; the **XOR clock interaction at max
  res enables a self-oscillating comb**. A signature/aggressive corner - must be
  in the model (dual-clock + high res -> comb oscillator). Full extent (which
  corners osc, freq vs settings) mapped in the corner grid.
- **Caveat**: time-varying system; the ess deconvolution shows the effective
  (averaged) response. Sustained-tone probes (DC4) needed for the true sideband
  spectrum. TODO detail: gain/drive intermod (DC3), input freq/volume (DC4).

#### CLK=both 16-corner map (Cutoff A/B x Res x Alias, monitor B)
Clean rule structure emerged (each cell measured, none inferred):

| Cutoff A | Cutoff B | Res | behavior |
|---|---|---|---|
| ccw | ccw | any | quiet, low-freq, no osc (both clocks min; HI louder than LO) |
| ccw | cw | ccw | clock-XOR comb (~196/392 Hz), no osc, LP louder |
| ccw | cw | **cw** | **SELF-OSC low comb** - LO [74,340,414,487], HI [52,106,146,294] |
| cw | ccw | ccw | single peak ~4.7k / small comb, no osc |
| cw | ccw | **cw** | **SELF-OSC** - LO clean **~13.5 kHz (clock)**, HI dirty ~1.8k comb + ultra-HF aliases |
| cw | cw | ccw | single peak ~4.7k (converged), no osc |
| cw | cw | cw | comb (~659 Hz), borderline ringing, no sustained osc |

**Rules for the model:**
1. **Self-oscillation iff cutoffs DIVERGENT (one min, one max) AND res high.**
   Converged cutoffs never osc, even at max res (reconciles the spec).
2. **Osc frequency depends on divergence DIRECTION**: A-low/B-high -> LOW comb
   (~74 Hz beat); A-high/B-low -> HIGH clock tone (~13.5 kHz, clean).
3. **Alias reshapes the osc timbre** (LO clean vs HI dirty/lower + ultra-HF aliases).
4. **Low res, divergent** -> clock-XOR comb filter (no osc), spacing ~ f_B.
5. **Converged** (A=B) -> single blended peak (~2x at high, quiet at low).
So "both" = a clock-XOR comb/oscillator whose regime (comb / osc-low / osc-high /
single-peak) is set by the A-vs-B cutoff relationship and resonance.

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

### Resonance (shared, cutoff noon, alias LO)
- **Q law** (BP -3 dB, deconvolved): ccw ~1.1, 9 oc ~1.8, noon ~9.0, 3 oc ~16.5,
  cw ~35. Roughly exponential; most of the action in the upper half of the knob.
- **Max Q ~35 with NO self-oscillation** (matches spec). BP peak grows in height
  (energy ~constant, bandwidth shrinks); center holds ~3 kHz.
- **A fixed sharp spike at ~4.8 kHz** sits above the BP at low res (+11 dB) and gets
  buried as the resonant peak grows (-18 dB at max res). Present at cutoff noon and
  cw -> looks like a **fixed switched-cap artifact** (clock feedthrough or a fixed
  resonance near ~4.8 kHz), NOT the resonance peak (it does not grow with res).
  Investigate: does it move with cutoff? (If fixed -> clock-related.)

### CLOCK LAW (measured 2026-07-18, hot send, single-A)
- **N ~= 25 (FIXED); clock = cutoff x 25.** Evidence: at 9 oc the resonance is ~30 Hz
  and the alias images fall at ~762 Hz harmonics (762/1524/2286/3048) -> 30 x 25 = 750.
  So the cutoff knob sets the clock frequency (via a fixed divide ratio), NOT a
  variable N. Corrects the earlier "clock = 2x cliff" reading.
- The ~5 kHz **cliff is a fixed OUTPUT anti-alias filter**, not clock/2.
- **Cutoff-vs-knob**: steep exponential ~30 Hz (low) -> 636 Hz (noon) -> ~4.7 kHz
  (3 oc), **saturating past 3 oc** (cw = 3 oc ~4.7 kHz). Low-knob exact values are
  aliasing-confounded (unreliable below ~200 Hz) but the structure is solid.
- So aliasing emerges naturally: low cutoff -> low clock (<1 kHz) -> harmonics fold
  into band (the grit); high cutoff -> clock >10 kHz, out of the way. **POC fix:
  N 8 -> 25.**

### Influence tests (2026-07-18)
- **Mode does NOT retune the clock.** Clean back-to-back at 3 oc (only mode flipped,
  cutoff untouched): HP 4922 Hz vs hidden 4907 Hz = -0.3% (noise). An earlier +4%
  reading was pure cutoff-knob-reset drift. So **clock is independent of mode**
  (whatever "mode touches all outputs" is, it is not a frequency retune).
- **B's cutoff bleeds into A's clock ~6-7%, even at clk-src A** (B not the source).
  Reversible: B ccw -> A 4907/4948 Hz; B cw -> A 4618 Hz (-6%), snaps back. Higher B
  cutoff -> LOWER A resonance (inverse). **The two clocks are permanently weakly
  cross-coupled** - model needs a small (~6%) inverse cutoff cross-term even in
  single-source mode.

### SELF-OSC mechanism (2026-07-18, full-state logged in states.tsv)
At A ccw / B cw / res cw / clk both / alias lo (monitor B): input-off RMS -16 dBFS,
a **~32 Hz-spaced comb** (34/65/105 + 337..468) - spacing ~= Cutoff A (the LOW
channel's frequency). Mapping:
- **Res threshold ~4 o'clock, SHARP**: res cw -16 dBFS (strong osc), res 3 oc
  -82 dBFS (dead). Feedback gain must cross a threshold.
- **Cutoff A: MULTIPLE osc windows** (osc at ccw, fizzles by 9 oc, RETURNS at noon).
  A clock-RATIO resonance condition, not just "A low" - the XOR beat aligns with the
  resonance at certain f_A/f_B ratios. Comb spacing tracks Cutoff A.
- **Needs clk=both + high divergence + res>~4oc.** Converged or low-res -> no osc.
- **Model mechanism**: XOR clock-beat comb (low-freq, spaced ~f_A) amplified by the
  resonance feedback once res crosses the gain threshold. Build the XOR beat feeding
  a near-unity resonance and the multi-window osc should emerge (POC TODO: push q to
  the instability threshold at max res + feed the XOR beat into the resonant path).

### Distortion (input amps) - CHARACTERIZED (LP out, cutoff cw, res down)
- The **Gain knob is an input VCA**: off at ccw (silence), and its taper
  **compresses near the top** (3 oc -> cw only +2 dB). At the loopback send level
  (301 VCA x5) the module stays clean (THD ~0.15-0.21%) across the WHOLE gain
  range - it does not reach clipping. Overdrive needs a **hot drive** into IN A.
- Driven hard (301 VCA ~x20, gain cw), swept input level -> the clean->clip curve:

  | send RMS | out RMS | THD | note |
  |---|---|---|---|
  | -30..-18 | -40..-28 | 0.15-0.28% | clean (floor) |
  | -12 | -22.7 | **1.8%** | soft knee (all harmonics rise together) |
  | -06 | -20.0 | **22%** | hard clip, H3 dominant |
  | -04 | -19.7 | 27% | H3 dominant |

- **Output saturates ~-20 dBFS** (in-chain): linear 1:1 up to the knee, then hard
  compression. Soft knee ~6 dB below clip. Hard clip is **odd-harmonic dominant
  (H3, H5)** = symmetric clipping; crest factor 1.62 -> 1.47 (squaring toward a
  square wave). A saturating nonlinearity with a soft knee.
- TODO: distortion **mode-dependence** (spec says it changes with mode) - measure
  the overdrive on AP (raw, flat) and other outputs; and asymmetry vs symmetry per
  mode.

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
