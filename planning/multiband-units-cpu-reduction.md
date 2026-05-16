# Parfait + Impasto CPU reduction

Optimization plan for the two multiband units:
- **Parfait** = `MultibandCompressor.cpp` (3-band compressor)
- **Impasto** = `MultibandSaturator.cpp` (3-band saturator + filter morph)

Both are heavy spreadsheet residents — flagged by user as
CPU-noticeable. Shared infrastructure (LR4 crossovers, tilt EQ, FFT
viz) creates shared optimization opportunities.

## Per-sample anatomy

Both units share a common shell: tilt EQ → LR4 crossover → per-band
processing → sum → viz/output. Differences are entirely in the
per-band stage.

### Parfait (MultibandSaturator) — `mods/spreadsheet/MultibandSaturator.cpp:411`

| Section | FLOPs/sample (typ.) | Notes |
|---|---|---|
| Tilt EQ | ~3 | Single-pole, unconditional |
| Crossover 0 (4-stage LR4) | ~8 | Serial within sample |
| Crossover 1 (4-stage LR4) | ~8 | Serial within sample |
| Per-band shaper (×3 bands, 8 types) | 5–30 each | Switch dispatch; varies per band |
| Per-band AA one-pole (×3) | ~9 | Identical math; **NEON candidate** |
| Per-band SVF (×3, conditional) | ~25 each when active | Identical math; **NEON candidate** |
| Sum + level + DC block | ~10 | |
| Safety limiter (branch) | ~2 | |
| Compressor (conditional) | ~15 | log2+exp2 |
| Output saturation (conditional) | ~10 | tanh |
| Mix | ~3 | |
| **Total (typical)** | **~100–150** | |

### Impasto (MultibandCompressor) — `mods/spreadsheet/MultibandCompressor.cpp:273`

| Section | FLOPs/sample | Notes |
|---|---|---|
| Tilt EQ (conditional) | ~3 | |
| Audio crossover 0 (4-stage) | ~8 | Serial within sample |
| Audio crossover 1 (4-stage) | ~8 | Serial within sample |
| **SC crossover 0 (4-stage)** | **~8** | **Runs even when SC disabled — wasted cycles** |
| **SC crossover 1 (4-stage)** | **~8** | **Runs even when SC disabled — wasted cycles** |
| Per-band detector + log10 + GR + makeup + level (×3) | ~50 | **Identical math across bands — clean NEON fit** |
| Output + mix | ~5 | |
| **Total** | **~90** | |

## Identified opportunities

### O1 — Impasto: skip SC crossover when sidechain disabled ⭐ EASY WIN

`MultibandCompressor.cpp:402-414` runs the sidechain crossover
unconditionally. When `scEnabled == false`, `scSig = x` and the SC
crossover is computing the *exact same thing* as the audio crossover
— pure waste.

Fix: guard `scXoverState[…]` updates with `if (scEnabled)`. When SC
is off (the common default state), copy `aBand{0,1,2}` into
`scBand{0,1,2}`.

**Savings: ~16 FLOPs/sample × 128 = ~2K FLOPs/block when SC disabled.**
~20% CPU reduction on Impasto in the typical-use config.

Effort: 5 minutes. Pure code-deletion class of win.

### O2 — Impasto: 3-band SoA NEON for per-band compression ⭐ BIG WIN

`MultibandCompressor.cpp:421-445` runs three iterations of identical
math (abs, branch-select coeff, IIR update, log10, GR, exp2, multiply).
Pad to 4 bands, lay out all per-band state SoA, vectorize:

```cpp
// Pad arrays to 4 lanes — class member; last lane unused
float detector_[4];        // currently comp[3].detector
float riseCoeff_[4];       // currently riseCoeff[3]
float fallCoeff_[4];       // currently fallCoeff[3]
float thresholdDb_[4];     // currently thresholdDb[3]
float ratioI_[4];          // currently ratioI[3]
float makeupCombined_[4];  // pre-mul makeupGain * bandLevel at block-rate

// Per-sample inner loop:
float32x4_t scq = vld1q_f32(scBands_padded);          // [b0, b1, b2, 0]
float32x4_t absq = vabsq_f32(scq);
float32x4_t detq = vld1q_f32(detector_);
uint32x4_t rising = vcgtq_f32(absq, detq);
float32x4_t riseQ = vld1q_f32(riseCoeff_);
float32x4_t fallQ = vld1q_f32(fallCoeff_);
float32x4_t coeff = vbslq_f32(rising, riseQ, fallQ);   // branchless
float32x4_t omc = vsubq_f32(vdupq_n_f32(1.0f), coeff);
detq = vmlaq_f32(vmulq_f32(coeff, detq), omc, absq);
vst1q_f32(detector_, detq);

// dB compute (4-lane log2):
float32x4_t levelDbQ = log2_poly_4lane(vaddq_f32(detq, vdupq_n_f32(1e-10f)));
levelDbQ = vmulq_f32(levelDbQ, vdupq_n_f32(20.0f * 0.30103f));
float32x4_t threshQ = vld1q_f32(thresholdDb_);
float32x4_t overQ = vsubq_f32(levelDbQ, threshQ);
overQ = vmaxq_f32(overQ, vdupq_n_f32(0.0f));            // clamp ≥0
float32x4_t ratioIQ = vld1q_f32(ratioI_);
float32x4_t reductionQ = vmulq_f32(overQ, vsubq_f32(vdupq_n_f32(1.0f), ratioIQ));
// 4-lane exp2:
float32x4_t grQ = exp2_poly_4lane(vmulq_f32(reductionQ, vdupq_n_f32(-0.16609f)));

// Apply to audio bands:
float32x4_t aQ = vld1q_f32(aBands_padded);
float32x4_t makeupQ = vld1q_f32(makeupCombined_);
float32x4_t compQ = vmulq_f32(vmulq_f32(aQ, grQ), makeupQ);
// Horizontal sum of lanes 0..2 (lane 3 is zero):
float sum = vgetq_lane_f32(compQ, 0) + vgetq_lane_f32(compQ, 1) + vgetq_lane_f32(compQ, 2);

// Metering accumulators stay scalar (block-rate output only).
```

This pattern is directly analogous to Rings modal kernel
(`mods/mi/rings/dsp/resonator.cc`) and Filterbank
(`mods/spreadsheet/Filterbank.cpp`) — SoA + class-member arrays +
branchless mode/select.

**Savings: per-band loop ~50 FLOPs → ~18 FLOPs. ~35% Impasto
per-sample reduction.**

### O3 — Impasto: 2-lane audio + SC crossover SIMD (when SC enabled)

Audio xover and SC xover share coefficients (`xCoeff[0]`, `xCoeff[1]`),
differ only in state arrays. Pack `[audio_state, sc_state]` into the
low 2 lanes of a NEON quad:

```cpp
// Stage 0 of crossover 0, audio + SC parallel:
float32x4_t state01q = vld1q_f32(xover01_state_combined); // [aud_s0, sc_s0, _, _]
float32x4_t inputq = (float32x4_t){x, scSig, 0, 0};
float32x4_t coeffq = vdupq_n_f32(xCoeff[0]);
float32x4_t newq = vmlaq_f32(state01q, vsubq_f32(inputq, state01q), coeffq);
// ... 4 stages serial, each as 2-lane vector
```

Each cascade stage is 2-lane SIMD. ~50% reduction on the 32 FLOP
crossover total → save ~16 FLOPs/sample when SC enabled.

Lower priority than O1 (which makes this irrelevant when SC is off).
Skip if O1 covers the common case.

### O4 — Parfait: 3-band SoA NEON for AA filter + SVF ⭐ MED WIN

`MultibandSaturator.cpp:536-570`. AA filter and SVF math are
identical structure per band. Same pattern as O2:

