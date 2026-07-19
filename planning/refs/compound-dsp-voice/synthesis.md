# Compound DSP voice - profiling synthesis & modeling brief

DUT: Industrial Music Electronics "Bionic Lester" dual switched-capacitor filter.
This is the handoff from profiling to the eventual 301 unit. Full detail:
`findings.md`; capture harness: this directory; level chain: `calibration.md`.
**Honest coverage/confidence ledger (read before trusting the model or scoping the
unit): `coverage-and-gaps.md`.**

## Modeling philosophy (read first)

Three principles govern the eventual unit:

1. **Complete coverage, not generalization.** This module is non-orthogonal and
   full of corner-case surprises (mode touches every output; self-osc appears only
   at divergent-clock + high-res; aliasing reverses sign with cutoff). We do NOT
   assume behavior between measured points - every regime must be captured. The
   complete capture set is the **validation target** for the model.

2. **Not bit-exact - emergent-by-construction.** We are not chasing sample-exact
   reproduction (it is a chaotic, drifting, analog switched-cap system - impossible
   and not the point). We ARE requiring that the **fundamentally emergent qualities
   fall naturally out of the model's mechanisms** rather than being hard-coded. If
   the mechanism is right, behaviors we never measured appear on their own.

3. **Clean separation of LITERAL vs SYSTEMIC.** Some things we transcribe directly
   from measurement (control laws, curve shapes). Others we build as mechanisms so
   the character generates itself. Getting this boundary right is the whole design.

## What the module is (from the data)

A **dual switched-capacitor (clocked) multimode filter built as a character /
interference engine**, not a clean filter. Its sound is inseparable from clock
artifacts, and every control cross-couples. Two identical filters (A = B) are
deeply coupled through a **shared clock system, shared resonance, shared mode, and
shared aliasing**. The prize behavior is the clock-mux: XOR-ing two clocks turns
two filters into a comb / oscillation engine that no ordinary filter can produce.

## The LITERAL / SYSTEMIC split

### LITERAL - transcribe the measured law/curve directly
These are parameters and shapes fed INTO the systemic mechanisms. Measure them
exactly and reproduce them as curves/lookup/fits.

| element | what to transcribe | status |
|---|---|---|
| Cutoff knob | knob -> **clock frequency** (exp, ~30 Hz->5 kHz, saturates past 3 oc) | mapped (coarse) |
| Resonance knob | knob -> **feedback/Q coefficient** (Q ~1->35 law) | mapped |
| Gain knob | knob -> **input drive level** (VCA, off at ccw, compresses near cw) | mapped |
| Distortion | the **saturating nonlinearity shape** (soft knee, odd-dominant symmetric clip, output ceiling) | mapped |
| Mode toggle | -> **filter-config parameter(s)** it sets (reconfigures ALL outputs) | UNDER-MAPPED |
| Aliasing toggle | -> the **clock/anti-alias parameter** it sets | partial |

The literal layer is the "skeleton": the knob-to-coefficient maps and the fixed
nonlinearity shape. These do NOT generate character on their own; they parametrize
the mechanisms below.

### SYSTEMIC - build the mechanism, let the character emerge
Do NOT hard-code these behaviors. Implement the underlying mechanism and they
arise for free - including at settings we never measured.

| mechanism to build | emergent behavior that falls out |
|---|---|
| **Switched-cap clock as an actual clocked/multirate filter** (cutoff sets a real clock; filter runs at clock rate) | aliasing, imaging, the ~clock/2 ceiling, clock feedthrough, the low-cutoff grit - ALL emerge; never tabulate the aliasing curve |
| **Clock-mux = XOR/combine two clock oscillators** (f_A, f_B), drive the filter with the result | convergence -> 2x peak; divergence -> comb (spacing ~f_B); the entire 16-corner regime map; combs we never measured |
| **Resonance as real feedback in the SC filter** | self-oscillation appears exactly where it should (divergent clocks + high res), at the right pitch (tracks divergence direction) - do NOT hard-code the osc frequencies |
| **A/B share clock-mux + resonance + mode + aliasing** | all the cross-channel interactions and the series/normal behaviors emerge from the shared wiring |

### The rule that ties it together
**The 16-corner map, the comb spacings, the self-osc tones, the aliasing curves
are OUTPUTS to validate against - never lookup tables.** If the clock + XOR +
feedback mechanisms are modeled correctly and fed by the literal knob laws, the
whole emergent zoo reproduces itself, and unmeasured corners behave plausibly.
That is the definition of success here.

## Coverage status

