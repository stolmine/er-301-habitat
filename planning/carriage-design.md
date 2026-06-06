# Carriage — dynamics manager + tonal counterweight

Fourth chain-as-unit in the `house` package. First **dynamics
character** entry in the catalog. Sits orthogonal to the three
existing chain units (TickerTape = tape rot color, Lacquer =
rate-domain lo-fi, Filament = filter motion).

Working name: **Carriage**. Open to rename before release, but
the metaphor — "the carriage carries the signal forward, getting
more active the less the signal carries itself" — fits the
identity well enough to ship under.

## Position in catalog

| Unit | Character |
|---|---|
| TickerTape | dark dual-band tape rot |
| Lacquer | rate-domain lo-fi (cut + polish) |
| Filament | filter motion (signal-FM LP + ghost resonance) |
| **Carriage** | **inverse-threshold transient manufacture + tonal distance against engagement** |

None of the existing chain units exposes input dynamics as a
user-controlled axis. Filament tracks dynamics internally (auto-
response, hidden) but the user only sees Cutoff/FM/Bloom knobs.
Carriage makes dynamics the explicit subject.

## The novel mechanic — inverse-threshold engagement

Standard compressor: above threshold → more reduction.
Carriage's inversion: **low dynamic variance → more synthetic
transients + more apparent distance**. The unit gets MORE active
on flat material and LESS active on already-dynamic material.
Self-balancing by construction: as the unit manufactures
transients into flat input, peak rises, engagement drops, the
system backs off.

### Two-envelope formulation

Two asymmetric envelope followers (per `feedback_asymmetric_envelope_follower`),
applied to a Form-selected source signal (see below):

- **Peak envelope** — fast attack ~5 ms, fast release ~50 ms.
  Tracks instantaneous transients.
- **Level envelope** — slow attack ~50 ms, slow release ~500 ms.
  Tracks program level.

Engagement:

```cpp
double ratio = peakEnv / (levelEnv + 1.0e-6);  // ratio ≥ 1 typical
// engagement is "how flat is this": 1 when ratio ≈ 1 (perfectly steady),
// → 0 as ratio grows (transients present).
double engagement = 1.0 / (1.0 + 4.0 * (ratio - 1.0));
if (engagement < 0.0) engagement = 0.0;
if (engagement > 1.0) engagement = 1.0;
```

The `4.0` slope constant tunes how quickly engagement falls off
as ratio grows. Tunable per audition.

Cheap (~10 cycles/sample + 2 alphas), bounded by construction,
no extra knob exposed.

## Form blend — envelope source selection

User-facing **Form** knob selects what the engagement detector
sees:

- **Form = 0.0**: raw input amplitude. Engagement responds to
  level dynamics directly.
- **Form = 0.5**: 50/50 blend of raw amplitude and trajectory
  delta. Engagement responds to both level dynamics and shape
  changes.
- **Form = 1.0**: pure trajectory delta. Engagement responds
  only to *kinks* (sharp changes in direction), ignoring overall
  level. A steady sine becomes "flat" no matter how loud.

Trajectory delta math (5-sample window, Cojones-inspired):

```cpp
// Per sample
trajHistory[idx++ & 7] = in;
double avgPrev = (trajHistory[(idx-1)&7] + trajHistory[(idx-2)&7]
                + trajHistory[(idx-3)&7] + trajHistory[(idx-4)&7]
                + trajHistory[(idx-5)&7]) * 0.2;
double predicted = avgPrev + (avgPrev - trajHistory[(idx-5)&7]);
double trajDelta = fabs(in - predicted);

double envSrc = (1.0 - formKnob) * fabs(in)
              + formKnob * trajDelta * 2.0;
```

The `2.0` on `trajDelta` is a normalization scalar — trajectory
deltas are smaller in magnitude than raw amplitude. Tune per
audition.

## Input-output competition feedback (default-on)

Replace the engagement source with `|envSrc - delayedOutput|`,
where `delayedOutput` is the unit's own post-processing output
delayed by a small N samples. **Spiral-wrap the delayed output
before differencing**, per `feedback_spiral_feedback_governor`,
because this is a feedback path.

