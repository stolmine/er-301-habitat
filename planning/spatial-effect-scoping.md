# Topology-morphing spatial effect — scoping

Status: **scoping**, idea-stage, no code. Brief drafted 2026-05-07.

Companion reference: `planning/refs/spatial-effect-brief.pdf` (full
design brief, prompt source for this scoping doc).

Working name **TBD** — pick a habitat-style original name when
implementation starts (per `feedback_no_third_party_branding`, no
external-source branding in repo files). Candidates to short-list at
that point: Mandala, Saghana, Lattis, Plexus (loaded), Reverberant.

## What it is

Standalone DSP **spatial effect** for the spreadsheet (or biome /
catchall) package. Explicitly **not a traditional reverb** — framed
as a "macro spatial simulation" / topology-morphing space effect.

Aesthetic:
- **Glitchy and shapeable** at one end (sparse, addressable
  reflectors, individual events perceptible)
- **Lush** at the other end (dense, blurred, textural)
- **Continuous travel** between — same network being structurally
  reconfigured, no mode switching

## Reference points

- **Plexiphon** (Make Noise / Tom Erbe, 2026): single Plexus parameter
  modulates both the *number* of active feedback paths and their
  *entanglement*. Size sets temporal relation between paths
  (delay-time at one extreme, room-size at the other). No mode
  switching.
- **Rainmaker** (Intellijel): 64-tap comb with per-tap addressable
  state. Whipsaw character from tap-as-event identity.
- **Erbe-Verb** (Make Noise): continuous parameterization across
  topology classes via a single perceptual axis.

## Constraints

- **Target**: AM3358 Sitara (Cortex-A8, NEON). Modest CPU budget.
  Rules out convolution, dense allpass tanks, waveguide meshes,
  large-N full FDNs. Sparse matrices, block-diagonal structures,
  short modulated delays only.
- **Macro UI**: a few perceptually meaningful knobs, **not per-tap
  editing**. Tap / reflector pattern is **generated** from macro
  parameters (geometry + seed + distribution), not user-edited.
- **Aesthetic**: glitchy/addressable at low density, textural at
  high density. Distinct events should remain perceptible at the
  sparse end.

## Three candidate topologies (all to be prototyped)

### 1. Selective-feedback multi-tap (Rainmaker++)

- N = 32–64 taps, each with `(delay, gain, sign, pan, optional 1-pole)`.
- Recycle stage selects k of N taps to sum back into the write head
  with sparse weighting.
- Glitchy at small k with rhythmic spacing; lush as k grows and
  spacing densifies.
- Tap pattern selectable: rhythmic / stochastic-velvet / geometric.
- Plexiphon-style morph axis = "tap density × feedback connectivity".

### 2. Micro-FDN cluster lattice

- M small FDNs (each N=2–4, Householder mixing) sparsely cross-coupled.
- Each cluster reads as a discrete reflector group at low connectivity,
  blurs to reverb at high.
- Morph parameter raises both M-active and inter-cluster coupling.
- Each cluster small enough that low-connectivity output reads as
  events, not tail.

### 3. Virtual reflector field (most distinctive — best fit for the brief's "macro spatial simulation" framing)

- N point reflectors in a 2D virtual space.
- Each reflector is a tap with delay ∝ distance, gain ∝ 1/r, pan from
  azimuth.
- Listener-motion parameter translates / rotates through the field,
  producing Doppler (delay-line modulation) and panning shifts as a
  *coherent spatial gesture*.
- Sparse field = glitchy and spatial; dense field with motion =
  textural cloud.
- Doesn't sound like standard reverb because the structural
  assumption is **geometric, not statistical**.

## Working hypothesis

Hybrid of (1) and (3): **multi-tap field where the tap pattern is
generated from a virtual geometry, with sparse selectable feedback
recycling**.

- Rainmaker-style whipsaw from individually addressable reflectors.
- Coherent spatial gestures from listener motion moving all taps
  consistently.
- Cheap implementation — under the hood it's a multi-tap delay with
  a parameter generator on top.

## CPU notes

- 64 taps × (read, gain, pan, 1-pole) at 48 kHz with NEON: well under
  1% of an A8.
- Cost centers to watch:
  - Large mixing matrices (keep sparse / block-diagonal).
  - Modulation on long delays (cache misses — **modulate short delays
    only**, gather memory locality wins on Cortex-A8).
  - High-order allpass chains.

## Glitch ↔ lush axis options (cheap → expensive)

1. **Per-tap or post-bus jitter** (small randomized delay
   perturbation) — cheapest.
2. **Short modulated allpass per tap** (~4 ops/tap).
3. **Topology-density parameter**: active tap count + connectivity —
   heaviest but most musical, changes the *kind* of texture rather
   than blurring a fixed one.

**Recommendation** (per the brief): primary control on density +
connectivity; reserve smear/allpass-jitter as a secondary "soften"
knob.

## Open questions

- Does final topology decision require A/B'ing prototypes of all
  three, or can (3) be skipped if hybrid (1+3) is built directly?
- Macro parameter set TBD until prototypes exist — likely some
  combination of: density, connectivity, size, decay, soften, motion,
  seed.
- Stereo strategy: dual-mono vs. coupled cross-feed (Plexiphon-style
  Couple / Skew) — defer until mono behavior is settled.

## Phase plan (when work begins)

### Phase 0 — Prototyping

Build minimal prototypes of (1), (2), (3) in the ER-301 emulator
environment with placeholder UI. Goal is **audible comparison**, not
optimization. Decision on which topology to develop further is
contingent on listening.

### Phase 1+ TBD

Architecture, signal-flow, macro-parameter set, control surface, viz,
test procedures, hardware CPU profile, release prep — all sequenced
post-prototype-listening.

## Cross-references

- `planning/refs/spatial-effect-brief.pdf` — full design brief.
- `feedback_neon_delay_gather.md` — multi-tap NEON discipline (3-pass
  compute / gather / combine pattern; carries directly to topology 1).
- `feedback_doppler_basedelay_smoother.md` — single-pipeline
  LinearRamp pattern for Doppler smoothing on multitap reads;
  applicable to topology 3's listener-motion path.
- `feedback_multitap_idx_wrap_ulp.md` — multitap idx-wrap edge guard,
  required if continuous read-pointer slew is involved.
- `feedback_no_third_party_branding.md` — name discipline; pick a
  generic / Sanskrit / habitat-flavored name when work starts.
- `feedback_persist_plans_to_repo.md` — why this scoping doc exists
  in the repo.
