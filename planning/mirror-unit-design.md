# Mirror — Aliasing-Paradigm Complex Osc Design

**Status:** design (2026-06-16). Working codename: **Mirror**. Final
habitat name deferred until sound is locked.

**Package:** spreadsheet (matches the home of other complex voices —
JF, Alembic, Ngoma, Helicase, Visadhara, Rauschen).

**Source paradigm:** `project_alias_synthesis_paradigm` memory. Mirror
is the proof-of-concept unit that should demonstrate "intentional
Nyquist folding as a synthesis paradigm." If Mirror is musically
compelling, the MirrorBlock atom underneath it becomes the foundation
for a small family of aliasing-mechanic units. If Mirror is not
compelling, the paradigm needs more thought before building a family.

**Chassis:** fork of Helicase. Carrier + modulator + sync + shaper
pipeline structure preserved. 2× OS stripped. Shaper bank replaced
with a continuous morph. Mirror block added between shaper and output.
Inherits Helicase's polyBLEP edge handling, DC HPF, sync detector
mechanism.

---

## Defining mechanic (one sentence)

Generate above-Nyquist content, then explicitly control how it folds
back via the Mirror block — the inverse of every other synthesis
paradigm (which either generates in-band content additively or filters
in-band content subtractively).

The Mirror block lives between the shaper and the output: it
downsamples to a user-controlled internal Nyquist, processes there
(passthrough is the simplest case), then upsamples — with **no
anti-aliasing filters at either boundary**. The folded content IS the
sound.

Phase threshold sync (Helicase-style) anchors pitch even when Mirror
is in chaotic territory. The Sync Threshold knob has lock zones (1:1,
3:2, 2:1, etc.) where carrier hard-syncs to the modulator at exact
integer ratios — pitch tracks V/Oct cleanly. Between lock zones, the
carrier resets at non-musical points per mod cycle, generating
controlled inharmonic content. The threshold knob is therefore the
primary chaos/lock axis and the primary character control.

---

## Signal flow

```
  V/Oct ──→ [Pitch ply] ──→ f0
                              │
        ┌─────────────────────┼─────────────────────┐
        │                     │                     │
        ▼                     ▼                     ▼
   ┌────────┐         ┌──────────────┐      ┌──────────────┐
   │ Mod φ  │         │  Carrier φL  │      │  Carrier φR  │
   │ (sine, │         │  (phase A)   │      │  (phase A +  │
   │  fixed)│         │              │      │   stereo Δφ) │
   └───┬────┘         └──────┬───────┘      └──────┬───────┘
       │                     │                     │
       │   ┌─── Sync Threshold detect ───┐         │
       │   │ (φmod crosses threshold)    │         │
       └───┘                             │         │
                                         ▼         ▼
                          ┌─── reset φL and φR ───┐
                          │  (also resets Mirror   │
                          │   counter if sub=ON)   │
                          └────────┬──────────────┘
                                   │
       ┌───────────────────────────┼───────────────────────────┐
       │                           │                           │
       ▼                           ▼                           ▼
   shared                  ┌──────────────┐           ┌──────────────┐
   Mod osc                 │  Source morph│           │  Source morph│
   (Mod Depth, ──FM──▶     │   (L) + Push │           │   (R) + Push │
    Mod Ratio)             └──────┬───────┘           └──────┬───────┘
                                  │                          │
                                  ▼                          ▼
                          ┌──────────────┐           ┌──────────────┐
                          │ Mirror block │           │ Mirror block │
                          │     (L)      │           │     (R)      │
                          └──────┬───────┘           └──────┬───────┘
                                 │                          │
                                 ▼                          ▼
                              Out L                       Out R

  Sub-outs (mono, tapped from L path / shared):
   • Clean : L Source post-morph, pre-Mirror (bandlimited reference)
   • Fold  : L Mirror output minus L Clean (alias residual)
   • Sync  : gate firing on Sync Threshold crossings
   • Mod   : raw mod osc audio
```

Stereo phase offset Δφ is a function of the Sync Threshold knob
position. At a lock zone Δφ = 0 (mono); between lock zones Δφ rises
toward π/2 (wide stereo). See "Stereo" below.

---

## Controls (top-level plies)

