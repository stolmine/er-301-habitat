# Filterbank (Tomograph) NEON refactor

**Plan persisted from `~/.claude/plans/let-s-go-detune-post-cheeky-tome.md`
2026-05-14, per `feedback_persist_plans_to_repo`. Source of truth
for the implementation pass.**

## Context

Per `planning/neon-opportunities.md` the Filterbank/Tomograph unit
(`mods/spreadsheet/Filterbank.{cpp,h}`) is the #1 fresh NEON win
identified by the audit. The hot loop in `Filterbank::process()`
(lines 592–620) is per-sample over `bandCount` independent SVF
bands with per-band gain + per-band energy follower:

```cpp
for (i<FRAMELENGTH) {
  for (b<bandCount) {
    switch (filterType[b]) { LP / RESON / PEAK ... }
    bandOut = s.filters[b].Process<MODE>(x);
    wet += bandOut * gain[b];
    bandEnergy[b] += (bandOut*bandOut - bandEnergy[b]) * 0.001f;
  }
  // ...mix, tanh, output...
}
```

Bands are fully independent — classic SIMD-over-bands shape, the
same one Rings modal resonator uses
(`mods/mi/rings/dsp/resonator.cc`). Default `bandCount = 8`, max
`kMaxBands = 16`. Expected per-`process()` kernel speedup ~3–4× on
Cortex-A8 (Rings is the proof point); less overall if the post-band
`tanhf` path is engaged (libm call dominates when active — separate
concern, out of scope here).

Goal: convert `Internal` from AoS (`stmlib::Svf filters[]`) to SoA,
NEON the 4-band SVF kernel, fold mode dispatch into branchless
per-band output gains, verify with `tools/check-neon-hints.sh`,
ship as 2.6.2.42.

## Approach

### Storage: AoS → SoA in `Internal`

Drop `stmlib::Svf filters[kMaxBands]` from `Internal` entirely.
Replace with parallel float arrays — same pattern as
`mods/mi/rings/dsp/resonator.cc`:

```cpp
// SVF coefficients (block-rate writes from updateFilterCoefficients)
float svfG[kMaxBands];      // tan(π·f / sr)
float svfR[kMaxBands];      // 1/Q
float svfH[kMaxBands];      // 1 / (1 + r·g + g²)

// SVF state (per-sample evolved)
float svfState1[kMaxBands];
float svfState2[kMaxBands];

// Branchless mode + gain bake:
//   FTYPE_LP    → bpGain = 0,        lpGain = gain[b]
//   FTYPE_PEAK  → bpGain = gain[b],  lpGain = 0
//   FTYPE_RESON → bpGain = gain[b],  lpGain = 0
// PEAK and RESON are byte-identical per-sample — both emit BP, only
// Q-floor differs at coefficient setup (verified in
// updateFilterCoefficients lines 429–478).
float bpGain[kMaxBands];
float lpGain[kMaxBands];
// lpMask kept for the energy follower (needs raw bandOut per band).
float lpMask[kMaxBands];    // 1.0 if FTYPE_LP else 0.0
```

Keep existing `gain[16]`, `filterType[16]`, `bandEnergy[16]`,
`freqHz[16]`, `targetFreq[16]`, `currentFreq[16]` (used by UI, edit
buffer, scale-distribution code).

Dead-field cleanup deferred to a separate commit (out of scope):
`bandQValues[16]` and `mLastVOctOffset` appear unused.

### Coefficient setup: inline g/r/h math, drop stmlib SVF dep

Rewrite `updateFilterCoefficients()` to write directly to
`svfG/svfR/svfH` instead of calling `stmlib::Svf::set_f_q`:

```cpp
const float g = stmlib::OnePole::tan<stmlib::FREQUENCY_FAST>(normalizedFreq);
const float r = 1.0f / bandQ;
svfG[b] = g;
svfR[b] = r;
svfH[b] = 1.0f / (1.0f + r*g + g*g);
const bool isLP = (filterType[b] == FTYPE_LP);
lpMask[b] = isLP ? 1.0f : 0.0f;
bpGain[b] = isLP ? 0.0f : gain[b];
lpGain[b] = isLP ? gain[b] : 0.0f;
```

Drop `#define TEST` hack at Filterbank.cpp:11 once `stmlib::Svf` is
gone. Reduce `#include "stmlib/dsp/filter.h"` to whatever minimum is
needed for `OnePole::tan`, or inline its polynomial directly.

### Per-sample NEON kernel (inlined into `process()`)

Mirror `mods/mi/rings/dsp/resonator.cc:122–138` (canonical TPT SVF
NEON transcription on Cortex-A8). Pad `bandCount` up to next
multiple of 4 at block-rate via the `bpGain[b] = lpGain[b] = 0`
zero-out for padding bands (Rings-style padding, no scalar tail
divergence).

```cpp
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
const float32x4_t inV     = vdupq_n_f32(x);
const float32x4_t energyA = vdupq_n_f32(0.001f);
float32x4_t wetV          = vdupq_n_f32(0.0f);

const int bandsPadded = (bandCount + 3) & ~3;
for (int b = 0; b < bandsPadded; b += 4) {
  float32x4_t s1 = vld1q_f32(&svfState1[b]);
  float32x4_t s2 = vld1q_f32(&svfState2[b]);
  float32x4_t g  = vld1q_f32(&svfG[b]);
  float32x4_t r  = vld1q_f32(&svfR[b]);
  float32x4_t h  = vld1q_f32(&svfH[b]);

  // TPT SVF — transcribed from stmlib::Svf::Process (filter.h:232–236)
  // hp = (in - r·s1 - g·s1 - s2) · h
  float32x4_t rs1   = vmulq_f32(r, s1);
  float32x4_t gs1   = vmulq_f32(g, s1);
  float32x4_t inner = vsubq_f32(vsubq_f32(vsubq_f32(inV, rs1), gs1), s2);
  float32x4_t hp    = vmulq_f32(inner, h);
  // bp = s1 + g·hp;  s1' = bp + g·hp
  float32x4_t bp    = vmlaq_f32(s1, g, hp);
  float32x4_t s1New = vmlaq_f32(bp, g, hp);
  // lp = s2 + g·bp;  s2' = lp + g·bp
  float32x4_t lp    = vmlaq_f32(s2, g, bp);
  float32x4_t s2New = vmlaq_f32(lp, g, bp);
  vst1q_f32(&svfState1[b], s1New);
  vst1q_f32(&svfState2[b], s2New);

  // Output: wet += bp·bpGain + lp·lpGain (mode dispatch baked in)
  float32x4_t bpG = vld1q_f32(&bpGain[b]);
  float32x4_t lpG = vld1q_f32(&lpGain[b]);
  wetV = vmlaq_f32(wetV, bp, bpG);
  wetV = vmlaq_f32(wetV, lp, lpG);

  // Energy follower: bandOut = bp + lpMask·(lp - bp); e = bandOut²
  float32x4_t lpMaskV = vld1q_f32(&lpMask[b]);
  float32x4_t bandOut = vmlaq_f32(bp, lpMaskV, vsubq_f32(lp, bp));
  float32x4_t e       = vmulq_f32(bandOut, bandOut);
  float32x4_t en      = vld1q_f32(&bandEnergy[b]);
  en                  = vmlaq_f32(en, vsubq_f32(e, en), energyA);
  vst1q_f32(&bandEnergy[b], en);
}

// Horizontal sum to scalar wet
float32x2_t pair = vadd_f32(vget_low_f32(wetV), vget_high_f32(wetV));
float wet = vget_lane_f32(vpadd_f32(pair, pair), 0);
#else
// Scalar fallback (linux x86, macOS): same SoA math, no intrinsics.
float wet = 0.0f;
const int bandsPadded = (bandCount + 3) & ~3;
for (int b = 0; b < bandsPadded; b++) {
  const float rs1 = svfR[b] * svfState1[b];
  const float gs1 = svfG[b] * svfState1[b];
  const float hp  = (x - rs1 - gs1 - svfState2[b]) * svfH[b];
  const float bp  = svfState1[b] + svfG[b] * hp;
  svfState1[b]    = bp + svfG[b] * hp;
  const float lp  = svfState2[b] + svfG[b] * bp;
  svfState2[b]    = lp + svfG[b] * bp;
  wet += bp * bpGain[b] + lp * lpGain[b];
  const float bandOut = bp + lpMask[b] * (lp - bp);
  const float e = bandOut * bandOut;
  bandEnergy[b] += (e - bandEnergy[b]) * 0.001f;
}
#endif

wet *= sumNorm;
float mixed = x * (1.0f - mix) + wet * mix;
// ...tanh, output gain (unchanged)...
```

