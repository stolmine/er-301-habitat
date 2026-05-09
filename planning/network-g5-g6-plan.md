# Network glitch — G5 (scrub) + G6 (reverse) implementation plan

## Context

The Character macro for the Network unit (catchall, 0.3.45) has its
full lush + glitch primitive set shipped except G5/G6, which were
deliberately deferred at plan time because they "need careful design
for dual-read integration." The dual-read playback pipeline (Pass A
integer +1/sample, Pass B scalar gather, Pass C NEON crossfade +
accumulate) was built around forward-only integer reads with a
one-block crossfade between consecutive geometry-derived positions.

This plan adds G5 + G6 as two new mutex glitch modes that integrate
cleanly with the dual-read pipeline by:

- **G6 reverse** — modify Pass A's per-tap advance from constant +1
  to a per-tap signed value (+1 forward / -1 reverse), and skip the
  block-rate "carry + recompute" geometry update for REVERSE-mode
  taps (so reverse reading is continuous across blocks instead of
  jumping back to current geometry every block).
- **G5 scrub** — keep forward Pass A advance, but in the block-rate
  dual-read shift, add a per-block-randomized offset to
  mTapNewReadIdx for SCRUB-mode taps. The dual-read crossfade
  *intentionally* produces a 5ms morph between consecutive scrub
  positions every block — that's the scrub character.

Both anchored per the original plan: G5 scrub depth scales with
`size × glitch`; G6 reverse coverage scales with `glitch`. Mutex
modes preserve per-cycle stability.

## Design

### Mode enum additions (Network.h)

```cpp
enum NetworkTapMode : uint8_t {
  NETWORK_TAP_NORMAL  = 0,
  NETWORK_TAP_MUTE    = 1,
  NETWORK_TAP_STUTTER = 2,
  NETWORK_TAP_CRUSH   = 3,
  NETWORK_TAP_SCRUB   = 4,   // new
  NETWORK_TAP_REVERSE = 5    // new
};
```

### Mode mutex assignment (extend the existing cumulative-threshold pattern)

Lines 670–691 in current Network.h. Add two more probabilities and
thresholds:

```cpp
const float kMaxMute    = 0.15f;
const float kMaxStutter = 0.40f;
const float kMaxCrush   = 0.25f;
const float kMaxScrub   = 0.15f;   // new
const float kMaxReverse = 0.15f;   // new

const float pMute    = glitchAmount * kMaxMute;
const float pStutter = glitchAmount * kMaxStutter;
const float pCrush   = glitchAmount * density * kMaxCrush;
const float pScrub   = glitchAmount * kMaxScrub;
const float pReverse = glitchAmount * kMaxReverse;

// Cumulative thresholds in 16-bit hash space, capped at 65535 so
// that overflow squeezes the lowest-priority modes (REVERSE then
// SCRUB) rather than giving any mode > its budgeted share.
uint32_t muteThresh    = (uint32_t)(pMute * 65535.0f);
if (muteThresh > 65535u) muteThresh = 65535u;
uint32_t stutterThresh = muteThresh + (uint32_t)(pStutter * 65535.0f);
if (stutterThresh > 65535u) stutterThresh = 65535u;
uint32_t crushThresh   = stutterThresh + (uint32_t)(pCrush * 65535.0f);
if (crushThresh > 65535u) crushThresh = 65535u;
uint32_t scrubThresh   = crushThresh + (uint32_t)(pScrub * 65535.0f);
if (scrubThresh > 65535u) scrubThresh = 65535u;
uint32_t reverseThresh = scrubThresh + (uint32_t)(pReverse * 65535.0f);
if (reverseThresh > 65535u) reverseThresh = 65535u;

// In the per-tap loop, dispatch:
if      (modeRand < muteThresh)    mode = NETWORK_TAP_MUTE;
else if (modeRand < stutterThresh) mode = NETWORK_TAP_STUTTER;
else if (modeRand < crushThresh)   mode = NETWORK_TAP_CRUSH;
else if (modeRand < scrubThresh)   mode = NETWORK_TAP_SCRUB;
else if (modeRand < reverseThresh) mode = NETWORK_TAP_REVERSE;
```

