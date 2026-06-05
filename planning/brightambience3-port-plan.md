# BrightAmbience3 port plan

Status: **PLANNED 2026-06-04**. Fourth AW atom in the house package. Per `planning/airwindows-reverb-research.md` addendum, "BrightAmbience3 is the gated bright halo; use the '3', not original BrightAmbience (original is naive-prime-tap, CPU-hungry)."

Reality check on the "use 3 not original" guidance: BrightAmbience3's CPU improvement over the original comes from the **multi-pole `lastRef[]` smoothing tail**, not from undersampling. At ER-301's 48k, `overallscale = 48000/44100 = 1.088 → cycleEnd = 1`, so the undersample shell fires every sample and provides no FLOP relief. The tail averaging IS still in play and IS still cheap. So "3" gives us the better-sounding-engine without the original's naive sparse-prime cost.

## Source

- Local: `~/repos/airwindows/plugins/MacVST/BrightAmbience3/source/{BrightAmbience3.h, BrightAmbience3.cpp, BrightAmbience3Proc.cpp}`
- License: MIT
- Naming: keep upstream as **BrightAmbience3**

## Topology (verified from source)

NOT an FDN. **Sparse-prime-tap delay summation**:

```
input
  → denormal flush + capture drySample
  → cycle++
  → IF cycle == cycleEnd:
      pL[count] = input + feedbackB
      pR[count] = input + feedbackA   (cross-coupled)
      sum 'length' taps from pL/pR at prime offsets [start..start+length]
      normalize by cbrt(length)
      → resonant SVF (one per channel) with figureL/R coefficients
      → sin(tempSample) * feedbackAmount → feedback
      gcount--
      update lastRefL/R[] at cycleEnd-specific positions (1..4)
      cycle = 0
    ELSE:
      output = lastRefL/R[cycle]   (interpolation tail)
  → multi-pole averaging (1-4 passes via switch fallthrough)
  → wet/dry crossfade
  → output
```

**Stereo handling**: cross-coupled feedback (L gets feedbackB, R gets feedbackA), but L→L and R→R routing. Same internal-stereo pattern as the three shipped atoms.

**Tap window**: `start = A*400 + 88` (range 88..488), `length = B^2 * 487 + 1` (range 1..488). Auto-clamped so `start + length <= 488`. Two 488-entry static prime arrays (primeL, primeR) sourced from `BrightAmbience3.h` lines 26-27 (file-static, ~4 KB rodata).

**Feedback math**: `feedbackAmount = C * 0.25` (max 0.25). `sin()` shaping per side per cycle.

**SVF**: fixed `freq = 1000/sampleRate` (≈ 3.5 kHz at 48k). Resonance scales with `length * feedbackAmount`. Computed once per block. One SVF state per channel.

## State arrays

| Group | Lines | Bytes (double) | Bytes (hybrid float) |
|---|---|---|---|
| pL, pR (sparse-tap buffers) | 2 × 32768 | 512 KB | **256 KB** |
| figureL, figureR (SVF coeffs+state) | 18 doubles | 144 B | 144 B (stays double) |
| lastRefL, lastRefR (interp tail) | 20 doubles | 160 B | 80 B (float) |
| feedbackA, feedbackB | 2 doubles | 16 B | 8 B (float) |
| gcount, cycle | 2 ints | 8 B | 8 B |
| primeL, primeR (file-static rodata) | 2 × 488 ints | 3.9 KB | 3.9 KB |
| **Total state per instance (hybrid)** | | | **~256 KB** |

L2 budget is 256 KB on Cortex-A8. Hybrid float lands right at the line; full double would blow it 2×. **Hybrid float is non-optional here**, not just a CPU optimization.

## Public parameters

| AW name | Range | Default | Mapping | Effect |
|---|---|---|---|---|
| A "Position" | 0..1 | 0.5 | `start = A*400+88` (clamped to 488-length) | Where in the 488-entry prime table the tap window starts. Low = nearer taps (early reflection feel), high = farther taps (longer halo) |
| B "Size" | 0..1 | 0.5 | `length = B^2 * 487 + 1` | How many sparse taps sum into one output. **The headline CPU dial** — at B=1.0, 487 taps per channel per cycle |
| C "Brightness" | 0..1 | 0.5 | `feedbackAmount = C*0.25` (caps SVF resonance + sin feedback) | Amount of resonant filter feedback. Higher = brighter, more rung. Above ~0.7 starts to self-resonate |
| D "Wetness" | 0..1 | 0.5 | wet/dry crossfade | Standard mix |

