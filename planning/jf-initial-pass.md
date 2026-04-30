# JF — initial pass plan

Status: **active**, 2026-04-30. Persists the v1 implementation plan to repo per `feedback_persist_plans_to_repo`. Companion to `planning/just-friends.md` (scoping).

Working title: **JF** (avoids third-party branding per `feedback_no_third_party_branding`). Display name in toc.lua = `"JF"`. C++ class = `JF`. Files = `JF.{cpp,h}` / `JF.lua`. Internal mention of the source module is restricted to comments referencing the technical map clean-room basis.

## Package home — DECIDED: `spreadsheet`

Spreadsheet is tier-1 alongside Helicase / Ngoma / Pecto. Houses original-design and licensed-port voices behind generic names. JF fits the same pattern (clean-room, generic title, complex DSP).

Package: `mods/spreadsheet/`
- `mods/spreadsheet/JF.{cpp,h}` — top-level od::Object
- `mods/spreadsheet/assets/JF.lua` — Lua wrapper
- `mods/spreadsheet/jf/` — vendored 4-lane DSP subset (osc.h, env.h, pitch.h, latch.h, util.h, attribution-credited)
- `mods/spreadsheet/spreadsheet.cpp.swig` — `%include "JF.h"`
- `mods/spreadsheet/assets/toc.lua` — register `{ title = "JF", moduleName = "JF", category = "Spreadsheet", keywords = "voice, slope, function, generator, multi, multiout" }`

Spreadsheet PKGVERSION bump: 2.6.0 → 2.7.0 on first ship.

## File / namespace layout

```
mods/spreadsheet/
├── JF.h                        // public class, 7 sub-outs, params
├── JF.cpp                      // main process(), trigger dispatch, MIX combiner
├── jf/
│   ├── README.md               // attribution to tomf for vendored DSP
│   ├── voice.h                 // jf::four::Voice = 4-lane slope engine
│   ├── osc.h                   // jf::four::Phase, polyBLEP — adapted from custom-units
│   ├── env.h                   // jf::four::SlewEnvelope, Coefficients
│   ├── pitch.h                 // jf::four::Vpo
│   ├── latch.h                 // jf::four::GateToTrigger
│   ├── util.h                  // jf::four::wrap, fclamp_unit, mix
│   ├── curve.h                 // CURVE 256x5 LUT + 4-lane shaper
│   └── mix.h                   // MIX combiner (Sound tanh / Shape index-max)
└── assets/
    ├── JF.lua                  // Unit wrapper
    ├── JFGateLayer.lua         // Gate ply with cascade-mask Lua-side computation (if needed)
    └── ...                     // existing spreadsheet assets unchanged
```

Top-level namespace `namespace jf` for the unit, sub-namespace `namespace jf::four` for the 4-lane SIMD primitives.

## Implementation phases

### Phase 1 — skeleton + plumbing (no DSP)

**Goal:** unit inserts on a chain, all 7 sub-outs declared, Lua plies wired but produce silence. Validates package layout and SDK contract.

- [ ] Add `JF.h` / `JF.cpp` with `od::Object` boilerplate, 7 outlets (`mMix`, `m1N`...`m6N`), 0 inlets initially.
- [ ] `process()` writes silence to all outlets — validates outlet wiring.
- [ ] `JF.lua` with `args.channelCount = 7`, `args.subOutLabels = {"mix", "1N", "2N", "3N", "4N", "5N", "6N"}`, all sub-outs connected via `connect(...)` in `onLoadGraph`.
- [ ] toc.lua entry under spreadsheet.
- [ ] SWIG include + build for linux + am335x.
- [ ] Insert in emu, verify all 7 outlets resolve, no crash. Verify QuadLFO-style sub-out picker works at fan-out 7 (validates the firmware-side Q2 from `er-301/docs/planning/just-friends-sdk-questions.md`).

**Deliverable:** unit exists as a 7-output silent block. Can be inserted, saved, restored.

### Phase 2 — single-voice scalar slope engine

**Goal:** prove the slope-engine semantics work for one voice across all 6 base cells in scalar, before SIMD-izing.

- [ ] Add scalar `JFVoiceScalar` with phase, rise/fall coeffs, gate-tracking state.
- [ ] Implement Sound/Cycle (free-running osc, hard-sync on trigger).
- [ ] Implement Sound/Transient (AR slope, ignore retriggers mid-slope).
- [ ] Implement Sound/Sustain (gate-following ASR-without-S, rises on gate-high, falls on gate-low).
- [ ] Same three for Shape range (just a frequency clamp on TIME).
- [ ] Wire to sub-out 2 (1N / IDENTITY) only; rest stay silent.
- [ ] V/Oct + TIME knob plies, comparator-driven gate ply.
- [ ] Hardware test: hit each of the 6 cells, confirm trigger semantics match tech map. Listen for cycle/transient/sustain audibly distinct.