At glitch=1, density=1 the raw budget sums to 1.10; the cap squeezes
REVERSE first (highest cumulative threshold), so REVERSE shrinks
toward 0 in the worst case. NORMAL stays in the residual.

### New per-tap state (single int32 array)

```cpp
// G6 reverse — per-tap advance for Pass A. +1 = forward (default),
// -1 = reverse. Set per block from mTapEffectMode.
int32_t mTapReadAdvance[kMaxNetworkTaps];
```

Constructor: init all to `1` (forward).

### Mode assignment side-effect: mTapReadAdvance

Right after writing `mTapEffectMode[i] = mode;`, set:

```cpp
mTapReadAdvance[i] = (mode == NETWORK_TAP_REVERSE) ? -1 : 1;
```

Inactive taps (`i >= activeTaps`) get advance=1.

### Block-rate dual-read shift extension

Existing loop at lines 862–870 carries `mTapOldReadIdx[t] = mTapNewReadIdx[t]`
and computes fresh `mTapNewReadIdx[t]` from current geometry. Add
two passes after it:

**G5 scrub — per-block randomized offset.** SCRUB-mode taps need
their position to *jump* every block (that's the scrub character;
the dual-read crossfade smears each jump into a 5ms morph, which
audibly is the scrub effect). Use a hash that varies per block by
mixing in `mWriteIndex` (which advances FRAMELENGTH per block, so
provides block-rate variation):

```cpp
const float kScrubMaxFrac = 0.25f;
const int scrubMaxSamples =
  (int)(sizeNorm * kScrubMaxFrac * glitchAmount * (float)maxDelay + 0.5f);
if (scrubMaxSamples > 0)
{
  for (int t = 0; t < activeTaps; t++)
  {
    if (mTapEffectMode[t] != NETWORK_TAP_SCRUB) continue;
    uint32_t hScr = mGlitchLcg ^
      ((uint32_t)t * 2654435761u + 0x59B7C9F1u);
    hScr = hScr * 1103515245u + 12345u +
           (uint32_t)(mWriteIndex / FRAMELENGTH);   // block counter
    hScr = hScr * 1103515245u + 12345u;
    const int span = 2 * scrubMaxSamples + 1;
    const int offset = (int)((hScr >> 16) % (uint32_t)span)
                       - scrubMaxSamples;
    int idx = mTapNewReadIdx[t] + offset;
    while (idx < 0)         idx += maxDelay;
    while (idx >= maxDelay) idx -= maxDelay;
    mTapNewReadIdx[t] = idx;
  }
}
```

Note: `mGlitchLcg` is cycle-locked, but mixing in `mWriteIndex /
FRAMELENGTH` (block counter) gives per-block-per-tap variation
without breaking other modes' cycle-stability (they don't read the
scrub hash).

**G6 reverse — keep newReadIdx aligned with oldReadIdx.** Without
this, the geometry-derived recompute (forward-positioned) creates a
2×FRAMELENGTH discontinuity per block on REVERSE taps:

```cpp
for (int t = 0; t < activeTaps; t++)
{
  if (mTapEffectMode[t] == NETWORK_TAP_REVERSE)
  {
    // Continuous reverse — both indices advance -1/sample together.
    // No crossfade (a == b in Pass C → tapV = b regardless of w).
    mTapNewReadIdx[t] = mTapOldReadIdx[t];
  }
}
```

### Pass A NEON modification (per-tap signed advance + bidirectional wrap)

Replace the constant `oneVec` advance with per-tap `mTapReadAdvance`,
and add lower-bound wrap. Current NEON loop at lines ~1220–1231:

```cpp
// BEFORE:
int32x4_t oldIdx = vld1q_s32(&mTapOldReadIdx[t]);
oldIdx = vaddq_s32(oldIdx, oneVec);
uint32x4_t oldWrap = vcgeq_s32(oldIdx, maxDelayVec);
oldIdx = vbslq_s32(oldWrap, zeroIVec, oldIdx);
vst1q_s32(&mTapOldReadIdx[t], oldIdx);

// AFTER:
int32x4_t advance = vld1q_s32(&mTapReadAdvance[t]);
int32x4_t oldIdx  = vld1q_s32(&mTapOldReadIdx[t]);
oldIdx = vaddq_s32(oldIdx, advance);
// Upper wrap: idx >= maxDelay → idx -= maxDelay
uint32x4_t hiO = vcgeq_s32(oldIdx, maxDelayVec);
oldIdx = vbslq_s32(hiO, vsubq_s32(oldIdx, maxDelayVec), oldIdx);
// Lower wrap: idx < 0 → idx += maxDelay
uint32x4_t loO = vcltq_s32(oldIdx, zeroIVec);
oldIdx = vbslq_s32(loO, vaddq_s32(oldIdx, maxDelayVec), oldIdx);
vst1q_s32(&mTapOldReadIdx[t], oldIdx);
```

Same pattern for `mTapNewReadIdx`. `vcltq_s32` and `vsubq_s32` are
standard Cortex-A8 NEON. Cost: +2 vbsl + 1 vcltq per 4-tap iteration
in Pass A (~50% increase in this loop, but Pass A is a small
fraction of total per-sample cost — negligible overall).

The scalar fallback (lines ~1244–1251) needs the same bidirectional
treatment:

```cpp
int o = mTapOldReadIdx[t] + mTapReadAdvance[t];
if (o >= maxDelay) o -= maxDelay;
if (o < 0)         o += maxDelay;
mTapOldReadIdx[t] = o;
```

Same for `n` / `mTapNewReadIdx[t]`.

### Interaction with existing primitives

- **G4 read kick** still applies to SCRUB and REVERSE taps. SCRUB
  taps already have per-block jumps; an additional ±256 sample kick
  is barely noticeable. REVERSE taps get a kick to `mTapNewReadIdx`,
  but the next block's "G6 align" step overwrites it with
  `mTapOldReadIdx` — kick is one-sample-scoped on REVERSE taps,
  effectively no-op. Acceptable.
- **G7 respawn** modifies `mReflectors[t]` regardless of mode; for
  SCRUB/REVERSE taps the geometry-derived position evolves slowly
  underneath the scrub or reverse playback. Smooth, no special
  handling needed.
- **L3 LP, G8 crush, gain smoother** are unaffected by mode (they
  process whatever Pass C produces). For REVERSE-mode taps, the LP
  still smooths correctly (reads time-reversed but filter is just a
  one-pole IIR, sounds reasonable). G8 doesn't trigger on REVERSE
  taps (mutex prevents).
- **Stutter** (NETWORK_TAP_STUTTER) is mutex with SCRUB and REVERSE,
  so no overlap. Stutter scalar pass is unchanged.

### Audible expectations

- glitch=0: dormant. SCRUB/REVERSE inactive, all taps NORMAL/lush.
  Pass A advances all forward by +1.
- glitch=1, size=0.1, density=0.5:
  - ~15% of taps SCRUB with depth ±2.5% of buffer (~5ms scrub).
    Subtle pitch-uncorrelated movement.
  - ~15% of taps REVERSE — clear time-reversed snippets in the wet
    bus.
- glitch=1, size=1, density=0.5:
  - SCRUB depth ±25% of buffer (~250ms). Very dramatic — buffer
    position jumps a quarter-second every block. Sounds like rapid
    scrubbing of recent material.
- glitch=1, size=0:
  - SCRUB depth = 0 (size gates depth). SCRUB-mode taps still exist
    but don't actually scrub — same as NORMAL. (Acceptable; size=0
    is geometric collapse anyway.)

### CPU cost

Pass A NEON: +50% in the advance loop (small fraction of total).
G5 scrub block-rate offset: <activeTaps × 5 ops, block-rate. Tiny.
G6 align loop: <activeTaps × 2 ops, block-rate. Tiny.
Per-sample inner loop: unchanged work, just different input data.

