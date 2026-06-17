# Mirror — Wavetable Formant Envelope + 1/f Drift

Planning doc for the next two architectural moves on Mirror,
combining research threads 3 (Voss-McCartney 1/f noise) and 4
(Mangrove-style envelope source) per the research notes at
`planning/mirror-research-notes.md`.

**Status:** design (2026-06-16). Builds on the Phase 2 prototype
shipped at spreadsheet 2.7.1.24. Reference: strike's
`osc::Formant` at `~/repos/er-301-custom-units/common/dsp/osc.h:506`.

**Scope:** replace the current Source morph (sine → poly3 →
wavefolder) with a wavetable-of-envelope-shapes driven by an
impulse-retriggered envelope phase accumulator. Add a 1/f noise
generator as a built-in modulation source. Keep everything else
(Sync Threshold + Mirror block + Mod + Stereo plans).

---

## Architectural shift

### Current (2.7.1.24)

```
V/Oct ─→ Mod osc (rate F_voct) ─┬─ Mod wrap → reset Carrier phase
                                │
                                └─ Mod sine → FM index into Carrier
                                              │
                                              ▼
                                         Source morph (sine/T3/wavefold)
                                              │
                                              ▼
                                         Mirror block (S&H divisor)
                                              │
                                              ▼
                                             Out
```

Carrier phase produces audio via Source morph. Source knob picks
shape; Push drives nonlinearity within shape. Static-state
spectrum is whatever Source generates.

### Revised

```
V/Oct ─→ Mod osc (rate F_voct) ─┬─ Mod wrap → trigger envelope retrigger
                                │
                                └─ Mod sine → FM index into Carrier phase
                                              │
                                              ▼
                                         Carrier phase tracks sync timing only
                                              │
                                              ▼
                                         Envelope phase (rate = Formant knob)
                                              │
                                              ▼
                                         Wavetable lookup [Shape knob frame]
                                              │
                                              ▼
                                         Air waveshaper (atan saturation)
                                              │
                                              ▼
                                         Mirror block
                                              │
                                              ▼
                                             Out
```

The envelope wavetable IS the audio signal. The carrier phase
exists only to time sync events for envelope retrigger.

Per-pitch-cycle envelope produces an impulse-train character —
the spectrum is determined by:
- **Shape** (wavetable frame): basic envelope shape, defines
  number of slope discontinuities + general spectral envelope
- **Formant rate**: how many envelope cycles fit per pitch cycle.
  When Formant rate > pitch rate, multiple envelopes per cycle.
  When Formant rate < pitch rate, **undertone series** (pitch
  division) — envelope spans multiple pitch cycles
- **Air**: post-envelope atan saturation, adds odd harmonics

Then Mirror folds whatever the envelope generates.

---

## Component 1 — Wavetable envelope generator

### State

