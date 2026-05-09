# Network — Character macro (glitch side, gltch ply)

## Context

Network at catchall 0.3.27 has its lush side fully developed:
geometry-driven multitap with dual-read crossfading delay, density-
compensated tap gain, per-tap pitch detune (S1, anchored to conn),
per-tap LFO rate variation (S2, anchored to motion), per-tap LP
filter (L3, anchored to decay), in-loop allpass diffusion, sparse
feedback recycle, tanh saturation, DC blockers. It sounds clean and
reverb-like.

The unit's design vision per `planning/network-design-notes.md` is a
spectrum from lush continuous reverb to discrete event-driven
glitch, controlled by a single Character macro. With lush now solid,
this plan adds the macro and the glitch-side primitives.

**User-locked architecture (decided in interview, see conversation
2026-05-09):**

- **Q1 — Unified ramp**: a single `glitchAmount` float (read from
  the new `gltch` ply parameter, clamped 0..1) is multiplied into
  every glitch primitive's event probability. No staggered fade-in;
  all primitives ramp together.
- **Q2 — Lush stays on at glitch=1**: this is "glitchy reverb," not
  two distinct sonic regions. S1/S2/L3, geometry, allpass diffusion,
  feedback recycle, saturation — all keep running unmodified
  underneath the glitch primitives.
- **Q3 — Probability semantics**: events fire at `glitchAmount ×
  primitiveMaxRate` probability per evaluation tick (block-rate or
  per-sample depending on the primitive). Cheap, deterministic per
  seed.
