# Spatial effect — hybrid topology planning

Status: **planning** (no code yet). Drafted 2026-05-07 from
`spatial-effect-scoping.md` after deciding the hybrid (1)+(3) is the
target direction. CPU expectations recalibrated against Pecto.

Working name **TBD** (no third-party branding per
`feedback_no_third_party_branding`). Package: **catchall** for now —
Visadhara and Ngoma anchor spreadsheet's percussion lineup; the
spatial effect's identity is closer to catchall's experimental tier
where Alembic also lives.

Companion: `planning/refs/spatial-effect-brief.pdf` (original brief),
`planning/spatial-effect-scoping.md` (three-topology survey + initial
analysis).

## What it is, in one sentence

A **multi-tap delay where the per-tap delay/gain/pan values are
generated from a virtual reflector geometry, with sparse selectable
feedback recycling and a single "listener motion" parameter that
translates / rotates the entire field coherently** (Doppler-coupled
on every tap simultaneously through one shared LinearRamp pipeline).

## Architecture sketch

```
                                ┌─────────────────────────────────┐
                                │  Param generator (block-rate)   │
                                │   geometry seed → reflectors    │
                                │   listener pos  → per-tap       │
                                │   density       │ {delay, gain, │
                                │   connectivity  │  pan, fb_w}   │
                                │   soften        │ arrays        │
                                └──────────────┬──────────────────┘
                                               │ (per-block update,
                                               │  smoothed at sample
                                               │  rate via single
                                               │  LinearRamp pipeline
                                               │  on listener_pos)
                                               ▼
in ───►(+)───►[ shared write head ]───►[ delay buffer ]
        ▲                                       │
        │                                       │ N taps
        │                                       │ (geometry-derived)
        │                                       ▼
        │                              ┌──────────────────┐
        │                              │ tap reads (NEON  │
        │                              │ 3-pass gather)   │
        │                              └────┬─────────┬───┘
        │                                   │         │
        │              feedback recycle ◄───┤         │ direct out
        │              (k of N selected,    │         │
        │               weighted sum)       │         ▼
        │                                   │   per-tap pan/gain
        │                                   │   + (optional)
        │                                   │   1-pole LP
        │                                   │   + (optional)
        │                                   │   short modulated
        │                                   │   allpass ("soften")
        │                                   │
        │                                   ▼
        │                              ┌─────────┐
        │                              │ stereo  │
        └──────────────────────────────┤ sum     ├───► out L/R
                                       └─────────┘
```

Three logical layers:

1. **Param generator** (block-rate, scalar): macro params + geometry
   seed → tap parameter arrays. This is where the novelty lives.
2. **Multi-tap DSP** (sample-rate, NEON-vectorized): write head, tap
   reads, feedback recycling, output mix. This is where Pecto's
   infrastructure carries over almost verbatim.
3. **Smoothing pipeline** (sample-rate, scalar LinearRamp on shared
   listener_pos): per-sample interpolation between block-rate target
   tap delays. Single pipeline serves all N taps because all per-tap
   delays are derived from one shared listener-position parameter
   through deterministic geometry.

## Param generator

State (block-rate updates):
- `seed` (uint32) — RNG seed for the reflector field
- `density` (0..1) — fraction of N reflectors active
- `connectivity` (0..1) — fraction of active taps that recycle into
  feedback
- `motion_phase` (0..1) — listener position along a fixed motion path
  through the field (or a 2D vector if we want translate+rotate)
- `field_size` (0..1) — overall scale of the virtual space (controls
  max tap delay)
- `soften` (0..1) — secondary smear knob (allpass jitter / 1-pole LP)
- `decay` (0..1) — feedback gain scaler

Reflector field generation (one-shot per `seed` change):
```
for i in 0..N:
  reflector[i].pos_x = 2*rng() - 1     // [-1, +1] virtual coords
  reflector[i].pos_y = 2*rng() - 1
```

Per-block from macro params (this is the cheap math):
```
listener_pos = motion_path(motion_phase)        // could be linear,
                                                // circular, or
                                                // figure-8
listener_dir = derivative(motion_path, motion_phase)

for i in 0..N:
  if i / N >= density: tap[i].active = false; continue
  dx = reflector[i].pos_x - listener_pos.x
  dy = reflector[i].pos_y - listener_pos.y
  dist = sqrt(dx*dx + dy*dy)
  azimuth = atan2(dy, dx) - heading(listener_dir)

  tap[i].delay_target = field_size * dist * MAX_TAP_DELAY
  tap[i].gain         = 1 / max(dist, MIN_DIST)        // 1/r
  tap[i].pan          = sin(azimuth)                   // -1..+1
  tap[i].fb_weight    = (selected_for_recycle(i, connectivity))
                          ? recycle_gain(i) : 0

LinearRamp.target(tap[i].delay_target)        // single shared ramp
                                              // updates ALL tap
                                              // delays correlated
```

