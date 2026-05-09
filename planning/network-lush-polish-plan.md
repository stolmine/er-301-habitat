# Network — lush polish (S1 + S2 + L3 lush halves)

## Context

Network at catchall 0.3.24 is feature-stable: dual-read crossfading
delay, density-compensated tap gain, all the spatial / feedback /
saturation infrastructure. The unit sounds clean and reverb-like at
typical settings, but per the PDF design notes
(`planning/refs/multitap-comb-design-notes.pdf`) it still reads as
"multitap comb" — taps are too similar in character.

Three planned items from `planning/network-design-notes.md`'s lush
side address this directly. The goal of this plan: implement them
**parametrically anchored** to existing user-facing controls (no new
plies, fully invisible UI-wise). Each item's intensity scales with a
parameter that has matching musical-purpose semantics:

| Item | Anchored to | Why |
|---|---|---|
| **S1: per-tap pitch detune** | `connectivity` | More feedback recycling means more risk of harmonic reinforcement / comb peaks. Pitch detune across taps is exactly the PDF's prescribed countermeasure. Higher conn → more detune to fight the reinforcement that conn itself enables. |
| **S2: per-tap LFO rate variation** | `motion` | Motion already drives the macro listener walker. Same parameter scaling per-tap LFO rate spread keeps motion as the singular "animation" control — when motion is high, the field is also "alive" at the per-tap chorus level. |
| **L3: per-tap LP filter** | `decay` | Convention. Damping-with-decay is the standard reverb pattern (see Rings: `lp_` set per damping in `eurorack/rings/dsp/fx/reverb.h:54-58`). Long decay tails accumulate HF — LP cutoff drops with decay to suppress this naturally. |

