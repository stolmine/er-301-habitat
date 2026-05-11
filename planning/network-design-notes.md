# Network — design notes (multitap comb → reverb)

Status: **active reference**. Drafted 2026-05-09, distilling the design
brief at `planning/refs/multitap-comb-design-notes.pdf` against
Network's actual implementation state at catchall 0.3.21.

This doc maps the PDF's two problem framings — close-pass impulses
and "still sounds like a multitap" — to what's been done and what's
queued. Treat it as the canonical reference for "what to do next" on
Network's character/identity work, complementing
`network-implementation-plan.md` (which is the phase-by-phase build
plan).

## Companion doc

`planning/refs/multitap-comb-design-notes.pdf` — the original design
notes from a collaborator. Read for full context. Quoting freely
here under markdown for searchability.

---

## Problem 1: Impulse/click artifacts when listener passes near a tap

**Root cause** (per the brief): "Smoothing bounds parameter *values*
but not *rates of change*. Near a tap, dParam/dt explodes relative to
user motion. Singularities in the math survive smoothing."

This is the math diagnosis we converged on independently across
0.3.16 → 0.3.21 — every "impulse fix" iteration was attacking
symptoms of the same underlying singularity.

### Three culprits (PDF) ↔ Network state

**1. Delay-time modulation singularity.** Near a reflector, read
head can move >1 sample per output sample → clicks under linear
interpolation. Smoothing trades click for chirp.

