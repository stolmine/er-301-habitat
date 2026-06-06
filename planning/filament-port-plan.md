# Filament implementation plan

Status: **PLANNED 2026-06-05**. Third chain-as-unit in the house package (after TickerTape and Lacquer). First **filter-character** unit in the catalog — the six reverbs + two tape units don't cover this territory.

**Name**: Filament (final, not codename). Evokes the "breathing" of a heated filament — the filter cutoff appears to glow brighter / darker in response to input voltage, like a real-world component that responds to the signal flowing through it. Also evokes the bowed-string / vocal-resonance character that signal-voltage-modulated filters give.

## What it is

Console0-wrapped Capacitor2 with ChainMix. Capacitor2 is AW's "signal-voltage-modulated LP/HP" filter — the filter's cutoff frequency is modulated by the input signal voltage itself, giving a "breathing" or "auto-responsive" character that's distinct from a static LP. Where typical filters need an external CV to modulate cutoff, Capacitor2 is self-responsive — louder transients pop the cutoff up; quiet passages settle.

For Filament:
- **Use only the LOWPASS side** of Capacitor2 (skip the highpass for simplicity, half the IIR state + cycles)
- Wrap in Console0 sat pair for input drive + level-dependent containment
- 4 plies: Drive / Cutoff / FM / Mix (the FM amount is the unit's defining character knob)

## Why monolithic

Same rationale as Lacquer: Capacitor2's 6-cell gearbox IIR state needs coordinated per-sample sequencing that's hard to expose cleanly across Lua-graph boundaries. Following the precedent — chain unit as a single C++ Object that inlines Console0 sat math + the Capacitor2 helper.

`house::Capacitor2Mono` will be exposed as a reusable component (like ChromeOxideMono), and a `house::Capacitor2` od::Object wrapper that other Lua units could `addObject` if useful.

## Macro topology

```
in (host rate)
  → Console0Channel sat (input drive)
  → Capacitor2 LP gearbox with signal-FM cutoff
  → Console0Buss desat (output recovery)
  → ChainMix dry/wet
  → out (host rate)
```

No rate brackets, no shells. Pure per-sample host-rate chain. Should be the cheapest chain unit by far.

## Math/rate audit

### Capacitor2 algorithm (lowpass side only)

Per sample per side:
1. **Dielectric scale**: `dielectricScale = fabs(1.0 - input * (1/nonLin))`
   - AW source has `fabs(2.0 - ((input + nonLin)/nonLin))` which algebraically simplifies to the above
   - `1/nonLin` is block-rate (precompute as `invNonLin`)
2. **Cutoff smoothing**: `lowpassBaseAmount = (lowpassBaseAmount*lpSpeed + lpChase) * oneOverSpeedPlusOne`
   - AW divides by `(lpSpeed + 1)` per sample (slow!); precompute `1/(lpSpeed+1)` at block-rate
3. **Effective amount**: `lowpassAmount = lowpassBaseAmount * dielectricScale` (sample-modulated)
4. **Gearbox 3-stage IIR LP** (count cycles 0..5, switch dispatches which trio of LP cells to use):
   - case 0: A→B→D
   - case 1: A→C→E
   - case 2: A→B→F
   - case 3: A→C→D
   - case 4: A→B→E
   - case 5: A→C→F
   - Each stage: `iirLP = iirLP * invLowpass + input * lowpassAmount; input = iirLP`
5. Apply `nonLinTrim` gain compensation: `out = input * nonLinTrim`

### Per-sample transcendental audit

**Zero per-sample transcendentals.** Capacitor2 uses pure IIR multiply-add math, fabs, and gearbox switch. The block-rate `pow` and `cbrt` calls run once per process() call (cheap, no concern).

### Per-sample divides (the optimization targets — per `feedback_cortex_a8_no_double_in_hot_loops`)

AW source has TWO per-sample divides per side:
1. `(input + nonLin) / nonLin` (in dielectricScale calc)
2. `(lowpassBaseAmount*lpSpeed + lpChase) / (lpSpeed + 1.0)` (cutoff smoothing)

Plus 1 per-sample divide for wet smoothing (we drop this — wet is handled by ChainMix at end).

**Both per-sample divides eliminated with block-rate precomputes**:
- `invNonLin = 1.0 / nonLin` (block-rate)
- `oneOverSpeedPlusOne = 1.0 / (lpSpeed + 1.0)` (block-rate)

Per-sample math becomes: 2 fabs + ~6 muls + ~6 adds per side. ~30 cycles per side ≈ 60 cycles stereo per sample. Cheap.

### Sample-rate concern (NOT a problem)

Capacitor2 deliberately does NOT scale cutoffs with sample rate. AW comment: "should not scale with sample rate, because values reaching 1 are important to its ability to bypass when set to max." 

This means the filter's actual Hz frequency shifts with the host sample rate, but the user-facing knob always reaches "fully open" and "fully closed" at the same param values. For character processing this is the right behavior. No rate compensation needed.

### Cortex-A8 hot-loop audit (per `feedback_cortex_a8_no_double_in_hot_loops`)

- **No `(double)` casts in the hot loop** — all storage is double (no float→double promotion)
- **No int division** — no fixed-point math
- **No transcendentals per sample** — verified
- **Two double divides per sample** in AW source → both replaced with block-rate reciprocal multiplies

Net: Filament's hot loop is pure double FMA. Should be the friendliest atom for Cortex-A8 scalar VFPv3.

## AW scalar defaults audit (per `feedback_aw_param_default_subtle`)

**Critical finding**: at AW defaults (A=1.0, B=0.0, C=1.0, D=1.0), Capacitor2 is essentially BYPASS:
- A=1.0 → lowpassChase=1.0 → lowpassAmount=1.0 → `iirLP = iirLP*0 + input*1 = input` (filter passes everything)
- B=0.0 → highpass amount=0 → no highpass effect
- C=1.0 → nonLin=1.0 → strong FM
- D=1.0 → fully wet

The math reduces to ≈ input. Per the AW design, default values give the user a "starting point of transparent."

**Required remap for Filament**: defaults must give an audible characterful sound. Mapped as:

| User knob | AW param | Filament default | At default → audible behavior |
|---|---|---|---|
| **Drive** | (Console0 gain) | 0.5 | unity gain into the saturation (transparent) |
| **Cutoff** | A (lowpass) | 0.6 | partially closed LP, audibly filtered but not muffled |
| **FM** | C (nonLin shape) | 0.5 | moderate signal-voltage modulation (filter "breathes" audibly with input) |
| **Mix** | (ChainMix) | 1.0 | 100% wet (set-and-forget default) |

(B is fixed at 0 — highpass side disabled entirely; we don't use it.)

At Filament defaults the user should hear a clearly-filtered signal with audible cutoff modulation responding to input level. Per the AW-defaults-subtle memory rule: drop the unit in fresh, immediately hear the character.

## State + memory budget

| Group | Size |
|---|---|
| Console0Channel state (4 avg doubles) | 32 B |
| Capacitor2 lowpass IIR cells (6 LP × 2 sides) | 12 doubles = 96 B |
| Capacitor2 gearbox counter | 1 int |
| Capacitor2 baseAmount smoothing state | 1 double |
| Console0Bus state | 32 B |
| **Total** | ~200 B per instance |

Trivial. No buffers. No allocations.

## CPU projection

Per host sample stereo:

| Stage | Cycles |
|---|---|
| Console0Channel sat (2 LP avg + sat polynomial per side) | ~60 |
| Capacitor2 LP gearbox (1 fabs + ~5 muls + ~5 adds per stage × 3 stages per side) | ~100 |
| Console0Bus desat | ~60 |
| ChainMix crossfade | ~10 |
| Per-sample boilerplate (denormal flush) | ~10 |
| **Total** | ~240 cycles |

At 720 MHz Cortex-A8: ~333 ns per sample. Audio frame at 48k: 20.83 µs. **CPU projection: ~1.6% stereo per instance.**

Lighter than TickerTape (~1.7%), much lighter than Lacquer (~14%). Should stack comfortably.

## Parameter mapping (4 plies, all continuous with standard coarse/fine)

| Knob | Range | Default | Behavior |
|---|---|---|---|
| **Drive** | 0..1 | 0.5 | Console0Channel + Bus gain (symmetric, transparent at 0.5). Same shape as TickerTape / Lacquer. |
| **Cutoff** | 0..1 | 0.6 | Maps to Capacitor2's A (lowpass). 0 = fully closed (mute), 1 = fully open (bypass). Default 0.6 = audibly filtered without being muffled. |
| **FM** | 0..1 | 0.5 | Maps to Capacitor2's C (nonLin shape) inverted: low FM knob = weak modulation, high FM knob = strong modulation. Internally: `nonLin = 1 + (1 - FM_knob) * 6` so FM_knob=0 → nonLin=7 (weak FM), FM_knob=1 → nonLin=1 (strong FM). |
| **Mix** | 0..1 | 1.0 | ChainMix dry/wet. Default 100% wet for set-and-forget. |

Stepping: standard `setSteps(0.1, 0.01, 0.001, 0.001)` — coarse/fine encoder feel per the Lacquer iteration lesson.

## LOAD-BEARING design invariants

1. **Lowpass side ONLY, highpass disabled** — saves IIR state + cycles. AW's full Capacitor2 has both; Filament intentionally drops HP for the "voltage-modulated LP synth filter" focus.
2. **Gearbox dispatch preserved verbatim** — the count cycle pattern A→B→D / A→C→E / etc. is what gives Capacitor2 its multi-pole-without-artifacts behavior. Don't simplify into a linear cascade.
3. **dielectricScale uses fabs (always positive)** — the math depends on absolute deviation from 1.0; sign would flip the modulation direction.
4. **nonLinTrim gain compensation** preserved — keeps perceived loudness constant as FM amount changes.
5. **Block-rate `pow` and `cbrt` calls stay block-rate** (not optimized into the hot loop) — already cheap once per block.
6. **Per-sample divides eliminated with reciprocal precomputes** — per `feedback_cortex_a8_no_double_in_hot_loops`. AW had 2 per-sample divides per side; we replace both with multiplies.

## CloudSeed-trap audit (preventive)

- No `firstFrame` guards — all state init via ctor
- No allocations after construction
- No host APIs (Capacitor2 explicitly doesn't use sampleRate)
- No `std::vector`
- No modulated reads — Capacitor2 has no buffers, just IIR state
- No runtime-branched DSP dispatch — gearbox switch is on `count` (deterministic 0..5 cycle)
- No transcendentals per sample (block-rate `pow` and `cbrt` only)
- Per-sample dither dropped per template
- `-fno-tree-vectorize` package-wide

**Verdict**: cleanest atom yet. No buffers, no rate magic, no transcendentals, no nonlinear feedback (the gearbox is feedforward).

## Phasing — single-shot (Phase A)

The atom is simple and the chain pattern is established. One commit:

1. `mods/house/atoms/Capacitor2.h` (NEW component — `Capacitor2Mono` per-side helper class + optional `house::Capacitor2` od::Object wrapper for future Lua-graph use; lowpass-side-only variant)
2. `mods/house/atoms/Filament.h` (NEW monolithic chain Object — inlines Console0 sat math + uses Capacitor2Mono internally + inline ChainMix logic)
3. `mods/house/assets/Filament.lua` (NEW unit, 4 plies)
4. `mods/house/house.cpp.swig` (add `%include` for both)
5. `mods/house/assets/toc.lua` (add Filament entry)
6. `mods/house/mod.mk`: bump 0.1.0.23 → 0.1.0.24

Build both arches + lints + install linux. Second-pass audit. Hardware audition.

**Hardware gate**:
- Filter character clearly audible at defaults (no "where's the effect?" moment)
- FM knob audibly modulates filter cutoff with input dynamics — sweeps should feel "alive"
- Cutoff sweep covers from closed to open across the knob travel
- CPU under 5% per instance stereo
- No instability / oscillation / runaway behavior

## Files

```
mods/house/atoms/Capacitor2.h       # NEW component (Mono helper + Object wrapper)
mods/house/atoms/Filament.h         # NEW monolithic chain Object
mods/house/assets/Filament.lua      # NEW 4-ply unit
mods/house/house.cpp.swig           # add %include lines
mods/house/assets/toc.lua           # add Filament entry
mods/house/mod.mk                   # bump 0.1.0.23 → 0.1.0.24
planning/filament-port-plan.md      # this doc
```

## Why this plan respects established rules

- `feedback_atoms_as_components`: Capacitor2 ships as component (Mono + optional Object); Filament IS the user-facing unit
- `feedback_aw_atom_port_template`: hybrid float (state in double for precision-critical IIR — but per `feedback_cortex_a8_no_double_in_hot_loops`, doubles are FINE in this loop because there are no float→double casts and no expensive ops; the doubles ARE the only storage type so no cast tax)
- `feedback_no_third_party_branding`: Capacitor2 keeps AW name (faithful math port); Filament is habitat-native (original chain composition)
- `feedback_aw_param_default_subtle`: AW defaults are bypass; remapped Filament defaults to give immediate audible character (Cutoff=0.6, FM=0.5)
- `feedback_cortex_a8_no_double_in_hot_loops`: per-sample divides eliminated via block-rate reciprocal precomputes; no implicit float→double casts in hot loop (all state is already double)
- `feedback_identical_means_identical`: Capacitor2 IIR + gearbox + dielectricScale math preserved verbatim; only the per-sample divides → multiplies for speed
- `feedback_no_out_of_line_virtuals`: header-only
- `feedback_disable_tree_vectorize_am335x`: package mod.mk enforces
- `feedback_always_build_both_arches`: Phase A builds both
- `feedback_linux_build_auto_install`: linux auto-install
- `feedback_package_version_bump`: 0.1.0.23 → 0.1.0.24
- `feedback_persist_plans_to_repo`: this plan doc lands before code

## Open implementation questions

1. **FM curve choice**: linear (`nonLin = 1 + (1-FM)*6`) or exponential? Linear is simple and matches AW's C scaling. Audition decides.
2. **Should B (highpass) be promoted to a knob too?** Current plan says no — keep LP-only for "synth filter character" focus. Could add as Phase B if user wants HP+LP morph.
3. **Cutoff response curve**: AW uses `lowpassChase = A²` (quadratic). User-facing Cutoff knob could be linear (passthrough) or square-mapped. Square-mapped feels more "musical" for filter sweeps. Default to square: `lowpassChase = Cutoff * Cutoff`.
4. **Default Cutoff = 0.6** — picked for "audibly filtered" at default. Could be 0.5 or 0.7. Audition tunes.