```cpp
// Per sample
double governedFeedback = spiralFastSaturate(prevOutputDelayed, 1.0);
double competitionSrc = fabs(envSrc - governedFeedback);
// Drive peak + level envelopes from competitionSrc instead of envSrc.
```

Behavior:

- When the unit's manufactured transients **match input
  character** (good prediction): the delta is small → engagement
  stays high → keeps working.
- When input has natural transients the unit didn't predict
  (real percussion arriving): the delta is large → engagement
  drops → backs off and lets the natural transients through.

This converts the unit into a system that pushes harder when
it's *losing the race* against input character. Pairs naturally
with the inverse-threshold formulation; the two reinforce each
other.

**Delay length**: 8 samples is enough to break the per-sample
algebraic loop (allows pipelined execution) without introducing
audible latency. Could increase to 16-32 if the algebraic loop
proves unstable in practice.

**Why this is safe by construction**: Spiral wrap bounds
`governedFeedback` to ±1; even if the difference grows large,
it's bounded by `|envSrc| + 1` per sample, which the envelope
followers smooth aggressively. No unbounded states.

## Atoms — what's existing, what needs porting

| Atom | Status | Role |
|---|---|---|
| `Console0Channel` / `Console0Buss` | EXISTING (used in all 3 chain units) | input drive + level-dependent containment |
| `Spiral` | EXISTING (header-only) | feedback governor on competition path |
| ChainMix logic | INLINE (existing pattern) | dry/wet at output |
| `PointMono` | **NEW** | transient designer — port from AW Point |
| `Distance2Mono` | **NEW** | air absorption + slew cascade — port from AW Distance2 |
| Trajectory delta math | INLINE | small (~10 cycles, 8-sample circular buffer per side) |
| Dual-asymmetric env follower | INLINE | small (~5 cycles + 2 doubles per side) |

Per `feedback_atoms_as_components`, `PointMono` and
`Distance2Mono` ship as C++-only helpers (no Lua units). Each
is a complete character processor in its own right but better
exposed via composite units; promote if standalone demand
arises.

## Signal flow

```
in L,R
  │
  ├──────────────────────────────► dry path (to ChainMix)
  │
  ▼
Console0Channel (Drive)
  │
  ▼
PointMono — transient injection
  │         (B knob driven by engagement × Reach × bipolar offset)
  │         (C knob driven by user Reach for time scale)
  ▼
Distance2Mono — air absorption
  │             (A knob driven by Air × engagement, inverse-coupled)
  │             (B fixed; C = Air)
  ▼
Console0Buss
  │
  ├──► to ChainMix wet input
  │
  └──► to feedback delay line (for competition source)
        │
        ▼
       8-sample delay → spiralFastSaturate → competition feedback path
        feeds envelope follower input on NEXT sample

dry + wet → ChainMix (Mix knob) → out L,R
```

## Knob layout — 5 plies

All continuous, standard coarse/fine encoder stepping (per
existing chain-unit convention).

| Ply | Range | Default | Behavior |
|---|---|---|---|
| **Drive** | 0..1 | 0.5 | Console0Channel + Buss gain, transparent at 0.5. Standard for chain units. |
| **Reach** | 0..1 | 0.6 | Engagement amount. At 0: Point and Distance2 both bypassed regardless of engagement. At 1: full inverse-compressor depth. Couples Point's transient boost gain AND Distance2's slew rigor on a shared curve. |
| **Form** | 0..1 | 0.4 | Envelope source blend (raw level ↔ trajectory delta). At 0.4: mostly tracks level but with some shape sensitivity. |
| **Air** | 0..1 | 0.5 | Distance2 wet amount AND HF darkness ceiling. Engagement scales this internally — at high engagement (flat input), Air's effect is at user-set ceiling; at low engagement (dynamic input), Air's effect is gated down. |
| **Mix** | 0..1 | 1.0 | Standard dry/wet, defaults full wet. |

## AW scalar remaps applied (per `feedback_aw_param_default_subtle`)

**Point** has its B knob centered at 0.5 = identity. We bypass
this by NEVER exposing B directly; engagement always drives a
positive (transient-boost) bias on B. At engagement = 0, Point
becomes near-identity (B ≈ 0.5); at engagement = 1, Point is
strongly in boost regime (B ≈ 0.8). User sees no identity zone.

