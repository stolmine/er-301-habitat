# er-301-habitat v2.4.0

Release date: 2026-04-29

Package updates: **spreadsheet 2.3.1 -> 2.6.0**, **catchall 0.1.0 -> 0.3.0**, **biome 2.1.0 -> 2.2.0** (Pecto removed), **mi 1.0.1 NEW** (consolidates clouds + commotio + grids + marbles + plaits + rings + stratos + warps).

## Highlights

Two new full-stack units: **Ngoma** (analog macro drum voice, spreadsheet) and **Alembic** (sample-trained 4-op phase-mod matrix synth, catchall). Major package reshape: 8 individual Mutable Instruments port packages collapse into a single **`mi`** package. Pecto zipper-noise on V/Oct + size knob sweeps fixed. Shift-handling audit decisions D1–D7 shipped. New utility unit **Constant Random** (biome).

## New unit: Ngoma (spreadsheet)

Analog-style macro drum voice. Six top-level controls (V/Oct, Character, Sweep, Decay, Level, Trigger) plus expanded auxiliary surface. Internal architecture is a 4-source mix (carrier osc + detuned-unison osc + sub-sine + 3 inharmonic membrane modes) summed in a 2× oversampled inner loop. Pitch-morph membrane mode ratios produce wide-spread sub-bass enrichment at low pitch and tight cymbal-sheen clusters at high pitch.

NEON-vectorized 4-lane phasor + polynomial sine, per-partial NEON envelope decay quad with feature-tied decay scaling (kick = short, cymbal = long), grit-knee saturation, optional EQ + clipper + level limiter. Rotating-cube viz on the Character control.

7 plies:
- **trig** -- comparator-driven trigger
- **V/Oct** -- pitch with octave on shift-sub
- **char** -- Character morph (carrier wavetable + grit + shape FM); rotating cube viz; shift sub-display exposes Shape / Grit / Punch
- **sweep** -- pitch envelope amount; shift sub: Sweep Time
- **decay** -- amplitude decay; shift sub: Hold / Attack
- **level** -- output level; shift sub: Clipper / EQ / Compressor
- **expanded** -- full sub-display surface across all 14 hidden parameters

Note: xform / randomize feature is deferred to a later release. The inserted unit produces pure synthesized output without parameter scrambling.

## New unit: Alembic (catchall)

Sample-trained 4-operator phase-modulation matrix synth voice. Loads any audio sample, analyzes it once at commit time, and populates a 64-slot per-instance preset table from per-pick spectral / temporal / statistical features. Three independent scan positions then traverse the trained dataset.

Signal chain: V/Oct + f0 → 4-op PMM voice (4×4 matrix) → 256-entry per-node wavetable shaper → 2× TPT SVF filter pair (topoMix / bpLpBlend / Q / drive) → 10-source × 6-destination × 8-lane routing matrix → 24-tap multitap comb (Pecto-engine clone, 16 patterns × 4 slopes × 4 resonator types) → drive → output limiter.

8 plies:
- **tune** -- V/Oct + tune offset
- **freq** -- fundamental frequency
- **sync** -- sync gate input
- **scan** -- main scan position; shift sub: K (path window) and sample-pointer depth; rotating sphere viz
- **reagent** -- independent reagent scan position (controls wavetable shaper); shift sub: amount
- **comb** -- single-fader collapsed dry/wet + comb-scan
- **ferment** -- chaos scalar macro (single fader 0..1.5; multiplies trained matrix + routing attens)
- **level** -- output level

Order 2 scan-neighbor gradients, Order 3 topology metrics (eccentricity / sparsity), and meta-mappings from sample-level features (variance, mean entropy, length) all shape the trained map at training time. Sample-pointer excitation reads the trained sample at runtime as a node-offset modulator. Per-node trained filter dry/wet ratio means clean tones stay drier, bright/chaotic picks engage filter character more.

**Phase 6 serialization is not yet shipped** -- trained state doesn't survive quicksave round-trip. Sample re-analyzes on each load. Intended for a follow-up release.

**Sample-swap caveat**: direct swap without detach can fail to retrain (Sample.Pool race). Workaround: detach / re-attach via menu.

## New unit: Constant Random (biome)

Single-fader probability utility. Output is either the held last value or a fresh random within the configured range, picked at clock edges. Probability fader sets the chance of swapping vs holding.

## New package: mi (consolidates 8 MI ports)

The 8 individual Mutable Instruments port packages collapse into one consolidated `mi` package v1.0.1:

| Was | Now |
|---|---|
| `clouds` 1.1.0 | `mi.Clouds` |
| `commotio` 1.1.0 | `mi.Commotio` |
| `grids` 1.1.0 | `mi.Grids` |
| `marbles` 1.1.0 | `mi.MarblesT` + `mi.MarblesX` |
| `plaits` 1.4.0 | `mi.Plaits` |
| `rings` 1.3.0 | `mi.Rings` |
| `stratos` 1.1.0 | `mi.Stratos` |
| `warps` 1.1.0 | `mi.Warps` |