- **Q4 — UI**: a 7th ply named `gltch` inserted before `wet` in the
  expanded layout. (User: "we will be folding some stuff together on
  an overview ply eventually, for now we can just add a 7th ply.")
- **Q5 — Per-primitive flavor anchors** (each glitch primitive's
  *flavor* is scaled by an existing param while macro controls
  *amount*):

  | Primitive | Anchored to | Flavor effect |
  |---|---|---|
  | G1 — S&H on tap positions | `motion` | Higher motion → faster S&H clock rate, more abrupt position steps. |
  | G3 — Probabilistic mute | `density` | Higher density → more taps eligible to mute. |
  | G2 — Per-tap stutter/freeze | `decay` | Higher decay → longer stutter durations, longer freezes. |
  | G4 — Transient-triggered events | `connectivity` | Higher conn → transients ricochet through more taps. |
  | G5/G6 — Scrub + reverse reads | `size` | Higher size → larger scrub depth in samples. |
  | G8 — Bitcrush + decimate subset | `density` | Higher density → larger affected tap subset (capped < 100% coverage); per-tap bit-depth and decimate factor seeded. |
  | G7 — Tap respawn | `motion` | Higher motion → faster respawn rate. (User said "conn or motion"; motion chosen for narrative coherence with G1: motion = all geometric flux.) |

  Two primitives on density (G3, G8) and two on motion (G1, G7).
  Connectivity, decay, size each get one. All spreads are
  intentional.

- **Q6 — Curve**: linear with per-primitive minimum-probability
  floor. `glitch == 0` → all primitives fully off (bit-exact lush).
  `glitch > 0` → each primitive's probability jumps to its floor,
  then ramps linearly to its max as `glitch → 1`. So nudging the
  macro just-on produces a "tasting menu" of all glitch characters
  at low rate immediately.

  Formula per primitive:
  ```cpp
  float p = (glitch <= 0.0f) ? 0.0f
          : minP + (maxP - minP) * glitch;
  ```

## Architecture

### Glitch infrastructure (added once, used by every primitive)

- **New ply / parameter**: `Glitch` float Parameter on Network,
  exposed via SWIG, ply named `gltch` placed before `wet` in
  `Network.lua`'s expanded layout. Default bias `0.0`, range
  [0, 1]. Standard GainBias control (CV branch like the others).
- **Block-rate read** in `process()`: `float glitchAmount =
  CLAMP(0.0f, 1.0f, glitch);` alongside the existing param reads.
- **Glitch RNG state** (separate from walker's `mWalkerLcg` so that
  glitch event timing is independent of motion phase):
  `uint32_t mGlitchLcg;` initialized in constructor from a fixed
  seed; advanced one step per evaluation tick where used.
- **Hash mask allocation** for per-tap, per-primitive seeded
  variation (4th XOR mask onward; lush polish used `0xA5A5A5A5`,
  `0x3C3C3C3C`, `0x77777777`):

  | Primitive | XOR mask | Used for |
  |---|---|---|
  | G1 S&H | `0xCCCCCCCCu` | per-tap S&H clock phase + step pattern |
  | G3 mute | `0x33333333u` | per-tap, per-tick mute decision |
  | G2 stutter | `0x66666666u` | per-tap stutter trigger + duration |
  | G4 transient | `0x99999999u` | per-event affected-tap selection |
  | G5/G6 scrub/rev | `0xF0F0F0F0u` | per-tap direction + scrub depth |
  | G8 crush | `0xC3C3C3C3u` | per-tap bit-depth + decimate factor |
  | G7 respawn | `0x3CC33CC3u` | per-tap respawn timing |

### Per-primitive design

#### G3 — Probabilistic mute (simplest; ship first)

- **Mechanism**: per-tap, per-block, decide mute/unmute. Apply as a
  multiplicative mask on `mTapGainLSmoothed[t]` and
  `mTapGainRSmoothed[t]` smoothed targets. Smoothed gain ramp
  prevents click on mute transition.
- **Probability**: `pMute(t) = density × (minMute + (maxMute -
  minMute) × glitchAmount)` where `minMute = 0.02`, `maxMute = 0.4`.
  Density gates eligibility, glitch scales probability.
- **State**: none (decision is recomputed per block from new hash;
  smoothed gain absorbs the transitions).
- **Where**: in the existing per-tap LFO loop (Network.h:379–423),
  after L3 LP coeff.

#### G1 — S&H on tap positions

- **Mechanism**: each tap has an S&H clock phase. When the clock
  ticks, the tap snapshots its current `mTapDelayTarget[t]` (post-
  geometry, post-S1 detune, post-S2 LFO — the full lush-side
  position). Until the next tick, the snapshot replaces the
  continuous value, freezing the tap's delay at a discrete step.
- **Clock rate**: `clkHz(t) = motion × (minClk + (maxClk - minClk) ×
  glitchAmount)` where `minClk = 1.0 Hz`, `maxClk = 16.0 Hz`. Motion
  governs absolute speed; glitch governs how stepwise the motion
  becomes.
- **State**: `mTapShClock[64]` (phase 0..1), `mTapShValue[64]`
  (snapshot in samples).
- **Where**: in the per-tap LFO loop. After computing the lush-side
  `mTapDelayTarget[t]`, advance S&H clock by `clkHz × blockDt`. On
  wraparound, snapshot. Then if glitchAmount > 0, blend in S&H
  value: `mTapDelayTarget[t] = lerp(continuous, snapshot,
  glitchAmount)` — gives smooth introduction. At glitch=1, full
  replacement.

#### G2 — Per-tap stutter/freeze

- **Mechanism**: per-tap state machine with two states: NORMAL
  (default) and STUTTER. In STUTTER, the tap's read pointer is
  frozen — it re-reads the same N-sample loop until exit. State
  evaluated per block.
- **Trigger probability**: `pTrig(t) = decay × (minTrig + (maxTrig -
  minTrig) × glitchAmount)` per block, where `minTrig = 0.005`,
  `maxTrig = 0.05`. Decay scales probability (long-decay reverbs
  hold the stutter audibly longer).
- **Stutter duration**: hashed per-trigger as 1..16 blocks (≈4..64
  ms at 48k/256-sample blocks).
- **Implementation**: per-tap state → frozen read indices. Easiest:
  freeze `mTapNewReadIdx[t]` and `mTapOldReadIdx[t]` to their
  pre-stutter values for the duration. The dual-read crossfade then
  produces a held loop with no per-sample writes overhead.
- **State**: `mTapStutterRemaining[64]` (uint8 — 0 = NORMAL, n =
  STUTTER for n more blocks), `mTapStutterIdxA[64]`,
  `mTapStutterIdxB[64]` (the frozen indices).

#### G4 — Transient-triggered events

- **Mechanism**: input transient detector (one-pole envelope
  follower difference) generates a rare global event. On event,
  randomly affect K taps (K ∝ connectivity × glitchAmount): pick
  one of {flip fb sign, mute for 1 block, kick the read pointer
  forward by a random offset}.
- **Detector**: `envFast = max(absInput, envFast × 0.95)`,
  `envSlow = envSlow × 0.999 + absInput × 0.001`. Transient when
  `envFast > envSlow × thresh` and not currently in cooldown.
- **State**: `mEnvFast`, `mEnvSlow` (scalar floats),
  `mTransientCooldown` (block counter).
- **Affected-tap count**: `K = (int)(connectivity × maxK ×
  glitchAmount)` where `maxK = 6`.
- **Per-event hash**: `mGlitchLcg` advances; pick K indices with
  rejection-free golden-ratio sampling.
- **Block rate**, applied between block-rate setup and per-sample
  loop.

#### G5/G6 — Scrub + reverse reads

- **Mechanism**: per-tap direction flag (forward/reverse) and per-
  tap scrub offset. When reverse is active, read pointer advances
  the opposite direction during the block. When scrub is active,
  read pointer offset is randomly perturbed each block.
- **Scrub depth**: `scrubMax = size × maxBufFrac × glitchAmount`
  where `maxBufFrac = 0.25` of buffer length.
- **Reverse probability**: per-tap, per-block, hash decides. `pRev =
  minRev + (maxRev - minRev) × glitchAmount` with `minRev = 0.01`,
  `maxRev = 0.15`.
- **Implementation**: this primitive interacts with the dual-read
  delay differently from the others — needs careful design to avoid
  unwinding the dual-read crossfade. **Defer details to Phase 3c
  design pass; sketch only here.** Likely requires per-tap
  read-direction flags piped through the existing read-index
  advance code (~Pass A in process()).
- **State**: `mTapRevFlag[64]` (uint8), `mTapScrubOffset[64]`
  (samples).

#### G8 — Bitcrush + decimate subset

- **Mechanism**: per-tap independent bitcrush + sample-rate
  decimate, applied to `tapV` *after* L3 LP and *before*
  `wetL/wetR/fbVec` accumulation in Pass C. Only a subset of taps
  affected, chosen probabilistically per block. Per-affected-tap
  bit-depth and decimate factor varied via seed hash.
- **Affected fraction**: `frac = density × maxFrac × glitchAmount`
  where `maxFrac = 0.6` (user constraint: never 100% coverage).
  Each tap independently passes the threshold check.
- **Bitcrush** (lifted from `mods/spreadsheet/Larets.cpp:265-271`):
  ```cpp
  float lvl = powf(2.0f, 12.0f - bitParam * 9.5f);
  out = floorf(in * lvl + 0.5f) / lvl;
  ```
  `bitParam` per-tap from G8 hash: range [0.3, 1.0] so even minimal
  crush is audibly bit-reduced.
- **Decimate** (lifted from `Larets.cpp:273-278`):
  ```cpp
  int factor = 1 + (int)(decimParam * 31.0f);
  if (++counter[t] >= factor) { hold[t] = in; counter[t] = 0; }
  out = hold[t];
  ```
  `decimParam` per-tap from G8 hash: range [0.0, 0.7] so factors
  span 1..23 (avoid full ×32 freeze; that overlaps G2).
- **State**: `mTapCrushMask[64]` (uint8 enabled flag, block-rate),
  `mTapCrushBitLvl[64]` (float `lvl`, block-rate),
  `mTapDecimFactor[64]` (uint8, block-rate),
  `mTapDecimCounter[64]` (uint8, per-sample),
  `mTapDecimHold[64]` (float, per-sample).
- **Per-sample apply**: scalar per-tap (NEON-unfriendly because
  decimate has per-tap counter branch); fall back to scalar tail
  loop when crushed-tap fraction is high. At low fractions, branch
  predicts well.
- **Where**: insert in Pass C after L3 LP filter, before gain/fb
  accumulation. **Note**: the "subset" mask makes Pass C's NEON
  4-wide harder. Implementation strategy: keep the NEON path for
  un-crushed taps (mask = 0), then a scalar second-pass over the
  crushed-tap subset that computes their contribution and adds to
  wet/fb. This preserves NEON throughput on the common (mostly-
  uncrushed) case.

#### G7 — Tap respawn (lifetimes)

- **Mechanism**: per-tap lifetime counter. When it expires, the tap
  re-randomizes its reflector position (single tap call into
  `network_geom::regenerateField` for one index, with a new sub-
  seed) and resets the counter.
- **Lifetime**: `lifeBlocks = (int)(motion ⁻¹ × baseLife ×
  (1 - glitchAmount))` — higher motion or glitch → shorter life.
  At glitch=0, lifetime is effectively infinite (no respawns).
- **Per-tap hash**: G7 mask seeds initial-life randomization so
  respawns aren't synchronized.
- **State**: `mTapLifeRemaining[64]` (int).
- **Implementation note**: respawn writes one `mReflectors[t]`
  entry. Geometry is recomputed per block from these reflectors via
  `recomputeTaps()`, so the new position takes effect within one
  block (≈5ms). No special crossfade — the existing dual-read
  delay's crossfade absorbs the discontinuity.

## Implementation order (incremental, each shippable)

Each step bumps PKGVERSION and produces an installable build. Order
chosen for cheapest-first / biggest-impact-first:

**Phase 3a — Macro infrastructure + simplest primitives** (0.3.28):
1. Add `Glitch` Parameter, `gltch` ply, SWIG, `glitchAmount` read.
2. **G3 — probabilistic mute** (multiplicative gain mask in per-tap
   loop; cheapest possible primitive).
3. **G1 — S&H on tap positions** (snapshot + clock in per-tap loop).

**Phase 3b — Per-tap state machines** (0.3.29):
4. **G2 — per-tap stutter/freeze** (state machine, frozen read
   indices).
5. **G8 — bitcrush/decimate subset** (Larets-derived per-tap, mask).

**Phase 3c — Most complex** (0.3.30+):
6. **G4 — transient-triggered events** (input envelope detector,
   global event, K-tap modifier).
7. **G7 — tap respawn** (lifetime + single-tap regeneration).
8. **G5/G6 — scrub + reverse** (deferred to last; needs careful
   integration with dual-read; may want a focused planning pass
   before this one).

After each phase, audition on hardware to validate:
- glitch=0 still produces bit-exact lush (no regression).
- glitch>0 immediately produces audible character (minimum-
  probability floor working).
- glitch=1 character matches the primitive's intent.

## Critical files

**To modify (in stages):**

- `mods/catchall/Network.h` — class members, ctor init, process()
  block-rate setup, per-tap LFO loop, Pass C insertion (G8). All
  primitive state arrays land here.
- `mods/catchall/assets/Network.lua` — add `gltch` ply + GainBias
  view, splice into expanded layout before `wet`.
- `mods/catchall/mod.mk` — PKGVERSION bump per phase.

**To reference (no edit):**

- `mods/spreadsheet/Larets.cpp:265-278` — bitcrush + decimate
  source for G8.
- `mods/catchall/network/geometry.h` — `regenerateField()` for G7
  per-tap respawn (will need a single-index variant or call the
  full regenerate with reseeded RNG).
- `planning/refs/multitap-comb-design-notes.pdf` — original brief.
- `planning/network-design-notes.md` — full G1–G8 categorization
  doc; cross-reference for design intent.

## Verification — end-to-end (per phase)

1. **Build clean**: `make ARCH=linux PKGNAME=catchall` and
   `make ARCH=am335x PKGNAME=catchall` both succeed (force-clean
   SWIG wrapper between header changes).
2. **NEON hint check**: `arm-none-eabi-objdump -d
   testing/am335x/mods/catchall/Network.o | grep -cE
   '\.32.*:(64|128)'` returns 0. (G8 in particular needs verifying;
   adding scalar second-pass shouldn't regress this.)
3. **Lint**: `tools/check-graphic-virtual-defs.sh` exits clean.
4. **vtable check**: `arm-none-eabi-nm -C
   testing/am335x/libcatchall.so | grep 'vtable for stolmine::Network'`
   shows `V` (COMDAT vague-linkage).
5. **Hardware audition (per phase)**:
   - **glitch=0 regression test**: confirm lush sound is unchanged
     vs. the prior phase's installed build.
   - **glitch nudge test**: at glitch=0.05, confirm primitive
     introduced this phase produces audible event(s) within 30s.
   - **glitch=1 test**: confirm character is "the right kind of
     glitch" (e.g. mute = audible holes, S&H = stepped position
     wobble, stutter = held buffer loop, crush = digital grit on a
     subset of taps).
   - **flavor-anchor test**: sweep the anchor param while glitch=1.
     Confirm character changes coherently (e.g. for G1 on motion:
     low motion → slow steps; high motion → fast steps).
6. **No-crash insert test**: insert/remove Network many times after
   each phase; the unit has had crash-on-insert hazards.

## Memory references (rules to comply with)

- Header-only inline preserved (no new .cpp).
- NEON 4-wide patterns retained where applicable; G8's per-tap
  branching needs the "scalar second pass over subset" pattern to
  avoid breaking the NEON path.
- No per-sample dispatch branches on `glitchAmount`; multiplicative
  scaling on probabilities is the safe pattern (per
  `feedback_runtime_branched_dsp_dispatch`).
- All new NEON-touched state must be class members
  (`feedback_neon_intrinsics_drumvoice`).
- Force-clean SWIG wrapper between header edits
  (`feedback_swig_header_dep`).
- No libm trig in package paths at per-sample rate; `powf` for
  bitcrush level computation is OK because it's block-rate per
  affected tap.
- PKGVERSION bump per build (`feedback_package_version_bump`).
- `cp testing/linux/catchall-X.Y.Z.pkg ~/.od/rear/` after each
  linux build (`feedback_linux_build_auto_install`).
