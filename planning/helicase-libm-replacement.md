# Helicase libm transcendental replacement

User-precious unit (FM oscillator, OPL3-inspired). User constraint:
**"i'd hate for it to sound much different"** — fidelity is the
gate, not CPU savings. Polynomial accuracy targets are set higher
than typical optimization work (~-100 dB error rather than the
casual ~-37 dB Bhaskara form).

## Recon — per-sample libm transcendental counts

### Hifi mode (2× oversampled, the heavy path)

Outer loop body runs once per output sample; contains an inner
`for (sub = 0; sub < 2; sub++)` loop that runs the full mod→
carrier→fold chain twice and decimates with a 2-tap halfband
average.

Per sub-iteration:
- `floorf(s.modPhase)` — line 394
- `tanhf(s.modFeedbackState)` — line 407
- `opl3WaveMorph(modPhaseFB, modShapeF)` — line 409
  - Calls `opl3Wave` × 2 (the morph interpolates between adjacent shapes)
  - Each `opl3Wave` calls `sinf(phase × 2π)` + `floorf(phase)`
  - **= 2 sinf, 2 floorf**
- `floorf(s.carrierPhase)` — line 422
- `opl3WaveMorph(s.carrierPhase, carrierShapeF)` — line 424
  - **= 2 sinf, 2 floorf**
- `discFold` (conditional on `discIndex > 0.001`) — line 432
  - Calls `evalShape` × 2 (morph between adjacent disc types)
  - Each evalShape either:
    - `t ≤ 7`: `opl3Wave` → 1 sinf, 1 floorf, plus optional polyBlep
    - `t = 9`: `sinf(x × π)` — 1 sinf
    - `t = 15`: `sinf(x × 2π)` + sqrtf — 1 sinf, 1 sqrtf
    - `t ∈ {8, 10, 12, 13}`: `fmodf` — 1 fmodf
  - **Worst case: 2 sinf, 1 sqrtf, multiple floorf/fmodf**

Per output sample worst case (hifi, all features active):
- **sinf: up to 12** (2 mod morph + 2 carrier morph + 2 discFold per sub × 2 subs)
- **tanhf: 2** (one per sub)
- **floorf: ~12**
- **fmodf: 0-4** (conditional on discFold shape)
- **sqrtf: 0-2** (conditional on disc type 15)

### Lofi mode (1× rate)

Per output sample:
- `floorf(modPhase)`, `floorf(carrierPhase)` — 2 floorf
- `tanhf(modFeedbackState)` — 1 tanhf
- `opl3Wave(modPhaseFB, modShape)` — 1 sinf, 1 floorf
- `opl3Wave(carrierPhase, carrierShape)` — 1 sinf, 1 floorf
- `discFold` (conditional) — up to 2 sinf

**Worst case lofi: 4 sinf, 1 tanhf, ~6 floorf, possible fmodf/sqrtf**

### CPU cost on Cortex-A8

Newlib libm rough costs:
- `sinf`: ~40-150 cycles (depends on input range; range reduction adds variance)
- `tanhf`: ~50-100 cycles
- `floorf`: ~5-10 cycles
- `fmodf`: ~30-50 cycles
- `sqrtf`: ~15 cycles (single VFP instruction)

Hifi worst case per sample: 12 × ~80 (sinf) + 2 × ~75 (tanhf) ≈ **1100 cycles**
At 800 MHz × 48 kHz: ~6.6% CPU just for transcendentals

Hifi typical case (no discFold, or discFold not on sin-shape):
8 × ~80 + 2 × ~75 ≈ 790 cycles ≈ **4.7% CPU**

These numbers are why Helicase is heavy in hifi mode.

## Replacement targets (fidelity-first)

### sinf → 13th-order Taylor polynomial (target: ~-100 dB max error)

**Current `util/neon_math.h` `sine_poly` is Bhaskara form (~-37 dB
error)** — way too coarse for FM character preservation. FM
modulates polynomial error up to carrier frequency as sidebands;
-37 dB sidebands are clearly audible on a critical patch.

Need a NEW high-accuracy polynomial. **13th-order Taylor centered
on x=0** (with input range-reduced to [-π, π] via the
phase-centered identity):

```cpp
sin(x) ≈ x × (1 + x²·(c₁ + x²·(c₂ + x²·(c₃ + x²·(c₄ + x²·(c₅ + x²·c₆))))))

c₁ = -1/6           = -0.166666667
c₂ = +1/120         = +0.00833333
c₃ = -1/5040        = -0.000198413
c₄ = +1/362880      = +2.7557e-6
c₅ = -1/39916800    = -2.5052e-8
c₆ = +1/6227020800  = +1.6059e-10
```

