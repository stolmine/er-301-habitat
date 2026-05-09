# Network — Character macro (gltch ply)

**Status: feature-complete at catchall 0.3.50.** This doc was a
forward plan and is now an as-shipped reference. Original plan
preserved at the bottom for context. Primary new doc for resuming
work is the **Polish backlog** at the top.

---

## Polish backlog (open work)

### Stutter loop boundary discontinuity (refinement of 0.3.43 ZC pass)

Current behavior: at trigger time, anchor and `mTapStutterLoopSamples`
are both nudged ±FRAMELENGTH samples to land on the smallest-
magnitude buffer sample (best zero approximation in range). Soft but
not silent — residual click remains audible at high stutter density.

**Proper fix (deferred):**

1. Replace min-magnitude ZC search with **sign-change ZC detection**
   per `er-301/mods/core/objects/granular/Grain.cpp:65`
   (`od::Grain::snapToZeroCrossing`). Searches forward for
   negative→positive crossing, backward for positive→negative
   crossing, picks closer direction. More rigorous than min-magnitude
   when residual signal is non-zero (which is most of the time).

2. **Add a small Hanning fade window at loop boundaries.** Last
   N samples of loop (~32–64) crossfade with first N of next
   iteration. Hides any residual sample-step at wrap. ER-301's
   `od::Grain` uses linear fade (`mFade` samples ramp); a Hann or
   sine half-period works equally well. Apply on top of the static
   stutter gain (no smoother needed since fade applies symmetrically
   to both ends per loop iteration).

3. Implementation outline:
   - At trigger time, sign-change ZC search ±FRAMELENGTH around
     anchor and around `anchor + loopSamples`.
   - Per-sample stutter pass: when `posInLoop > loopSamples - fadeN`,
     compute fade weight `w = (loopSamples - posInLoop) / fadeN`
     and apply `sample *= w`. Symmetrical at loop start (`posInLoop
     < fadeN` → `w = posInLoop / fadeN`).
   - Cost: per-trigger ZC scan is fine; per-sample fade is the hot
     path. Use a precomputed 64-entry LUT for the Hann window or
     just compute `0.5f - 0.5f * cosf(...)` (block-rate constant
     not viable here since trigger varies). LUT is cleanest.

### Stutter CPU at high density (open)

At density=1, glitch=1 we hit ~91% CPU. Optimizations from 0.3.38
helped but per-sample scalar stutter pass is the dominant cost.
Possible follow-ups:
- Cap absolute max stutter taps regardless of mode budget.
- NEON-vectorize the stutter pass (with scalar gather since
  Cortex-A8 has no proper gather instruction).
- Pre-batch stutter reads per-tap-then-per-sample for better cache
  locality.

Defer until polish round; not critical for hardware audition.

---

## As-shipped architecture (catchall 0.3.50)

### UI

7-ply layout left→right: `size`, `dens`, `motn`, `conn`, `decay`,
`gltch`, `wet`. All standard GainBias controls with CV branches.

### Lush body (always-on)

- **S1** per-tap pitch detune (XOR mask `0xA5A5A5A5`,
  `connectivity`-anchored — ±0.5ms delay offset hashed per tap).
- **S2** per-tap LFO rate variation (`0x3C3C3C3C`,
  `motion`-anchored — per-tap LFO rate ±50% spread at full motion).
- **L3** per-tap LP filter (`0x77777777`, `decay`-anchored, glitch-
  attenuated — coefficient pulled toward 1.0 at high glitch so
  HF survives in feedback for grittier texture).
- **4-stage allpass diffusion** in feedback path
  (`connectivity`-driven via `soften` mix).
- **Sparse selectable feedback recycle** (`kRecycle = conn ×
  activeTaps`, signed-random `mFbWeight` magnitudes for resonance-
  break).
- **Density-compensated tap gain** `2.5 × N^(-0.4)` (softened from
  statistical √N to preserve presence at high density).
- **DC blockers** on input, feedback, and L/R output.
- **Tanh saturation** on feedback recycle (Padé 3/3).
- **Smooth-random listener walker** drives geometry / tap
  delays/pans; rate scales with `1 + 4 × conn × decay`.