**Deliverable:** one fully-working voice on sub-out 2; mode/range switches functional.

### Phase 3 — 6-voice NEON via vendored DSP

**Goal:** scale to 6 voices via `MultiVoice<2>` pattern. INTONE morph + RAMP + CURVE shaping operational.

- [ ] Vendor `jf/{osc.h, env.h, pitch.h, latch.h, util.h}` from `er-301-custom-units/common/{dsp,util}/`. Adapt namespaces to `jf::four::*`. Add attribution README to `mods/spreadsheet/jf/README.md`.
- [ ] Run am335x objdump on vendored .o — verify no `:64` traps post-vendor (lessons from `feedback_neon_intrinsics_drumvoice` + `feedback_neon_hint_surfaces`).
- [ ] Replace scalar voice with `jf::four::Voice` × 2 groups = 8 lanes; mask 2 lanes off.
- [ ] Implement INTONE morph: continuous CCW (undertone 6:1..1:1) → noon (unison detune) → CW (overtone 1..6).
- [ ] Implement RAMP: per-voice rise/fall ratio shift.
- [ ] Implement CURVE: 256×5 LUT shaper, 4-lane gather + adjacent-shape interp.
- [ ] Wire each voice lane to its corresponding sub-out (1N → sub-out 2, ..., 6N → sub-out 7).

**Deliverable:** 6-voice harmonically-coupled hex output across sub-outs 2–7. INTONE/RAMP/CURVE all responsive.

### Phase 4 — FM + MIX + OUT crossfader

**Goal:** complete the global control inventory. Sub-out 1 (MIX) operational. OUT crossfader with config option.

- [ ] FM ply: bipolar GainBias. CW → linear FM to TIME (TZFM via `phase += inc + fmAmount; phase -= floorf(phase)` adapted to NEON via `util::four::wrap`). CCW → linear FM to INTONE (per-voice index-weighted).
- [ ] FM input AC-coupling in Sound range (one-pole HPF ~5 Hz). Bypassed in Shape.
- [ ] MIX combiner: Sound = `tanh(sum)` clamped; Shape = `max(v[i]/i)`. Branch on Range; emit on sub-out 1.
- [ ] OUT crossfader on Page 1 ply 8. Smooth mode = cosine-taper between adjacent voices; Snap mode = round-to-int.
- [ ] OUT mode (`smooth` / `snap`) as `od::Option` with `Unit:onShowMenu` config item; serialized.

**Deliverable:** full v1 audio path. All 8 globals functional.

### Phase 5 — trigger sub-chain cascade + per-mode dispatch

**Goal:** 6 gate plies with right-to-left normalling. Per-cell trigger semantics fully dispatched.

- [ ] Add 6 inlets `mTrig1N`...`mTrig6N` + 6 ControlBranches in Lua.
- [ ] Lua-side `JFGateLayer.lua` computes `cascadeMask[6]` from `branch:getInputSource(1) ~= nil` checks. Pushes mask to a C++ Parameter.
- [ ] C++ `process()` reads all 6 trig inlets unconditionally, uses cascadeMask to select effective source per voice (cells with `mask[i]=0` inherit from nearest right-side `mask[j]=1`).
- [ ] Per-mode trigger dispatch table: switch on (Range, Mode) → start AR / set gate / phase reset semantics per the tech-map per-cell rules.
- [ ] Hardware test: cascade behavior matches tech map (trigger to 6N alone fires all; trigger to 6N + 2N partitions; etc.).

**Deliverable:** v1 feature-complete.

### Phase 6 — polish + ship

- [ ] Trig LUT sweep across all `sinf`/`cosf` references (slope phasor terminating in CURVE sine anchor, MIX combiner if any).
- [ ] CPU profile on hardware: 6 active lanes at 48 kHz, all 6 cells exercised with audio-rate FM. Confirm <20% one-core.
- [ ] Test procedures entry in `docs/test-procedures-clean.md`.
- [ ] Vanilla compatibility test: package loads on stolmine vanilla firmware, sub-out 1 (MIX) auto-wires, sub-outs 2–7 silently inaccessible (no crash).
- [ ] Spreadsheet PKGVERSION bump 2.6.0 → 2.7.0.
- [ ] Release notes entry.

## Open items deferred to first build pass

- TIME's exact V/Oct anchor frequency (tech map says "Hz to kHz" in Sound — pick e.g. A2 = 110 Hz at 0V to match Helicase's f0 default, octave-track with V/Oct).
- INTONE detune amount at noon ("slight detune spread either side, supersaw territory" — pick 2-5 cents per voice spread).
- CURVE LUT generation script (one-off Python or Lua during build to populate the shapes).
- Overview viz — defer until v1 audio is shipping; can ship v1.0 with no overview, add in v1.1.

## Now starting: Phase 1

Skeleton + plumbing. Goal is a silent 7-output unit that inserts cleanly. Will commit at end of phase.