`always_inline` (`feedback_neon_voice_bus_template` Layer 5) is met
automatically because the kernel lives inside `process()`, not in a
separate function. Following Rings exactly.

### Branchless dispatch (per `feedback_runtime_branched_dsp_dispatch`)

The per-sample `switch (filterType[b])` is gone entirely. Mode
dispatch is encoded once per block in `bpGain/lpGain/lpMask` and
applied via NEON mul/mla — no runtime-branched DSP dispatch on the
per-sample path.

### Auto-vec init trap (per `feedback_neon_hint_surfaces`)

The new SoA arrays auto-vectorize at `-O3 -ffast-math` during their
zero-fill in `Internal::Init()`, emitting `[reg :64]` quad-store
hints that trap on Cortex-A8.

Same risk in `distributeFrequencies()` (lines 255+) — scalar loops
over `kMaxBands` and the 255-element candidate pool that auto-vec
would happily take. Runs at parameter-change rate, but a single
trap there still hangs hardware on first parameter movement.

**Preferred fix:** file-level `#pragma GCC optimize("no-tree-vectorize")`
at the top of Filterbank.cpp. Intrinsics aren't gated by
tree-vectorize, so the hot kernel still emits the NEON we wrote by
hand. Less attribute noise than per-function decoration.

Alternative if file-level pragma turns out to interact badly: per-
function `__attribute__((noinline, optimize("no-tree-vectorize")))`
on `Internal::Init()` and `distributeFrequencies()`.

## File map

- **`mods/spreadsheet/Filterbank.h`** — no public API change. Dead
  field cleanup (`bandQValues`, `mLastVOctOffset`) deferred.
- **`mods/spreadsheet/Filterbank.cpp`** — all changes:
  - Drop `#define TEST` hack at line 11.
  - Reduce `#include "stmlib/dsp/filter.h"` to minimum (just
    `OnePole::tan`).
  - `Internal` struct (lines 56–106): drop `Svf filters[kMaxBands]`,
    add 8 new SoA arrays.
  - File-level `#pragma GCC optimize("no-tree-vectorize")`.
  - `updateFilterCoefficients()` (lines 429–479): rewrite to populate
    SoA + per-mode gain bake.
  - `process()` band loop (lines 592–620): replace with NEON+fallback
    kernel.
- **`mods/spreadsheet/mod.mk`** — PKGVERSION 2.6.2.41 → 2.6.2.42.

No SWIG impact (header layout unchanged). The spreadsheet Makefile
auto-tracks header deps anyway since 2.6.2.29.

## Reference files

- **`mods/mi/rings/dsp/resonator.cc`** — canonical NEON SVF bank.
  Lines 122–138 (NEON kernel) and the SoA member declarations in
  `resonator.h`. Mirror exactly.