```cpp
// Pad to 4: aaState_[4], svfG_[4], svfR_[4], svfH_[4], svfS1_[4], svfS2_[4]
// Inner loop:
float32x4_t bandSigQ = vld1q_f32(bandSig_padded);
float32x4_t aaStateQ = vld1q_f32(aaState_);
aaStateQ = vmlaq_f32(aaStateQ, vsubq_f32(bandSigQ, aaStateQ), vdupq_n_f32(aaCoeff));
vst1q_f32(aaState_, aaStateQ);
bandSigQ = aaStateQ;

// SVF TPT bandpass — branchless across bands (per-band morph bake
// at block rate sets svfActive_[b] = 0 or 1, multiply into output):
float32x4_t gQ = vld1q_f32(svfG_);
float32x4_t rQ = vld1q_f32(svfR_);
float32x4_t hQ = vld1q_f32(svfH_);
float32x4_t s1Q = vld1q_f32(svfS1_);
float32x4_t s2Q = vld1q_f32(svfS2_);
float32x4_t hpQ = vmulq_f32(
    vsubq_f32(vsubq_f32(vsubq_f32(bandSigQ, vmulq_f32(rQ, s1Q)), vmulq_f32(gQ, s1Q)), s2Q),
    hQ);
float32x4_t bpQ = vmlaq_f32(s1Q, gQ, hpQ);
float32x4_t newS1Q = vmlaq_f32(bpQ, gQ, hpQ);
float32x4_t lpQ = vmlaq_f32(s2Q, gQ, bpQ);
float32x4_t newS2Q = vmlaq_f32(lpQ, gQ, bpQ);
vst1q_f32(svfS1_, newS1Q);
vst1q_f32(svfS2_, newS2Q);

// Branchless mode-mix (block-rate baked gains):
float32x4_t lpGQ = vld1q_f32(lpGain_padded);   // 0 when band's morph is off
float32x4_t bpGQ = vld1q_f32(bpGain_padded);
float32x4_t hpGQ = vld1q_f32(hpGain_padded);
float32x4_t svfOutQ = vmlaq_f32(vmlaq_f32(vmulq_f32(lpQ, lpGQ), bpQ, bpGQ), hpQ, hpGQ);

// When morph is off: lp_g = bp_g = hp_g = 0 → svfOut = 0, AND we
// take bandSig (= aaState) as the output. Use a per-band "useSVF"
// mask baked at block rate, vbsl-select between aaState and aaState+svfOut.
float32x4_t useMaskQ = vld1q_f32(useSvfMask_); // 1.0 or 0.0 per band
float32x4_t finalQ = vmlaq_f32(bandSigQ, svfOutQ, useMaskQ);
vst1q_f32(bandSig_padded, finalQ);
```

**Savings: ~30 FLOPs/sample on the AA+SVF section → ~20% Parfait CPU
reduction.** Independent of waveshaper diversity.

### O5 — Parfait: per-band shaper SIMD when bands share type

When all 3 bands use the same waveshaper type, the dispatch becomes a
single SIMD'd shaper call. When they differ, fall back to scalar per
band.

Block-rate decision: `bool allSameType = (bandType[0]==bandType[1] &&
bandType[1]==bandType[2])`. If true, use SIMD path; else scalar.

The 8 shapers vary in complexity (tube uses `fast_tanh`, fold uses
iterative folding, sine fold uses `fast_sinf`, fractal uses iterated
polynomial). Each can be NEON'd individually:

- Tube, half-rect, diode — small polynomial / branch, easy NEON
- Tri Fold — iterative wavefolder, NEON-able with vbsl masking
- Sine Fold — `fast_sinf`, needs poly_sine_4lane (foundation phase)
- Fractal — iterated polynomial, straight NEON
- Crush — μ-law + quantize, has `floorf` (NEON-able)

