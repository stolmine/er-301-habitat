# Compound DSP voice - profiling synthesis & modeling brief

DUT: Industrial Music Electronics "Bionic Lester" dual switched-capacitor filter.
This is the handoff from profiling to the eventual 301 unit. Full detail:
`findings.md`; capture harness: this directory; level chain: `calibration.md`.

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

## Model architecture sketch (systemic-first)
- Two SC-emulated (multirate) filter cores, identical, each with a real clock
  oscillator whose rate = f(cutoff-knob) [literal law].
- A **clock-mux** stage: A / B / XOR(A,B) drives each core's clock [systemic].
- Resonance = shared feedback coefficient [literal law] applied as real feedback
  [systemic] -> self-osc emerges.
- A **mode** parameter that reconfigures the core [literal, once mapped].
- A saturating input amp per filter [literal nonlinearity] with the gain law.
- Aliasing = a clock/anti-alias parameter [literal] into the SC model.
- Validate the whole thing against the COMPLETE capture set via the null test;
  success = the emergent behaviors (aliasing, combs, self-osc, corner map) match
  without any of them being explicitly programmed.