**Point's C** (time scale) — we expose this via Reach. AW
default C = 0.5 → time scale ~50ms. Carriage maps Reach to a
time scale range that always produces audible transient
shaping: at Reach = 0, scale ≈ 80ms (slow, gentle); at Reach =
1, scale ≈ 8ms (fast, aggressive).

**Distance2's B** (filter darken) — fixed at 0.4 internally.
Provides characterful HF rolloff inside the air axis without
exposing a separate filter knob. Audition target: clear
"distance" without becoming muddy.

**Distance2's wet C** — driven by Air knob × engagement (the
invisible cross-coupling described above).

## Invisible cross-coupling matrix

(Per `feedback_invisible_param_cross_coupling`.)

| Internal variable | Driven by | Curve |
|---|---|---|
| Point's nibnobFactor bias | engagement × Reach | `1.0 + (engagement * Reach * 0.6)` — 0 = identity, 1 = +60% transient gain |
| Point's time scale C | Reach (block-rate) | `0.5 - Reach * 0.35` — Reach high → smaller C → faster time scale |
| Distance2 active wet | Air × engagement | `airKnob * pow(engagement, 0.7)` — softer-than-linear engagement scaling so distance hangs around briefly |
| Distance2 softslew (A) | Air | `0.6 + pow(airKnob, 3) * 24.0` — AW's original cubic curve, preserved |

The Distance2 engagement coupling uses `engagement^0.7` rather
than linear because the air dimension feels right when it
*lingers* slightly after dynamics arrive — a stage rolls off
visually slower than the lights. This is a feel call to audition.

## Tonally-relevant governors (no parameter band-aids)

Per `feedback_no_paths_of_least_resistance` and the user's
explicit ask: don't clamp values, convert misbehavior into
character.

- **Feedback path Spiral wrap** — bounds competition feedback;
  runaway becomes saturating ring.
- **Engagement self-limit** — the inverse-threshold formulation
  is self-balancing by construction. No external limiter
  needed; successful processing reduces engagement.
- **Distance2's slew cascade** is itself a tonally-relevant
  governor for HF energy — clamping rate-of-change creates
  air, not artifacts. Built into the design, not added.
- **Console0 pair** wraps the whole chain — level-dependent
  containment, per mechanic #1 of AW handoff.

## Hot loop layout (per sample, per side)

Sketch (double-precision throughout per Filament precedent):

```cpp
// (a) Console0Channel sat
double samCh = console0SinSat(in * driveGain);

// (b) Trajectory reconstruction (5-sample lookahead reconstruction)
trajBuf[trajIdx & 7] = samCh;
double trajPredicted = computeTrajectoryPrediction(trajBuf, trajIdx);
double trajDelta = fabs(samCh - trajPredicted);
trajIdx++;

// (c) Form-blended envelope source
double envSrc = (1.0 - formKnob) * fabs(samCh)
              + formKnob * trajDelta * formNormScale;

// (d) Input-output competition (default-on)
double governedFb = spiralFastSaturate(fbDelay[fbIdx & 15], 1.0);
double competitionSrc = fabs(envSrc - governedFb);

// (e) Dual asymmetric envelope follower (peak + level)
double absCompetition = competitionSrc;
double peakAlpha  = (absCompetition > peakEnv ) ? 0.999  : 0.99   ;  // ~5ms / ~50ms
double levelAlpha = (absCompetition > levelEnv) ? 0.9999 : 0.99992;  // ~50ms / ~500ms
peakEnv  = peakEnv  * peakAlpha  + absCompetition * (1.0 - peakAlpha );
levelEnv = levelEnv * levelAlpha + absCompetition * (1.0 - levelAlpha);

// (f) Engagement
double ratio = peakEnv / (levelEnv + 1.0e-6);
double engagement = 1.0 / (1.0 + 4.0 * (ratio - 1.0));
if (engagement < 0.0) engagement = 0.0;
if (engagement > 1.0) engagement = 1.0;

// (g) Point — driven by engagement × Reach
double pointBoostBias = 1.0 + engagement * reachKnob * 0.6;
double samPt = pointProcess(samCh, c.pointNibDiv, c.pointNobDiv * pointBoostBias);
// pointProcess updates internal nibA/nibB/nobA/nobB and returns sample * nibnobFactor

// (h) Distance2 — engagement-modulated air
double activeWet = airKnob * pow(engagement, 0.7);
double samDist = distance2Process(samPt, c.distSoftslew, c.distOffsetScale, activeWet);

// (i) Console0Buss desat
double samBuss = console0AsinDesat(samDist);

// (j) Feedback delay write
fbDelay[fbIdx & 15] = samBuss;
fbIdx++;

// (k) Output
double samWet = samBuss * c.busLevelCorrect;
out = mix * samWet + (1.0 - mix) * in;
```