**Well-mapped (HIGH confidence):**
- A = B (model one filter).
- Core filter laws: cutoff range/tuning, resonance Q law, distortion transfer.
- Multi-output mode shapes: HP, AP (flat), Notch (-31 dB), hidden (bright notch).
- Aliasing LO/HI HF-reshaping (on the multi out).
- Gain staging (per-filter input amp; A->B normal pre-gain).
- Clk-src = clock-mux; "both" XORs the clocks (mechanism inferred, strong evidence).
- **Dual-clock 16-corner regime map** (divergence x resonance -> comb / osc / single).

**UNDER-MAPPED (must complete for full coverage):**
1. **MODE reconfigures ALL outputs** (new, top priority). We characterized modes
   only on the multi out; LP and BP also change with mode -> the toggle is a global
   filter reconfiguration. Remap **mode x every output x (aliasing, clock)**. This
   likely grows the matrix the most.
2. **Dual-clock interiors / transition boundaries**: res-onset of self-osc,
   divergence-onset of the comb (the regime edges the model must hit).
3. **DC3 gain/drive** intermod with the clock combs, and **DC4 input frequency x
   volume** through the combs/osc.
4. **Exact clock frequency / clock-to-cutoff ratio (N)** - needed to build the
   systemic SC clock; current cutoff-law is coarse and aliasing-confounded (needs
   the hot-send + BP-center method flagged in findings.md).
5. **Distortion mode-dependence** (spec says it changes with mode; untested).
6. Low-cutoff corners are SNR-limited - **hot-send re-takes** for detail.

## Remaining profiling plan (to complete coverage)
- **Mode-across-all-outputs** matrix (the big one): tap LP, BP, multi in turn x
  each mode x aliasing x (single + clk-both).
- **Dual-clock interiors**: res-onset and divergence-onset sweeps.
- **Clock-frequency measurement**: hot-send BP-center tracking to nail the tuning
  law and the SC clock ratio (feeds the systemic clock model directly).
- **DC3/DC4**: gain and input freq/volume through the dual-clock regimes.
- **Distortion mode-dependence** and low-cutoff hot-send re-takes.

## Model architecture sketch (systemic-first) - REVISED by POC v5 (2026-07-18)

Key architectural finding: **"clk src = both" is NOT one filter on a combined
(XOR) clock - it is TWO SC cores cascaded, each running on its OWN clock, sharing a
resonance loop.** The XOR-single-filter model produced a fixed ~50 Hz artifact comb
that did not track any control; the dual-core model makes the comb spacing TRACK the
low clock (a core clocked at f_B S&H-images the signal into an f_B comb) and unifies
every dual-clock behavior. Verified in poc/model.py.

- Two SC-emulated (multirate) cores, identical, each with a real clock oscillator
  whose rate = cutoff-knob * N (N~25 measured) [literal law]. Each clock DRIFTS
  slowly (+/-5%, ~2 Hz) [systemic] -> analog wander.
- **Clock routing** [systemic]:
  - single (A or B): signal through one core -> filtering + that core's own-clock
    aliasing/imaging; NO self-osc.
  - both: signal cascaded A->B, each on its own clock, PLUS a shared resonance loop
    (B out -> A in, gain rising past res~0.7).
- Emergent from the dual-core wiring (NONE hard-coded), all confirmed in v5:
  - **comb** at low res + divergent clocks, spacing TRACKS the low clock f_B.
  - **2x peak** at convergence (two identical cores stack; +6 dB vs single).
  - **self-oscillation** at high res via the shared loop - exists ONLY with both
    cores, so single-clock never self-oscs (matches the module). Onset res~0.8-0.9.
  - **breathing**: divergent clocks -> two pitches beat in the loop + drift ->
    the osc wanders/morphs (spectral adjacent-frame corr ~0.70; hw ~0.78). Converged
    -> one pitch -> clean.
  - **mode** shifts the self-osc timbre AND reshapes every output by picking the tap
    + scaling feedback (no MODE_FCAR lookup - it falls out of the SVF config).
- Resonance = shared feedback coefficient [literal law: Q~1..40] -> the loop gain.
- A saturating input amp per core [literal nonlinearity] with the gain law.
- Aliasing = a clock/anti-alias smoothing parameter [literal] into the SC model.
- Validate against the COMPLETE capture set; success = the emergent zoo (aliasing,
  combs, self-osc, breathing, corner map) matches without being programmed.

### Still to pin (POC known-gaps)
- Comb-spacing detector fumbles at wide spacings (measurement, not model); confirm
  f_B tracking across the full cutB range with the hot-send clock data.
- Converged self-osc currently louder than divergent - re-check against hardware
  (may want a divergence dependence on loop gain).
- Exact clock law (N, cutoff curve) still coarse pending the hot-send measurement.