Per-instance scalar state:
- `envPhase` — float in [0, 1+], advanced each sample at envelope
  rate. Wraps at 1.0 (single envelope cycle) or accumulates beyond
  if Formant rate < sync rate (handled per strike's offset logic).
- `prevSyncEdge` — bool, for edge detection

### Wavetable design

LUT size: **16 frames × 256 samples per frame = 16 KB**.

Each frame is a single envelope cycle normalized to [-1, 1] or
[0, 1] (TBD per shape). Stored as `float wavetable[16][256]`,
precomputed once at module init.

#### Frame inventory (16 shapes, simple → exotic)

| Frame | Shape | Spectral character |
|---|---|---|
| 0 | **Square gate** | constant 1.0 for envelope duration; flat spectrum, DC-heavy. Pure gate behavior. |
| 1 | **Saw (instant attack, linear fall)** | strong odd + even harmonics, classic. |
| 2 | **Triangle symmetric** | odd harmonics, smooth. The Mangrove default. |
| 3 | **Exponential decay** | smooth decay, broad spectrum tilted to low freq. |
| 4 | **Half-cosine bell** (`sin(π·t)`) | smooth, low harmonic content. |
| 5 | **Gaussian-like** (`exp(-((t-0.5)/σ)²)`) | smooth peak, very low harmonic content. |
| 6 | **Asymmetric triangle (fast rise, slow fall)** | plucked character. |
| 7 | **Asymmetric reverse (slow rise, fast fall)** | anti-pluck, reverse-cymbal character. |
| 8 | **Two-peak lobed** (e.g. `sin(2π·t) × envelope`) | two formants per envelope cycle → richer spectrum. |
| 9 | **Three-peak lobed** | three formants per cycle. |
| 10 | **Damped sine** (`sin(N·π·t) × exp(-decay·t)`) | introduces an audio-rate sub-resonance inside each envelope. |
| 11 | **Inverse exp** (slow start, accelerating end) | mirror of frame 3. |
| 12 | **Sinc-shaped** (`sin(π·N·t)/(π·N·t)`) | peak + symmetric side lobes — multi-formant. |
| 13 | **Sparse impulse train** (3-5 narrow spikes per envelope) | wide spectrum from spike-trains. |
| 14 | **Half-window** (front half only) | half-bandwidth limited. |
| 15 | **Self-similar fractal pattern** | iterated, gives self-similar spectral motif. |

Order matters — adjacent frames should sound musically related so
linear interpolation between them is smooth. Frame ordering above
is rough; final ordering picked during audition.

### Envelope phase advance + sync retrigger

```cpp
// At each sample:
float envDelta = formantHz * invSr;     // formantHz = envelope rate
envPhase += envDelta;

// On carrier-phase wrap (= sync from mod), retrigger envelope.
// Simple rule for v1: ignore sync if envelope still active.
// → Gives clean envelope-per-impulse when formantHz > syncHz,
//   undertone series (pitch division) when formantHz < syncHz.
if (syncEdge && envPhase >= 1.0f) {
  envPhase = 0.0f;
}

// Look up wavetable
if (envPhase >= 1.0f) {
  envOut = 0.0f;  // envelope completed, silence until next retrigger
} else {
  float sampleIdx = envPhase * 255.0f;
  int   idx0 = (int)sampleIdx;
  float frac = sampleIdx - (float)idx0;
  // Shape knob: 0..1 → frame 0..15 with interpolation
  float frameIdx = shapeKnob * 15.0f;
  int   f0 = (int)frameIdx;
  int   f1 = (f0 < 15) ? f0 + 1 : 15;
  float ffrac = frameIdx - (float)f0;
  float a0 = wavetable[f0][idx0];
  float a1 = wavetable[f0][idx0 + 1 < 256 ? idx0 + 1 : 255];
  float b0 = wavetable[f1][idx0];
  float b1 = wavetable[f1][idx0 + 1 < 256 ? idx0 + 1 : 255];
  float fa = a0 + (a1 - a0) * frac;
  float fb = b0 + (b1 - b0) * frac;
  envOut = fa + (fb - fa) * ffrac;
}
```

CPU: ~12 ops + 4 LUT reads per sample. Negligible.

### Strike's smooth-sync trick (deferred)

Strike's `syncDelta` math (lines 530-554 of osc.h) handles sync
arrival mid-envelope by computing a continuous phase offset so the
envelope doesn't click on restart. v1 uses the simpler "ignore
sync until complete" rule. Click on hard retrigger is bandlimited
by polyBLEP (kept from current code). If audition flags clicks as
musically objectionable, port strike's syncDelta logic in v1.5.

### Plies that change

| Old ply | New ply | Notes |
|---|---|---|
| Source | **Shape** | wavetable position 0..15 across 16 frames |
| Push | **Formant** | envelope rate in Hz (or as ratio of carrier rate, FIXED/FREE mode) |
| — | **Air** | atan saturation depth (was the implicit hard-clip at the end) |

Mod Depth, Sync Threshold, Mirror, Level — unchanged.

Formant rate range: musical when in 100 Hz–8 kHz absolute, or 1×–32×
relative to carrier. FREE/FIXED mode option (per strike) tucked on a
sub-display.

### Air control

Post-envelope, before Mirror:
```cpp
float air = airKnob;  // 0..1
float driven = envOut * (1.0f + air * 4.0f);
float saturated = atanf(driven) * (2.0f / M_PI);  // atan, normalized to [-1,1]
float airMixed = envOut + (saturated - envOut) * air;
```

At Air=0: no saturation (linear). At Air=1: full atan, sine-shaped
soft clip → odd harmonics, Mangrove-AIR-like character.

---

## Component 2 — Voss-McCartney 1/f drift

### Generator

Per-instance state:
- `pinkCounter` — uint32_t
- `pinkOctaves[16]` — float[16], one per octave
- `pinkTotal` — float, running sum

Per-sample update (or per-block to save CPU):
```cpp
int k = __builtin_ctz(pinkCounter) & 15;
pinkCounter++;
float prev = pinkOctaves[k];
float fresh = rand_uniform_pm1();   // ±1
pinkOctaves[k] = fresh;
pinkTotal += (fresh - prev);
float white = rand_uniform_pm1() * 0.5f;  // top octave filler
float pink = pinkTotal + white;
// Normalize so amplitude stays in ~[-1, 1]
pink *= (1.0f / 8.0f);  // sum of 16 unit-range randoms / 16 octaves
```

Update at every Nth sample (e.g. N=16) keeps spectrum centered
well below 1 Hz with tail to ~1.5 kHz — comfortable modulation
band. CPU is one trailing-zero count + a few floats per Nth
sample. Negligible.

Random source: `rand()` is fine for v1 (audio-rate aliasing of
linear-congruential `rand()` is not audible at the amplitudes we
use). Replace with a fixed seed deterministic LCG for reproducible
behavior at v1.5 if needed.

### Routing

New ply: **Drift** (0..1, depth control).

For v1, Drift routes to **Formant rate**:

```cpp
float driftedFormantHz = baseFormantHz * (1.0f + pink * drift * 0.5f);
```

Drift = 0 → no modulation. Drift = 1 → ±50% wander on Formant
rate (timbre wanders slowly across wavetable frame regions because
the envelope rate shifts).

Future v1.5: sub-display destination picker — let user route Drift
onto Sync Threshold (carrier-mod ratio drift), Shape (wavetable
position drift), or Mirror (divisor drift). Multi-destination via
matrix is v2.

### Why Formant rate is the right v1 destination

- Less risky than Sync Threshold (which would push you off lock
  zones unpredictably)
- More musical than Mirror (Mirror drift would change fold
  density which is more dramatic)
- Same-mechanism coupling: drift modulates the FORMANT
  oscillation rate, which is what makes the envelope-impulse
  spectrum move. Coupled to the same mechanic the user is
  hearing.

---

## Combined unit topology

### Plies (top-level, 8 + 1 sub)

1. **Pitch** (V/Oct view control)
2. **Fundamental** (f0 base in Hz)
3. **Sync Threshold** (cubic-around-locks → carrier/mod ratio)
4. **Mod Depth** (FM index)
5. **Shape** (wavetable position 0..15)
6. **Formant** (envelope rate)
7. **Air** (atan saturation depth)
8. **Mirror** (S&H divisor)
9. **Drift** (1/f modulation depth onto Formant)
10. **Level** (output gain)

Plus on sub-displays:
- Fine (cents offset) on Pitch
- Formant FIXED/FREE mode on Formant
- Mirror Reset on Mirror (already designed)

10 top-level plies is more than the design doc's 8. Options to
tighten:
- Combine Mirror + Mirror Reset (already on sub)
- Put Drift on a sub of Shape or Formant (kills Drift as a
  top-level expressive control; probably wrong)
- Put Level on the unit's expansion view, not a top-level ply

For v1 I'd ship all 10 top-level and audition; if it's too dense,
collapse Drift to a sub-display in v1.5.

### Outlets (same as Phase 2: 5 mono)

- Out, Clean (= envelope output pre-Mirror), Fold (= Mirror output
  minus Clean), Sync (gate on sync edges), Mod (mod osc audio).

Stereo (Phase 3) still planned as separate work — two carrier
pipelines with sync-threshold-derived Δφ. Doesn't affect this
plan.

### Inlets (same)

V/Oct, FM. All knobs accept CV via ParameterAdapter.

---

## Implementation phases (this work)

### Phase 4a — Wavetable engine

1. Add `precomputeWavetable()` called once from the Mirror
   constructor. Generates the 16 × 256 LUT. Static lifetime;
   shared across all Mirror instances (precompute once globally).
2. Add envelope phase state + sync edge detection to `Internal`.
3. Replace current `sourceShape()` call with wavetable lookup +
   shape interpolation.
4. Remove the current Source / Push knobs from the C++ side;
   replace with Shape + Formant + Air. Old code paths can stay
   stubbed for one build then deleted.
5. Bump PKGVERSION. Build both arches. Install linux.
6. Audition: pitched tone at 110 Hz, sweep Shape across 16
   frames. Confirm each frame sounds distinct. Confirm Formant
   rate sweep gives undertone series below carrier rate.

### Phase 4b — Air saturation

1. Add Air post-envelope, pre-Mirror.
2. Audition: Air=0 should pass envelope clean; Air=1 should sound
   like Mangrove's AIR overdrive on the impulse train.

### Phase 4c — Voss-McCartney 1/f drift

1. Add pink noise state + generator to `Internal`. Update at
   every Nth sample (N = 16 for v1).
2. Add Drift ply.
3. Apply pink × drift_depth × 0.5 as multiplicative modulation
   on `formantHz`.
4. Audition: at Drift=0, Formant is static. At Drift=0.5,
   formant rate wanders slowly. Should hear musical motion that's
   not periodic, not random — fractal.

### Phase 4d — Lua updates

1. Update Lua wrapper for new plies (Shape, Formant, Air, Drift).
2. Update toc.lua keywords if needed.
3. Update PKGVERSION.

### Phase 4e — Hardware audition (am335x)

After emu audition: install am335x build, test on hardware. Watch
for:
- Wavetable LUT alignment issues (4-byte float, no NEON ops in
  the lookup, should be safe)
- CPU envelope check (probably under 10% per voice)
- Any new entries in NEON suspect list (objdump check)

### Phase 4f — Stereo (Phase 3 of original design doc)

Defer to next plan. Two carrier pipelines, Δφ from Sync Threshold.

---

## Open design questions to settle during audition

1. **Final 16-frame inventory** — the table above is starting
   point. Listening will tell us which to keep, which to swap.
2. **Shape interpolation: linear vs cubic** — linear is cheap, may
   show audible knob steps between frames. Audition first.
3. **Formant rate unit** — Hz absolute (FREE mode) or ratio of
   carrier (FIXED mode)? Strike's pattern is to have both via the
   option. v1 should probably ship both modes from day one.
4. **Drift update rate** — every 1 / 16 / 64 / 128 samples? 16
   feels right for "musical motion" but audition will confirm.
5. **Drift destination** — Formant is the v1 choice but if it
   feels muted, route to Shape instead.
6. **Strike syncDelta** — do we need it for click-free retrigger,
   or does polyBLEP handle it? Audition.
7. **Number of plies** — 10 vs 8. Probably 10 is fine for a
   paradigm-bearing complex voice; audition the unit's overall
   density.

---

## Risks

1. **Wavetable shapes lack distinctiveness** — if all 16 sound
   too similar, the Shape knob feels like a slow morph rather
   than a regime selector. Mitigation: order frames carefully so
   adjacent are related, distant are different.
2. **Undertone behavior is harsh** — when Formant rate < sync
   rate, the envelope ignores syncs until complete. May create
   subharmonic clicks. Mitigation: polyBLEP + Mirror folding may
   soften.
3. **Drift feels random rather than coherent** — if the 1/f
   spectrum doesn't read as "musical motion" at the chosen update
   rate. Mitigation: tune update rate; potentially apply a final
   tilt EQ to the pink noise to weight more low freq.
4. **CPU overrun on am335x** — wavetable LUT is 16 KB which fits
   in L2 cache easily. Drift generator is trivial. Should be
   under Phase 2's CPU. Risk low.
5. **Old preset compatibility** — current Source / Push
   parameters get repurposed or removed. Anything currently saved
   referencing those becomes garbage. Mitigation: Mirror hasn't
   shipped publicly yet, so no compat concern. If we'd already
   released, would need a migration shim.

---

## Out of scope for this plan

- Devil's staircase for Sync Threshold (research thread 2) — defer
  to its own plan
- Alias-activity feedback (research thread 1) — defer
- BARREL rise/fall asymmetry knob — per user, not in scope
- Stereo Phase 3 — defer
- Custom viz — Phase 4 of original design doc, not this work
- Final habitat name — still "Mirror" codename

---

## Related references

- `planning/mirror-unit-design.md` — original locked architecture
  (this plan supersedes the Source / Push portion)
- `planning/mirror-research-notes.md` — research threads 1-4,
  this plan implements 3 + 4
- `~/repos/er-301-custom-units/common/dsp/osc.h:506` — strike's
  Formant struct, reference architecture for envelope osc
- `~/repos/er-301-custom-units/mods/strike/Formant.h` — strike's
  Formant unit wrapper
- `feedback_aw_param_default_subtle` — Shape default should land
  somewhere musical (e.g. frame 2 = symmetric triangle), not at 0
  where the gate frame is least interesting
- `feedback_disable_tree_vectorize_am335x` — still applies
- `feedback_no_paths_of_least_resistance` — wavetable inventory
  picked by audition, not by what's cheapest to implement

---

## TL;DR

Replace Source morph with wavetable-of-envelope-shapes (16 frames
× 256 samples, scalar lookup + interpolation, ~12 ops/sample).
Mod-wrap retriggers envelope; Formant rate controls envelope
rate; when Formant < carrier, undertone series emerges. Add Air
atan saturation post-envelope. Add Voss-McCartney 1/f drift onto
Formant rate as native motion source. All scalar, all cheap, all
paradigm-coherent — the envelope IS the carrier and Mirror folds
it. Ten plies: Pitch / Fundamental / Sync / Mod / Shape / Formant
/ Air / Mirror / Drift / Level. CPU under Phase 2's. Hardware-safe
(no NEON gather, no out-of-line virtuals, LUT in L2).
