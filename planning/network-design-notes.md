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

Network's current trajectory is firmly on path 1. Adding path 2 as
a parallel signal with a macro-crossfade is exactly the
Plexiphon-axis goal from the original brief.

This could land as a `mode` or `character` parameter that morphs
between:
- 0.0: glitch — S&H tap positions, probabilistic mute, granular
  reads, no smoothing
- 1.0: lush — current state + per-tap pitch shift + per-tap delay
  modulation rates

---

## Concrete next-action queue (in priority order)

Based on the PDF's "highest leverage" ranking and our current state:

1. **If 0.3.21 still has audible impulses**: run the per-sample
   d(delay)/dt + d(pan)/dt diagnostic. We've removed magnitude
   modulation but if delay or pan rates are still spiking, those
   are the residual sources. Choose remediation (dual-read, 1/(r+ε)
   spatial fade, etc.) per finding.

2. **Per-tap static pitch offset** (~30 cents range, hash from
   seed). Cheapest move with biggest character impact per the PDF.
   Adds shimmer, breaks integer-ratio comb peaks.

3. **Per-tap LFO rate variation**. Currently all taps share 0.5 Hz;
   give each tap its own rate. Trivial code change, large
   perceptual impact ("synchronized chorus" → "truly choral").

4. **Cross-feedback FDN matrix**. Promote feedback path from
   "sparse selectable recycle" to "proper sparse FDN with unitary
   mixing". Mathematical reverb-tail behavior. Significant
   restructure but high payoff.

5. **Per-tap character module** — pick 1-2 from the PDF's stacking
   list (e.g., per-tap LP filter with seed-derived random cutoffs,
   per-tap soft saturator). Bind to a new `color` parameter.

6. **Glitch path** as a parallel character — second tap-readout
   pipeline using S&H positions + probabilistic gates, crossfaded
   against the lush path via a macro.

Items 1-3 are roughly Phase 2.5 work. Items 4-6 are Phase 3+ scope.

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