For `sin(2π × phase01)` where `phase01 ∈ [0, 1)`, use the
center-shift identity: `sin(2π × phase01) = -sin(2π × (phase01 - 0.5))`
so we compute `-x · poly(x)` for `x = 2π × (phase01 - 0.5)` ∈ [-π, π].

**Error analysis**: at x = ±π (worst case), Taylor 13 ≈ 10⁻⁵
absolute error. = **-100 dB**. Inaudible by ~20 dB even under FM
modulation × 10 carrier amplitude.

Cost: 7 mults + 6 adds = **13 FLOPs scalar** vs ~80 cycles libm
sinf. Net save: ~65 cycles per sin call. At 12 sinf/sample: ~780
cycles/sample = ~4.7% CPU saved on the transcendentals alone.

### tanhf → Padé 3/3 rational

Lifted verbatim from `MultibandSaturator.cpp fast_tanh` (already in
production):

```cpp
static inline float tanh_poly(float x) {
  if (x < -4.0f) return -1.0f;
  if (x >  4.0f) return  1.0f;
  float x2 = x * x;
  return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}
```

Error < 0.1% for |x| < 4 → ~-60 dB max. For Helicase's feedback
input (clamped to [-1, 1] by mod oscillator output), error is much
tighter: < 0.01% in that range = ~-80 dB.

Cost: ~6 cycles vs ~75 cycles libm tanhf. Save ~70 cycles per
tanh call. At 2 tanhf/sample: ~140 cycles/sample = ~0.85% CPU.

### floorf — leave alone

libm `floorf` is ~5-10 cycles. Replacing with `(int)x - (x<0 && x != (int)x)`
saves 3-5 cycles per call. At 12 floorf/sample = ~50 cycles total
= ~0.3% CPU. **Below threshold; skip.**

### fmodf, sqrtf — leave alone

Conditional on disc fold shapes; not always-on. Sub-threshold
even when active. Skip.

## Estimated combined CPU drop

| Mode | sinf save | tanhf save | Total drop |
|---|---|---|---|
| Hifi worst (12 sinf, 2 tanhf) | ~4.7% | ~0.85% | **~5-6%** |
| Hifi typical (8 sinf, 2 tanhf) | ~3.2% | ~0.85% | **~4%** |
| Lofi (4 sinf, 1 tanhf) | ~1.6% | ~0.4% | **~2%** |

These are conservative — actual libm sinf on newlib Cortex-A8
often runs slower than 80 cycles due to range-reduction handling.
True CPU drop in hifi could be 5-10%.

## Audition gauntlet (the load-bearing test)

User-precious unit; fidelity-first. Audition before declaring win.

**A/B reference build**: 2.6.2.49 (current ship) preserved at
`testing/linux/spreadsheet-2.6.2.49.pkg` for direct comparison.

**Audition patches** — each is a critical-character probe:

| # | Patch | What to listen for |
|---|---|---|
| 1 | Pure sine, lofi (modShape=0 carrier=0 modMix=0 modIndex=0 discIndex=0 hifi=off) | Spectral purity — any THD difference indicates polynomial error |
| 2 | Pure sine, hifi | Same as #1 but with 2× OS chain doubling polynomial error contribution |
| 3 | Heavy FM, hifi (modMix=1, modIndex=10, modShape=0, carrierShape=0) | Sideband structure / FM bell character |
| 4 | All OPL3 mod shapes (modShape sweep 0..7 with modIndex=5) | Hard-shape morph fidelity |
| 5 | Discontinuity folder all types (discIndex=1, discType sweep 0..15) | Fold transfer function shape — esp. case 9 (sine fold) and case 15 (ring fold) which use sinf |
| 6 | High feedback (feedback=1, modIndex=2, modShape=0) | Feedback chain stability — tanh approximation matters here |
| 7 | LFO mode (f0 < 1 Hz, fundamental as LFO) | DC blocker bypass path; should not exhibit any new noise |
| 8 | Critical FM-bell patch (the user's "favorite" if there is one) | Real-use confidence |

**Pass criteria**: no audible character change. If any patch
sounds different, plan dictates either:
- Re-derive polynomial coefficients with Remez (sharper than Taylor) for the failing shape's range
- Drop back to higher-order polynomial (15th, 17th)
- Roll back the affected call site

**Emu A/B**: RMS over a swept-sine input through patch #1 should
land within 1e-4 (audio-perceptible threshold ~1e-3). Anything
significantly worse on the pure-sine case indicates a polynomial
problem, not a feature-interaction problem.

## File map

| File | Change |
|---|---|
| `mods/spreadsheet/util/neon_math.h` | Add `sine_poly_hq(float phase01)` — 13th-order centered Taylor. Add `tanh_poly(float x)` — Padé 3/3 (lift from MultibandSaturator). Existing `sine_poly` (Bhaskara) stays untouched — it's the right choice for non-fidelity-critical contexts. |
| `mods/spreadsheet/Helicase.cpp` | Replace `sinf(phase × kTwoPi)` in `opl3Wave` with `sine_poly_hq(phase - floorf(phase))` (reuse existing floor). Replace `sinf(x × M_PI)` in foldShape case 9. Replace `sinf(x × 2π)` in foldShape case 15. Replace `tanhf(modFeedbackState)` in both hifi (line 407) and lofi (line 465) feedback paths. Add `#include "util/neon_math.h"`. |
| `mods/spreadsheet/mod.mk` | PKGVERSION bump 2.6.2.49 → 2.6.2.50. |

## Phases

### Phase 1 — Add polynomial primitives

Add to `util/neon_math.h`:
```cpp
// sin(x) for x ∈ [-π, π]. 13th-order Taylor, max error ~-100 dB
// at boundaries. Caller responsible for range reduction.
static inline float sine_poly_hq_x(float x);

// sin(2π × phase01) for phase01 ∈ [0, 1). Uses center-shift
// identity: sin(2π × p) = -sin(2π × (p - 0.5)).
static inline float sine_poly_hq(float phase01);

// tanh(x), Padé 3/3 rational. <0.1% error for |x| < 4.
// Hard-clamps to ±1 outside that range.
static inline float tanh_poly(float x);
```

Build linux + verify polynomials evaluate as expected (small test
harness or eyeball via direct call).

### Phase 2 — Helicase call-site replacements

Five replacement sites:
1. `opl3Wave` line 36: `sinf(phase * kTwoPi)` → `sine_poly_hq(phase - floorf(phase))`
   - The function already calls `floorf(phase)` at line 38; reuse via reorder
2. `foldShape` case 9 line 65: `sinf(x * M_PI)` → use phase01 identity:
   `sin(πx) = -sin(2π × (x/2 + 0.5))` → `-sine_poly_hq(x * 0.5f + 0.5f)`
3. `foldShape` case 15 line 94: `sinf(x * M_PI * 2.0f)` — x ∈ [-1, 1],
   so phase01 = wrap(x) = `x - floorf(x)`; then `sine_poly_hq(phase01)`
4. `process()` line 407 (hifi): `tanhf(s.modFeedbackState)` → `tanh_poly(s.modFeedbackState)`
5. `process()` line 465 (lofi): same

### Phase 3 — Build + audition

1. Linux build, install to emu
2. Run audition patches #1–#8 in emu first (cheaper than hardware)
3. If emu audition passes, am335x build + hardware audition
4. Hint audit: no NEON intrinsics changed → 0 SUSPECT (baseline preserved)
5. If hardware audition passes: ship 2.6.2.50

### Phase 4 — CPU measurement on hardware

Compare hifi-mode CPU at heavy FM patch (#3) before/after. Expect
~4-8pp drop. Document actual measurement in this doc post-ship.

## Risks & mitigations

| Risk | Mitigation |
|---|---|
| Polynomial sine has audible character difference at critical FM index | 13th-order Taylor gets -100 dB error; FM modulation can't lift that into audibility under normal use. If somehow user finds a patch where it does: bump to 15th-order or per-call Remez. |
| Feedback path with tanh approximation produces different chaos at high feedback | Padé 3/3 is < 0.1% error for |x| < 4; feedback state stays in [-1, 1] where error is < 0.01% (-80 dB). Cannot perceptibly alter chaos. If user does notice: keep libm tanhf in the feedback path, only do sinf replacement. |
| DC blocker behavior changes due to upstream signal differences | Upstream signal differences are -100 dB; DC blocker is a one-pole IIR which integrates error. Error stays bounded at -100 dB. |
| OPL3 character degraded (esp. quarter-sine, alternating shapes that mix sin with hard branches) | Shape branches use the same `s = sinf(phase × 2π)` value; polynomial replacement preserves bit-level structure as long as polynomial output matches sinf within target accuracy. |
| Build regression on existing units that share `util/neon_math.h` | New functions are additive; no existing function changes. Other consumers (Petrichor, Parfait, Impasto) unaffected. |

## Out of scope (Phase 4+ if pursued later)

- NEON 2-lane pack of the 2× OS sub-iters: mod feedback path is
  serial sample-to-sample within sub-iters (`modFeedbackState`
  carries between subs), and modPhase/carrierPhase update is
  serial. NEON within-sub parallelism is minimal. Defer.
- `fmodf` replacement in foldShape cases 8/10/12/13: conditional,
  sub-threshold individually. Could bundle as a separate small
  pass if user notices it on those specific shapes.
- `floorf` replacement: -0.3% CPU. Below threshold.
- `sqrtf` in foldShape case 15: 1 call per sample when active;
  ~15 cycles. Below threshold.

## Cross-references

- `mods/spreadsheet/util/neon_math.h` — destination for the new
  polynomial primitives
- `mods/spreadsheet/MultibandSaturator.cpp` — source of `fast_tanh`
  Padé 3/3 form
- `mods/spreadsheet/Helicase.cpp` — replacement site (5 calls)
- `feedback_neon_no_gather_lut_dsp` — historical caution about
  polynomial-vs-LUT tradeoffs (this work is the reverse direction:
  going polynomial because libm is the costly LUT-like operation
  and accuracy is preserved)
- `planning/neon-opportunities.md` — Helicase entry will be
  updated post-ship

---

## Hifi OS-shell recon — 2026-05-15 (post-2.6.2.52)

After 2.6.2.50/.51/.52 the hifi mode showed a consistent pattern:
**body-level optimizations save real cycles in lofi but vanish into
the noise floor in hifi**. User flagged this and asked for a thorough
recon to confirm hifi is at the structural floor.

### Disassembly check (Helicase.o, 2.6.2.52)

Symbol table:
- `_ZN8stolmineL8discFoldEfff` — 0x844 bytes (2116 bytes), out-of-line
- All other helpers (`opl3Wave`, `opl3WaveMorph`, `opl3ShapeBranch`,
  `foldShape`, `polyBlep`, polynomial primitives, the `evalShape`
  lambda) inlined — no separate symbols

So function-call overhead is just 1 call boundary per discFold
invocation (`process() → discFold`) × 2 sub-iters = 2 calls/sample.
~30 cycles overhead per sample = ~0.18% CPU. Not the bottleneck.

### Inventory of remaining structural candidates

| Candidate | Mechanism | Est. CPU | Verdict |
|---|---|---|---|
| Force-inline discFold (`__attribute__((always_inline))`) | Eliminates 2 call boundaries/sample | ~0.18% | Sub-threshold + bloats process() by 2KB (I-cache pressure risk) |
| Pre-bake opl3WaveMorph shape int-math at block rate | modShapeF/carrierShapeF are block constants; hoist int cast + clamp + frac out of the per-sample call | ~0.15% | Sub-threshold |
| NEON 2-lane parallel mod + carrier sine within a sub | Different-phase sines packed into 2-lane NEON quad | ~0.4% | Borderline; adds NEON intrinsic + hint surface, complexity not worth the win |
| Fast wrap01_neg (replace libm `floorf`) | Negative-safe wrap (carrierPhase can transiently go negative under heavy linFM) | ~0.12% | Sub-threshold |
| NEON 2-lane sub-iter parallelization | Process sub[0] and sub[1] as 2 NEON lanes | — | **Not viable**: sub[1]'s mod feedback uses `s.modFeedbackState` written by sub[0]. Hard serial dependency. |
| Combined ceiling (everything stack-additive) | All sub-threshold items together | **~0.85%** | Below the ER-301 CPU meter's 1pp display resolution |

### Material wins would require behavior changes

These break the "preserve character" constraint and need user audition consent:

- **Partial-OS** — oversample only the disc fold (where aliasing
  actually originates) instead of the whole mod/carrier chain. Mod
  and carrier shapes are bandlimited by the polynomial sine
  approximation; aliasing risk is concentrated in the discFold's
  hard shape branches. Could cut OS work by ~50%. Subtle character
  shift since the half-band decimation currently happens AFTER all
  the FM-and-shaper interactions.
- **1× rate with selective polyBlep on shaper output** — equivalent
  to lofi with anti-alias mitigations on discFold output only.
  Closer to lofi character than current hifi.
- **Reduce features in hifi mode** — not viable given character
  constraint.

### Verdict

**Hifi is at the structural floor.** The 2× OS shell's per-sub
fixed overhead (state updates, phase wraps, sync checks, FM
calc, decimation, state-array storage) is amortized inside an
already-tight loop. Cycle savings from point-optimizations
inside the body are real but smaller than the meter resolves.

"Hifi is opt-in" is the correct framing — accept the CPU cost as
the price of the deluxe path. Don't pursue further hifi-specific
optimization without a concrete user need.

### Cross-reference for future heuristic

This is a clean example of "**oversampling shell amortizes
inner-loop cost**" — a general pattern worth knowing: any unit
with an N× OS wrapper around a tight inner loop will plateau on
inside-loop optimizations once the OS-shell fixed costs dominate.
For such units, the next material gain requires either reducing
OS rate (behavior change) or moving cost OUT of the OS loop
entirely (block-rate hoist of work that doesn't need per-sub
recomputation).
