# CreamCoat port plan

Status: planning. Target: house package, atom architecture.
Third AW atom after kWoodRoom (29% stereo, shipped) and
WoodenBox (14% stereo, shipped). Per
`planning/airwindows-reverb-research.md` addendum: "CreamCoat —
proves the canonical divisor+Bezier mechanic in isolation;
kWoodRoom already implements this pattern via its outer Bezier,
but CreamCoat is the canonical implementation."

## Source

- Local: `~/repos/airwindows/plugins/MacVST/CreamCoat/source/{CreamCoat.h, CreamCoat.cpp, CreamCoatProc.cpp}`
- License: MIT (Airwindows, Chris Johnson)
- AW category: bright-ambience engine (ClearCoat / CreamCoat /
  CrunchCoat trio); CreamCoat is the "lush + cheaper" middle of
  the three, per handoff §1.2
- Naming: keep upstream as **CreamCoat**

## Topology (verified from source)

A 4-stage 4×4 diff-Householder FDN per side (same `hX - sum
others` math as WoodenBox), wrapped in a **user-controlled
Bezier-undersample shell** (DeRez = param C), with a long
**15000-sample predelay buffer** read at a user-controlled tap.
Cross-coupled between L and R via the final-stage feedback taps,
same internal-stereo pattern as kWoodRoom / WoodenBox.

The headline difference from kWoodRoom and WoodenBox:
**DeRez is a user knob**, not hard-coded or internal. Lower
DeRez = the FDN core runs at a lower internal rate (every Nth
input sample) and Bezier-reconstructs back to host rate. **Lush
+ cheaper** is the explicit design tradeoff.

```
input
  → Bezier-undersample accumulator (bez[] holds in+SampL/R)
  → (when cycle > 1.0 / derez:)
      Predelay write/read (countZ runs at the undersampled rate;
        D param sets the predelay tap = predelay*D*derez samples)
      4 input lines fed from (SampL + UnInL) + feedback*regen
      4-stage 4×4 FDN per side, walked in reverse line order on R
      Final-stage outputs → feedback taps + Householder-summed output
  → Bezier reconstruction (every input sample, quadratic curve)
  → output clipping at ±1
  → wet * inputSample + dry * drySample (submix-style, both can be 1.0)
```

**Stereo handling**: NO L/R swap (unlike WoodenBox). Input L
routes to L verb path → output L straight through. Cross-channel
feedback inside the FDN gives a stereo "wash" but the L/R
labeling stays consistent.