Note: `pow(engagement, 0.7)` per sample is expensive (per
`feedback_cortex_a8_no_double_in_hot_loops`). Replace with
polynomial approximation or a 16-entry LUT indexed by engagement
× 16 — per-sample pow() would dominate the CPU budget. Audition
during Phase 4 tuning.

Block-rate work: precompute `pointNibDiv`, `pointNobDiv * (1.0 +
maxEngagement * reachKnob * 0.6)` ranges, `distSoftslew`,
`distOffsetScale`, alpha values from sample rate.

## Distance2 stage count

AW's 13 stages give a *very* distant, heavily-rolled-off
character. For Carriage's purpose (orthogonal counterweight,
not a maximum-distance effect), 5-7 stages may give better
character at much lower CPU. Reduces per-sample state from
26 doubles (13 stages × 2 sides) to 10-14.

**Audition decision in Phase 3**: start with 7 stages (golden-
ratio thresholds A..G from AW), audition against 13, pick.

Threshold table for 7 stages:
```cpp
static constexpr double kDistThresholds[7] = {
  0.618033988749894,   // A
  0.679837387624884,   // B
  0.747821126387373,   // C
  0.822603239026110,   // D
  0.904863562928721,   // E
  0.995349919221593,   // F
  1.094884911143752    // G
};
```
Scaled by `softslew / overallscale` at block rate.

## Cortex-A8 considerations

- All hot-loop math in `double` per Filament precedent (Console0
  and Capacitor2 ran double cleanly at acceptable CPU).
