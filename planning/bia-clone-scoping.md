# BIA-clone — habitat port scoping

Status: **scoping**, 2026-05-01. Source material: Noise Engineering's
Basimilus Iteritas Alia manual (`manuals.noiseengineering.us/bia/`),
which documents the algorithm Alter introduced and Alia retains
unchanged. Clean-room implementation from the technical description.

Working title in this doc: **BIA-clone**. Per the no-third-party-branding
policy (`feedback_no_third_party_branding`), the shipped unit must
have a generic habitat name. Candidates to pick from:

- **Tympani** — Latin for drum membranes; directly thematic
- **Saliens** — Latin "leaping/pulsing"; biology coinage
- **Drupa** — botanical "stone fruit"; short, distinct
- **Resilient** / **Calyx** — alternates

Pick one before starting Phase 1 code so the C++ class / pkg toc /
file paths land right the first time.

## Architecture (verbatim from the source manual)

The voice has six tonal oscillators plus one noise oscillator running
in three operator configurations:

> "Basimilus Iteritas Alia uses six tonal and one noise oscillator in
> three configurations to generate sound."

Signal chain:

> "The oscillators are summed and then the Attack envelope is applied
> to the sum. This then feeds into a threshold-reflection folder with
> amplitude compensation and the ability to dynamically add more fold
> stages."

> "The final touch was to re-apply the overall envelope to the signal
> after the folder which gave back a lot of the dynamics that are
> lost when folding."

### Three modes

| Mode | Architecture |
|---|---|
| **Skin** | 6-oscillator additive — tonal sounds, snares, synth stabs |
| **Liquid** | 6-osc additive + pitch envelope on all 6 — adds kick character |
| **Metal** | Pair of 3-operator phase-modulated oscillators — metallic, noisy, alien |

### Front-panel controls

| Control | Range / behavior |
|---|---|
| Pitch | Encoder + −2V to +5V CV, 1V/oct |
| Attack | Left = noise added; center = classic analog pop; right = slow attack |
| Decay | Global decay coefficient for all oscillators (no sustain phase) |
| Morph | Continuous blend through sine → triangle → saw → square |
| Harmonic | CCW = single tone; first quarter fades in 2nd tone; remainder extends decays then amplitudes of the other 4 harmonics |
| Spread | Inter-osc frequency spacing — harmonic series at CCW to prime series at CW |
| Fold | First 3/4 = threshold of folder (with dynamic stage multiplication); top 1/4 mixes in a pulse train |
| Bass / Alto / Treble | ±2 octave switch (3-position) |
| Skin / Liquid / Metal | Mode select (3-position) |
| Trigger | Fires AR envelope; no sustain phase |

### Implementation hints from the manual

> "Spread is quite simple in implementation as it adjusts the intervals
> between the drum modes from the harmonic series to the prime series.
> Harm is quite a bit more complicated as it adjusts the decays and
> amplitudes of the oscillators to produce a wide variety of tonal
> structures."

> "Basimilus Iteritas adds as many fold sections as will still continue
> to fold to maximize the amount of produced harmonics. It as well
> cleanly compensates for the volume changes that occur during folding."

> "The oscillators are essentially wavetable though they are evaluated
> on the fly as this is needed for the Morph knob. They have a period
> of 65536 samples but are decimated by a different amount depending
> on the octave of the pitch."

### Engineering decisions called out by the designer

- Variable sample rate: `"a sample rate that is a multiple of the
  fundamental (lowest) oscillator frequency"` — moves alias power to
  multiples of the fundamental (musically benign).
- Internal precision: `"8.24 fixed point, quantized to 16-bit for
  DAC output"`.
- Noise generator: linear congruential, decimated by octave.

## Habitat reuse — what we already ship

Almost every subsystem maps to existing habitat code:

| BIA subsystem | Habitat reuse |
|---|---|
| 6 voices via SIMD | `jf::four::Voice` (mods/spreadsheet/jf/voice.h). MultiVoice<2> = 8 lanes; mask 2 off → 6 active. Pattern from JF Phase 3a, hardware-tested. |
| AR envelope, no sustain | JF Transient mode (jf::four Voice::process kTransient path). |
| Pitch envelope (Liquid) | Ngoma's pitch-sweep envelope (`feedback_doppler_basedelay_smoother` adjacent). |
| 3-op PMM (Metal) | Helicase's 2-op FM extends to closed-loop 3-op. We'd add a feedback path; OPL3 wave morph from Helicase reuses cleanly. |
| Wave morph (sin→tri→saw→sq) | Varishape Osc (biome) ships polyBLEP morph across these exact shapes. |
| Trig LUT (no libm in package draw paths) | `kLutCos`/`kLutSin` from FilterResponseGraphic.h (spreadsheet pkg has it; copy into BIA-clone's package — cross-package dep policy). |
| Per-partial decay scaling | Ngoma's `mPartialDecayCoeffs[4]` NEON quad pattern. |
| Threshold-reflection folder | New code (not in existing units), but standard DSP — ~30 lines for the basic folder + ~20 for dynamic stage multiplication + amplitude compensation. |
| Polynomial sine | Helicase's polynomial sine helper / Ngoma's `kDrumVoiceSineLUT`. |
| Class-member NEON storage | Per `feedback_neon_intrinsics_drumvoice` — pattern is well-documented and codified in JF/Ngoma. |

The Spread mechanism is essentially INTONE from JF with a different
anchor: instead of overtone (CW) ↔ undertone (CCW), it's harmonic
(CCW) ↔ prime series (CW). Same per-voice-frequency-multiplier shape.

The Harmonic mechanism is essentially per-partial decay/amplitude
scaling of 4 inharmonic partials — Ngoma already does this with its
3 membrane modes.

## Package home

**Spreadsheet** — tier-1 alongside Helicase / Ngoma / JF / Pecto. The
unit is a percussion-leaning macro drum voice; that's the same
category as Ngoma. Both can ship.

Spreadsheet PKGVERSION bump on first ship: 2.6.0 → 2.7.0 (or whatever
JF lands at).

## Phase plan

### Phase 1 — Skin mode skeleton + 6-voice additive

- C++ stub class extending `od::Object`, V/Oct + Trigger inlets, mono Out
- 6 voices via two `jf::four::Voice` instances (8 lanes, 6 active, 2 masked)
- Per-voice AR envelope (Transient mode dispatch from JF voice)
- Spread parameter as per-voice frequency multiplier (harmonic ↔ prime)
- Harmonic parameter as per-voice amplitude scaling (CCW = first only,
  CW = all six progressively)
- Morph parameter as wave-shape blend (sine → tri → saw → square LUT or
  polyBLEP per Varishape)
- 3-pos octave switch as `od::Option`
- Lua wrapper, single-page UI

Deliverable: working Skin-mode percussion voice on hardware.

### Phase 2 — Folder + Attack mode + noise

- Threshold-reflection folder with dynamic stage multiplication and
  amplitude compensation. Top-quarter pulse train mix.
- Attack parameter's tri-band behavior: left → noise injection at
  attack, center → instant attack ("analog pop"), right → slow attack
- 7th oscillator: linear congruential noise generator, octave-decimated

### Phase 3 — Liquid mode

- Pitch envelope: per-trigger transient that modulates all 6 voices'
  pitch on a fast decay (the "extra kick")
- Mode switch (`od::Option`): Skin / Liquid / Metal — Phase 3 wires
  Skin and Liquid; Metal stub returns silence

### Phase 4 — Metal mode (3-op PMM pair)

- Two 3-operator phase-mod operator chains in parallel
- Reuse Helicase's polynomial sine + phase accumulator
- Inharmonic ratios; the manual calls this `"who cares about aliasing
  frequency modulation in all of its noisy glory"`. Closed-loop FM
  topology.
- Mix Metal output with Skin's 6-osc bus when in Metal mode

### Phase 5 — Polish + ship

- Final-envelope re-application post-folder (the percussive-thump
  trick from the manual)
- Custom viz on one of the plies (rotating circle / radial waveform /
  TBD — must be header-only inline per `feedback_no_out_of_line_virtuals`)
- am335x objdump pre-flight (NEON `:64` hint check)
- Hardware CPU profile under worst-case
- Test procedures entry
- Spreadsheet PKGVERSION bump

## Open design questions

1. **Final unit name.** Pick from candidates above (Tympani, Saliens,
   Drupa, etc.) before Phase 1.
2. **Dynamic fold-stage count.** The manual is vague: "as many fold
   sections as will still continue to fold." Implementation: iterate
   reflection until signal magnitude < threshold or some max-iters
   safety (e.g. 8). Verify on listening tests.
3. **Pulse-train shape for top-quarter Fold.** Manual says "a pulse
   train based on the signal is mixed in" — interpret as
   `sign(folded_sig) * |folded_sig| > threshold ? 1 : 0` or similar.
   Listening-test calibrate.
4. **Variable sample rate**. Original BIA clocks at a multiple of f0.
   Habitat runs at 48 kHz fixed. We accept the alias trade — runs at
   audio rate identical to other voices. Document that aliasing
   character will differ from hardware Alter slightly.
5. **Octave switch wraps**: Bass = −2 oct, Alto = 0, Treble = +2 oct.
   Or three contiguous positions Bass = 0, Alto = +2, Treble = +4?
   Manual says each step offsets by two octaves; need clarification
   from listening or video reference.

## Cross-references

- `planning/jf-initial-pass.md` — JF Phase 3a 4-lane NEON voice
  pattern; the BIA-clone reuses this.
- `project_ngoma_codex.md` — drum voice precedent in spreadsheet;
  pitch-morph membranes, NEON discipline.
- `feedback_no_out_of_line_virtuals.md` — graphics class shape rules;
  any custom viz must follow.
- `feedback_neon_intrinsics_drumvoice.md` — NEON working memory must
  be class members, not stack-locals.
- `feedback_package_trig_lut.md` — no `sinf`/`cosf` in package draw
  paths; LUT or polynomial.
- `feedback_no_third_party_branding.md` — no "BIA" or "Basimilus" in
  shipped names / file paths.