**Submix wet/dry mix**: per AW source comment, "this reverb
makes 50% full dry AND full wet, not crossfaded. that's so it
can be on submixes without cutting back dry channel when
adjusted." So at Wetness=0.5, output is `1.0 * input + 1.0 *
dry`. At Wetness=0.25 (default), wet is scaled down but dry
stays at full. At Wetness=1.0, wet is full and dry is 0.
Different from kWoodRoom and WoodenBox's standard crossfade.

**Predelay** uses a single shared 15000-sample buffer (one per
channel) with the tap controlled by `adjPredelay = predelay * D
* derez`. At full rate (DeRez=1.0) and Predlay=1.0, that's
~312 ms at 48k. At low DeRez the predelay scales with the
undersample rate.

**Householder coefficient**: `outX - (sum others)` — same
4-line variant WoodenBox uses. Sum-preserving.

## State arrays (per channel, then total)

| Group | Lines | Bytes / side | Notes |
|---|---|---|---|
| 4×4 FDN `a{A..P}{L,R}` | 16 | ~164 KB | per-line sizes 12..3110 samples |
| Predelay `aZ{L,R}` | 1 | ~120 KB | 15000+5 samples |
| Counters `count*{L,R} + countZ` | 33 ints | ~132 B | 16 per side + countZ |
| Feedback floats | 8 | 64 B | cross-channel |
| `previous*L/R` | 10 | 80 B | declared but unused in proc |
| `bez[13]` | 13 | 104 B | adds InL/R + UnInL/R vs WoodenBox |
| `short*` (current delay lengths) | 16 ints | 64 B | preset table |
| `prevclearcoat` | 1 int | 4 B | Select-change detection |

**Total ≈ 568 KB stereo** — significantly heavier than
kWoodRoom (~113 KB) or WoodenBox (~53 KB). Predelay alone is
~240 KB stereo; the 16 FDN lines sum to ~328 KB stereo because
several lines are quite long (kshortN=3110, kshortJ=2645).

Phase 2+ float-convert of the FDN arrays would save ~164 KB.
Predelay float-convert saves another ~120 KB. Worth doing if
CPU/memory budget tightens; defer until needed per the template.

## Public parameters

| AW name | Range | Default | Mapping | Effect |
|---|---|---|---|---|
| A "Select" | int 0..16 (17 stops) | 0.5 → 8 | `box` (stepped via continuous map) | Picks one of 17 prebaked `short*` preset tables. Same shape as WoodenBox. Triggers state reset on change. |
| B "Regen" | 0..1 | 0.5 | `regen` | Per-line feedback amount; `regen = (1-(1-B)^2)*0.0625`. Max effective ≈ 0.0625. Higher cap than WoodenBox (which capped at 0.0336). |
| C "DeRez" | 0..1 | **1.0** | `derez` | **Headline knob**. User-controlled Bezier-undersample rate. 1.0 = full host rate (max CPU); lower = the FDN core runs every Nth sample (lush + cheaper). Hard-locked to `1/N` subdivisions. |
| D "Predlay" | 0..1 | 0.0 | `predelay` | Predelay tap (`adjPredelay = 15000 * D * derez`). At C=1.0 D=1.0 ≈ 312 ms at 48k; at lower DeRez the predelay length scales with the undersample rate. |
| E "Wetness" | 0..1 | 0.25 | `mix` | Submix-style wet/dry. 0.5 = 100% wet AND 100% dry (sum, no crossfade). Quadratic curve on wet. Suited for sends. |

5 params total — between WoodenBox's 3 and kWoodRoom's 6.

## Per-sample work estimate

Two regimes (same shape as kWoodRoom / WoodenBox):

**Every input sample (~always-on):**
- ~15-25 FLOPs (Bezier accumulation + reconstruction + wet/dry
  mix + ±1 clipping; no IIR filter stage)
- Drop the per-sample dither per template

**Every reverb-cycle hit (fires at `derez` rate — every sample at
DeRez=1.0, every other sample at DeRez=0.5, etc.):**
- Predelay read+write: ~4 ops (write, increment, wrap check, read)
- 4 stages × 4 lines × ~7 ops per Householder = ~112 ops per side
  × 2 sides = ~224 ops
- ~64 counter-advance branches per cycle
- Total ~250 FLOPs per cycle hit, ~64 branches

At default DeRez=1.0, that's per-sample cost. Projected stereo
CPU on Cortex-A8 at default: **~18-22%** (between WoodenBox's
14% and kWoodRoom's 29% — same topology family as WoodenBox
plus predelay tap, no inner Bezier filter). Lower DeRez drops
proportionally: at DeRez=0.5 (every other sample), expect
~10-12%; at DeRez=0.25, ~6-8%.

Memory bandwidth is the bigger concern than FLOPs at this state
size — 568 KB stereo blows past L2 (256KB). Per the handoff,
this is the AM335x bottleneck for any reverb in this tier.
Manage by listen-testing for cache-thrash artifacts; if any,
that's a Phase 2 motivation to float-convert.

## CloudSeed-trap audit

Verified against source:

- **No `if (firstFrame)` guards.** Counters init to 1; first
  reverb sample fires when `bez[bez_cycle] > 1.0` after enough
  accumulation. **LOAD-BEARING**: AW source does NOT initialize
  `bez[bez_cycle]` to 1.0 (unlike kWoodRoom/WoodenBox). The
  first-frame behavior is "accumulate until cycle hits 1.0, then
  fire." This means there's an audible first-frame silence
  proportional to 1/derez. At DeRez=1.0 that's one sample
  (inaudible). At DeRez=0.01 that's 100 samples (~2 ms,
  arguably noticeable). Accept as AW behavior; user defaults
  DeRez=1.0 so this is moot.
- **No allocations after constructor.** All arrays fixed-size.
- **No init-order dependencies.**
- **No host APIs.** `getSampleRate()` is the only host call,
  read at top of `process()` per block.
- **No `std::vector` resizes.**
- **No modulated reads.** Same fixed-tap read pattern as
  kWoodRoom / WoodenBox (`arr[c - ((c > d) ? d+1 : 0)]`).
- **Predelay read at user-controlled offset** (`adjPredelay`)
  is single fixed-tap per cycle, not modulated. Safe.
- **No runtime-branched DSP dispatch in the per-sample loop.**
  Select switch runs once per change, not per sample.
- **Per-sample dither** dropped per template.

**Verdict: clean.** Same risk profile as the two shipped ports.

## Phasing

Per `feedback_aw_atom_port_template`, **skip Phase 0 Smoketest
gate** and go straight to Phase 1 atom + unit. Two consecutive
hardware-first-try successes (kWoodRoom, WoodenBox) validate the
template path. CreamCoat is structurally similar (same FDN
shape, same first-frame-init discipline minus the bez_cycle=1.0
trick).

If hardware hangs, reconstitute Smoketest from git `5c0f29c`
and bisect. Most likely culprits if something goes wrong:

1. The predelay buffer access — `adjPredelay` math goes wrong
   at edge cases (DeRez near 0, very small predelay). The
   wrap-correction formula handles it, but bears auditing.
2. First-frame silence at low DeRez — not a crash, but the
   user might think it's broken if Wetness > 0 and audio is
   silent for the first cycle. Default DeRez=1.0 means one
   sample silence, inaudible.

### Phase 1 — atom + unit (full port in one cycle)

1. Drop `mods/house/atoms/CreamCoat.h` (header-only
   `od::Object` subclass per the template).
   - All template adaptations: drop VST host deps, drop dither,
     drop `rand()` fpd seed, memset state arrays, keep counters
     at 1.
   - **First-frame note**: AW source does NOT init bez[bez_cycle]
     to 1.0. Don't add it. The first-cycle silence is AW
     behavior.
   - Stereo handling: standard (no L/R swap), cross-coupled
     inside the atom; `In L` / `In R` → `Out L` / `Out R`.
   - 5 params: `mSelect` (0..1, default 0.5), `mRegen` (0..1,
     default 0.5), `mDeRez` (0..1, default 1.0), `mPredlay`
     (0..1, default 0.0), `mWetness` (0..1, default 0.25).
   - Preserve the Select-change state reset literally.
   - Preserve the submix wet/dry math literally (`wet = E*2`
     then clamp + quadratic; dry = `2-wet` then clamp).
   - Preserve output ±1 clipping (lines 327-330 of source).
2. Drop `mods/house/assets/CreamCoat.lua` (thin unit per
   template).
   - `addObject("op", libhouse.CreamCoat())`.
   - Standard channelCount In/Out wiring.
   - 5 ParameterAdapter ties → 5 GainBias plies.
   - Default biases match the C++ defaults — note DeRez default
     is **1.0** not 0.5.
3. Update `mods/house/house.cpp.swig`:
   - `#include "atoms/CreamCoat.h"` in `%{ %}` block
   - `%include "atoms/CreamCoat.h"` line