**Conditional savings: ~30–60 % shaper cost when bands share type.**
The "fixed same shaper across bands" use case is common (e.g., "tube
all bands"); the audition gate is whether the bands-differ scalar
fallback is acceptable.

Skip this in Phase 1; do it as a Phase 2 after O4 ships.

### O6 — Both units: block-rate hoist of per-sample branches

Per-sample conditionals on block-rate-constant values:

**Parfait**:
- `if (bandMute[b]) continue;` — per band, per sample → bake `bandActive_[b] = 0/1` and multiply at block rate
- `if (bandAmount[b] > 0.001f)` — per band, per sample → block-rate skip flag
- `if (morph > 0.01f)` — per band, per sample → block-rate `useSvfMask_[b]`
- `if (compActive)` — block-rate flag, already hoisted partially; full skip on disabled
- `if (tanhAmt > 0.001f)` — block-rate skip flag
- `if (wet > 1.5f || wet < -1.5f)` — per sample but rarely true; keep as-is (branch prediction wins)

**Impasto**:
- `if (toneAmt > 0.001f)` — block-rate skip
- `if (overDb < 0.0f) overDb = 0.0f;` per band — replace with `vmaxq_f32` in O2

Small individual savings (~2–5 FLOPs/sample each) but they aggregate
and simplify the inner loop for the compiler.

### O7 — Both units: NEON 4-lane fast_log2 / fast_exp2

Current scalar versions use IEEE 754 bit-twiddling:
- Impasto's `fast_log2` is **bit-extract only, no mantissa correction
  — max error ~5 %!**
- Parfait's `fast_log2` adds a quadratic mantissa correction, ~1 % error.
- Both `fast_exp2` are crude quadratic.

A 4-lane polynomial version (5th-order Remez or minimax on mantissa)
gives **both** ~–60 dB error AND throughput parallelism. Use case in
O2 (Impasto per-band dB compute) packs all 3 bands in one call.

Stub for `mods/spreadsheet/util/neon_math.h` (NEW):

```cpp
inline __attribute__((always_inline))
float32x4_t log2_poly_4lane(float32x4_t x);  // 5th-order Remez on mantissa
inline __attribute__((always_inline))
float32x4_t exp2_poly_4lane(float32x4_t x);  // 5th-order Remez on fractional
```

Same library as the planned Plaits foundation
(`planning/plaits-cpu-reduction.md` Phase 0) — share the math
primitives across spreadsheet + plaits packages.

### O8 — Parfait: block-rate pre-bake of SVF morph crossfade gains

`MultibandSaturator.cpp:552-568`: the lp_g/bp_g/hp_g computation has
3 if/else branches per band per sample. The morph value changes at
block rate. **Hoist to block-rate**: pre-compute lp_g/bp_g/hp_g once
per band per block, store in class-member arrays. The per-sample
inner loop becomes a 3-multiply blend (already covered in O4's
branchless SVF design).

**Savings: ~6 FLOPs + 2 branches per band per sample = ~24 FLOPs/sample
across 3 bands.** Subset of O4.

### O9 — Impasto: pre-multiply `makeupGain × bandLevel` at block rate

`MultibandCompressor.cpp:437-438` reads `bandLevel` from a Bias ref
inside the sample loop. Both `bandLevel` and `makeupGain` are
block-rate constants. Pre-compute `makeupCombined_[b] = makeupGain[b] *
bandLevel[b]` once per block, eliminate the per-sample read + multiply.

**Savings: ~6 FLOPs/sample. Aligns with O2 SoA layout.**

### O10 — FFT viz: already PFFFT, likely NEON-accelerated

PFFFT (Pretty Fast FFT, `pffft.c` in the build) auto-enables NEON on
ARM. Both units already invoke it at block-rate-12. **No action**
unless instrumentation reveals it's NOT taking the NEON path on
am335x — verify with `arm-none-eabi-objdump -d ... | grep vld1` on
`pffft.o` in a follow-up.

### O11 — Crossover cascade: NOT trivially NEON-able within a sample

The 4-stage one-pole cascade `s[k] += (s[k-1] - s[k]) * c` is
fundamentally serial within a sample — each stage feeds the next.
Closed-form matrix-form parallelization exists (4×4 lower-triangular
state-transition matrix per sample) but takes ~16 FLOPs vs the
direct 8 FLOPs.

**Reject** as a vectorization target. The cross-stream parallelism
(O3 for Impasto) is the actual NEON win for crossovers.

### O12 — Both units: avoid `powf` / `sinf` / `cosf` / `expf` / `log10f` in `process()`

Hoist setup math out of `process()` where possible. Currently:

**Parfait**:
- `powf(10.0f, toneAmt * 0.3f)` — block-rate, already hoisted out of sample loop ✓
- `sinf` / `cosf` for SVF coefficient — block-rate, hoisted ✓ (uses sinf/cosf)
- `expf(-1.0f / (0.001f * sr))` for compressor — block-rate, hoisted ✓
- `log10f(compThreshold²)` — block-rate, hoisted ✓
- `cosf(2π k/255)` for hann window — done once on init ✓
- `sqrtf` for energy / FFT magnitude — per-block / per-bin, modest

**Impasto**:
- `powf(10.0f, …)` — block-rate, hoisted ✓
- `expf(-sp/attack[b])` per band — block-rate, hoisted ✓
- `fast_log10` and `fast_fromDb` — **per-sample, per band**: O2 / O7 covers

Most setup is already block-rate. Audit confirms no obvious per-sample
transcendentals.

## Ranked plan

| Phase | Change | Unit | ROI | Effort | Risk | Status |
|---|---|---|---|---|---|---|
| 1a | O1: skip SC xover when disabled | Impasto | ⭐⭐⭐ (~20 % CPU when SC off) | Trivial | None | **SHIPPED 2.6.2.43** |
| 1b | O9: pre-mul makeup×bandLevel | Impasto | ⭐ (~6 FLOPs/sample) | Trivial | None | **SHIPPED 2.6.2.43** |
| 1c | Foundation: `util/neon_math.h` | Both | (enables 2,4) | Small | Low — math primitives | **SHIPPED 2.6.2.43** |
| 2 | O2: Impasto per-band SoA NEON | Impasto | ⭐⭐⭐ (~35 % CPU) | Medium | Low — direct Rings/Filterbank pattern | **SHIPPED 2.6.2.43** |
| 3 | O4 + O8: Parfait AA+SVF NEON + morph bake | Parfait | ⭐⭐ (~20 % CPU) | Medium | Low — same pattern as O2 | **SHIPPED 2.6.2.43** |
| 4 | O6: block-rate hoists (both units) | Both | ⭐ (5–10 % CPU each) | Small | None | pending |
| 5 | O3: Impasto audio+SC 2-lane (when SC on) | Impasto | ⭐ (~15 % CPU when SC on) | Medium | Low | pending |
| 6 | O5: Parfait shaper SIMD (same-type case) | Parfait | ⭐ (situational, ~10–20 % when shapers match) | High | Medium — 8 shapers to NEON | pending |
| ✗ | O11: crossover matrix-form | — | NEGATIVE | — | Reject | — |
| ✗ | O10: FFT NEON | — | already done | — | Verify only | — |

## 2.6.2.43 ship notes

Phases 1, 2, 3 shipped together. Hint audit clean
(`tools/check-neon-hints.sh` reports 0 SUSPECT hints on both
`MultibandCompressor.o` and `MultibandSaturator.o`).

Implementation notes worth carrying forward:
- Per-sample NEON scratch arrays (`scBandsScratch_`, `aBandsScratch_`,
  `compScratch_`, `grScratch_`, `bandSigScratch`) live in `Internal`
  (heap), NOT on the stack — a first build with stack-locals emitted
  `:64` hints on `[r4 :64]` / `[lr :64]` / `[r3 :64]` even with
  `__attribute__((aligned(16)))`. Class-member is the proven-safe
  pattern per `feedback_neon_intrinsics_drumvoice`.
- Block-rate coefficient SoA arrays (`riseCoeff_`, `fallCoeff_`,
  `thresholdDb_`, `ratioI_`, `makeupCombined_`, `svfG/R/H`,
  `lpGain/bpGain/hpGain`, `useSvfMask`, `bandCombinedGain`) all live
  in `Internal` for the same reason and emit `:128` hints (16-byte
  aligned, truly safe).
- File-level `#pragma GCC optimize("no-tree-vectorize")` on both
  `.cpp` files prevents the auto-vec init trap per
  `feedback_neon_hint_surfaces`.
- Parfait AA/SVF mode-mix is now always-on with branchless
  `useSvfMask` instead of `if (morph > 0.01)` per-band-per-sample
  branch. Behaviorally better: SVF state stays "warm" so transitions
  from morph=0 to morph=0.5 don't have a state-settling transient.
- `comp[b].gainReduction` was a dead store in Impasto's original (no read
  site) and is dropped from `Internal`.