- **PDF fix**: dual read heads with crossfading ("Doppler-free /
  crossfading delay"). Allpass interpolation helps but is incomplete.
- **Network state**: single read head with linear interpolation.
  We accept Doppler chirps as character; this hasn't been the
  dominant artifact.
- **Action queued**: only if chirps become objectionable. Dual-read
  crossfade adds significant complexity — defer unless audible.

**2. Pan/azimuth singularity.** atan2 to a tap flips ~180° as
listener crosses through it. Scalar smoother on angle either lags
heavily or pops.

- **PDF fix**: smooth pan as a 2D vector (x/y or sin/cos pair) and
  re-derive angle. Or fade spatialization weight by 1/(r+ε) so
  orientation stops mattering near zero.
- **Network state**: we never compute angle at all — pan is
  `dy/dist` directly (the y-component of the unit vector from
  listener to reflector), which is `sin(azimuth)` without going
  through atan2. **This is already the 2D-vector pan-smoothing
  approach**. Pan is bounded ±1, smoothed via gainL/gainR
  smoothers (which are gainL = mag·0.5·(1-pan), gainR =
  mag·0.5·(1+pan)). No atan2 singularity.
- **Status**: ✅ already correct.

**3. Gain cap is a C⁰ corner.** `clamp(k/r, 0, max)` is continuous
but not differentiable.

- **PDF fix**: soft cap, e.g. `max·tanh(k/(r·max))`.
- **Network state**: we removed gain-from-distance entirely in
  0.3.21 — `mag = 0.5` constant per tap. No cap, no corner, no
  differentiability issue. The trade was a hard architectural
  choice (lose distance-amplitude character) but eliminates the
  whole class of impulses.
- **Status**: ✅ resolved by going further than the PDF suggests
  (no cap rather than soft cap).

**Pragmatic fix (PDF)**: enforce minimum radius `r ≥ ε` (e.g. 5% of
field radius) "so the user can't physically pass through a tap.
Removes the singularities; reverbs aren't supposed to let you stand
inside a wall reflection anyway."

- **Network state**: listener orbits at radius **1.3** (outside the
  unit-disc reflector field). Min possible distance to any reflector
  is 0.3 — listener cannot pass through any reflector.
- **Status**: ✅ in place since 0.3.19.

### Diagnostic the PDF recommends (we haven't yet run)

> "Log per-sample delta of each tap's delay length and pan angle
> during the artifact. Whichever spikes is the culprit."

If impulses are still audible after 0.3.21's constant-magnitude
move, this diagnostic is the next step. Build a debug variant that
dumps per-sample d(delay)/dt and d(pan)/dt for the first few taps
to a ring buffer, snapshot during playback, plot offline.

---

## Problem 2: Sounds recognizably like a multitap comb

**Root cause** (per the brief): "All taps are identical-character
copies summed at integer-ish delay relationships, producing
predictable spectral peaks. To break the sound, break (a) similarity
of taps, (b) linearity of the system, or (c) stationarity of the
tap structure."

This is exactly what the user has been asking for ("we are still
listening to what sounds like a multitap comb"). The PDF lays out a
roadmap.

### Highest-leverage moves (PDF ranking) ↔ Network state

**Per-tap pitch shift, ±5–30 cents randomized per tap.**
"Destroys harmonic reinforcement that creates comb peaks. Larger
shifts (fifths, octaves) → shimmer."

- **Network state**: not implemented. We have a single global LFO
  modulating all taps in sync (golden-angle phase offsets give
  some decorrelation, but rate is shared at 0.5 Hz).
- **Action queued**: implement per-tap *static* pitch offsets via
  permanent micro-detune of each tap's delay base. Hash from
  reflector seed to a ±N-cents value. Deterministic per seed.
  Cheap (just modifies the geometry-derived delay target per
  block).

**Per-tap modulated allpass diffusion.**
"Short chain of allpasses per tap with slow random LFO on the
coefficient. Smears each tap's transient so taps stop sounding
like discrete echoes."

- **Network state**: we have ONE 4-stage allpass chain in the
  feedback path, not per-tap. All recycled signal goes through the
  same diffuser.
- **Action queued**: per-tap allpass diffusion is heavyweight
  (~32 × 4 allpass states × ~12ms each). Not free CPU-wise.
  Consider: shared diffusion bank with LFO-modulated coefficients,
  randomly assigned to taps by hash.

**Independent delay-time modulation per tap, different LFO
rates/phases.**
"Even a few samples of wander breaks comb resonance into something
choral."

- **Network state**: we have golden-angle phase offsets on a
  single 0.5 Hz LFO (Rings shimmer pattern). All taps share the
  same RATE — only phases differ.
- **Action queued**: give each tap its own LFO rate (slight
  variation around a base). E.g., per-tap rate = baseHz × (0.8 +
  0.4·LCG_random). Different rates produce *truly* incoherent
  per-tap modulation, breaking the "synchronized chorus" feel.
  Cheap — one additional state float per tap (the per-tap rate),
  computed once at seed-regen time.

### Per-tap character (PDF list)

The PDF's secondary moves, all of which are character expansions
not yet attempted:

- **Random per-tap filtering** (bandpass with random center, lowpass
  with random cutoff, comb-within-comb). Filters drift so timbre
  evolves.
- **Per-tap waveshaping/saturation** with different curves per
  tap. Nonlinearity introduces inharmonic content the comb can't
  predict.
- **Bitcrush / SR-reduce** a subset of taps, especially when
  modulated.
- **Ring-mod a tap** against a slow sine or against another tap.
- **Reverse-buffer reads** on some taps.

These all deserve consideration in a Phase 5+ "character expansion"
sprint. Each adds depth at modest CPU cost. Picking 2-3 to
implement and macro-blending them with `soften` (or a new "color"
parameter) would be the most expressive use of the current
architecture.

### Structural / topological (PDF list)

- **Cross-feedback between taps** (not just to the sum). Sparse
  FDN. With a unitary (Hadamard or Householder) feedback matrix,
  yields proper diffuse reverb tails from the same skeleton.
- **Tap respawn**: taps have lifetimes, fade out and reappear at
  new random positions.
- **Tap probability** as a function of position — dense regions
  vs sparse regions.

The cross-feedback FDN move is the heaviest but most rewarding —
turns a multitap into a proper diffuse reverb structurally. Worth
a Phase 3-equivalent design pass.

### Glitch / discontinuous controls (PDF list)

- **S&H on tap positions** at irregular trigger rates — quantum-jump
  topology. Smoothing OFF on these is the point.
- **Per-tap stutter/freeze**: tap latches buffer contents and loops
  for N ms.
- **Probabilistic tap mute/unmute** on a clock.
- **Transient-triggered events**: input transient causes a random
  tap to jump position, freeze, or pitch-shift momentarily.
- **Buffer scrub**: read pointer for a tap occasionally jumps to a
  random position.
- **Granulate at the tap output**: each tap emits grains rather
  than a continuous read.

Glitch direction is what the original Plexiphon-inspired brief
called the "addressable / event-driven" end. Our current Network is
firmly on the lush side; the glitch direction would be a separate
effort.

### Two design philosophies (PDF closing recommendation)

> 1. **Continuous lushness** — pitch shift + allpass diffusion +
>    delay modulation. Sounds like a reverb, no longer like a comb.
> 2. **Glitchy/event-driven** — S&H controls, probabilistic gates,
>    transient-triggered jumps. Sounds alive and unpredictable,
>    still recognizably a multitap.
>
> Running both in parallel and crossfading via a macro gets a huge
> range out of one tap structure.

Network as of 0.3.22 is firmly on the lush side. The eventual
**Character macro** crossfades between lush and glitch pipelines
that share the same underlying delay buffer + tap geometry.

The macro can land as a top-level ply or as the headline `character`
control. 0.0 = pure glitch, 1.0 = pure lush, midpoint = blend.

---

## Lush vs. glitch categorization

Each future move maps to exactly one of three buckets: **Lush**
(turns on with macro→1), **Glitch** (turns on with macro→0), or
**Spanning** (parameter values morph as macro sweeps; same code path
on both sides, different config).

### LUSH side (continuous, smearing, reverb-flavored)

| # | Move | Status | Implementation note |
|---|---|---|---|
| L1 | Per-tap modulated allpass diffusion (per-tap chains, slow LFO on coefficient) | queued | Schroeder/Dattorro per-tap. Heavyweight — ~32 × 4-stage allpass. Could share a diffusion bank pool with random per-tap routing. |
| L2 | Cross-feedback sparse FDN with unitary (Hadamard / Householder) mixing | queued | Promote feedback path from "selectable recycle" to "proper sparse FDN". Mathematical reverb-tail behavior. Heaviest move but biggest payoff. |
| L3 | Per-tap LP filter with seed-derived random cutoffs | queued | One-pole LP per tap. Cheap. Adds timbre evolution. |
| L4 | Existing in-loop allpass diffusion (4-stage Schroeder, ~32ms) | shipped 0.3.16 | Currently mixed by `connectivity`-driven internal soften. Already on lush side. |
| L5 | Existing per-tap shimmer LFO (0.5Hz, golden-angle phases, ±8 samples) | shipped 0.3.19 | Always on. Lush by nature; on glitch side could be silenced or repurposed as S&H source. |
| L6 | Existing dual-read crossfading delay (Doppler-free, smooth blocks) | shipped 0.3.22 | Always on (foundational). Lush by design. On glitch side, could be replaced by single-read with abrupt position jumps. |

### GLITCH side (discontinuous, event-driven, addressable)

| # | Move | Status | Implementation note |
|---|---|---|---|
| G1 | S&H on tap positions at irregular trigger rates | queued | Per-tap LFSR-clocked sample-and-hold of (delay, pan) — quantum-jump topology. Smoothing OFF. Macro=0 enables. |
| G2 | Per-tap stutter / freeze | queued | Tap latches buffer contents and loops a slice for N ms. Per-tap state: latch flag + slice start position + position counter. |
| G3 | Probabilistic tap mute/unmute on clock | queued | Each tap has a probability gate that fires on internal clock divisions. Like Euclidean rhythm across the tap field. |
| G4 | Transient-triggered events | queued | Input transient detector triggers random tap to jump position / freeze / pitch-shift momentarily. |
| G5 | Buffer scrub | queued | Read pointer occasionally jumps to a random position in the delay line for a tap. Per-tap "scrub probability" parameter. |
| G6 | Reverse-buffer reads | queued | Per-tap flag: read pointer decrements instead of incrementing. Plays buffer backwards through that tap. |
| G7 | Tap respawn (lifetimes) | queued | Each tap has a random lifetime; fades out and reappears at a new random position. Topology evolves over time. |
| G8 | Bitcrush / SR-reduce subset of taps | queued | Per-tap bitcrush state (target bit depth, hold counter for downsampling). Selected by hash from tap index. |

### SPANNING (same code path, parameter morphs along the macro axis)

| # | Move | Lush config | Glitch config | Implementation note |
|---|---|---|---|---|
| S1 | Per-tap pitch shift | ±5–30 cents random per tap (PDF's primary lush move) | Per-tap pitch S&H jumping ±semitones at irregular rates | Current LFO is 0.5Hz / ±8 samples = lush. At glitch end, S&H replaces continuous LFO. |
| S2 | Per-tap delay-modulation rate | All taps slow LFO (~0.5 Hz, golden-angle phases) | Per-tap rates jitter wildly (sub-second, divergent) | Single mechanism, macro modulates rate distribution. |
| S3 | Per-tap waveshaping / soft-sat | Gentle soft-clip per tap (subtle nonlinear character) | Hard distortion / random curves per tap (unpredictable timbre per tap) | Per-tap waveshaper with curve-strength macro. |
| S4 | Ring-mod (per tap, per pair, or against slow sine) | Slow sine ring-mod (~5Hz subaudio detune) | Audio-rate ring-mod against random other tap (chaotic coupling) | Frequency parameter morphs with macro. |
| S5 | Tap probability per region of space | Stable density gradient (some regions dense, some sparse) | Jittery density (regions flicker active/inactive at clock rate) | Spatial probability map with rate macro. |
| S6 | Granular tap output | Long grains, dense overlap (continuous texture) | Short grains, sparse trigger (event identity preserved) | Grain size + density macros bound by character. |

### MACRO-INDEPENDENT (always on, both sides)

These shipped or queued items aren't crossfade subjects — they're
foundational infrastructure that runs identically regardless of
character position:

- Phyllotaxis reflector field (geometry)
- Smooth-random listener walker (motion source)
- Constant per-tap magnitude (no distance-amplitude dep)
- Pan from azimuth (`dy/dist`), gainL/R smoothers
- DC blockers (input / fb / stereo output @ 50Hz)
- Two-stage tanh saturation (fb path + buffer-write)
- Sign randomization on fb_weight
- 1/√k feedback normalization
- Per-tap fb_weight dual smoothers
- **Density-compensated tap gain** (queued — not yet shipped).
  Per-tap magnitude scales by `1/√activeTaps` so summed wet RMS
  stays roughly constant as density sweeps. Without this, density
  doubles as a level knob: at high density the wet bus accumulates
  enough energy to push tanh saturation hard (or force the chain
  output to clip), so users currently have to either lower the
  input gain or back off density to avoid clipping. With it,
  density becomes purely a structural / spatial-richness control
  uncoupled from amplitude. One-line fix in the gainScale arg
  passed to `network_geom::recomputeTaps()`.

### Macro architecture options

**Option A: two parallel signal paths.** Lush bus and glitch bus
each compute their own wetL/wetR from the shared delay buffer, with
their own tap-readout strategies. Macro crossfades the buses.

- Pros: clearest separation; each side optimized for its purpose;
  PDF's recommended approach.
- Cons: ~2× CPU at midpoint where both pipelines run; more state.
- Best when: lush and glitch readouts are fundamentally different
  (lush continuous interp vs. glitch S&H jumps).

**Option B: one pipeline with macro-modulated parameters.** Single
tap-readout path; macro modulates parameter values along the
spanning items (S1-S6) and gates lush-only items (L1-L3) /
glitch-only items (G1-G8) with a bias.

- Pros: cheaper CPU, simpler state.
- Cons: extreme ends are less differentiated; some items (S&H vs.
  smooth) don't morph cleanly.

**Recommendation**: hybrid. Single delay buffer + tap geometry
(macro-independent). Two parallel readout pipelines for items where
lush and glitch are fundamentally different (read strategy, time
domain). Single pipeline with macro-modulated parameters for items
where they're the same mechanism with different settings (spanning
S1-S6). Macro biases gating of items in L vs G categories.

---

## Concrete next-action queue (in priority order)

Reorganized by macro side:

### Lush side polish (low-hanging fruit, no macro needed)

1. **S1 lush half: per-tap static pitch offset** (~30 cents range,
   hash from seed). Cheapest move with biggest character impact per
   the PDF. Breaks integer-ratio comb peaks. **Do first.**
2. **S2 lush half: per-tap LFO rate variation**. Currently all taps
   share 0.5 Hz; give each tap its own rate. Trivial code change,
   large perceptual impact. **Do second.**
3. **L3: per-tap LP filter with seed-derived random cutoffs**. Adds
   timbre evolution per tap. One-pole LP, cheap.
4. **L4 already shipped (in-loop allpass diffusion)** — currently
   mixed by `connectivity`. Could expose a separate `diffusion`
   parameter or fold differently into the lush macro.

### Macro framework

5. **Add the Character macro**. Architecture decision (Option A / B
   / Hybrid) needs to be made before Phase 6+ work proceeds. Hybrid
   recommended.

### Glitch side build-out (after macro framework)

6. **G1: S&H on tap positions** — first glitch primitive. Foundation
   for subsequent G items.
7. **G3: probabilistic tap mute/unmute on internal clock**. Cheap,
   immediately characterful.
8. **G2: per-tap stutter / freeze**. State machine per tap.
9. **G4: transient-triggered events**. Input transient detector +
   random tap modulator.
10. **G5/G6: buffer scrub + reverse reads**. Per-tap state flags.
11. **G8: bitcrush/SR-reduce subset of taps**. Per-tap state.
12. **G7: tap respawn (lifetimes)**. Most complex glitch item.

### Heavyweight architectural

13. **L2: cross-feedback sparse FDN with unitary mixing**. The
    biggest reverb-quality move. Significant restructure of the
    feedback path. After macro framework lands.

Items 1-4 are roughly Phase 2.6+ scope (extending Phase 2). Items
5-13 are Phase 3+ scope.

## Cross-references

- `planning/refs/multitap-comb-design-notes.pdf` — original brief.
- `planning/refs/spatial-effect-brief.pdf` — original Plexiphon-
  inspired scoping doc.
- `planning/network-implementation-plan.md` — phase build plan.
- `planning/spatial-effect-hybrid.md` — original hybrid topology
  scoping.
- `mods/catchall/Network.h` — current implementation.
- `mods/catchall/network/geometry.h` — per-tap parameter generator
  (where many of the per-tap modifications would land).

---

## Addendum (2026-05-11): true plexus topologies

The current audio architecture is a **star multitap**: one shared
delay buffer, all taps read in parallel from different positions,
single feedback sum back to the one write head. No tap-to-tap
routing in the audio path. This matches `spatial-effect-hybrid.md`'s
"hybrid" framing (visual metaphor is a plexus; audio is a
multitap with selectable feedback), but it does not literally
route audio through the tap network.

If we ever want the audio to read as a true plexus — signal
actually flowing through the tap graph rather than being summed
across parallel readers — three architectural shapes are on the
table. Parked here for a future session; not on the current
roadmap.

### Option 1: Serial cascade by distance

Taps sorted by distance from listener. Signal enters the nearest
tap, gets delayed by its slice, output feeds the next-nearest
tap, ... up to a `size`-determined final tap (the endpoint of
the path). Other taps are distributed at intermediate distances
along the path in patterns (linear / log / phyllotactic / etc.).

- Each tap becomes its own short delay line. Memory ~ same as
  today if total path length is bounded at ~1s.
- Inherently sequential — kills the NEON 4-wide tap-gather
  optimization that brings Pecto-style multitap to ~6% CPU; we'd
  be back to per-tap inline processing.
- Glitch modes (MUTE/STUTTER/CRUSH/SCRUB/REVERSE) currently apply
  post-sum on a single wet bus. They'd need to become per-tap
  *node* effects, applied to the signal as it passes *through*
  that tap — fundamentally changes the sound design.
- Feedback is no longer a single bus; would need rethinking
  (e.g., feedback per-edge, or a single tail of the cascade
  feeding back to its head).
- Effort: ~1–2 weeks for a clean rewrite + re-audition pass.

### Option 2: Feedback Delay Network (FDN)

N delay lines plus an N×N mixing matrix routes between them.
True mesh; signal can take any path. Most flexible.

- Matrix design is a real DSP problem on its own — sparse vs
  unitary (Hadamard / Householder), how to define "neighborhood",
  how to keep stable feedback under modulation.
- Memory ~ proportional to N × per-line-length. With N=64 and
  modest per-line lengths, feasible.
- Mode/glitch effects again need to map to nodes / edges rather
  than a post-sum bus.
- Effort: significantly bigger than #1. Probably warrants a
  proper design pass against existing FDN literature before
  picking specifics.

### Option 3: Hybrid junction taps

Smallest change. Keep most taps as parallel readers (today's
shared-buffer star). Designate a subset as "junction" taps that
*also write* their modified output back into the buffer at
their own positions, creating local feedback territories inside
the shared buffer.

- Doesn't deliver true 2D routing; signal still doesn't flow
  through the tap network in any directed-graph sense. But it
  *does* create per-junction recirculation that breaks the
  pure-star symmetry and would shift the character toward
  something less uniform.
- Days of work, not weeks. Mostly per-junction writes during
  Pass C, plus stability tuning so junctions don't run away.
- Could be a stepping stone or a permanent middle ground.

### Recommendation if revisiting

- If "plexus character" is the actual goal, Option 1 with the
  `size`-as-endpoint sketch is the right shape for the user's
  framing.
- Option 3 is the cheapest way to introduce some routed
  character without throwing away the current architecture.
- Option 2 is the most musically rich but the riskiest /
  largest scope.
- Whichever direction: write a design doc before code. The
  current architecture is well-tuned and ships; replacing it
  needs a clear sound-design target to aim at.