All 9 unit functionality is preserved. The consolidation eliminates a nasty cross-package source dep (Stratos's `clouds/dsp/{frame,reverb}.h` reuse) and a Lua cross-package require (Rings's `plaits.EngineSelector` reuse). Single shared stmlib build de-duplicates per-package stmlib copies. Stolmine-side modifications (48 kHz rate scaling for Commotio, NEON-vectorized SVF bank for Rings, STM32 stubs for Plaits/Warps) all preserved as override files in the consolidated layout.

**Migration note**: saved patches that referenced `clouds.Clouds`, `plaits.Plaits`, `rings.Rings`, etc. won't auto-resolve and will show "missing unit" placeholders on the affected chains. Re-load the patch and re-bind the unit references manually.

## Pecto: zipper-noise fix

`combSize` and V/Oct knob sweeps now produce a smooth Doppler-style pitch glide instead of audible zipper noise. Mechanism: per-sample one-pole LP smoother on the `baseDelay` scalar (~25 ms time constant) replaces the previous block-rate constant. Continuous read-pointer slew is musical for a multitap comb -- matches analog tape-delay character that pitch-tracking resonator types (Karplus, sitar, clarinet) call for.

The fix path was an unusually deep debugging arc -- multiple iterations including a fully-faithful SDK two-read crossfade that crashed Cortex-A8 (multitap doubling NEON register pressure beyond what process() could absorb). Final version closes a previously-latent **float-precision-edge bug in the multitap wrap math**: when `p = writeIdx - delay` was barely-negative below `ulp(maxDelayF)`, the wrap `p + maxDelayF` rounded to exactly `maxDelayF`, then `(int)maxDelayF = maxDelay` → out-of-bounds buffer read → eventual hardware data abort under sustained double-modulation. Symmetric `idx0 >= maxDelay -> 0` guard matches the existing `idx1` wrap.

If you specifically liked the abrupt step character on Pecto knob sweeps, this is a behavior change. (Future release will expose Doppler slew time as a sub-param.)

## Shift-handling audit (spreadsheet + biome)

Seven UI decisions locked and shipped:

- **D1**: shift-hold + encoder touch suppresses the shift-toggle on release.
- **D2**: Pattern C (TransformGate, Ratchet) secondary-toggle dropped; mirrors Pattern A.
- **D3**: subGraphic swap unified across Pattern A and C.
- **D4**: invert-flag normalization → unified `paramMode` state across previously-divergent control implementations.
- **D5**: shift+sub in paramMode opens the keyboard for that readout (matches stock GainBias).
- **D6**: Pecto migration biome → spreadsheet (already shipped during v2.4.x dev).
- **D7**: paramMode preserved across cursor leave/return; not serialized; `paramFocusedReadout` resets to nil on leave.

Decision 8 (subGraphic visible-highlight indicator on paramMode entry) has a partial fix and a known visual-only bug on Helicase / Larets / MixInput sub-displays. The encoder controls correctly; the highlight is missing. **Pinned for the next release.**

## Other fixes

- **Catchall Lambda crash on insertion** (pre-existing): `Lambda.lua` had a typo'd require string `spreadsheet.libcatchall` that would never have resolved. Now `catchall.libcatchall`.
- **Build path for aarch64 hosts** (RPi 4 dev rig): drops `-msse4`, NEON is in the base ISA. Same pattern carried into the new `mi` package.
- **`tools/check-neon-hints.sh`** (new): am335x objdump pre-flight that flags trapping NEON `:64` / `:128` hint patterns. Used during the Ngoma + Pecto debug arcs.
- **`tools/run-emu-gdb.sh`** (new): linux emu launch wrapper with auto-bt on SIGSEGV / SIGBUS / SIGFPE for hardware-crash reproduction in emu where possible.
- **Install script update**: pick-latest-version per package + SD cleanup scripts under `scripts/`.

## Migration notes

- **Saved patches with old MI port references** (`clouds.Clouds`, `plaits.Plaits`, etc.): re-load the patch and re-bind unit references to `mi.Clouds`, `mi.Plaits`, etc.
- **Saved patches with `biome.Pecto` references**: Pecto migrated biome → spreadsheet during v2.4.x development. Use `spreadsheet.Pecto`.
- **Pecto knob-sweep character**: zipper noise replaced by smooth Doppler glide. If you preferred the abrupt step character, future release will expose slew-time as a sub-param.
- **Alembic from any pre-2.4.0 build**: not shipped externally before this release; no migration concern.

## Known issues

- Decision 8 visible-highlight bug on Helicase / Larets / MixInput paramMode shift sub-displays (the encoder control works; only the highlight is missing).
- Pecto Doppler slew time hard-coded at 25 ms; sub-param exposure deferred.
- Alembic Phase 6 serialization not shipped -- trained state doesn't persist quicksave round-trip; sample re-analyzes on each load.
- Alembic sample-swap-without-detach can fail to retrain reliably (Sample.Pool race); detach / re-attach workaround.
- Alembic comb retrofit (Doppler smoother + idx wrap ulp guard) not yet ported from Pecto -- latent at block-rate, not crash-causing under typical use.