## 2.6.2.44 ship notes — fast-path Phase 4

Added block-rate dispatch flags to skip work when per-band kernel
isn't doing anything meaningful:

- **Phase 4a (Impasto)** — `anyCompActive` flag. If no band has
  `thresholdDb < -0.1 AND ratioI < 0.995`, per-sample loop skips the
  detector/log2/exp2 chain. Detector flushed on active→inactive edge
  via `mPrevCompActive` for clean re-engage.
- **Phase 4b (Parfait)** — `anySvfActive` flag. If all bands have
  morph=0, per-sample loop skips SVF kernel (AA still runs). SVF
  state flushed on inactive→active edge via `mPrevSvfActive`.

Both are config-conditional wins. Hardware results: Impasto ~1-2pp
drop with partial fast-path engagement; Parfait null when SVF morph
is active on any band (typical full-tilt config).

## 2.6.2.45 ship notes — FFT viz rate

Dropped FFT viz update from every-4 blocks (94 Hz) to every-6 blocks
(62 Hz). Stays above the 55 Hz viz framerate floor. Rollback is a
single-line `#define FFT_BLOCKS_PER_UPDATE` change at the top of
each `.cpp` file. Smoothing constants (`FFT_RMS_DECAY` /
`FFT_PEAK_DECAY` / `FFT_RMS_SMOOTH`) calibrated to preserve the
4-block time-constant: `α^(period/4)` for peak; `1-(1-α)^(period/4)`
for smoothing.

