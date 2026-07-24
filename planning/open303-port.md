# Open303 port - plan of record

Status: **planning** (2026-07-11). Supersedes `planning/open303-port-scoping.md`
(2026-04-30 surface assessment) on two points: the wavetable tables are NOT
cache-friendly on am335x, and we should go `float`-first, not double-internal.
The scoping doc's file inventory, audio-path walk, and phased shape still hold;
read it for the per-file LOC table.

Source: `~/repos/Open303` (github RobinSchmidt/Open303). License: **MIT**,
`Copyright (c) 2009 Robin Schmidt` (`License.txt` verified). Attribution stays
in every vendored file. Ledger item: `open303-port`; part of the `port-mit-direct`
backlog (Open303 = spreadsheet flagship alongside Ngoma).

## Goal

An am335x-shippable monophonic acid-bassline voice. Prototype in `catchall`
(experimental tier, like Alembic), promote to `spreadsheet` after the listening
pass. User-facing name/description stays generic (no Roland / 303 / TB wording,
per the no-third-party-branding convention); internal `rosic_*` filenames keep
their names for attribution.

## Port boundary

Keep the voice, drop the sequencer. `rosic::Open303::getSample()` branches on
`sequencer.getSequencerMode() != OFF`; run it OFF and the whole internal 303
sequencer falls away. Drive `triggerNote / slideToNote / releaseNote` from
ER-301 inlets instead.

- **Keep** (audio path + envelopes + wavetable + init FFT): `rosic_Open303`,
  `rosic_BlendOscillator`, `rosic_MipMappedWaveTable`, `rosic_TeeBeeFilter`,
  `rosic_AnalogEnvelope`, `rosic_DecayEnvelope`, `rosic_LeakyIntegrator`,
  `rosic_OnePoleFilter`, `rosic_BiquadFilter`, `rosic_EllipticQuarterBandFilter`,
  the `Global*` / `rosic_RealFunctions` / `rosic_NumberManipulations` /
  `rosic_FunctionTemplates` math, and (construction-time only) the FFT trio
  `rosic_FourierTransformerRadix2` / `rosic_Complex` / `fft4g.c`. The FFT can be
  swapped for the `pffft` spreadsheet already ships; cheap to defer.
- **Drop**: `rosic_AcidSequencer`, `rosic_AcidPattern`, `rosic_MidiNoteEvent`,
  the `std::list` note tracking (replace with a 3-4 entry fixed ring), the VST
  shell, `Build/`, `Notes/`.

## The hot-path reality (why the optimization work is required, not optional)

Measured against the actual code (`rosic_Open303.h:317-410`,
`rosic_MipMappedWaveTable.h`):

- **The oscillator lives inside the 4x oversampling loop** and blends TWO
  wavetables, so each output sample costs **8 mip-table gathers**.
- **Tables are `double[12][2052] x 2` ~= 385 KB** - every mip level is a full
  2048 samples regardless of how few harmonics it holds (the source even
  comments "room for optimization here"). 385 KB blows the am335x 256 KB L2, so
  those 8 gathers/sample thrash cache. This is the same L2 wall that made
  anamnesis CM4-only.
- **Per-sample `pow(2.0, ...)`** on the env-modulated cutoff
  (`rosic_Open303.h:374`) - a libm call every output sample.
- **4x oversampling** through a 12th-order elliptic decimator - fixed per-sample
  cost.

## Optimization plan

### A. Structural: pull the oscillator OUT of the oversampling loop (biggest win, hits L2 + CPU)

The oscillator is already band-limited by its mip tables, so it does not need
oversampling for its own aliasing - the 4x exists for the filter nonlinearity
(resonance + `shape()` + tanh drive). Compute one oscillator sample per output
sample and linear-interpolate it up into the filter's 4x grid. Gathers/output
drop 8 -> 2 (removes ~75% of the memory-bound oscillator work and its CPU); the
filter keeps its anti-alias benefit. Intermod difference is inaudible on an acid
bass.

### B. Table footprint (stackable, ~385 KB -> ~30 KB)

1. **`float` not `double`** -> ~197 KB. Mandatory; also the only path to NEON and
   the reason to reverse the scoping doc's "double-internal" call.