Total expected CPU increase: <2% at full settings. Measurable but
not significant.

## Implementation order

Single commit (0.3.46):

1. Add `NETWORK_TAP_SCRUB` / `NETWORK_TAP_REVERSE` to enum
   (Network.h:48–55 area).
2. Add `mTapReadAdvance[kMaxNetworkTaps]` member; init to `1` in
   constructor.
3. Extend mode mutex thresholds with G5/G6 budgets (Network.h
   ~670–691).
4. In mode assignment loop, set `mTapReadAdvance[i]` based on mode.
5. Add G5 scrub block-rate offset pass after the dual-read shift
   loop.
6. Add G6 align pass (set `mTapNewReadIdx = mTapOldReadIdx` for
   REVERSE) after G5 scrub pass.
7. Modify Pass A NEON to use `mTapReadAdvance` + bidirectional wrap.
8. Modify Pass A scalar fallback identically.
9. Bump PKGVERSION 0.3.45 → 0.3.46.
10. Build, verify, install.

## Critical files

**To modify:**
- `mods/catchall/Network.h` — only file. Enum, state, ctor, mode
  setup, dual-read shift, Pass A NEON + scalar.
- `mods/catchall/mod.mk` — PKGVERSION bump.

**To reference (no edit):**
- `mods/catchall/network/geometry.h` — already correct for both
  modes (geometry just outputs delays/gains; reading direction is a
  pipeline concern).
- `er-301/mods/core/objects/granular/Grain.cpp:65` — for future ZC
  polish (recorded in repo plan, not part of this commit).
- `planning/network-character-macro-plan.md` — full repo plan; will
  be updated to mark G5/G6 done after ship.

## Verification

1. **Build clean**: linux + am335x both succeed.
2. **NEON hint check**: `arm-none-eabi-objdump -d
   testing/am335x/mods/catchall/Network.o | grep -cE
   '\.32.*:(64|128)'` returns 0.
3. **Lint**: `tools/check-graphic-virtual-defs.sh` exits clean.
4. **vtable check**: `arm-none-eabi-nm -C
   testing/am335x/libcatchall.so | grep 'vtable for stolmine::Network'`
   shows `V`.
5. **Hardware audition**:
   - **glitch=0 regression test**: confirm lush sound is unchanged
     vs. 0.3.45.
   - **glitch=1, size=1, density=0.5**: should hear scrub-character
     bursts (rapid position jumps with morphs) on ~15% of taps and
     reversed snippets on another ~15%.
   - **size sweep at glitch=1**: SCRUB depth audibly varies.
   - **glitch sweep**: SCRUB and REVERSE coverage scales.
   - **No-crash insert test**: insert/remove Network ≥10× to
     verify the new state arrays don't trip any vtable / heap
     hazards.

## Risks / known limitations

- **G5 scrub generates 5ms morph artifacts every block on
  SCRUB-mode taps**. This is intentional — the morph is the scrub
  sound. If audibly too harsh at full size, the cap can be reduced
  (e.g., kScrubMaxFrac 0.25 → 0.15) in a tuning round.
- **Long REVERSE taps will cross the buffer write head** (~1s
  buffer) just like long stutters. Reverse playback will start
  reading freshly-written content. Acceptable; reverses are at most
  one cycle long (~few seconds) and the audible effect — reverse
  playback "morphing" into current input — is musically acceptable.
- **Mode budget overflow** at glitch=1 squeezes REVERSE then SCRUB
  toward 0. Acceptable — it means at extreme settings the higher-
  priority modes (mute/stutter/crush) still get their full share.

## Memory references (rules to comply with)

- Header-only inline preserved (no new .cpp).
- NEON 4-wide patterns retained in Pass A; bidirectional wrap is
  branchless.
- All new state (`mTapReadAdvance[64]`) is a class member.
- Force-clean SWIG between header edits.
- PKGVERSION bump.
- `cp testing/linux/catchall-X.Y.Z.pkg ~/.od/rear/` after build.
