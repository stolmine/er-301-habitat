# CloudSeed reverb port to ER-301 (separate unit)

## Status

**Plan** — not started. Slot in after Visadhara + JF reach release state.

The Network unit cascade-FDN rebuild was abandoned (see
`planning/network-cascade-postmortem.md`); Network stays at the V1
parallel-multitap engine. This plan describes a **separate new unit**
ported from CloudSeed, not a Network successor. If we want a "Network
v2" later we can return to it; this work is independent.

## Goal

Land a clean, working algorithmic reverb on ER-301 by porting
[CloudSeedCore](https://github.com/GhostNoteAudio/CloudSeedCore)
(MIT, by Valdemar Erlingsson / Ghost Note Audio). The aesthetic
target is Lexicon-224-adjacent: huge endless spaces, modulated
echoes, lush diffuse tails.

**Working title:** `Cloudling` (or whatever feels right at integration time).
Package: `spreadsheet` (musical-quality tier alongside Pecto,
Petrichor, Ngoma, JF).

## Why this port (and not retrofit)

CloudSeedCore is the right architectural fit for our needs:

- **MIT license throughout** — matches package licensing without
  per-function audit.
- **C++14, no external dependencies** (no Boost, no JUCE).
- **DSP kernel is already separable** from plugin UI in a dedicated
  `DSP/` folder (16 modular files).
- **32-bit float at 48 kHz, per-block API** — matches ER-301 natively.
- **Architecturally decouples early reflections from late tail**:
  early = multi-tap + modulated allpass diffusers; late = up to 12
  parallel delay lines with feedback filters + modulation +
  diffusion. This is the exact decoupling the Network cascade
  retrofit kept failing to achieve.
- **Embedded port proof point**: `baylessj/daisy-reverb` (fork of
  `guitarml/daisycloudseed`) runs CloudSeed on Daisy Seed
  (STM32H7 Cortex-M7 @ 480 MHz, 32 MB SDRAM). ER-301's am335x is
  Cortex-A8 @ 1 GHz with 64 MB DDR — strictly more headroom.

The Faust reverb library (`zita_rev_fdn`, `fdnrev0`, etc.) was
considered and rejected: LGPL-with-exception per-function license
audit needed; generated C++ is monolithic and hard to hand-optimize
for Cortex-A8 NEON; less control over individual stages.

## Source provenance

Pull from `https://github.com/GhostNoteAudio/CloudSeedCore` master
branch. Copy the `DSP/` folder verbatim — do not edit upstream files
during initial port; keep them as-is so future upstream sync is clean.

Files to import (16 in `DSP/`):
- `AllpassDiffuser.h`
- `Biquad.cpp`, `Biquad.h`
- `DelayLine.h`
- `Hp1.h`
- `LcgRandom.h`
- `Lp1.h`
- `ModulatedAllpass.h`
- `ModulatedDelay.h`
- `MultitapDelay.h`
- `RandomBuffer.cpp`, `RandomBuffer.h`
- `ReverbChannel.h`
- `ReverbController.h`
- `Utils.h`

Plus `Parameters.h` for the parameter enum (we expose a subset; full
list stays available for menu/preset use).

Drop them into `mods/spreadsheet/cloudseed/` (new subdir, mirrors
`mods/spreadsheet/network/` convention). Vendor verbatim. Add the
upstream LICENSE file alongside.

## Parameter surface

CloudSeed has 45 parameters. We expose a curated subset following
the ER-301 spreadsheet convention (6 main + sub-controls + menu).

### Tier 1: main visible controls (CV-modulatable GainBias)

| Knob | Maps to (CloudSeed internal) | Range | Notes |
|---|---|---|---|
| **mix** | DryOut / LateOut balance | 0..1 | Dry/wet, universal grammar. |
| **size** | macro → `TapLength` + `LateLineSize` + `EarlyDiffuseDelay` | small room → cathedral | Single "room size" axis. Macro curve is a tuning decision worth ear-time — at small size, late lines stay relatively shorter than taps (slap-back); at large size, late lines extend disproportionately (cathedral). |
| **decay** | `LateLineDecay` | 0.1s..30s (log) | Tail length only — orthogonal to size. |
| **modulation** | macro → `EarlyDiffuseModAmount` + `LateLineModAmount` | 0..100% (capped) | Combined chorus/swirl depth, both sections move in lockstep. |
| **tone** | bipolar tilt: drives `LowCut` (warmer at +) and `HighCut` (darker at –) | −1..+1 | Single tilting filter, one of the most reached-for reverb knobs. |
| **diffusion** | macro → counts: `TapCount` + `EarlyDiffuseCount` + `LateDiffuseCount` | sparse..dense | Echo density / smoothness axis. |

### Tier 2: shift sub-controls (paramMode pattern per `feedback_parammode_convention`)

| Sub | Maps to | Range | Notes |
|---|---|---|---|
| **predelay** | `TapPredelay` | 0..500 ms | Important but rarely live-modulated. |
| **modrate** | LFO rate (both ER + late) | 0..5 Hz | Modulation character. |
| **brightness** | `HighCut` alone | 1k..20k Hz | Finer tone independent of the tilt. |
| **balance** | `EarlyOut` vs `LateOut` ratio | 0..1 | ER-heavy vs tail-heavy. |

### Menu (config-time, set-and-leave)

- **seed** (Task) — single composite seed driving all four internal
  seeds (`SeedTap`, `SeedDiffusion`, `SeedDelay`, `SeedPostDiffusion`).
  Rerolling gives "a new instance of the same algorithm." Discrete
  int 1..999.
- **lateLineCount** — discrete choice {1, 3, 5, 8, 12}. CPU-cost-
  affecting. Default **5** (matches Daisy port — safe starting CPU).
- **lateMode** — `Pre` / `Post` diffusion option.
- **filterSection** — master enable for input filter section
  (LowCutEnabled + HighCutEnabled).
- **eqSection** — master enable for shelving EQ section
  (LowShelf + HighShelf + Lowpass).

### Hidden / fixed at sensible defaults

- Four random seeds individually (composed via menu `seed`).
- Individual `EarlyDiffuseFeedback` / `LateDiffuseFeedback` —
  set to CloudSeed preset values.
- `EqCrossSeed`, individual EQ band controls.
- `InputMix` — always 100%.
- `EqLowShelfEnabled` / etc. individual flags — covered by
  `eqSection` menu master.

### Default ship values (rough — tune at audition)

- `mix = 0.3` (subtle wet)
- `size = 0.5`
- `decay = 0.5` → ~2-4 s tail
- `modulation = 0.3`
- `tone = 0` (neutral)
- `diffusion = 0.6`
- `predelay = 0`
- `modrate = 0.5 Hz`
- `brightness = 8k Hz`
- `balance = 0.5`
- `lateLineCount = 5`
- `lateMode = Post`
- `seed = 1`

## Architecture / file layout

```
mods/spreadsheet/
  Cloudling.h              # od::Object subclass, owns ReverbController
  cloudseed/               # vendored upstream DSP/
    LICENSE                # CloudSeedCore MIT
    AllpassDiffuser.h
    Biquad.cpp / Biquad.h
    DelayLine.h
    Hp1.h / Lp1.h
    LcgRandom.h
    ModulatedAllpass.h
    ModulatedDelay.h
    MultitapDelay.h
    Parameters.h
    RandomBuffer.cpp / RandomBuffer.h
    ReverbChannel.h
    ReverbController.h
    Utils.h
  assets/
    Cloudling.lua          # Unit Lua + main view + shift subs
  spreadsheet.cpp.swig     # SWIG additions
  toc.lua                  # register Cloudling
```

`Cloudling.h` is the small wrapper:
- Inherits `od::Object`.
- Owns a `CloudSeed::ReverbController` instance.
- ER-301 inlets: `In1` (mono in), or `In1/In2` (stereo).
- ER-301 outlets: `Out1/Out2` (stereo).
- ER-301 parameters: the 6 main + 4 sub (10 `Parameter` / `GainBias`).
- `process()`: per-block, calls `ReverbController::process()`.
- Parameter mapping (`mix`, `size`, etc.) translates from ER-301
  Parameter values to CloudSeed enum values per block.

## Phases

### Phase A: vendored DSP compiles in our build (1-2 days)

A1. Copy `DSP/` from CloudSeedCore master.
A2. Drop into `mods/spreadsheet/cloudseed/`, add LICENSE.
A3. Update `mod.mk` to compile `*.cpp` files in `cloudseed/` into the
   spreadsheet shared library.
A4. Resolve any `#include` paths and any C++14 vs ER-301 build flags.
A5. Force-clean and rebuild — verify no link errors, no
   regressions to existing spreadsheet units.

**Verify**: clean build both arches (am335x + linux), no NEON `:64/:128`
hints in objdump.

### Phase B: minimal wrapper, hardcoded params, audible output (2-3 days)

B1. Create `Cloudling.h` skeleton: `od::Object` subclass, inputs,
   outputs, `process()` calling `ReverbController::process()` with
   hardcoded parameter values.
B2. Hardcode a small-hall preset (`mix=0.3, size=0.5, decay=0.5`,
   etc.) inside `process()` setup.
B3. Add SWIG binding in `spreadsheet.cpp.swig`.
B4. Register in `toc.lua`.
B5. Minimal `Cloudling.lua` — just expose the unit, no controls.
B6. Hardware test on am335x: insert unit, hear reverb. Period.

**Verify**: unit instantiates, audio passes, reverb tail audible.
Crash-free for 5 min at default settings.

### Phase C: parameter mapping (3-4 days)

C1. Add `addParameter` / `addInput` calls for the 6 main controls.
C2. In `process()`, translate ER-301 Parameter values to CloudSeed
   parameter values via the macro mappings defined above.
C3. Build the `size` macro curve carefully — this is the most
   sound-design-sensitive mapping. Audition the curve.
C4. `Cloudling.lua` main view with 6 GainBias controls.
C5. Hardware audition: sweep each knob, confirm musical response.

**Verify**: every knob does what its name says. No unexpected
parameter interactions. CPU under 30% at default settings.

### Phase D: sub-controls + menu (2-3 days)

D1. Add the 4 shift sub-controls (predelay, modrate, brightness,
   balance) following the paramMode pattern from
   `feedback_parammode_convention`.
D2. Menu items: seed (Task with regenerate button), lateLineCount
   (discrete option), lateMode (Pre/Post), filterSection +
   eqSection toggles.
D3. Serialize/deserialize per `feedback_serialize_deserialize_pattern`
   for all params + menu state. Verify preset round-trip.

**Verify**: shift sub-controls work, menu items persist across
save/load, seed regeneration produces audibly distinct reverbs of
the same character.

### Phase E: CPU profile + NEON optimize hot loops (3-5 days, only if needed)

E1. Hardware CPU measurement at full settings (lateLineCount=12,
   decay=max, modulation=max).
E2. Identify hot loops via objdump + timing. Likely candidates:
   `DelayLine::Read*`, `ModulatedDelay`, `AllpassDiffuser::Process`.
E3. NEON-ify per the Pecto 3-pass template
   (`feedback_neon_delay_gather`): split into compute / gather /
   combine passes with explicit `arm_neon.h` intrinsics and 8-ahead
   prefetch. Watch for `:64/:128` alignment hints
   (`feedback_neon_intrinsics_drumvoice`).
E4. Heap-allocate any large NEON scratch arrays as class members,
   not stack-local.
E5. Re-profile until ≤ 20% stereo at default settings, ≤ 35% at max.

**Verify**: NEON `:64/:128` hint count = 0. CPU within targets.
30-min hardware soak at full settings without underrun.

### Phase F: visualization + polish (2-3 days)

F1. Decide on viz character. Options:
   - **Simple**: scope-style late-tail decay envelope on the
     overview ply.
   - **Spatial**: time-domain impulse response display
     (early reflections + tail envelope).
   - **Structural**: visualize the 12 late lines as parallel decay
     bars, modulation on each as small sine wiggles.
   - Pick one — F1 is a design call.
F2. Implement as `CloudlingOverviewGraphic.h` (header-only inline
   per `feedback_no_out_of_line_virtuals`).
F3. Tune defaults across the parameter space. Sweep size × decay ×
   mix space, pick a tasteful starting point.
F4. Bump PKGVERSION per `feedback_package_version_bump` (4th digit
   for dev iterations).

**Verify**: hardware-tested at multiple parameter regions. Defaults
feel musical. No graphic-induced encoder lag at any setting.

### Phase G: ship (1 day)

G1. Install script, hardware soak test.
G2. Update `docs/` with unit description (Pecto/Petrichor pattern).
G3. Add to `toc.lua` category.
G4. Final PKGVERSION bump for release.

## Calendar

| Phase | Time | Critical path |
|---|---|---|
| A — Vendored DSP builds | 1-2 days | Build system integration. |
| B — Minimal wrapper, hardcoded params | 2-3 days | `ReverbController` per-block API matches our `process()`. |
| C — Parameter mapping | 3-4 days | Macro curves for size + diffusion. |
| D — Sub-controls + menu | 2-3 days | Serialize round-trip. |
| E — CPU + NEON (only if needed) | 3-5 days | NEON delay reads, alignment hints. |
| F — Visualization + polish | 2-3 days | Viz design call + default tuning. |
| G — Ship | 1 day | Hardware soak. |
| **Total focused** | **14-21 days** | |
| Part-time elapsed (~50%) | ~5-7 weeks | |
| Plan budget with buffer | 8 weeks | |

## Risks and mitigations

| Risk | Probability | Mitigation |
|---|---|---|
| CPU too high on Cortex-A8 at full settings | Medium | Start with `lateLineCount=5` (Daisy-port config); make line count a menu choice rather than a knob; NEON in Phase E. Daisy port works at 480 MHz Cortex-M7 — we have headroom. |
| Per-block API mismatch (ER-301's `FRAMELENGTH=64` vs CloudSeed's configurable `BUFFER_SIZE`) | Low | Configure `BUFFER_SIZE=64` (or whatever max ER-301 ever passes). The CloudSeed API is designed for variable buffer sizes. |
| Memory allocation in audio path | Medium | Audit any `new` / `malloc` in CloudSeed init paths. Hoist all allocations to `allocateTimeUpTo()` time per ER-301 convention. |
| `Parameters.h` enum count is 45 — UI design overwhelm | Low | The plan above already curates to 10 user-facing controls. The full 45 stays available for preset / menu use if needed. |
| Vendoring CloudSeedCore complicates upstream sync | Low | Keep `cloudseed/` files unmodified. Any ER-301-specific changes go in `Cloudling.h` wrapper. |
| Encoder capture from viz (per `feedback_viz_encoder_capture_architectural`) | Low | Design viz with draw-path structure (tile granularity, state cache, time slicing) per the Colmatage reference. Avoid SomSphereGraphic anti-pattern. |
| Audio character doesn't match "lush hall" expectation | Low | CloudSeed's existing preset bank gives starting points. Listen to the upstream presets to set defaults. |

## Open design questions (resolve at Phase F)

1. **Mono vs stereo input.** CloudSeed has `ReverbChannel` per
   channel — naturally stereo. Mono-in / stereo-out is the typical
   reverb pattern. Decide whether to expose stereo input (extra
   inlet, more CPU) or keep mono-in / stereo-out.
2. **Visualization choice.** See Phase F1 options. Could even be no
   custom viz — just rely on the default ER-301 unit chrome. The
   spatial visualization that was Network's identity doesn't carry
   over directly to a non-cascade structure.
3. **Late line count knob vs menu.** If CPU profiling shows minimal
   cost difference between 3 and 12 lines, promote `lateLineCount`
   to a Tier-2 sub-control instead of menu. Audition at F to decide.
4. **`size` macro curve shape.** Linear, log, custom-tuned? Should
   feel musical end-to-end. May want a small lookup table.
5. **Modulation sync / freezing.** Should mod LFOs have a "freeze"
   option (set rate to 0 to halt mod, useful for static reverb)?
   Free-floating LFOs are typically what users want; can be deferred.

## References

- [CloudSeedCore (GhostNoteAudio, MIT)](https://github.com/GhostNoteAudio/CloudSeedCore) — upstream source
- [Original CloudSeed (ValdemarOrn, archived)](https://github.com/ValdemarOrn/CloudSeed) — original C# / C++ port
- [daisy-reverb (Daisy embedded port)](https://github.com/baylessj/daisy-reverb) — embedded-target proof point
- [Valdemar Erlingsson Audio Projects](https://valdemarorn.github.io/AudioProjects.html) — author's writeups
- `planning/network-cascade-postmortem.md` — lessons that informed
  the "separate unit, not retrofit" decision
- `feedback_neon_delay_gather` — Pecto NEON template for any hot
  delay-line loops in Phase E
- `feedback_no_out_of_line_virtuals` — graphics authoring rule
- `feedback_parammode_convention` — shift sub-controls pattern
- `feedback_serialize_deserialize_pattern` — preset state round-trip
- `feedback_package_version_bump` — 4th-digit iteration during dev
- `feedback_swig_header_dep` — force-clean SWIG wrapper on header
  edits
- `feedback_neon_intrinsics_drumvoice` — heap-allocated NEON
  scratch arrays
- `feedback_viz_encoder_capture_architectural` — viz draw-path
  structure rules