- **G7 reflector drift** (motion × glitch driven, ±0.15 unit-disk
  delta per respawn — smooth field evolution, no teleport clicks).

### Glitch macro (gltch ply, mutex modes per cycle)

Cycle-locked seed (`mGlitchLcg`). Reseeds on each walker revolution
wrap → glitch decisions stay deterministic within a cycle, shuffle
on cycle wrap. Motion thus controls glitch pattern shuffle rate.

Mode mutex: each tap gets exactly one of {NORMAL, MUTE, STUTTER,
CRUSH, SCRUB, REVERSE} per cycle via cumulative-threshold dispatch
(single hash, XOR mask `0xDD55DD55`).

**At glitch=1 (any density):**

| Mode | Share | Behavior |
|---|---|---|
| MUTE | 15% | gain + fbWeight zeroed; smoother absorbs ramp |
| STUTTER | 40% | multi-block loop with iter-counted duration |
| CRUSH | 11.25% | bitcrush+decimate sub-modes |
| SCRUB | 11.25% | block-rate position offset |
| REVERSE | 11.25% | reverse Pass A advance |
| NORMAL | 11.25% | clean lush tap |

All glitch coverage is purely glitch-fader-scaled. Density only
controls active-tap count + level compensation (decoupled).

### Glitch primitives (in-mode behaviors)

- **G3 MUTE** — `mTapGainL/R` and `mFbWeight` zeroed for MUTE-mode
  taps. Smoother handles the transition (~50ms ramp).

- **G2 STUTTER** — per-tap multi-block loop:
  - Loop length 24..96 blocks (~125ms..512ms = 16th–quarter @120BPM),
    triangular distribution mode-biased by `decay`.
  - Soft motion-cycle subdivision snap (loop length halfway-snapped
    to nearest cycle/N for N in {2,3,4,6,8,12,16,24,32,48,64} that
    falls in range).
  - Iterations 2..8 (full loop traversals), triangular by `decay`.
  - Speed picked uniformly from {×0.5, ×1, ×2} (octave shift).
  - Anchor + loop end snapped to min-magnitude buffer samples
    (±FRAMELENGTH search) at trigger.
  - Separate scalar per-sample playback path with float read
    pointer + linear interp; bypasses Pass A/B/C (which are zeroed
    via gain=0 for STUTTER taps).
  - Static stutter gain captured at trigger = lush gain × 1.5
    boost.
  - mTapStutterFbW = 0 (stutter doesn't recycle into feedback,
    avoids long-stutter self-feedback runaway).

- **G8 CRUSH** — per-tap bitcrush + sample-rate decimate, three
  sub-modes hashed equally (`0x55555555`):
  - BITCRUSH_ONLY (factor=1, just bit reduction)
  - DECIMATE_ONLY (16-bit clean, just rate reduction)
  - BOTH (combined)
  - bit depth: uniform [0,1] hash (XOR `0xC3C3C3C3`) → bitLvl in
    [4096 (12-bit), 5.66 (~2.5-bit)] (Larets formula)
  - decim factor: uniform [0,1] hash (XOR `0x99CC55AA`) → factor
    1..32 (Larets formula)
  - Per-sample branchless NEON 4-wide in Pass C; mask blend with
    un-crushed tapV.

- **G5 SCRUB** — per-block randomized offset on `mTapNewReadIdx`
  for SCRUB-mode taps (XOR `0x59B7C9F1`, mixed with
  `mWriteIndex/FRAMELENGTH` block counter for per-block variation
  while mGlitchLcg stays cycle-locked). Offset depth = `size ×
  glitch × 0.25 × maxDelay`. Dual-read crossfade smears each jump
  into a 5ms morph — that morph is the scrub character.

- **G6 REVERSE** — per-tap signed advance in Pass A
  (`mTapReadAdvance[t] = -1` for REVERSE-mode taps, `+1`
  otherwise). Block-rate alignment of `mNewReadIdx ← mOldReadIdx`
  for REVERSE taps (prevents 2×FRAMELENGTH discontinuity from
  geometry-derived recompute fighting reverse playback). Pass A
  NEON modified to use per-tap advance + bidirectional wrap
  (`vcgeq` for upper, `vcltq` for lower, branchless `vbslq`).