All three are deterministic per `seed`, so seed regeneration (already
the user's "randomize" gesture) shifts the per-tap variation pattern
along with the field geometry. Reproducible across sessions.

## Design

### S1 — Per-tap pitch detune (delay-offset implementation)

The PDF describes ±5–30¢ pitch shift per tap. **True pitch shift**
requires per-tap varispeed reads (fractional read pointers, per-tap
rate, linear interp), which would unwind the dual-read crossfading
delay we just landed in 0.3.22. The pragmatic substitute that
preserves the PDF's *purpose* (destroying integer-ratio comb peaks)
is **per-tap static delay offset in samples**: each tap's delay gets
±N samples of seeded random offset, scaled by connectivity. Comb
reinforcement is destroyed via delay incoherence rather than
frequency translation — same audible result for the comb-peak
problem, no shimmer character (PDF's "fifths/octaves" extension).

- **Mechanism**: per-tap offset hashed from `(t, mLastSeed)`, scaled
  by `connectivity × kMaxOffsetSamples`. Added to
  `mTapDelayTarget[t]` after `recomputeTaps()` returns (alongside the
  existing per-tap shimmer LFO addition that's already there).
- **Range**: `kMaxOffsetSamples = 24` (~0.5ms at 48kHz). At
  connectivity=1, taps spread ±0.5ms around their geometry-derived
  delays. At connectivity=0, no offset (taps are at exact geometry
  positions).
- **Hash**: golden-ratio multiplier × LCG step (same pattern as the
  fb_weight sign randomization, `Network.h:386-388`). Seed-derived
  bits give deterministic ±1 magnitude.
- **Where it lands**: extend the existing per-tap LFO loop in
  `process()` (around line 348 of `Network.h`) — same iteration over
  active taps, just adds the offset alongside the LFO modulation.

### S2 — Per-tap LFO rate variation

Currently a single `mLfoPhase` scalar advances at `kLfoHz = 0.5` for
all taps; per-tap phase comes from golden-angle offset only. Change
to **per-tap phase + per-tap rate**, both maintained as class-member
arrays.

- **State change**: `mLfoPhase` (scalar) → `mLfoPhase[64]` (array).
  Add `mLfoRate[64]` array (computed at seed-regen time, fixed
  per seed).
- **Per-tap rate**: hashed at seed-regen — `rate[t] = kLfoHz × (1 +
  seedHash(t) × kRateSpread)`. Always-on base rate, regardless of
  motion. So even when motion=0, taps wobble at slightly different
  rates for chorus.
- **Motion scaling**: at low motion, rate spread is small (taps
  nearly synchronous). At high motion, rate spread expands so rates
  diverge ±50% around base. Implementation: `rate[t] = kLfoHz × (1 +
  motion × seedHash(t) × 0.5)` computed each block (cheap; 64-tap
  loop with hash + mul).
- **Per-block update**: each tap advances independently:
  `mLfoPhase[t] += rate[t] × blockDt; mLfoPhase[t] -= floorf(...)`.
  Replaces the current single-scalar advance.
- **Where**: same per-tap LFO loop in `process()` (currently around
  Network.h:342-355). Refactor that loop to iterate per-tap phase
  arrays.

### L3 — Per-tap LP filter scaled by decay

Per-tap one-pole LP filter applied to `tapV` before the gain/fb
multiplications in Pass C. Each tap's cutoff is the *base cutoff*
(decay-driven) × *seed-derived per-tap variation*.

- **State**: `mTapLpState[64]` (per-sample filter state),
  `mTapLpCoeff[64]` (block-rate computed coefficient).
- **Base cutoff**: maps `decay` 0..1 logarithmically to ~18kHz..3kHz.
  Computed at block-rate as `baseCutoff = exp(ln(18000) - decay ×
  ln(6))`. Convert to one-pole coefficient via `coeff = 1 -
  exp(-2π × cutoff / sampleRate)`.
- **Per-tap variation**: `coeff[t] = baseCoeff × (0.7 + 0.6 ×
  seedHash01(t))`. Seeded ±30% spread around the base cutoff —
  preserves L3's "random cutoffs" character per PDF while keeping
  decay as the unifying anchor.
- **Per-sample LP step**: `mTapLpState[t] += mTapLpCoeff[t] × (tapV
  - mTapLpState[t]); tapV = mTapLpState[t]`. Five ops per tap per
  sample. NEON 4-wide vectorizable along the same axis as the
  existing gain/fb_weight smoothers.
- **Where**: insert in Pass C just after `tapV` is computed (around
  Network.h:591) and before `wetLVec`/`wetRVec`/`fbVec` accumulation.

## Critical files

**To modify:**

- `mods/catchall/Network.h` — only file that changes.
  - Class member section: replace `float mLfoPhase` with arrays
    `mLfoPhase[64]` + `mLfoRate[64]`; add `mTapLpState[64]` +
    `mTapLpCoeff[64]`.
  - Constructor: zero all new arrays.
  - Block-rate setup in `process()`:
    - Compute per-tap pitch-detune offsets (in the existing per-tap
      LFO loop), scaled by connectivity.
    - Update per-tap LFO rates from motion + seed (when seed or
      motion changes — could be every block for simplicity).
    - Compute per-tap LP coefficients from decay + seed.
  - Per-tap LFO loop: refactor to per-tap phase arrays.
  - Pass C: insert per-tap LP filter step on `tapV` before gain/fb
    accumulation (NEON 4-wide).

**To reference (no edit):**

- `mods/spreadsheet/visadhara/pmm.h` — LCG hash pattern.
- `eurorack/rings/dsp/fx/reverb.h:54-58` — Rings's lp damping
  convention (decay-driven LP cutoff).
- `mods/catchall/network/geometry.h` — for context only; geometry's
  output already feeds `mTapDelayTarget` which receives our additions.
- `planning/refs/multitap-comb-design-notes.pdf` — PDF design notes.
- `planning/network-design-notes.md` — categorization doc.

## Implementation order (incremental, each independently testable)

1. **S1 first** (cheapest, biggest character impact per PDF). Add
   per-tap delay offset to the existing LFO loop. Verify connectivity
   sweep produces audible comb-peak destruction at high settings.
2. **S2 second** (small refactor of existing LFO state). Migrate
   `mLfoPhase` scalar → array, add `mLfoRate` array. Verify motion
   sweep produces "chorus convergence/divergence" effect.
3. **L3 last** (largest change — new state, Pass C insertion). Add
   per-tap LP. Verify decay sweep produces audible HF damping on long
   tails.

Each step bumps `mod.mk` PKGVERSION (`0.3.25`, `0.3.26`, `0.3.27`)
and produces an installable build for hardware testing before
proceeding.

## Verification — end-to-end

1. **Build clean**: `make ARCH=linux PKGNAME=catchall` and
   `make ARCH=am335x PKGNAME=catchall` both succeed.
2. **NEON hint check**: `arm-none-eabi-objdump -d
   testing/am335x/mods/catchall/catchall_swig.o | grep -cE
   '\.32.*:(64|128)'` returns 0.
3. **Lint**: `tools/check-graphic-virtual-defs.sh` exits clean.
4. **vtable check**: `arm-none-eabi-nm -C
   testing/am335x/libcatchall.so | grep 'vtable for stolmine::Network'`
   shows `V` (COMDAT, vague-linkage).
5. **Hardware audition**:
   - Insert Network. Verify no crash on insert (the recurring
     hazard).
   - Sweep `connectivity`: at 0, taps should sound straight. At 1,
     comb peaks audibly destroyed (S1 working).
   - Sweep `motion`: at 0, taps wobble in sync. At 1, divergent
     chorus character (S2 working).
   - Sweep `decay`: at 0, full bandwidth wet. At 1, audibly damped
     HF on long tails (L3 working).
6. **Combined test**: maximum connectivity + motion + decay should
   sound lush and reverb-like with no resonance buildup, comb peaks,
   or harshness. Ideal lush state.

## Memory references (rules to comply with)

Same as the parent plan — header-only inline preserved (no new
.cpp), NEON 4-wide patterns retained, no per-sample dispatch
branches, class-member arrays for all NEON-touched state, force-
clean SWIG before each rebuild, no libm trig in package paths
(decay→cutoff `exp` is OK at block rate).