4. Update `mods/house/assets/toc.lua` — one entry:
   - `{ title = "CreamCoat", moduleName = "CreamCoat", category = "House", keywords = "reverb, ambience, bright, cream, lush, derez, undersample, airwindows" }`
5. Bump `mods/house/mod.mk` PKGVERSION: `0.1.0.5 → 0.1.0.6`.
6. Build verification per template:
   - `make house-clean ARCH=am335x && make house ARCH=am335x`
   - `make house` (linux)
   - `tools/check-graphic-virtual-defs.sh` — pass
   - `tools/check-neon-hints.sh testing/am335x/libhouse.so` — 0 suspect
   - `arm-none-eabi-nm -C testing/am335x/mods/house/house_swig.o | grep "vtable for house::CreamCoat"` — expect V
   - `ls testing/am335x/mods/house/CreamCoat.o` — expect not found
7. Install linux, ship am335x, audition.

Estimated time: one focused session. Larger DSP body than
WoodenBox (~290 lines of per-sample work vs ~200) and an extra
predelay block to add, but the structural pattern is the same.

## Future phases (deferred per template)

- **Phase 2 (later)**: float-convert the FDN arrays (saves ~164
  KB) and predelay buffer (saves ~120 KB). Listen-test against
  the double reference. Per handoff: float precision in long
  regenerating feedback loops is exactly where denormals + slow
  drift bite — A/B carefully.