4 params, all 0..1, identical to AW source. Default of 0.5 matches AW out-of-box behavior.

## Per-sample work estimate

**Every sample (always-on):**
- Denormal flush, drySample capture
- cycle increment + branch
- multi-pole averaging switch (1-4 passes of `lastRef[]` interp)
- wet/dry crossfade
- ~10-15 FLOPs

**Every cycle hit (= every sample at 48k):**
- 1 write to pL, 1 write to pR
- `length` reads from pL + `length` reads from pR (each at a sparse prime offset)
- 2 sums (one per channel), 2 divides by `cbrt(length)` (precomputable per block? yes — move to block setup)
- 1 SVF biquad-style step per channel (~6 mul + 4 add)
- 1 sin() per channel
- lastRef[] update (3-5 stores depending on cycleEnd)

At B=1.0 (length=487), worst case: ~487 sparse buffer reads × 2 channels × 48000 = 47M sparse loads/sec. Each load is 4 bytes (float). With pL/pR each 128 KB > L2/2, most loads miss L2. This is **memory-bandwidth-bound, not FLOP-bound**.

**Projected stereo CPU on Cortex-A8 at default (B=0.5, length≈122):**
- ~20-30% — between WoodenBox's 14% and the worst case
- At B=1.0 (length=487): **likely 50-70% or worse**, potentially infeasible
- At B=0.3 (length=44): probably ~10%

**Risk**: at high B, may saturate CPU on hardware. Mitigation options:
1. **Clamp max length** for ER-301 build (e.g. B saturates at 0.6 → length ≈ 175). Forces a "house-tuned" variant.
2. **Ship vanilla, let user dial B carefully.** Document the CPU sensitivity in the unit description.
3. **NEON the inner sum loop** in Phase 2. 487 reads with prime-stride is gather-load territory, hard to NEON.

**Recommend option 2 for Phase 1** — ship faithful, document. If hardware testing shows it's unusable above some B threshold, soft-clamp in the unit's biasMap. Don't add ER-301-specific length clamps inside the atom (preserves AW behavior for emu match).

## CloudSeed-trap audit