### Baseline-mode glitch (always-on, not gated by glitch macro)

- **G4 input transient ricochet** — block-rate envelope detector
  (fast vs slow follower); on transient, perturb K random taps
  (K = `conn × density × 6`) with one of {flip fb sign, duck gain
  ~50ms, kick read pointer ±256 samples}. STUTTER-mode taps
  skipped (no-op anyway). Audible in lush mode (glitch=0) too —
  feels like sidechain ducking + tap re-allocation on input
  transients. Default cooldown 10 blocks (~53ms refractory).

### Effect interaction summary

Effects are **mutex per tap, per cycle** but **stack across taps
via the shared feedback bus**. A STUTTER tap's wet-bus
contribution writes to the buffer (via the feedback recycle), where
a CRUSH tap on the next cycle reads it and crushes the stuttered
material. Same with all combinations. So at high glitch, the
character is layered (one tap producing the loop, another reading
+ crushing it, another reading + reversing it) without any single
tap doing multiple things at once.

### Key tunable constants (Network.h)

| Constant | Value | Role |
|---|---|---|
| `kMaxMute` | 0.15 | MUTE coverage at glitch=1 |
| `kMaxStutter` | 0.40 | STUTTER coverage at glitch=1 |
| `kMaxCrush` | 0.1125 | CRUSH coverage at glitch=1 |
| `kMaxScrub` | 0.1125 | SCRUB coverage at glitch=1 |
| `kMaxReverse` | 0.1125 | REVERSE coverage at glitch=1 |
| `kStutterMinLenBlocks` | 24 | Stutter loop min length (~125ms @48k) |
| `kStutterMaxLenBlocks` | 96 | Stutter loop max length (~512ms @48k) |
| `kStutterMinIterations` | 2 | Stutter min loop iterations |
| `kStutterMaxIterations` | 8 | Stutter max loop iterations |
| `kStutterGainBoost` | 1.5 | Stutter gain × lush gain at trigger |
| `kScrubMaxFrac` | 0.25 | Scrub depth fraction of buffer at full |
| `kMaxRespawnHz` | 2.0 | G7 max respawn rate per tap |
| `kRespawnMaxDelta` | 0.15 | G7 max position drift per respawn |
| `kZCSearchRange` | FRAMELENGTH | Stutter ZC alignment search ±range |
| `kStutterSnapStrength` | 0.5 | Cycle-subdivision snap strength |
| `kGlitchReseedXOR` | 0xDEADBEEF | Cycle-wrap reseed perturbation |
| `kMaxK` (G4) | 6 | G4 max ricochet count |

---

## Original plan archive (pre-implementation, 2026-05-09)

The architecture below was the forward plan; deviations during
implementation:

- **Decay decoupling from stutter trigger** (0.3.34) — original
  formula coupled stutter trigger probability to `decay × glitch`;
  refactored to glitch-only after audition showed loops weren't
  audible across the decay range.
- **Mutex modes** (0.3.34) — original plan had primitives
  independently triggered per tap (could overlap). Audition showed
  this muddied character; refactored to mutex.
- **G4 promoted to baseline** (0.3.45) — original plan had G4
  glitch-gated. Audition showed musically useful in lush mode too.
- **G5/G6 design pass** (0.3.46) — deferred during initial Phase 3c
  per plan; designed in `network-g5-g6-plan.md` and shipped.
- **CRUSH severity collapse fix** (0.3.36) — original plan used
  triangular distribution with mode=density which collapsed to a
  single severity at extremes. Refactored to uniform-uncorrelated
  hashes with categorical sub-modes.
- **CRUSH density-decoupled** (0.3.49) — final state: CRUSH coverage
  is glitch-only, no density coupling.

Rest of original plan (verification procedures, file references,
memory rules) still applies. Implementation order shipped: Phase
3a (0.3.28) → Phase 3b (0.3.30) → Phase 3c (0.3.40) → tuning
rounds (0.3.31..0.3.50).

For G5/G6 design rationale specifically, see
`planning/network-g5-g6-plan.md`.