2. **Shrinking mip pyramid.** High-octave levels hold few harmonics and need a
   fraction of 2048 samples. A pyramid (2048, 2048, 1024, 512, 256, ...) sums to
   ~2-3x the base table instead of 12x -> ~30 KB/waveform. Needs per-level length
   + phase-scale in the table read. Biggest pure-memory win.
3. **Derive the square from one saw table.** The 303 square is a phase-shifted-saw
   difference (`set303SquarePhaseShift`); store only the saw and compute square as
   two phase-offset reads + subtract -> the second table disappears (another x0.5).
4. **Optional int16 tables + fixed-point interp** -> another x0.5; quality cost
   negligible on a gritty voice, Cortex-A8 int loads are cheap. Reserve for
   headroom.

Stacking 1-3 lands the table set at ~30 KB, permanently hot in L2.

### C. Oversampling / CPU

- After A, the 4x body is only filter + elliptic decimator + declicker.
- **Tiers:** am335x = **2x**, CM4 = **4x**. 2x halves the remaining OS cost; swap
  the steep elliptic for a polyphase halfband at 2x.
- **Fast `exp2` polynomial** to replace the per-sample `pow(2.0, ...)` cutoff.
- Use the existing **`calculateCoefficientsApprox4()`** rather than the exact
  tan/exp coeff path (cutoff moves every sample, so coeffs recompute per-sample).
- **No NEON for a single serial mono voice** - the lever is float-scalar + fewer
  ops. (NEON would only pay off in a paraphonic SoA fork.)

## Target tiers

| tier | build | oversampling | tables | precision | intent |
|---|---|---|---|---|---|
| am335x | `ARCH=am335x` | 2x, halfband | float pyramid, single saw (~30 KB) | float | shippable hardware voice |
| CM4 / linux | `ARCH=linux` | 4x, elliptic | float pyramid | float | max fidelity |

Same source, tier switched at build (an `OS_FACTOR` + decimator selection).
`double` is not retained on either tier - the filter numerics are re-validated
by ear in the listening pass, not preserved by precision.

## ER-301 unit surface

- **Inlets:** V/oct -> pitch, Gate (rising edge, `> 0.5f` per the comparator
  convention), Accent gate, Slide gate.
- **Controls (v1):** Cutoff, Resonance, EnvMod, Decay, Accent, Waveform, Drive
  (tanh shaper), Tuning, Volume. ~6 plies (trig/pitch/cutoff/reso/envmod/decay/
  accent/level), matching Pecto/Ngoma.
- **Devil Fish aux** (AmpSustain, tanh offset, pre/feedback/post highpass,
  square phase shift): expanded-view aux controls, ship in v1.x once the core
  sound is dialed.
- **Wrap:** SWIG'd `od::Object` (the anamnesis / Ngoma pattern); vendored DSP
  under `mods/<pkg>/open303/` for namespace isolation.

## Build order

- **Phase 1 - minimal voice in catchall (float, 4x):** vendor the keep-set under
  `mods/catchall/open303/`, strip the sequencer from `getSample()`, wire trig/
  V-oct/accent/slide inlets, float throughout, single voice, 6 plies. Build both
  arches; run `tools/check-neon-hints.sh` on the `.o` for `:64` traps.
- **Phase 2 - optimization pass:** apply A (osc out of loop) + B2/B3 (mip pyramid
  + single-saw square) + C (fast exp2, approx coeffs, 2x am335x tier). Re-measure
  CPU + L2 on hardware.
- **Phase 3 - listening + tuning:** hardware, driven by Excel/Ballot; verify the
  resonance sweep, accent, and slide against 303 reference recordings. If the
  sound is wrong, revisit the filter topology (`TeeBeeFilter` mode enum) before
  reaching for double.
- **Phase 4 - promote catchall -> spreadsheet** flagship (bump versions), add the
  Devil Fish aux controls.

## Open decisions

- **Voice count:** monophonic (matches the 303, saves CPU) vs a paraphonic SoA
  fork (would justify NEON). Lean monophonic for v1.
- **FFT:** carry the rosic FFT trio (construction-only, cheap) vs substitute
  `pffft`. Lean carry-for-v1, swap later if the pyramid rework needs it.
- **int16 tables (B4):** only if 2x + pyramid still leaves L2 tight after Phase 2
  measurement.