Per-block compute is N × ~15 ops (sqrt + 1/r + atan2 + sin + branch +
stores). At N=32 and 750 blocks/s = 360k ops/s. Negligible.

Caveats:
- `atan2` and `sin` would normally be libm; per
  `feedback_package_trig_lut` we cache them in a LUT or use the same
  Bhaskara approximation as `visadhara_morph::poly_sin`.
- One-shot seed regeneration uses a deterministic LCG (we already
  have one in `visadhara_pmm.h`; pull into a shared util if we want
  to use it elsewhere).

## Multi-tap DSP — Pecto infrastructure carries over

Direct lifts from `mods/spreadsheet/Pecto.cpp`:

- **3-pass NEON delay-tap gather** (`feedback_neon_delay_gather`):
  Pass A computes per-tap idx0/idx1/frac arrays; Pass B does the
  scalar gather (pre-fetched cache lines); Pass C does the fused
  multiply-add into the output. This pattern got Pecto stereo from
  ~50% to ~6% on its own — directly applicable here.
- **`mSmoothedBaseDelay` LinearRamp** (`Pecto.cpp:489-491`,
  `feedback_doppler_basedelay_smoother`): single per-sample one-pole
  LP slew on the shared scalar that drives all per-tap delays. We
  use exactly this pattern with `listener_pos` as the smoothed
  scalar.
- **`idx0 = (int)wrappedP` ulp-edge guard** (`Pecto.cpp:578`,
  `feedback_multitap_idx_wrap_ulp`): when `wrappedP` rounds exactly
  to `maxDelay`, idx0 becomes OOB. Pecto's symmetric guard
  (`if (idx0 >= maxDelay) idx0 = 0`) carries verbatim.
- **Per-tap-position normalized array** (`Pecto.cpp:70-88`,
  `tapPosition[kMaxCombTaps]` in [0, 1]): tap delays scaled by a
  shared `baseDelay` scalar. Same idea here: `tap_delay[i] = field_size
  * dist[i] * MAX_TAP_DELAY`.

What's new vs Pecto:
- Per-tap **pan + gain** instead of just per-tap weight (Pecto sums
  taps with a single per-tap weight; we need stereo pan + amplitude).
- **Selectable feedback** — Pecto recycles all taps through a single
  feedback gain; we recycle a sparse subset of taps with per-tap
  weights. Implementation: precompute `fb_weight[i]` array at block
  rate; per-sample `feedback = sum(tap_out[i] * fb_weight[i])`.

## CPU budget — recalibrated from Pecto

Pecto reference: 24 active taps (out of `kMaxCombTaps = 64`), comb
topology with feedback, single-channel Doppler smoother. **12-20%
stereo on AM335x.**

Per-tap-channel cost ≈ 0.25-0.42% on Pecto's baseline. Linear scaling
estimate for hybrid:

| Tap count | Mono | Stereo |
|---|---|---|
| 16 taps  | 4-7%   | 8-14%   |
| 24 taps  | 6-10%  | 12-20%  (Pecto-equivalent) |
| 32 taps  | 8-13%  | 16-27%  |
| 48 taps  | 12-20% | 24-40%  |
| 64 taps  | 16-27% | 32-53%  |

Adders on top of Pecto baseline:
- **Per-tap pan + gain** instead of single weight: +5-10% relative
  cost (extra L/R multiply and accumulate per tap).
- **Selectable feedback weights**: +5-10% relative (per-tap fb_weight
  multiply + accumulate).
- **Soften — short allpass per tap**: ~6 ops/tap = +20-30% relative
  cost. Optional / disabled unless the soften knob is non-zero.
- **Param generator** (block-rate): negligible (<0.5%).
- **Geometric LUTs (atan2/sin)**: <0.5% block-rate.

**Realistic targets:**
- **MVP**: 32 taps stereo, no soften, single motion path → **~18-25% stereo**.
- **Comfortable**: 24 taps stereo, soften enabled → **~15-22% stereo**.
- **Aggressive**: 64 taps stereo → **~35-50% stereo**, only viable
  with major NEON optimization beyond Pecto's pattern (likely needs a
  4-tap-per-NEON-cycle gather rewrite).

I'd start at **32 taps stereo, no soften** and see where we land
before pushing density up. Soften is a Phase 2 add.

## Adjacent reverb references in the repo

Code we can lift, study, or compare against:

- **`mods/spreadsheet/Pecto.{h,cpp}`** — closest direct cousin. 64-tap
  comb with selectable density, NEON gather, Doppler smoother, ulp
  guard. Lift the multi-tap NEON infrastructure verbatim.
- **`mods/spreadsheet/MultitapDelay.{h,cpp}`** (Petrichor) — 24-tap
  delay with feedback. Different architecture (each tap is a
  standalone delay-with-pan-and-feedback rather than a shared write
  head with multiple read taps), but the per-tap pan/gain plumbing is
  worth studying.
- **`eurorack/rings/dsp/fx/reverb.h`** + **`fx_engine.h`** — Mutable
  Instruments' nested-allpass reverb. 184 lines + 301 lines of FX
  engine. Used by `mi.Rings` and `mi.Clouds`. **Not** what we're
  building (it's a Dattorro-style allpass tank), but a reference for
  how Mutable structures their reverb DSP for embedded ARM. Useful
  if we want to add a "soften" stage that's effectively a small
  diffusion network rather than per-tap allpass jitter.
- **`mods/spreadsheet/Larets.{h,cpp}`** — uses similar 3-pass NEON
  gather pattern; another reference for multitap discipline.
- **`mods/mi/clouds/`** — granular reverb paradigm. Different idea
  but useful if we want to compare textures: granular cloud feels
  similar to dense-reflector-field at high density.

External / commercial precedents we identified in the scoping doc
(for sound-design intuition, not code references):
- Plexiphon, Rainmaker, Sound Particles, Spat Revolution, Liquid
  Sonics Cinematic Rooms, Eventide TimeFactor.

## Macro parameter set (first draft, expect to revise from prototype)

Plies, in priority order. Each is GainBias + CV unless noted.

| # | Ply | Range | Notes |
|---|---|---|---|
| 1 | **density** | 0..1 | Fraction of reflectors active. Glitch↔lush primary axis. |
| 2 | **connectivity** | 0..1 | Fraction of active taps recycling into feedback. Feeds into "Plexus"-flavor morph. |
| 3 | **size** | 0..1 | Field scale = max tap delay. Small = early-reflection-y, large = room-y. |
| 4 | **decay** | 0..1 | Feedback gain scaler (recycled tap amplitude). |
| 5 | **motion** | 0..1 | Listener position along the motion path. CV-able for performance gestures. |
| 6 | **soften** | 0..1 | Per-tap allpass-jitter / 1-pole LP amount. Smooth fallback. |
| 7 | **seed** | int (config menu) | Field regeneration seed. Set-once or per-trigger? TBD. |
| 8 | **input** | gate/audio | Stereo audio in. |

Total: 6 audio/CV plies + 1 input + 1 seed config = comparable to
Pecto's plies. Reasonable density.

Worth considering: **dry/wet** as a top ply or buried in config. Many
modular reverbs put it on a top ply; Pecto has it on the comb ply
(combined with size). Probably top-level for this unit since the
glitch character at low density wants explicit dry/wet control.

## Phase plan

### Phase 0 — Multi-tap baseline (1 week)

Skeleton implementation: 24-tap delay with random tap pattern + Pecto's
NEON gather + Doppler-smoothed shared baseDelay. **No geometry
generator yet** — taps are just at random positions. **No selectable
feedback** — global feedback gain. **Mono.**

Verify CPU sits at ~Pecto-mono levels (6-10%). Verify NEON gather is
working. Verify Doppler smoother behaves cleanly under macro changes.

This is the "infrastructure works" milestone. Lots of code is direct
copy-paste-and-rename from Pecto.

### Phase 1 — Geometry-derived tap parameters (1-2 weeks)

Add the param generator: reflector field, listener_pos motion path,
per-block tap (delay/gain/pan/active) computation. Each tap now has a
spatial location. Listener-position smooth slew via the existing
LinearRamp pipeline.

Add stereo: per-tap pan + L/R sum.

Add LUT for atan2 + sin (or use polynomial approximations). Bench on
hardware.

This is the "spatial coherence" milestone. The unit should sound
distinctly geometric vs. random when comparing to Phase 0.

### Phase 2 — Selectable feedback recycling (1 week)

Add per-tap fb_weight array. Connectivity macro selects k of N taps
for recycling. Per-sample feedback = sum(tap_out * fb_weight). Verify
stability across the connectivity range (per-tap recycling can blow
up if total feedback gain exceeds 1; clamp the sum or normalize).

Add density × connectivity as the headline morph axis. A/B against
classic monolithic feedback.

This is the "Plexus" milestone.

### Phase 3 — Soften + polish (1 week)

Per-tap short modulated allpass (~6 ops/tap, gated by soften > 0).
Per-tap 1-pole LP for high-freq damping. Listening polish.

Custom viz: a small 2D field showing the reflector positions, the
listener trajectory, and which taps are currently feeding back.
Header-only inline per `feedback_no_out_of_line_virtuals`.

Hardware CPU profile under worst case (max density + stereo +
soften). Trim if necessary.

### Phase 4 — MVP ship

Test procedures, vanilla compatibility check, package version bump,
release notes.

Total estimate: 4-5 weeks of focused work. Realistically 2-3 months
of part-time work.

## Open design questions

1. **Motion path topology**: linear translate? Circular orbit?
   Figure-8? Manual 2D position via two CV inputs? The motion path
   shapes the gestural feel. Start with circular orbit (single
   `motion` macro = phase around the orbit); revisit during
   prototyping.
2. **Tap allocation policy**: at density=0.5, which 16 of the 32
   reflectors are active? Distance-from-listener (closest first)?
   Random seeded subset? Energy-weighted? Probably "closest first"
   gives the most musical behavior — denser at low density too.
3. **Feedback selection policy** at connectivity=0.5: which 8 of the
   16 active taps recycle? Random? Distance-weighted? Pattern-based
   (every other)? Same investigation as (2).
4. **Stereo input**: single mono input panned by tap geometry, or
   stereo input where each channel feeds half the field? Probably
   mono in, stereo out to start (matches modular convention).
5. **Seed regeneration trigger**: random per-trigger button? Set-once
   in config? CV-trigger-able? Affects whether the field feels
   "frozen" or "live."
6. **Anti-aliasing on tap-delay modulation**: linear interp at
   minimum (Pecto pattern); cubic might be needed for the listener-
   motion gestures since they slew the tap delays continuously and
   any aliasing surfaces as zipper.
7. **Field topology**: 2D point field is the brief's recommendation.
   Could explore alternatives — 3D with elevation (more natural HRTF
   feel but adds compute), 1D ring (simpler), graph/network with
   actual edges (heavier).

## Risk catalog

- **CPU overrun**: extrapolated 18-25% stereo at 32 taps is workable
  but doesn't leave headroom for other DSP on the same chain. If
  hardware profile comes in higher, drop to 24 taps or simplify
  per-tap ops.
- **Aliasing on listener motion**: continuous per-tap delay
  modulation through the LinearRamp; need to verify the multitap
  idx-wrap ulp guard catches it (`feedback_multitap_idx_wrap_ulp`).
  Pecto's pattern handles this.
- **Feedback stability**: sparse selectable feedback can be
  pathological if the selected taps have collinear delay positions
  (regular comb-filter ringing). Normalize feedback gain by
  `1/sqrt(k)` or similar; clamp total feedback at <1.
- **Mode-switch click**: changing density / connectivity reshuffles
  which taps are active. If a tap is silenced by becoming inactive,
  its current state contribution drops abruptly → click. Mitigation:
  short crossfade (~5ms) on per-tap activation.
- **Param generator latency**: at block boundaries the per-tap
  arrays jump to new values. Single LinearRamp on listener_pos slews
  the shared scalar but the *geometry* (which tap is at which
  reflector) doesn't slew — only the scaling does. Should be OK at
  block rate but worth listening for during Phase 1.

## Non-goals

- **Convolution-based reverb**: ruled out by AM335x CPU budget per
  the brief.
- **3D HRTF spatialization**: too compute-heavy for this platform.
  2D field is the pragmatic stopping point.
- **Beyond-N=64 tap counts**: would require NEON optimization beyond
  what Pecto's 3-pass pattern delivers. Park as a redesign-tier
  ambition.
- **User-editable per-tap state**: the brief is explicit — tap
  pattern is generated, not user-edited. Do not provide per-tap
  controls.

## Cross-references

- `planning/spatial-effect-scoping.md` — three-topology survey.
- `planning/refs/spatial-effect-brief.pdf` — original design brief.
- `mods/spreadsheet/Pecto.{h,cpp}` — primary code reference.
- `eurorack/rings/dsp/fx/reverb.h`, `fx_engine.h` — Mutable's
  nested-allpass reverb (reference, not blueprint).
- `feedback_neon_delay_gather.md` — multi-tap NEON 3-pass pattern.
- `feedback_doppler_basedelay_smoother.md` — single-pipeline
  LinearRamp pattern.
- `feedback_multitap_idx_wrap_ulp.md` — multitap idx-wrap edge guard.
- `feedback_package_trig_lut.md` — sin/cos/atan2 LUT discipline.
- `feedback_no_out_of_line_virtuals.md` — class shape rule.
- `feedback_no_third_party_branding.md` — naming.
- `feedback_persist_plans_to_repo.md` — why this doc exists in repo.
