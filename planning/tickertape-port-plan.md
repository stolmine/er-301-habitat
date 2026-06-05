# TickerTape implementation plan

Status: **PLANNED 2026-06-05**. First tone-shaping chain unit in the house package. Demonstrates the chain-as-unit pattern from `planning/house-atom-architecture.md` end-to-end: a Lua composition of three C++ atoms (Console0Channel + ChromeOxide + Console0Buss) exposed as a single user-facing unit with three character knobs.

**Name is final** (not a codename) — TickerTape evokes paper-tape encoding/decoding which matches the Channel→Bus containment metaphor, with the tape-rot character coming from the ChromeOxide stage in between. House-themed (financial/clerical architecture) without being literal.

## What it is

Console-wrapped tape-rot saturator. Signal flows:

```
in
  → Console0Channel (continuous-gain pre-saturate via BigFastSin polynomial)
  → ChromeOxide   (alternating-IIR band-split + glitch overdrive on lows
                   + noise-FM warble on highs + spiralFast sat + recombine)
  → Console0Buss  (continuous-gain post-desaturate via BigFastArcSin rational)
  → out
```

Always 100% wet for v1 (no Wetness ply). User mixes externally if dry-blend is wanted. Could add Wetness in a v2 if requested.

## Source

No upstream source as a unit — this is an original chain composition. Atoms drawn from:

- **`~/repos/airwindows/plugins/MacVST/Console0Channel/source/`** (MIT) — the BigFastSin sat curve math
- **`~/repos/airwindows/plugins/MacVST/Console0Buss/source/`** (MIT) — the BigFastArcSin desat curve math
- **`mods/house/atoms/ChromeOxide.h`** (already shipped) — tape-rot per-sample helper
- **`mods/house/atoms/Spiral.h`** (already shipped) — `spiralFastSaturate` for ChromeOxide's high-band sat

## Naming decision

Console0Channel/Buss math is faithful to AW (polynomial sat/desat curves preserved). But the **gain control is changed from AW's bitshift quantization (6 dB steps, A=0 = mute) to continuous gain** for smoother UX in habitat. That's a derivative work, not a literal port — so per `feedback_no_third_party_branding`, the AW open-source-faithful-port naming exception does NOT apply for these specific atoms.

**Decision**: name the atoms `Console0Channel` / `Console0Buss` anyway, with a clear header comment noting the gain deviation. Reasoning:
- The defining character (the sat curve math) IS the AW Console0 sound, verbatim
- The gain change is a UX adjustment, not a sonic change
- Habitat-native alternative names ("Channel"/"Bus", "ConsoleIn"/"ConsoleOut", "Saturator"/"Desaturator") all lose the AW lineage that's musically relevant

Mark this as a precedent for future derivative AW ports: faithful curve math + UX-tweaked control mapping is OK to keep upstream names, but **document the deviation clearly in the header**.

For TickerTape itself: habitat-native original name, no concerns.

## Math/rate audit (from source read)

| Concern | AW behavior | Our handling |
|---|---|---|
| **`sin()` per sample** | NONE — uses polynomial `(x/2)(2.8274-x)` for Channel sat, rational `(2x)/(3-x)` for Bus desat | Inherit verbatim, no transcendental cost |
| **Sample-rate dependency** | NONE — averaging filters are sample-rate-relative; sat curves are rate-independent | No coefficient calibration needed; works at any rate. Direct opposite of ChromeOxide's rate problem. |
| **Bitshift gain quantization** | 6 dB steps, A=0 = MUTE, A∈[0.6, 0.9] all map to gain=2.0 (no resolution) | **Deviate**: continuous gain `gainScale = 0.05 + A * 1.95` (range [0.05, 2.0]). Smooth UX, no muting at A=0. |
| **Pan param** | Both atoms have Pan (B) param that shifts gain between L/R | **Hidden from TickerTape Lua surface** (`hardSet("Pan", 0.5)` at construction). Could expose later if needed. |
| **Polynomial sat/desat don't perfectly invert** | `desat(sat(x)) ≠ x` exactly. Round-trip mismatch IS the Console "glue" character. | Preserve. Document. |
| **Signal range expectations** | Channel clamps at ±√2; Bus clamps at ±2.8 | Inner chain ChromeOxide output is bounded around [-1, 1]; well inside both ranges. Document; monitor at audition. |
| **Averaging filters group delay** | ~2 samples total across Channel+Bus pair | Negligible for TickerTape (no feedback through the pair). Document for future feedback-wrapping chains. |
| **Dither** | Per-sample 32-bit dither in float path | Drop per template (`feedback_aw_atom_port_template`). |
| **Denormal flush** | `fpdL * 1.18e-17` (uses fpd RNG) | Replace with deterministic constant `1.18e-17` per template. |

