---
name: JF codex
description: hex-voiced harmonically-coupled slope-engine voice in spreadsheet pkg. Architecture, signal chain, NEON state, phase progress, sound-design knobs. Read before touching JF.cpp/.h or post-Phase-5 work.
type: project
originSessionId: bd738562-37e5-4b2a-9bdc-85a9feb561af
---
# JF — codex

Clean-room slope-engine voice based on the public Mannequins technical map. v1 in spreadsheet package. Working title "JF" avoids third-party branding per `feedback_no_third_party_branding`.

Files:
- `mods/spreadsheet/JF.{cpp,h}` — top-level od::Object
- `mods/spreadsheet/jf/voice.h` — 4-lane NEON DSP (slope engine, GateToTrigger, CURVE LUT)
- `mods/spreadsheet/jf/neon_shim.h` — x86 emu shim for NEON intrinsics
- `mods/spreadsheet/jf/README.md` — attribution to tomf for the polygon 4-lane SIMD pattern
- `mods/spreadsheet/assets/JF.lua` — UI (8 globals plies + 6 gate plies on a separate view)

## Architecture

**6 voices via NEON 4-lane SIMD.** Two `jf::four::Voice` instances (8 lanes, 6 active). Voice 1N (IDENTITY) at G0[0]; voices 2N..4N at G0[1..3]; voices 5N, 6N at G1[0,1]; G1[2,3] are masked off via gate=0. Pattern adapted from tomf's polygon (`er-301-custom-units/mods/polygon/voice.h`) with verbal blessing.

**Signal chain per voice** (Cycle/Transient modes):
1. Phase accumulator `phase += inc; wrap_phase(phase)` — supports signed inc for TZFM.
2. `ramp_triangle(phase, T)` — RAMP-asymmetric duty cycle. `T = 0.5 + 0.49 * RAMP_pos`, clamped to [0.01, 0.99]. CCW = saw down, noon = symmetric triangle, CW = ramp up.
3. `CurveLut.lookup(progress, anchor0, anchor1, morph)` — 5 anchors × 256 entries. Anchors: rect (CCW full) → log → lin (noon) → exp → sine (CW full). Continuous blend.
4. Polarity: Sound = `2*shaped - 1` (bipolar ±1); Shape = passthrough (unipolar 0..1).

Sustain mode bypasses ramp_triangle + CURVE — phase IS the trapezoid level (rises while gate-high, falls while gate-low, clamped).

**MIX combiner per tech map:**
- Sound: `tanh(sum_of_voices)` via Padé 3/3 fast_tanh.
- Shape: `max(v[i] / i)` — analog-max of index-scaled (1N÷1, 2N÷2, ..., 6N÷6).

**OUT crossfader** (sub-out 1 / primary outlet selector, 0..6):
- 0 = MIX (default), 1..6 = per-voice 1N..6N.
- Smooth mode (default): tent-function blend across [MIX, 1N..6N].
- Snap mode (config menu option): rounds to nearest integer.
- CV-able via ParameterAdapter.

**FM** — bipolar two-destination, separate from the FM input branch:
- CW (+depth): linear FM to TIME — `inc += fm * depth * 100/sr` (TZFM).
- CCW (-depth): linear FM to INTONE — per-voice index-weighted (voice n weight = (n-1)/5).
- Sound range: FM input AC-coupled (one-pole HPF α=0.99934, ~5 Hz cutoff).
- Shape range: DC-coupled (constant offsets work for linear speed/duration shifts).

**INTONE morph** — continuous CCW (-1) → noon (0) → CW (+1):
- CW: voice n multiplier = n (overtone series 1:2:3:4:5:6).
- noon: 1 + (n-1) × 0.005 (slight detune spread, ~0.5%/voice).
- CCW: (7-n)/6 (undertone — 1N=1, 6N=1/6, ratios 6:5:4:3:2:1 minor-triad voicings).

## Multi-output topology

7 distinct C++ outlets: `mMix`, `mOut1N`, ..., `mOut6N`. Lua framework declares `args.channelCount = 8` with both Out1 and Out2 sourcing from the same `Mix` C++ outlet — gives vanilla stereo chains MIX on both L+R rather than MIX/1N. `subOutLabels = {"mix", "mix R", "1N", "2N", "3N", "4N", "5N", "6N"}`.

Cascade trigger inputs (Phase 5): 6 inlets `mTrig1N`..`mTrig6N` + `mCascadeMask` Parameter. Lua subscribes to each gate sub-chain's `contentChanged`, recomputes 6-bit mask, pushes via ParameterAdapter. C++ resolves right-to-left normalling at block start: `effBuf[n]` = pointer to the trig source voice n should read, or `nullptr` when no patched neighbor exists rightward.

## UI

Two-page main view per Plaits / Xxxxxx (Accents) pattern:
- `views.expanded` (default): 8 globals plies — V/Oct, TIME, INTONE, RAMP, CURVE, FM Depth, FM in, OUT.
- `views.gates`: 6 trigger plies — 1N, 2N, 3N, 4N, 5N, 6N.
- Switching via config-menu Tasks (`changeView`).

Config menu (header hold):
- View: Globals / Gates
- Range: shape / sound (od::Option)
- Mode: trans / sust / cycle (od::Option)
- OUT mode: smooth / snap (od::Option)

## Phase progress

| Phase | Done | What |
|---|---|---|
| 1 | ✓ | Skeleton + 7 sub-out plumbing |
| 2 | ✓ | Single-voice scalar slope engine across 6 base cells |
| 3a | ✓ | NEON 6-voice via vendored polygon pattern + INTONE morph |
| 3b | ✓ | RAMP rise/fall asymmetry |
| 3c | ✓ | CURVE 5-anchor LUT morph |
| 4a | ✓ | FM (TZFM + INTONE-FM, AC-coupling in Sound) |
| 4b/c | ✓ | MIX combiners (tanh/index-max) + OUT crossfader |
| 5 | ✓ | Per-voice trigger inlets + right-to-left cascade + 2-page UI |
| 6 | pending | Trig LUT sweep, hardware CPU profile, test procedures, ship |

## Known issues / v1.x backlog

- FM Depth and FM in are two adjacent plies on the globals view. Could consolidate into a single custom ViewControl (knob → mFmDepth, CV → mFM) but stock GainBias doesn't fit the multiply semantics. Cosmetic; deferred.
- v2: alt RUN-mode personalities (SHIFT, STRATA, VOLLEY, SPILL, PLUME, FLOOM). RUN ply omitted from v1 since RUN is functionally inert without alt-modes.
- v3 / probably never: Just-Type (poly voice + Geode round-robin allocator). Depends on i2c/external sequencing habitat doesn't have.

## NEON / am335x notes

- Hint count post-mitigation: 4 suspect `[reg :64]` (heap-aligned Internal struct stores). 0 stack-spill quad-D pairs.
- `__attribute__((optimize("no-tree-vectorize")))` on JF::process() — prevents GCC auto-vec from emitting :64 stores on per-sample output buffer writes.
- `make_4`, `make_mask` use vsetq_lane (register-only). Stack-local + vld1q is the trap pattern (per `feedback_neon_intrinsics_drumvoice`).
- vgetq_lane_f32 used for per-voice output reads; no `float voices[8]; vst1q_f32(voices, ...)` stack gather.

## Tech map references

`/tmp/Mannequins-Technical-Maps/just-friends/just-friends.md` (cloned at planning time). Verbatim quotes for TIME, CURVE, MIX, FM live in `planning/just-friends.md` "Resolved design decisions" section.
