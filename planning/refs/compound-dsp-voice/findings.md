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