**No transcendentals per sample. No rate-dependent IIRs. Console0 is the cheapest sat atom in the house catalog.** Direct opposite of XYZ's problem.

## ChromeOxide modifications (Phase A includes these)

Two changes needed to make ChromeOxide usable from TickerTape's Lua composition:

### Change 1: Add `house::ChromeOxide` od::Object wrapper

ChromeOxide.h currently only exposes the `ChromeOxideMono` per-sample helper class — no `od::Object`, so it can't be `addObject`'d from Lua. Add an `od::Object` subclass `house::ChromeOxide` (in the same header) that:
- Owns two `ChromeOxideMono` instances (L + R, with different seeds for noise decorrelation)
- Exposes inlets `In L` / `In R`, outlets `Out L` / `Out R`
- Exposes parameters `Drive` (= AW's A, 0..1, default 0.5) and `Output` (= AW's B, 0..1, default 0.5) — AW-faithful names
- Calls `chromeOxideBakeCoefs` once per block, then per-sample `processSplit` on each instance and sums low+high for output

The existing `ChromeOxideMono` helper class stays unchanged for any future per-line use (RotCoat's archived approach, etc.). Both APIs available.

### Change 2: Use `spiralFastSaturate` instead of inline `sin()` in `processSplit`

The current code:
```cpp
// Spiral-style sin saturation on the high band (inlined).
double br = fabs(inputSample) * c.densityA;
if (br > 1.57079633) br = 1.57079633;
br = sin(br);
inputSample = (inputSample > 0.0) ? (br / c.densityA) : -(br / c.densityA);
```

Replace with:
```cpp
inputSample = spiralFastSaturate(inputSample, c.densityA);
```

(Add `#include "Spiral.h"` to ChromeOxide.h.) ~20× speedup on that one call per sample per channel (~200 cycles → ~10). 0.45% curve error at π/2, inaudible for saturator.

## TickerTape parameters

Three plies, all continuous, all CV-controllable:

| Knob | Range | Default | Routing |
|---|---|---|---|
| **Drive** | 0..1 | 0.5 | Maps to gain on BOTH Console0Channel.A AND Console0Buss.A. Symmetric — input drive matches output recovery, so loudness stays comparable across the Drive range. |
| **Tape** | 0..1 | 0.5 | Maps to ChromeOxide.Drive (intensity = 0.9 + Tape²). Controls how aggressively the tape character bites. |
| **Bias** | 0..1 | 0.3 | Maps to ChromeOxide.Output. Controls the noise-FM warble offset / depth. |

No Pan, no Wetness ply. Minimal surface, focused on character.

## State + memory budget

| Atom | State per instance |
|---|---|
| Console0Channel | 4 doubles (avgAL/AR/BL/BR) + 2 floats (A, B) |
| ChromeOxide Object | 2 × ChromeOxideMono (8 doubles + 1 bool + 1 uint32 each) = ~144 B |
| Console0Bus | 4 doubles (avgAL/AR/BL/BR) + 2 floats (A, B) |
| **Total** | ~250 B per TickerTape instance. Trivial. |

No buffers, no large arrays. Footprint is essentially zero — TickerTape uses no allocated memory beyond its state struct.

## CPU projection

Per-sample work per side:
- Console0Channel: ~15 ops (2 LP averaging + gain mul + polynomial sat + denormal) = ~20 cycles
- ChromeOxide: ~30 ops + 1 spiralFastSaturate (~10 cycles) + 4 IIR steps + 5-slot interp = ~80 cycles
- Console0Bus: ~15 ops (2 LP + gain + rational desat + denormal) = ~25 cycles
- **Per side total**: ~125 cycles per sample
- **Stereo**: ~250 cycles per sample

At 720 MHz Cortex-A8, 250 cycles = ~350 ns. At 48k host rate, sample budget = 20.83 µs.

**Projected CPU per stereo TickerTape instance: ~1.7%**.

Multiple instances stack trivially. 10× TickerTape = 17% CPU. Comfortable headroom for elaborate patches.

This is the cheapest house atom by a wide margin. Direct consequence of Console0's polynomial-only design.

## CloudSeed-trap audit

- **No `firstFrame` guards needed** — all state init to 0 from memset / ctor
- **No allocations after constructor**
- **No host APIs** beyond `globalConfig.sampleRate` (not even used by Console0 — it's rate-independent)
- **No `std::vector`**
- **No modulated reads** — Console0 has no buffers; ChromeOxide's 5-slot history is bounded by branch chain
- **No runtime-branched DSP dispatch in per-sample loop** — Drive/Tape/Bias read once per block
- **No transcendentals per sample** in Console0 (huge); only 1 spiralFast in ChromeOxide (cheap)
- **Per-sample dither dropped** per template
- **`-fno-tree-vectorize`** in effect package-wide

**Verdict: cleanest atom in the package by a long way.** No structural risks. Should work first try on hardware.

## Phasing — single-shot (Phase A)

The math is simple, no rate gotchas, established patterns. One commit:

1. **`mods/house/atoms/Console0Channel.h`** (NEW od::Object atom, header-only, hybrid float — but most state is naturally double-precision)
2. **`mods/house/atoms/Console0Buss.h`** (NEW od::Object atom)
3. **`mods/house/atoms/ChromeOxide.h`** (MODIFY: add `house::ChromeOxide` od::Object wrapper + switch to `spiralFastSaturate`)
4. **`mods/house/assets/TickerTape.lua`** (NEW unit, 3 plies)
5. **`mods/house/house.cpp.swig`** (add `%include` for Console0Channel/Buss + ChromeOxide)
6. **`mods/house/assets/toc.lua`** (add TickerTape entry)
7. **`mods/house/mod.mk`**: bump 0.1.0.17 → 0.1.0.18

Build both arches + lints + install linux. Hardware audition.

**Hardware gate**: TickerTape produces audible tape-rot character with the Console pair containment. Drive sweeps from clean to saturated. Tape sweeps from clean to bitten. Bias adjusts warble offset. CPU under 5% per instance.

If first audition passes: commit + push, then start on the next chain unit (Crush via DeRez2 + Capacitor2, per the broader chain-as-unit direction).

## What this validates / establishes

This unit is the **first chain-as-unit in the package** — proves the pattern from `planning/house-atom-architecture.md` end-to-end:
- Atoms compose via Lua `addObject` + `connect` + `tie`
- Same atoms can be reused in other chains (Console0 pair will be in Crush, Bloom, Smear; ChromeOxide may get more uses)
- No C++ chain harness needed for simple feed-forward chains
- Hardware CPU budget for a 3-atom chain is well under 5%

If TickerTape lands clean, the next 4-5 chain units (per the proposal) will each take ~1 session apiece since the atom library will already cover most needs.

## Open implementation questions (minor)

1. **Console0 gain mapping curve**: continuous `0.05 + A * 1.95` is linear-in-gain. Could use exponential (`pow(2, A*4 - 1)` for ±12 dB log range) for more musical knob feel. **Decide at audition**: if linear feels grabby, switch to exp.
2. **TickerTape default Drive 0.5**: gives gain ~1.0 (transparent input level). Could go higher (0.7?) for "always a bit saturated" default. **Audition decides.**
3. **Console0 group delay (~2 samples)**: irrelevant for TickerTape but document for future feedback-wrapping chains.

## Files

```
mods/house/atoms/Console0Channel.h     # Phase A new
mods/house/atoms/Console0Buss.h        # Phase A new
mods/house/atoms/ChromeOxide.h         # Phase A modified
mods/house/assets/TickerTape.lua       # Phase A new
mods/house/assets/toc.lua              # Phase A add entry
mods/house/house.cpp.swig              # Phase A add includes
mods/house/mod.mk                      # Phase A bump 0.1.0.17 → 0.1.0.18
planning/tickertape-port-plan.md       # this doc
```

## Why this plan respects established rules

- `feedback_atoms_as_components`: Console0Channel/Buss + ChromeOxide Object wrapper are atoms (od::Objects), no Lua units, no toc entries. TickerTape IS the user-facing unit.
- `feedback_aw_atom_port_template`: hybrid float, dropped dither, dropped fpd seeding, header-only `od::Object`
- `feedback_no_third_party_branding`: TickerTape is habitat-native (original chain composition). Console0Channel/Buss keep AW name with documented deviation (gain control changed; sat curves preserved).
- `feedback_identical_means_identical`: BigFastSin / BigFastArcSin / averaging filters preserved verbatim
- `feedback_no_out_of_line_virtuals`: header-only throughout
- `feedback_disable_tree_vectorize_am335x`: package mod.mk already enforces
- `feedback_always_build_both_arches`: Phase A builds both
- `feedback_linux_build_auto_install`: auto-installs linux pkg
- `feedback_package_version_bump`: 0.1.0.17 → 0.1.0.18
- `feedback_persist_plans_to_repo`: this plan doc lives in `planning/` before any code lands
- **XYZ lesson**: no libm `sin/asin` per sample. Console0 uses polynomial math; ChromeOxide gets `spiralFastSaturate` swap.
- **ChromeOxide rate lesson** (the original RotCoat finding): only applies when running ChromeOxide at REDUCED rate. TickerTape runs at host rate, so the IIR coefficients are calibrated correctly. No issue.