- **`eurorack/stmlib/dsp/filter.h:177–247`** — `stmlib::Svf` source
  of truth for TPT equations and `set_f_q` coefficient math.
- **`mods/spreadsheet/Network.h`** — in-package precedent for 4-lane
  NEON over independent state (multi-tap).
- **`mods/spreadsheet/visadhara/morph.h`** — local idiom for
  `__attribute__((always_inline))` + `#if defined(__ARM_NEON)`.

## Risks & mitigations

| Risk | Mitigation | Reference |
|---|---|---|
| Stack-local NEON arrays | All SoA arrays are `Internal` members (heap) | `feedback_neon_intrinsics_drumvoice` |
| Auto-vec init trap | File-level `no-tree-vectorize` pragma | `feedback_neon_hint_surfaces` |
| Runtime-branched dispatch | Eliminated — baked into per-mode gains at block-rate | `feedback_runtime_branched_dsp_dispatch` |
| Register-pressure `[sp :64]` spills | Kernel inlined into `process()`; no live quads across calls | `feedback_neon_hint_surfaces` |
| SVF math divergence | NEON kernel transcribes stmlib `Process()` line-by-line; emu A/B with swept impulse + noise should match scalar within float-rounding | — |
| Padding-band overhead | Bounded: 0–3 wasted slots out of `(bandCount+3)&~3`; default bandCount=8 has zero padding | — |
| High-Q stability (RESON Q floor=20) | TPT SVF unconditionally stable; Rings runs Q≤500 fine | stmlib filter.h |
| Linux/x86 fallback parity | Scalar fallback uses identical TPT equations | — |
| SWIG wrapper staleness | No header layout change; auto-tracked anyway | `feedback_swig_header_dep` |

## Out of scope (flagged follow-ups)

- **`tanhf` libm call at line 625** — dominates per-sample CPU when
  `tanhAmt > 0.001`. After this refactor it'll be the largest
  remaining slice. Candidate for `stmlib::SoftLimit`-style
  polynomial in a separate change.
- **Dead-field cleanup**: `bandQValues[16]` (write-only, never
  read), `mLastVOctOffset` (never written or read). Verify with
  grep across the codebase and drop in a separate commit.
- **Hardcoded `48000.0f` in `Init`** (line ~94). Wrong if
  sr ≠ 48 kHz at construction. Separate cleanup.

## Verification

1. **Build both arches.**
   ```bash
   make spreadsheet ARCH=linux
   make spreadsheet ARCH=am335x
   ```
2. **NEON hint audit (mandatory).**
   ```bash
   tools/check-neon-hints.sh testing/am335x/mods/spreadsheet/Filterbank.o
   ```
   Acceptance: zero new `[sp :64]` quad-D spills, zero new non-sp
   `:64` hints. Pre-existing `spreadsheet_swig.o` count (3 sp
   quad-D PMM false positives) unchanged.
3. **Install to emulator** via `./install-packages.sh`.
4. **Emu A/B against scalar reference** — `git stash` the refactor,
   build, capture WAVs through Tomograph on swept impulse + noise
   at bandCount=8 / 2 / 13 / 16 across the three modes. Pop stash,
   rebuild, capture again. RMS difference should be at float-
   rounding level (the NEON kernel uses plain `vmulq + vsubq +
   vmlaq` — no `vrecpeq`-class approximation here).
5. **Hardware install + audition.** Tomograph at default
   bandCount=8, sweep through bandCount range, verify each filter
   mode (peak/LP/reson) on known patches. Watch for denormal
   silence tails, click on type-change, meter behavior.
6. **CPU spot check (informal).** Multiple Tomographs at
   bandCount=16; observe encoder responsiveness / glitching.
7. **Commit + version bump:** 2.6.2.41 → 2.6.2.42.
8. **Update `planning/neon-opportunities.md`:** mark Filterbank
   entry DONE; promote item #2 (Rings non-modal modes) to top of
   Recommended Next Moves.