**Hardware result** (asymmetric):
- Parfait dropped ~3pp as expected
- Impasto went **up** ~3pp at idle — regression despite the work
  reduction. Likely cause: iCache/code-alignment shift from the
  recompile that asymmetrically affected the per-band kernel.
- Visuals at 62 Hz were perceptually fine.

## 2.6.2.46 ship notes — Impasto FFT rollback

Reverted Impasto's `FFT_BLOCKS_PER_UPDATE` to 4 (94 Hz) and
`FFT_RMS_DECAY` to 0.85f. Parfait stays at every-6. Asymmetric
tuning per the 2.6.2.45 hardware measurement.

---

Phase 1 is two trivial wins + foundation. Phase 2 is the main Impasto
win (~35 % CPU reduction). Phase 3 mirrors it for Parfait (~20 %).
Phase 4 mops up small wins. Phases 5–6 are conditional / situational.

## Implementation phases

### Phase 1 — Trivial + foundation

**1a — O1 SC crossover skip** (Impasto, `MultibandCompressor.cpp:402-414`):
Guard the SC crossover block with `if (scEnabled)`. When false, copy
audio bands to SC bands.

**1b — O9 pre-mul** (Impasto): pre-compute `makeupCombined_[b]` in
the block-rate setup loop; replace `gr * makeupGain[b] * bandLevel`
with `gr * makeupCombined_[b]`.

**1c — Foundation `mods/spreadsheet/util/neon_math.h`** (NEW): provides
`log2_poly_4lane`, `exp2_poly_4lane`, `wrap01_4`. Lifted from
visadhara's coefficients (5th-order Remez). `always_inline`. File-level
`#pragma GCC optimize("no-tree-vectorize")` not needed for a
header-only library, but consumers must use it.

Ship as **Parfait + Impasto minor version bump** (1a+1b are
behavioural-equivalent changes; 1c is a new dependency).

### Phase 2 — Impasto per-band NEON SoA

`MultibandCompressor.cpp:421-445` → SoA layout (class-member padded
arrays in `Internal`), NEON 4-lane (3 valid + 1 padded) inner loop
per O2 design above.

Block-rate setup loop pre-fills:
- `detector_[3] = 0` (during Init)
- `riseCoeff_[b]`, `fallCoeff_[b]`, `thresholdDb_[b]`, `ratioI_[b]`,
  `makeupCombined_[b]` for b=0..2; `[3]` padding = neutral values
  (riseCoeff = fallCoeff = 1.0, ratioI = 0.0 etc. — anything; lane 3
  is ignored in horizontal sum).

Inner loop: full NEON quad arithmetic per O2. Horizontal sum of
lanes 0–2 via `vgetq_lane_f32` × 3 (4 lanes is one less crossing than
`vpadd_f32` cascade for 3-of-4 SIMD).

File-level pragma in `MultibandCompressor.cpp`:
```cpp
#pragma GCC optimize("no-tree-vectorize")
```
per `feedback_neon_hint_surfaces` — prevents auto-vec init trap on
SoA zero-fills.

### Phase 3 — Parfait AA + SVF NEON

`MultibandSaturator.cpp:536-570` → same SoA pattern as Impasto Phase
2. AA + SVF state arrays padded to 4. Branchless mode-mix with
block-rate-baked `lpGain_padded`/`bpGain_padded`/`hpGain_padded` and
`useSvfMask_` per O4.

Block-rate setup pre-bakes:
- `lpGain_[b]`, `bpGain_[b]`, `hpGain_[b]` from `bandFilterMorph[b]`
  (the if-else chain in `:552-568` runs at block rate)
- `useSvfMask_[b] = (bandFilterMorph[b] > 0.01f) ? 1.0f : 0.0f`
- `bandActiveMask_[b] = bandMute[b] ? 0.0f : 1.0f` (for O6)

Waveshaper stays scalar in Phase 3 (Phase 6 problem).

### Phase 4 — Block-rate hoists

Both units. Per O6:
- Bake `bandActiveMask_[b]` per O3 ER. Replace `if (bandMute[b])
  continue` with a multiply.
- Bake `shaperActiveMask_[b] = (bandAmount[b] > 0.001f) ? 1.0f :
  0.0f` (Parfait) — but only if O5 hasn't shipped yet, otherwise this
  feeds O5's shaper SIMD.