- **Phase 3 (later)**: NEON Householder reduction (same `3*hX -
  total` mechanic as queued for kWoodRoom and WoodenBox Phase
  4). 4-line variant: `2*hX - total` per output. Per-stage cost
  drops from 16 ops to ~6 ops.
- **Phase 4 (later)**: predelay buffer float-convert + cache-
  friendly access pattern. Predelay is a single fixed-tap read
  per cycle, but the 15000-sample working set is big.
- **DeRez UX polish (later)**: the headline knob lands on a
  continuous 0..1 readout that internally hard-locks to `1/N`
  subdivisions. Audibly stepped at low values. Could surface a
  stepped/quantized readout if the user wants to land on exact
  divisor values.

## Open questions

1. **DeRez default 1.0 vs 0.5**: AW ships 1.0 (full rate, max
   character + max CPU). For a hardware-constrained target like
   AM335x, default 0.5 might be friendlier (half CPU, still
   plenty of character). **Recommend keeping AW default of 1.0**
   per the faithful-port discipline; user can dial down to taste.
2. **Predelay tap range**: at DeRez=1.0 D=1.0 → ~312 ms predelay
   at 48k. Plenty for most uses. At DeRez=0.1 the max predelay
   stretches to ~3.1 seconds. Acceptable, just unusual.
3. **Submix wet/dry math** is non-obvious to users coming from
   standard crossfade reverbs. Worth a note in the unit
   description: "Wetness=0.5 is full wet AND full dry (sum,
   send-style mix)".

## Risk audit

| Risk | Mitigation |
|---|---|
| First-frame hang on Cortex-A8 (CloudSeed pattern) | Source-audit clean; kWoodRoom + WoodenBox precedent; if it hangs, reconstitute Smoketest from `5c0f29c` |
| `pow()` per-sample dither hangs | Dropped per template |
| Predelay buffer access edge cases | Same `arr[c - ((c > d) ? d+1 : 0)]` wrap pattern as the FDN; safe |
| First-cycle silence at low DeRez confuses user | Accept as AW behavior; default DeRez=1.0 minimizes; doc note in unit |
| Submix wet/dry surprises user | Doc note: 0.5 = both full, not crossfade |
| Memory pressure on AM335x cache (568 KB stereo blows past 256 KB L2) | Listen-test for cache-thrash artifacts; if any → Phase 2 float-convert priority |
| Select-change reset glitch | WoodenBox showed this is sub-audible at this state size; CreamCoat reset is ~5× the memory (more time) but still bounded |
| Wrapper class-layout drift | `SWIG_HEADER_DEPS` auto-regenerates wrapper on header touch |

## Files / commits to reference

- Template: `feedback_aw_atom_port_template` memory
- Worked-pattern reference: `mods/house/atoms/WoodenBox.h`
  (closer to CreamCoat than kWoodRoom — same 4×4 FDN family)
- Unit reference: `mods/house/assets/WoodenBox.lua`
- Architecture rationale: `planning/house-atom-architecture.md`
- Per-bucket rationale: `planning/airwindows-reverb-research.md`
  addendum
- Phase 1 commit references: `0a89103` (kWoodRoom), `a2f1db6`
  (WoodenBox)
