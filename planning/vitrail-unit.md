# Vitrail - dual switched-capacitor character filter (unit build plan)

A spreadsheet unit modeling the dual switched-capacitor filter profiled in
`planning/refs/compound-dsp-voice/`. Systemic-by-construction (POC v5 architecture):
two SC cores, each on its own drifting clock, with a shared resonance loop, so the
aliasing grit, clock combs, breathing self-oscillation, and mode reshaping emerge
rather than being tabulated. See `synthesis.md` + honest `coverage-and-gaps.md`.

Name: **Vitrail** (stained-glass lattice - the comb structure). No third-party
branding per `feedback_no_third_party_branding`.

## Decisions (locked with user 2026-07-18)
- **Target: am335x**, but build full-fidelity FIRST (no pre-optimization); harden
  for Cortex-A8 after it sounds right. Non-negotiable am335x rules still apply from
  the start: file-level `-fno-tree-vectorize`, no per-sample idiv/trig, no out-of-
  line virtuals on framework subclasses.
- **Scope: full dual-core from the start** (not a single-core MVP).
- **Package: spreadsheet** (resonant/character territory; version 2.8.3.31 -> .32).

## Architecture (port of poc/model.py v5)

### DSP object `Vitrail` (od::Object; Vitrail.h/.cpp)
Inlets (all audio-rate modulatable per `feedback_inlet_vs_parameter_audio_rate_mod`):
- `In` - signal in.
- `CutA`, `CutB` - GainBias [0,1] -> exp cutoff Hz (30..5000) -> clock = cutoff x 25.
- `Res` - GainBias [0,1] -> Q law + shared-loop gain (engages past ~0.7).
- `Gain` - GainBias drive into the input softclip.

Options (discrete; `feedback_option_vs_parameter`, values 1..N never 0):
- `Mode` (1..6): LP / BP / HP / Notch / AP / Hidden - selects the output tap AND
  scales feedback (MODE_Q). Folds the hardware's LP+BP hard outs + mode multi-out
  into one selectable main out for the MVP (multi-out taps can come later).
- `ClkSrc` (1..3): A / B / Both. Both = two cores cascaded + shared resonance loop.
- `Alias` (1..2): LO / HI (HI = mild HF anti-alias smoothing).

Outlet: `Out` - the mode-tapped result.

Emergent (NOT coded as special cases): comb (low res + divergent, spacing ~fB),
2x peak at convergence, self-osc at high res via the shared loop (only exists with
both cores -> single-clock never self-oscs), breathing from two drifting clocks.

### DSP internals
- `cutoffHz(k) = 30 * (5000/30)^min(k/0.8, 1)`.
- Two phase-accumulator clocks; each freq gets slow block-rate drift (+/-5%).
- Cross-coupling: each cutoff pulls the OTHER clock down ~6% (Tier-2, tunable).
- `_scCore`: SVF advances only on its clock tick (S&H + ZOH); tanh feedback limiter.
- clk=Both: cascade coreA->coreB + shared loop (B out -> A in, gain rises past res 0.7).
- Softclip on the gain-driven input (odd-dominant cubic; refine vs measured knee later).
- Alias HI: one-pole/FIR HF smoothing on the output.
- Mode tap: block-constant select among lp/bp/hp/notch/ap/hidden.

### Lua unit `assets/Vitrail.lua`
GainBias views: cutA ("cutA"), cutB ("cutB"), res ("res"), gain ("gain").
ModeSelector or OptionControl for Mode; menu OptionControls for ClkSrc + Alias.
(Follow Canals.lua wiring: ParameterAdapter+tie for params, GainBias+connect for
audio-rate inlets. Cutoff/Res/Gain are inlets -> GainBias+connect.)

### Registration
- `spreadsheet.cpp.swig`: `#include "Vitrail.h"` + `%include "Vitrail.h"`.
- `assets/toc.lua`: entry (category Spreadsheet, keywords filter/switched-capacitor/
  comb/aliasing/self-oscillation/character).
- `mod.mk`: PKGVERSION 2.8.3.31 -> 2.8.3.32 (dev digit per `feedback_dev_digit`).

## Milestones
1. **DSP core + minimal unit, linux emu.** Full dual-core; verify the 6 POC behaviors
   audibly in the emulator (auto-install per `feedback_linux_build_auto_install`).
2. **am335x build + hardening.** Both arches (`feedback_always_build_both_arches`);
   fast-tanh, verify no NEON hints / no per-sample idiv; objdump check; on-hardware
   sanity vs `feedback_disable_tree_vectorize_am335x`, `runtime_branched_dsp_dispatch`.
3. **Character pass.** Fit distortion knee + alias shape to measured curves; tune the
   self-osc onset + converged-vs-divergent level; the comb-track sweep.
4. **UI / expansion / viz** (optional): comb/response graphic, control expansion view.

## Known model risks carried in (from coverage-and-gaps.md)
- Dual-core is modeled, not measured (vs XOR/blend). No null-test yet.
- Distortion mode-dependence, series/normalling, CV response all UNTESTED - the unit
  will EXPOSE the mechanisms; exact fidelity in those regimes is unverified.
- Constants (N=25, 6% coupling, 5% drift, 3% feedthrough) are single-point/plausible.