- Block-rate `compEnable` mask for compressor section.
- Block-rate `tanhEnable` for output sat.

### Phase 5 — Impasto 2-lane audio+SC crossover (conditional, only when SC on)

`MultibandCompressor.cpp:387-414` → packed `[audio_state, sc_state, ?,
?]` per-stage 2-lane SIMD with shared `xCoeff[c]`. Only on the
SC-enabled path; the SC-disabled path uses Phase 1a (skip).

This is the **branchy** phase: two code paths (SC on vs SC off) with
different inner loops. Use block-rate dispatch via function pointer
or `if (scEnabled) { … } else { … }` at the outer level (not
per-sample).

### Phase 6 — Parfait shaper SIMD (conditional)

`applyShaper` switch dispatch → 8 individual NEON kernels (one per
shaper type) selected at block rate via `if (allSameType) { simd_path()
} else { scalar_path() }`.

Each shaper kernel implemented as `__attribute__((always_inline))`
`float32x4_t shape_tube_4(float32x4_t)` etc. Sine Fold consumes
`sine_poly_4lane` from `util/neon_math.h` (must exist by Phase 6).

**Audition required** per shaper: NEON output vs scalar reference, RMS
within 1e-4, hardware audition on representative bandings.

## Verification per phase

Standard gates per `feedback_linux_build_auto_install` +
`feedback_package_version_bump`:

1. `make spreadsheet ARCH=linux && cp testing/linux/spreadsheet.pkg
   ~/.od/rear/` — emu install
2. `make spreadsheet ARCH=am335x` — hardware build
3. `tools/check-neon-hints.sh
   testing/am335x/mods/spreadsheet/libspreadsheet.so` — zero new
   SUSPECT hints
4. Emu A/B against pre-phase build with swept noise, RMS within 1e-4
5. Hardware audition: representative patches per unit. For
   Impasto — solo + sidechain modes, all 3 bands compressing; for
   Parfait — each shaper type, morph filter on/off
6. CPU measurement on hardware (worst-case patch). Acceptance: ≥60 %
   of estimated win
7. PKGVERSION bump + commit

## Risks & mitigations

| Risk | Mitigation | Reference |
|---|---|---|
| SoA init code triggers auto-vec :64 hint | File-level `#pragma GCC optimize("no-tree-vectorize")` | `feedback_neon_hint_surfaces` |
| Padding lane carrying garbage into horizontal sum | Init lane[3] = 0 for state, neutral (1.0) for coeffs; verify via emu A/B | — |
| Block-rate-baked masks lag user input by 1 block | 128-sample block @ 48kHz = 2.7ms latency, well under perceptual threshold | — |
| Shaper SIMD changes per-band timbre on edge cases (Phase 6) | Per-shaper audition; allow user-fallback flag if needed | — |
| FFT viz timing changes if scalar→NEON path shifts block-rate cost | FFT already on a 4-block schedule; insensitive to single-block timing variation | — |
| Float-rounding differences in NEON vs scalar break test fixtures | Acceptance criterion: RMS A/B within 1e-4 (audio-perceptible threshold is ~1e-3) | — |
| Compressor coefficient pre-bake reorders parameter response | Audition-only check; pre-bake values are mathematically equivalent | — |

## Cross-references

- `mods/spreadsheet/Filterbank.cpp` — canonical SoA SVF NEON pattern;
  Phase 3's SVF lifts directly
- `mods/mi/rings/dsp/resonator.cc` — class-member NEON-safe scratch
  pattern; both phases use it
- `feedback_neon_soa_svf_bank` — SoA filter-bank template
- `feedback_neon_intrinsics_drumvoice` — class-member array rule
- `feedback_neon_hint_surfaces` — auto-vec init trap
- `feedback_runtime_branched_dsp_dispatch` — branchless dispatch rule
  (relevant to O4's mode-mix and Phase 6's shaper dispatch)
- `feedback_package_version_bump` — PKGVERSION bump per shipped phase
- `feedback_linux_build_auto_install` — `cp testing/linux/*.pkg ~/.od/rear/`
- `planning/multiband-saturation.md` — original Parfait design notes
- `planning/plaits-cpu-reduction.md` — shares the `neon_math.h`
  foundation (Phase 0) with this plan's Phase 1c
- `planning/neon-opportunities.md` — audit doc; add Parfait + Impasto
  rows after Phase 2/3 ship