- **No `if (firstFrame)` guards.** `cycle` and `gcount` init to known values, all state arrays memset to 0. First-frame behavior: `cycle++` hits cycleEnd=1 on the very first sample, so the cycle path fires immediately. With state zero'd and `feedbackA/B = 0`, the first cycle just writes input to pL/pR[gcount] and reads back zeros (since pL[anywhere else] = 0). Output is 0 for the first cycle. No constant-init needed.
- **No allocations after constructor.** All arrays fixed-size.
- **No host APIs.** `getSampleRate()` only, at top of `process()` per block.
- **No `std::vector` resizes.**
- **No modulated reads** — tap offsets are determined by the block-rate `start` + the static `primeL/R` table. Within a block, identical taps every cycle. Safe from doppler-edge issues.
- **No runtime-branched DSP dispatch in the per-sample loop** in a problematic way — the cycleEnd switch fallthrough is a fixed pattern at block-rate-constant `cycleEnd`. Per `feedback_runtime_branched_dsp_dispatch`, this is OK: the branch is on a block-constant value, not per-sample variable state.
- **One `sin()` per channel per cycle** — at 48k cycleEnd=1 that's per-sample. Need to verify sin() doesn't trigger libm/.so trap (per `feedback_disable_tree_vectorize_am335x` — already disabled package-wide in `mod.mk`). Cortex-A8 scalar sin is ~50ns; should be fine.
- **One `cbrt()` per cycle** in the source — **hoist to block setup** as an optimization (length doesn't change within a block). One less per-sample call.
- **Per-sample dither** dropped per template.

**Verdict: clean.** Same risk profile as the three shipped ports, plus one extra: **CPU sensitivity to B (Size)** under user control. No structural hazards.

## Phasing

Skip Phase 0 Smoketest per template (three consecutive first-try successes on shipped atoms).

### Phase 1 — atom + unit (hybrid float from start)

1. Drop `mods/house/atoms/BrightAmbience3.h`:
   - Header-only `od::Object` subclass per template
   - All template adaptations: drop VST host, drop dither, drop `rand()` fpd seed, memset state, hoist `getSampleRate()`
   - **Hybrid float from Phase 1**:
     - `float pL[32768], pR[32768]` (state arrays — 256 KB stereo, fits L2)
     - `float feedbackA, feedbackB, lastRefL[10], lastRefR[10]` (intermediates/taps)
     - `double figureL[9], figureR[9]` (SVF state stays double — per-sample IIR accumulates error fast in float)
     - `double overallscale, feedbackAmount, wet, K, norm` (block-rate scalars)
     - `int gcount, cycle, cycleEnd, start, length`
   - Hoist `cbrt(length)` to block-rate (compute once per block, store as `double normalizer = 1.0 / cbrt(length)`, multiply per cycle instead of divide)
   - `primeL[488]`, `primeR[488]` as `static constexpr int[]` arrays in the header (or inside the class as `static const`). 3.9 KB rodata, no per-instance cost
   - 4 params: `mPosition` (default 0.5), `mSize` (default 0.5), `mBrightness` (default 0.5), `mWetness` (default 0.5)
   - Preserve cross-coupled feedback literally (L gets feedbackB, R gets feedbackA)
   - Preserve switch fallthrough averaging literally (cycleEnd-pole average)
2. Drop `mods/house/assets/BrightAmbience3.lua`:
   - `addObject("op", libhouse.BrightAmbience3())`
   - Standard channelCount In/Out wiring
   - 4 ParameterAdapter ties → 4 GainBias plies (Position, Size, Brightness, Wetness)
   - Default biases match C++ defaults (0.5 across)
3. Update `mods/house/house.cpp.swig`:
   - `#include "atoms/BrightAmbience3.h"` in `%{ %}` block
   - `%include "atoms/BrightAmbience3.h"` line
4. Update `mods/house/assets/toc.lua` — one entry:
   - `{ title = "BrightAmbience3", moduleName = "BrightAmbience3", category = "House", keywords = "reverb, ambience, bright, halo, sparse, prime, brightambience3, airwindows" }`
5. Bump `mods/house/mod.mk` PKGVERSION: `0.1.0.6 → 0.1.0.7`
6. Build verification per template:
   - `make house-clean ARCH=am335x && make house ARCH=am335x`
   - `make house` (linux)
   - `tools/check-graphic-virtual-defs.sh` — pass
   - `tools/check-neon-hints.sh testing/am335x/libhouse.so` — 0 suspect
   - `arm-none-eabi-nm -C testing/am335x/mods/house/house_swig.o | grep "vtable for house::BrightAmbience3"` — expect V
   - `ls testing/am335x/mods/house/BrightAmbience3.o` — expect not found
7. Install linux, audition in emu (start at default B=0.5), then ship am335x, audition on hardware (start with B low and bring up to find CPU ceiling).

Estimated time: one focused session. DSP body is moderate (~150 lines of per-sample work, less than CreamCoat's ~290), but the sparse-tap loop deserves care.

### Phase 2 (deferred) — CPU optimization

- **NEON the prime-tap sum**: 4-wide partial sums into a vector accumulator, hadd at the end. The challenge is the prime-offset gather — Cortex-A8 NEON has no gather load (per `feedback_neon_no_gather_lut_dsp`). May need to manually 4-unroll with explicit `vld1.f32 [pL + primeL[off+0]]` style loads, accumulate into a NEON quad, then hadd. Possibly 2-3× speedup on the inner sum.
- **SVF as TPT state-variable**: AW's `figureL[2..8]` is a custom 2nd-order biquad-shape filter. Converting to TPT SVF (per `feedback_neon_soa_svf_bank`) would let us pre-pack coefficients and use the established kernel. Modest win; defer.

## Files / commits to reference

- Template: `feedback_aw_atom_port_template` memory
- Worked-pattern reference: `mods/house/atoms/CreamCoat.h` (hybrid float exemplar)
- Architecture rationale: `planning/house-atom-architecture.md`
- Per-bucket rationale: `planning/airwindows-reverb-research.md` addendum
- Phase 1 commit references: `0a89103` (kWoodRoom), `a2f1db6` (WoodenBox), `dd40ea0` (CreamCoat)

## Open questions / decisions

1. **Length cap** — leave at full 487 (faithful) or soft-cap via biasMap at the unit level to keep CPU manageable. **Decision: leave at full, document. Revisit after hardware audition.**
2. **Position/Size readouts** — could expose `length` and `start` as readable integers in the ply readout instead of raw 0..1. **Decision: keep 0..1 for first ship; revisit if user wants integer readout.**