| Ply | Type | Default | Range | Notes |
|---|---|---|---|---|
| Pitch | GainBias (V/Oct) | A4 (440 Hz) | C-2..C8 | Standard pitch ply. Bias = base pitch, gain = V/Oct CV scaling. |
| Source | GainBias | 0.0 (sine) | 0..1 | Continuous morph through 2-3 source shapes. Specific shapes TBD in prototype audition. |
| Push | GainBias | 0.3 | 0..1 | Drive into shaper. At 0 = clean source, at 1 = saturated/heavy. |
| Mod Depth | GainBias | 0.5 | 0..1 | Modulator strength into carrier FM path. |
| Sync Threshold | GainBias | 0.0 (1:1 lock) | 0..1 | **Primary character knob.** Cubic-around-integer-ratios mapping (see below). Lock zones at 1:1, 3:2, 2:1, 5:2, 3:1. |
| Mirror | GainBias | 0.0 (1× = no folding) | 0..1 | Continuous exponential mapping. 0 = host SR (no folding), 1 = host SR / 16 (heavy folding). Final divisor floor TBD. |

**Mod Ratio**: lives on Mod Depth's shift sub-display. Default 1:1.
Range 0.25..8.0 (sub-harmonic to several harmonics above). Discrete
snap to musical ratios as a sub-option (TBD).

**Mirror Reset Switch**: lives on Mirror's shift sub-display. Default
ON (Mirror counter resets on every sync edge → locked alias pattern,
instrument character). OFF → free-running Mirror counter (textural
character, alias drifts across notes).

Total: 6 top-level plies, 2 sub-display secondary controls. Tight for
a complex voice.

---

## Inlets / outlets

### Inlets (2 dedicated)

- **V/Oct** — audio-rate pitch CV
- **FM** — audio-rate FM into carrier

All other knobs (Source, Push, Mod Depth, Sync Threshold, Mirror)
accept CV via standard ParameterAdapter — no dedicated inlets for
them. Mod Ratio + Mirror Reset are sub-display controls and likewise
modulatable through their parameter adapters.

No external sync input. Sync is always internal-mod-driven.

### Outlets (6 total)