- Distance2's slew cascade is a 7-deep dependency chain per
  side per sample — completely serial, no NEON opportunity
  (and we don't want vectorization here per `feedback_disable_tree_vectorize_am335x`).
  `mod.mk` already has `-fno-tree-vectorize` for am335x globally.
- `pow(engagement, 0.7)` MUST become LUT or polynomial — see
  hot loop note above.
- Stack-local NEON arrays: NONE (no NEON used here; Distance2
  cascade is inherently scalar, Point is two-sample alternation).
  Avoids the `feedback_neon_intrinsics_drumvoice` trap by
  construction.
- Feedback delay buffer of 16 doubles per side — small enough
  to be class member, fits in L1.
- Trajectory buffer of 8 doubles per side — same.

CPU projection: estimating per sample per side:
- Console0 pair: ~8 cycles
- Trajectory math: ~12 cycles
- Form blend: ~3 cycles
- Spiral wrap: ~10 cycles
- Competition diff + abs: ~3 cycles
- Dual env follower: ~10 cycles
- Engagement formula (replace pow with LUT): ~5 cycles
- Point: ~15 cycles (two leaky integrators + ratio + mult)
- Distance2 (7 stages): ~30 cycles (7 × 4 cycles slew clamp +
  6 × 1 cycle state shift + offset math)
- ChainMix: ~3 cycles
- **Total: ~100 cycles/sample/side, ~200/sample stereo**

At 48k stereo: 200 × 48000 ≈ 9.6M cycles/s. On Cortex-A8 ~720 MHz
≈ 1.3% CPU. Even with overhead and cache misses, target
**~5-8% stereo** — comparable to Lacquer and Filament.

## Implementation phases

**Phase 1 — skeleton + Point port** (~30 min)
- Create `mods/house/atoms/Point.h` with `PointMono` helper
- Skeleton `Carriage.h` Object with 5 plies, Console0 pair, ChainMix, Point inserted
- Static B/C values, no engagement, no feedback path
- Build linux + am335x, install linux, hardware spot-check that Point's transient designer works in isolation

**Phase 2 — engagement detector** (~30 min)
- Add dual asymmetric envelope follower (peak + level)
- Add Form blend with trajectory reconstruction
- Engagement formula (with `1.0 / (1.0 + 4.0 * (ratio - 1.0))` self-limit)
- Wire engagement into Point's nibnobFactor bias
- Build both arches, hardware audition: does flat material get
  manufactured transients? Does dynamic material ride through?

**Phase 3 — Distance2 port** (~45 min)
- `mods/house/atoms/Distance2.h` with `Distance2Mono` helper
- Start with 7 stages, audition vs 13, pick
- Wire into chain post-Point
- Air knob exposes wet × engagement coupling
- Build both arches, audition: does distance feel orthogonal to
  transient activity? Does it linger after dynamics?

**Phase 4 — input-output competition feedback** (~30 min)
- Add 16-sample feedback delay (class-member doubles)
- Spiral-wrap delayed output
- Swap envelope source from `envSrc` to `|envSrc - governedFb|`
- Audition: does the system back off correctly on natural
  transients? Are there any instabilities?

**Phase 5 — tune + ship** (~30-60 min)
- Replace `pow(engagement, 0.7)` with LUT or polynomial
- Verify CPU on hardware
- Tune scalars (engagement slope, normalize for trajDelta,
  curve constants) until character feels right at default
  knob positions
- Per `feedback_aw_param_default_subtle`, validate defaults
  produce immediately audible, characterful behavior
- Write `Carriage.lua`, add to toc.lua, bump PKGVERSION
- Build both arches, install, hardware regression sweep

## Open questions / things to validate

1. **7 vs 13 stage Distance2** — audition decision Phase 3.
2. **Engagement slope constant** (currently 4.0) — tune Phase 5.
   Too steep → discrete on/off behavior. Too shallow → engagement
   never reaches 1 even on truly flat material.
3. **Trajectory normalization scalar** (currently 2.0) — calibrate
   so that Form = 1.0 on a clean sine gives roughly the same
   engagement as Form = 0.0 on the same sine.
4. **Feedback delay length** (currently 8 samples) — increase
   if instability surfaces.
5. **`pow(engagement, 0.7)` replacement** — LUT-16 indexed by
   engagement × 16, or polynomial like `engagement * (0.4 +
   engagement * 0.6)`. Phase 5 decision.
6. **Does Air's engagement-coupling lag feel right?** — `^0.7`
   makes air linger; could go `^0.5` for stronger lingering or
   `^1.0` for tighter coupling. Audition.
7. **Bipolar B exposure?** — current plan: NEVER expose Point's
   bipolar B (always positive boost). Alternative: expose as
   secondary "Tame" mode where engagement drives B negative
   (transient REDUCTION on flat material — a smoother, more
   subdued output). Could be a future v2 mode. Out of v1.

## Memory cross-references

- `feedback_asymmetric_envelope_follower` — dual envelope follower pattern
- `feedback_invisible_param_cross_coupling` — Reach × engagement, Air × engagement
- `feedback_spiral_feedback_governor` — competition feedback path wrap
- `feedback_aw_param_default_subtle` — Point B/C remap, Distance2 B fixing
- `feedback_aw_atom_port_template` — hybrid float defaults, drop dither, drop fpd, memset state
- `feedback_atoms_as_components` — Point + Distance2 ship as C++-only
- `feedback_cortex_a8_no_double_in_hot_loops` — pow → LUT replacement
- `feedback_disable_tree_vectorize_am335x` — am335x flag already set in mod.mk
- `feedback_no_paths_of_least_resistance` — tonally-relevant governors throughout
- `feedback_always_build_both_arches` — linux + am335x every build
- `feedback_linux_build_auto_install` — auto-cp to ~/.od/rear/

## Identity in one sentence

A dynamics character processor that **gets less active the more
the input does on its own** — manufacturing transient bite and
apparent distance into flat material, then receding when the
input arrives with its own character. Composed of three coupled
elements: an inverse-threshold engagement detector, transient
injection (Point), and orthogonal air absorption (Distance2),
with a competition-feedback path that backs off when natural
dynamics arrive.