| Out | Signal | Notes |
|---|---|---|
| 1 | **Out L** | Main stereo left. Carrier L → Source L → Push → Mirror L. |
| 2 | **Out R** | Main stereo right. Carrier R (phase-offset) → Source R → Push → Mirror R. |
| 3 | **Clean** | L carrier path tapped at Source-morph output, pre-Mirror. Bandlimited reference. Self-patch into FM for clean carrier-feedback structures. |
| 4 | **Fold** | L Mirror output minus Clean. Alias residual only. Self-patch into FM for cascading inharmonic feedback (the paradigm's "Fold" outlet). |
| 5 | **Sync** | Gate signal. Rises to 1.0 on each Sync Threshold crossing, falls to 0 between. Use as clock source for downstream sequencers/envelopes. |
| 6 | **Mod** | Raw internal modulator sine, audio rate at f0 × Mod Ratio. Self-patch as a sync'd modulation source for other units. |

Per `unitOutputNames` memory: stolmine firmware auto-generates Out1..
Out99 labels post-2026-04-30, so 6 outs is supported without table
extension.

Multi-out picker pattern (per `multi_output_framework` memory):
`args.channelCount = 6`, `args.subOutLabels = { "Out", "Out R",
"Clean", "Fold", "Sync", "Mod" }`. Constructor must `addOutput()` for
every outlet (per `addOutput_required_for_multiout` memory — Canals
2.7.1.16 lesson).

---

## Sync Threshold detail (the heavy-lifting knob)

The modulator phase accumulator runs at `f0 × ModRatio`. On every
sample, check whether `φ_mod` crossed the threshold value `T`. If so:
- Reset `φL` and `φR` to 0 (or to a small fixed offset)
- If Mirror Reset sub = ON, reset Mirror block counter
- Emit a tick on the Sync outlet (1-sample pulse)

### Cubic-around-locks knob mapping

Linear `T` is wrong perceptually — lock zones at integer ratios become
infinitely thin needles. We want lock zones to widen into sticky
plateaus along the knob travel, with smooth-but-chaotic transitions
between them.

The mapping shape, for v1:

```
knob ∈ [0, 1]
  ↓
nominal ratio R(knob): piecewise linear through anchor ratios
  knob = 0.00 → R = 1:1
  knob = 0.25 → R = 3:2
  knob = 0.50 → R = 2:1
  knob = 0.75 → R = 5:2
  knob = 1.00 → R = 3:1

Then apply cubic squish around each anchor:
  local_offset = (knob - nearest_anchor_knob) / segment_half_width
  squished    = local_offset³                       // cubic
  T(knob)     = T_anchor + segment_half_T * squished
```

Effect: knob movement near an anchor barely changes T (sticky lock
plateau). Knob movement away from an anchor accelerates rapidly toward
the next anchor (smooth chaos transition).

Anchor ratios for v1: 1:1, 3:2, 2:1, 5:2, 3:1. Five lock zones cover
the most useful complex-osc territory; finer subdivisions can be added
in v2 if user feedback wants them.

### Sync emission shape

The Sync outlet emits a 1-sample positive pulse on every crossing.
Per `comparator_gate_threshold` memory, downstream gate consumers
threshold `> 0.5f`, so the pulse height of 1.0 is unambiguous.

---

## Mirror block detail

```cpp
struct MirrorBlock {
  int divisor;          // 1, 2, 3, ..., DIVISOR_MAX (~16)
  int counter;          // 0 .. divisor-1
  float held_sample;    // last sampled value at counter==0

  inline float tick(float in) {
    if (counter == 0) {
      held_sample = in;
    }
    counter++;
    if (counter >= divisor) counter = 0;
    return held_sample;  // upsample by hold (no anti-alias)
  }

  inline void reset_counter() {
    counter = 0;
  }
};
```

That's it. The Mirror block is literally a divider-clocked S&H with no
filters. The "downsample to internal Nyquist of host_SR / divisor" and
"upsample with hold (= zero-order interpolation)" are both done by the
single S&H step.

**Why this is the paradigm**: a standard decimator applies an
anti-alias LP filter before sampling, and a standard interpolator
applies an LP after holding. Mirror does neither. The aliased products
from downsampling and the imaging products from holding both survive
into the output spectrum. They fold around `host_SR / (2 × divisor)`
and around multiples of `host_SR / divisor` respectively.

### Mirror knob mapping

```
knob ∈ [0, 1] → divisor ∈ [1, DIVISOR_MAX]
divisor(knob) = round( exp( knob × ln(DIVISOR_MAX) ) )
```

DIVISOR_MAX for v1: 16. Picked by audition — large enough for heavy
folding, small enough that the resulting fold pattern is still
musical. Adjust during prototype tuning.

### Reset-on-sync

When the sub-display switch is ON (default), `counter` resets to 0 on
every sync edge. This means each carrier cycle starts with a fresh
sample at the Mirror block's hold buffer — the alias pattern repeats
with the sync rate, so fold landings re-occur at known pitch
intervals. Strongly pitched, tonal, instrument-like character.

When OFF, `counter` runs free. The alias pattern drifts inside each
carrier cycle; every note has a slightly different fold structure.
Textural, aleatory character.

---

## Stereo

Two true carrier pipelines run in parallel. Each has its own phase
accumulator (`φL`, `φR`), Source morph state, Push state, Mirror
block. They share: pitch (V/Oct), modulator (φ_mod, depth, ratio),
sync detector, all knob values, Mirror counter reset (both reset on
same sync edge).

The stereo phase offset Δφ is derived from the Sync Threshold knob:

```
At a lock zone:        Δφ = 0     → φR = φL    → MONO image
Mid-chaos position:    Δφ = π/2   → φR = φL+π/2 → WIDE image
```

Specifically, Δφ uses the cubic-around-locks shape, **inverted**:
locks → Δφ = 0, chaos peaks → Δφ = π/2.

```
distance_from_nearest_lock = |knob - nearest_anchor_knob|
normalized_dist            = distance_from_nearest_lock / segment_half_width  ∈ [0, 1]
Δφ                         = (π/2) × (1 - (1 - normalized_dist)³)
```

This gives:
- Lock plateaus: Δφ stays near 0 (mono)
- Chaos zones: Δφ rises smoothly toward π/2 (wide)
- Maximum width at the exact midpoint between two anchors

The result: lock zones sound mono-anchored center, chaos zones spread
wide, and finding a lock collapses the image back to center. Stereo
width *is* the chaos axis. Single coherent musical handle.

### Why two true carriers (not all-pass or delay)

Discussed in the Round 3 interview. Summary:
- All-pass phase shifter is frequency-dependent → "phase-y" character,
  not clean stereo image
- Sample-delay produces comb-filtering on Mirror's fold discontinuities
  → would sound like cheap chorus on every fold event
- Two true carriers cost ~1.8× single-voice CPU but produce a clean
  stereo voice with consistent character L vs R

CPU implications discussed in "Implementation" below.

---

## Helicase chassis: what we keep, what we strip

### Keep

- Carrier + modulator + sync structure
- Phase threshold sync detector mechanism (with our cubic-around-locks
  knob mapping replacing Helicase's simpler shape)
- polyBLEP edge handling on the carrier sync reset (carrier phase
  resets are step discontinuities → broadband splatter without BLEP).
  Mirror's downsample-and-hold steps are *intentional* fold sources
  and **do not** get BLEP'd
- DC HPF (`feedback_helicase_dc_offset` per todo.md) — engaged when
  f0 > 1 Hz, bypassed for LFO use, ~40 ms settling on shape changes.
  Reuse verbatim
- Carrier shape fine-step `(1, 0.02, 0.001, 0.0001)` per Helicase's
  morph audit
- Smoothed-abs (`sqrt(s² + ε²)`) pattern for any V-corner shape used
  in the morph (per `feedback_helicase_shape_15` lessons)

### Strip

- **2× oversampling.** Mirror is the *anti*-OS unit. Pre-output OS
  on the inner loop would defeat the paradigm. Mod+feedback+FM+carrier+
  shaper all run at native rate
- **The full 16-shape bank.** Replaced with a continuous morph between
  2-3 hand-picked shapes (see below)
- **Helicase's feedback knob.** Mirror has Fold-as-outlet for
  self-patching feedback structures, which is more compositional than
  an internal feedback knob

### Replace

- Shape bank → Source morph (sine → poly_n → FM pair, or similar
  triplet, picked during prototype audition)

### Source morph: starting hypothesis

For v1 audition, propose three shapes interpolated by the Source knob:

| Source knob | Shape | Harmonic character |
|---|---|---|
| 0.0 | Sine | Single sinusoid. Low fold content. Mirror at high divisor produces only the carrier itself folded with itself. |
| 0.5 | `poly3 + drive` | `x = sin(φ); s = (3x − x³); s ← s * Push` — Chebyshev-like odd-harmonic generator. Drive (= Push) controls harmonic count. Lots of fold content available. |
| 1.0 | FM pair | `s = sin(φ + index × sin(φ × mod_ratio))` — classic 2-op FM. `index` driven by Push. Spectrally rich, partials extend well above Nyquist. |

Linear interpolation between shapes for v1; non-linear shape morphs
(weighted blends, phase-aligned transitions) only if linear sounds
muddy.

Per `feedback_no_paths_of_least_resistance`: confirm these three are
right by audition; don't assume them.

---

## Visualization (per-ply graphics)

Ambition: every ply has its own dedicated viz, each one rendering the
specific role of that knob. Mirror is paradigm-bearing — the viz
should make the paradigm legible at a glance, with each ply telling
its own visual story.

Two anchoring visual references from the design session:

- **Concentric wheels of fire / "biblically accurate angels"** —
  Ezekiel's Ophanim: wheels within wheels, rotating at related rates.
  Visually IDENTICAL to the carrier/modulator phase relationship at
  the heart of this unit. Lock zones = wheels with integer-ratio
  rotation rates (pattern repeats predictably). Chaos zones = wheels
  precessing past each other (pattern never repeats cleanly). Natural
  home: **Sync Threshold ply**.
- **Waveshapes wrapped around a sphere / rubber band ball** —
  abstract paradigm-bearing object, beautiful at 4-bit depth, each
  shape wrapping the sphere at a specific frequency. Mirror divisor =
  how many times the shape wraps before returning. Natural home:
  **Mirror ply**.

### Per-ply viz map (first-pass)

| Ply | Viz | Rendering hint |
|---|---|---|
| Pitch | Linear scrolling waveform of the current Out L signal. Standard waveform tile. | Single-pass `pixel()` writes for bright dots; `fill()` for bg tile per FB blend rules. |
| Source | The current source shape itself, drawn as one cycle. Morph knob deforms the shape live. | Sample one period at viz tile resolution, plot. Anti-pattern: don't re-render the inner DSP at viz rate — sample the existing process() output's last block. |
| Push | The shaper transfer curve at current Push amount. X-axis = input amplitude, Y-axis = output. Sliding a hand-trace of the current input value across the curve. | Curve = polyline of ~32 samples plotted via `pixel()`. Hand-trace = single moving dot. |
| Mod Depth | A spectrum-style display of modulator-induced sidebands forming around the carrier. As depth rises, more sideband dots appear at integer offsets. | Block-rate FFT or analytical sideband count (cheaper). Encode magnitude as brightness. |
| Sync Threshold | **Concentric wheels.** Inner wheel = carrier phase (φL), outer wheel = mod phase (φ_mod). Wheels rotate at their actual rates (scaled to viz framerate). Threshold marker visible on the outer wheel — when modulator crosses it, both wheels flash and a sync mark drops. Lock zones: wheels visually align after one round-trip. Chaos zones: wheels precess endlessly without re-alignment. | LUT for the circular geometry per `feedback_package_trig_lut`. Per-pixel `pixel()` calls for the wheel arcs. Brightness encodes phase position (lit at the leading edge). |
| Mirror | **Wrapped-around-sphere waveshape.** A sphere wireframe (3D projection at 4-bit). The current carrier waveshape is drawn as a great-circle band wrapping the sphere. At Mirror divisor 1: one wrap. At divisor 2: two non-intersecting wraps. At divisor 8: eight wraps forming a dense web. As Mirror rises, the band count grows and the visual becomes the "rubber band ball." Sphere rotates slowly for depth perception. | Pre-computed sphere LUT (vertex positions + projection per frame). Bands rendered as polylines via `pixel()`. Mirror divisor = `divisor` value used at unit's `process()` for the band count. |

### Expanded-view viz (overview when no specific ply focused)

When the user is in the unit's main view (no specific ply selected),
show the **wheels-of-fire** viz as the overview — it conveys the most
about what the unit is doing right now and the lock-vs-chaos position
of the sync axis is the unit's primary character. Optionally overlay
the stereo Δφ as a phase indicator between the two wheels (could be a
spread between L and R wheel centers, encoding stereo width
graphically).

### Stereo-aware vizzing

The wheels-of-fire and rubber-band-ball both have a natural way to
encode stereo:
- Wheels: render two pairs of wheels offset by current Δφ. At lock
  zones the pairs visually merge; at chaos zones they spread.
- Sphere: two bands per wrap, separated by Δφ. Locked = bands fused.
  Wide chaos = bands clearly separated.

### Render constraints (inherited from prior viz work)

- **No out-of-line virtuals** on the graphic subclasses (per
  `feedback_no_out_of_line_virtuals`). Every virtual stays inline in
  the .h. Confirmed via `tools/check-graphic-virtual-defs.sh`.
- **No `line()` / `fill()` for dark-on-bright** (per
  `feedback_framebuffer_blend_vs_set`). Both are bitwise-OR
  (lighten-only). Wheels and sphere bands need per-pixel `pixel()`
  calls to render dark linework on a brightened tile background.
- **Trig LUTs on am335x** (per `feedback_package_trig_lut`) — runtime
  `sinf`/`cosf` from package .so miscompute on hardware. Pre-bake
  cos/sin LUTs of resolution appropriate to each viz (72-entry was
  enough for FilterResponseGraphic; sphere may want 144 for smooth
  wrap).
- **Encoder capture is architectural** (per
  `feedback_viz_encoder_capture_architectural`). UI lag from viz is
  a draw-path structure problem (tile granularity, state cache, time
  slicing) not a CPU problem. Colmatage is the reference; the
  rubber-band-ball viz with multiple wrap bands risks slow draw if
  not tile-cached.
- **List-style graphics need count-reduction clamp** (per
  `feedback_steplist_count_clamp`) — irrelevant for this unit's viz
  (no step lists) but noted.

### Viz CPU/quality tradeoff

Most ambitious viz combination (wheels + sphere on a per-ply basis +
spectrum on Mod Depth) is design overhead, not DSP. The DSP CPU
target (Phase 5) is independent. Viz draws at ~40 Hz (frame rate); a
sphere wireframe with 16 bands and 144-step LUT is on the order of
~2300 `pixel()` calls per frame — well under the per-frame draw
budget. CPU impact is negligible.

The real risk is *design time* — six bespoke graphics is a lot of
authoring. Phase 4 (audition + tune) is where viz authoring happens;
each one is its own ~half-day-to-day of work. Worth it if the unit
ships as the paradigm-bearer.

### Viz prioritization for v1

If we have to ship a subset:
1. **Sync Threshold wheels** — paradigm-defining, most-used knob
2. **Mirror sphere** — paradigm-defining, second most distinctive
3. Source shape (cheap)
4. Push transfer curve (cheap)
5. Mod Depth spectrum (more work)
6. Pitch waveform (most generic)

Could ship just 1+2+3+4 in v1 and add 5+6 in v1.1 if performance
audition reveals viz authoring time is the bottleneck.

---

## Implementation phases

### Phase 1 — MirrorBlock atom

Standalone header-only `od::Object` is not needed since Mirror is
trivial state (one int, one int, one float). Instead: inline the
`MirrorBlock` struct in `Mirror.h` for v1. If a future family of units
wants to share the block, refactor to a header.

Smoke test: a unit-test main that feeds a sine into the block at
various divisors and confirms the spectrum shows the expected fold
landings.

**Don't ship as a stand-alone unit** per `feedback_atoms_as_components`
(Mirror block alone is not user-facing-valuable; only the full unit
is).

### Phase 2 — Mono Mirror unit

Fork `Helicase.cpp/.h` → `Mirror.cpp/.h`. Strip OS layer, replace
shape bank with the morph, add MirrorBlock between shaper and output.
Wire up the four sub-outs (Clean, Fold, Sync, Mod). Single carrier
(no L/R yet). Six top-level plies, two sub-controls.

Verify:
- V/Oct tracks cleanly at lock zones (1:1, 2:1, etc.)
- Threshold knob has audible sticky lock plateaus
- Mirror knob produces audible fold content that grows with the knob
- Source morph audible across travel
- No clicks at sync edges (polyBLEP working)
- No DC drift (HPF working)
- Sub-outs route signal through downstream chain subscriptions
  (multi-out picker fully exercised)

### Phase 3 — Stereo extension

Duplicate the carrier pipeline. Add the stereo Δφ derivation from
Sync Threshold. Add Out R outlet. Test:
- Lock zones sound mono-anchored
- Chaos zones sound wide
- Sync resets both L and R coherently
- CPU is in the expected ~1.8× range vs mono

### Phase 4 — Audition + tune

Run on hardware. Tune:
- The three Source shapes (sine + ??? + ???) until each is musically
  distinct and the morph interpolation feels coherent
- DIVISOR_MAX (start 16, adjust)
- Default positions for each ply
- Anchor ratios (1:1, 3:2, 2:1, 5:2, 3:1 — maybe finer or different
  set)
- Threshold cubic exponent (start 3, adjust)
- Mirror knob exponential map (start log2-spaced, adjust)

### Phase 5 — Optimization (if warranted)

Profile on hardware. If CPU is comfortably <20% stereo, ship as-is.
If above, candidates:
- NEON 4×2 voice-bus pattern for the two carriers (per
  `feedback_neon_voice_bus_template`)
- Block-rate baking of any sample-rate-invariant scalars
- Polynomial replacement for any LUT lookups (Cortex-A8 NEON has no
  gather, per `feedback_neon_no_gather_lut_dsp`)

Per `feedback_disable_tree_vectorize_am335x`, ensure `-fno-tree-
vectorize` is set for the file regardless of optimization phase.

### Phase 6 — Final name + release

Pick the habitat name based on audition character. Drop the "Mirror"
codename. Promote to package release artifact set.

---

## Open details to answer in prototype audition

These are tuning, not architecture. Doc-locked decisions stop here.

- Which 2-3 shapes for Source morph (starting hypothesis above)
- DIVISOR_MAX (starting 16)
- Anchor ratios for Sync Threshold (starting 1:1, 3:2, 2:1, 5:2, 3:1)
- Cubic exponent for threshold and stereo Δφ (starting 3)
- Mirror knob exponential coefficient
- Default positions for each ply
- Mod Ratio default and snap-to-musical-ratios behavior
- Push behavior detail: linear gain into shaper, or knee'd

---

## Risks

1. **Pitched-character doesn't emerge from lock zones.** If the
   threshold lock zones don't produce a clean pitched voice in
   audition, the unit reads as "just chaos" and fails the melodic-
   playable character target. Mitigation: ensure carrier phase resets
   cleanly at lock zones, polyBLEP on resets, DC HPF active.
2. **Stereo width feels arbitrary rather than musical.** If lock-mono /
   chaos-wide doesn't track perceptually with the chaos axis, the
   stereo strategy is a misfire. Mitigation: audition early; if it
   doesn't work, fall back to a fixed L-R offset or no-stereo for
   v1.
3. **Mirror block at heavy divisor produces unmusical noise rather
   than character.** If divisors above ~6 just sound like garbage,
   DIVISOR_MAX needs lowering or the upper end of the knob needs
   compressing. Tune in audition.
4. **CPU explosion from two true carriers.** Mitigation: voice-bus
   NEON if needed. Phase 5 work, not phase 2.
5. **Hardware crash on first insert.** New unit with multi-out picker
   and forked Helicase chassis — risk surface includes
   `addOutput_required_for_multiout` (every outlet), `no_out_of_line
   _virtuals` (any custom Graphics subclass), and
   `disable_tree_vectorize_am335x` (file-level CFLAG). All known and
   guardable.
6. **Paradigm doesn't generalize.** If Mirror sounds great as a
   one-off but the MirrorBlock atom doesn't suggest other compelling
   units, the paradigm fails as a paradigm but Mirror still ships as
   a strong character voice. Acceptable failure mode.

---

## Out of scope for v1

- Filter (canals-style SCF clocked by sync alias content). Original
  proposal. User scoped out: "let's start without a filter and see if
  we need one." Revisit after audition.
- External sync input.
- Polyphony.
- More than three source shapes.
- A separate Mirror knob CV input as a dedicated inlet (uses
  ParameterAdapter like the other knobs).
- Wavetable bank source.
- Buchla 259 cross-coupling (full mod waveshape + own pitch).
- Custom viz beyond a standard waveform display (consider in Phase 4
  if a phase-space or fold-landing display feels valuable).

---

## Related memories / docs

- `project_alias_synthesis_paradigm` — paradigm origin doc, Mirror is
  its proof-of-concept
- `feedback_no_out_of_line_virtuals` — if Mirror gets a custom
  Graphics subclass in Phase 4
- `feedback_addoutput_required_for_multiout` — 6 outlets in
  constructor
- `feedback_disable_tree_vectorize_am335x` — file-level CFLAG
- `feedback_neon_voice_bus_template` — if Phase 5 NEON pass needed
- `feedback_comparator_gate_threshold` — Sync outlet pulse threshold
- `feedback_helicase_dc_offset` (from `Helicase` todo entry) — DC HPF
  pattern to inherit
- `multi_output_framework` + `docs/multi-output-units-author-guide.md`
  — 6-output picker plumbing
- `feedback_aw_param_default_subtle` — for Source morph defaults,
  avoid the "all knobs at 0.5 = identity" trap
- `feedback_no_paths_of_least_resistance` — applies to source morph
  shape selection
- `planning/refs/canals-spreadsheet-redesign.md` — sibling unit doing
  topology-first complex work in the same package

---

## TL;DR

Mirror is a Helicase fork that strips the anti-aliasing layer and adds
a Mirror block (divider-clocked S&H with no AA filters) between the
shaper and output. Sync Threshold knob has cubic-around-locks mapping
(sticky integer-ratio plateaus, smooth chaos in between) and is the
primary character control. Stereo phase offset is derived from
threshold position (locks = mono, chaos = wide). Six outlets (L/R +
Clean + Fold + Sync + Mod). Internal-mod-driven sync only. Defaults
aim for melodic/playable at center positions. Filter is shelved for
v1. CPU budget: prove the paradigm first.

If it works, the paradigm is real and the MirrorBlock atom underneath
becomes the foundation of a small family. If it doesn't, Mirror still
ships as a strong character voice and the paradigm goes back to
parking.
