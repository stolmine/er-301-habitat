# Mirror — Research & Ideation Notes

Living scratchpad. Updated as we audition + read. Distinct from
`mirror-unit-design.md` which captures locked architecture.

## Status

- Phase 2 mono prototype shipping at spreadsheet `2.7.1.24`.
- Lever fixes for low-F0 alias landed: Fibonacci LockRatio anchors
  {1, 2, 3, 5, 8, 13}, iterated triangle wavefolder as Source shape C.
- User audition (2026-06-16): better but unit still feels too static
  at sustained parameter positions. The interest comes from MOTION;
  static spectra read as boring.

## Core unsolved problem

**How does Mirror feel alive at static knob positions?**

User's diagnostic: "a lot of the interest occurs when things are
moving. especially when sync moves around a plateau. ... bring the
influence of >Nyquist material down unto the lower frequency or
more still valleys of param combos."

Translation: we need a mechanism that injects motion into the
audible band, ideally derived from the alias activity itself, so
the unit self-modulates at the boundary of lock zones and stays
quiet inside lock plateaus.

---

## Research thread 1 — Alias-activity feedback loop

**Hypothesis**: Use the Fold outlet (alias residual) as the SOURCE
for a slow modulator that drives one of the audible-band controls.
Creates a self-reinforcing motion loop: more alias → more
modulation → more drift through param space → more alias.

**Mechanism sketch**:
- Tap Fold signal
- Full-wave rectify + heavy lowpass (1–10 Hz)
- Result = alias-intensity envelope
- Route as DC offset onto: Sync Threshold (closes loop most
  directly), or Source morph, or Mirror divisor

**Why this fits**: at static knobs in "still valleys" (low alias
activity), the envelope is quiet, parameters don't drift. Near
lock-zone EDGES (where chaos lives), envelope grows, drives drift,
the unit comes alive. Self-stabilizing motion.

**Open questions**:
- Where in the chain should the rectification + LP happen?
- What should the depth/scaling be? Should there be a user knob to
  set feedback intensity?
- Single destination or multi-destination (matrix)?
- Does this break V/Oct tracking? (Probably not if depth is small
  enough.)

---

## Research thread 2 — Devil's staircase as the true sync structure

**Hypothesis**: Our current cubic-around-locks at six anchors is a
finite slice of the actual mathematical structure of mode locking
in coupled nonlinear oscillators. The real function is the **devil's
staircase** — fractal, with infinitely many lock plateaus at every
rational, with width decreasing as denominator complexity grows.

If we implement the actual staircase instead of hand-picked anchors,
the knob has self-similar structure at every scale. Tiny movements
near a plateau reveal smaller lock zones inside chaos transitions.
Knob exploration becomes inherently interesting.

**Mechanism sketch**:
- Pre-compute a LUT of the devil's staircase (4096 samples) derived
  from the circle map at coupling strength K just below 1.0
- Map knob 0..1 → LUT index
- Each lookup gives lock ratio with the fractal step structure

**Mathematical pointers**:
- Arnold tongues (mode-locking regions in driven nonlinear
  oscillators)
- Circle map: θ_{n+1} = θ_n + Ω - (K/2π) sin(2π θ_n)
- Below K=1 (sub-critical), the rotation number traces the devil's
  staircase as Ω varies
- Above K=1 (super-critical), chaotic regions appear inside the
  steps — bonus character source

**Open questions**:
- LUT resolution: 4096 is overkill for human knob resolution but
  maybe right for CV modulation?
- K choice: sub-critical (clean staircase), critical K=1 (golden
  ratio behavior), super-critical (chaos inside steps)?
- Does it need to update at audio rate or block rate?

---

## Research thread 3 — Fractal noise as native motion source

**Hypothesis**: Add a built-in 1/f (pink) noise generator at
sub-audio rate. Route as a low-amplitude DC drift onto carrier
phase, Push, Source morph, or Mirror knob.

1/f noise has self-similar amplitude structure across timescales —
it drifts at every speed simultaneously, which means the unit moves
at the speed of musical attention.

**Mechanism**: Voss-McCartney algorithm at block rate. ~8 octaves
of summed white noise, gives 1/f spectrum. Cheap.

**Routing options** (character predictions):
- → Carrier phase: vibrato-like wander
- → Source morph: timbre evolves
- → Push: bandwidth wander
- → Mirror divisor: fold-density wander

Probably wants a Depth knob; could be modulated by alias-activity
envelope (thread 1) for compound effect.

**Open questions**:
- Static-only fractal noise, or fractal noise WHOSE INTENSITY is
  driven by alias activity?
- One destination or matrix?

---

## Research thread 4 — Mangrove-style per-cycle envelope (formant character)

**Hypothesis (from user 2026-06-16)**: Borrow Mannequins Mangrove's
mechanism — pitched oscillator emits impulse train, each impulse
retriggers an AD envelope, envelope modulates something at audio
rate. In Mangrove, envelope amplitude-shapes a second formant osc
at independent frequency; the formant is the resulting spectral
peak.

**Reframe for Mirror**: The Mirror block already creates spectral
peaks via alias landings (at SR/(2N) where N = divisor). So we
don't add a formant oscillator — we let **the envelope modulate
the Mirror divisor**, sweeping the alias-landing frequency through
the spectrum each pitch cycle. The formant IS the alias landing.

**Architectural placement**:
- Mod osc wrap → fires impulse
- Impulse retriggers AD envelope
- Envelope shape (decay knob = "Squish" equivalent) sweeps Mirror
  divisor from low (clean) to high (dense fold) and back
- Result: each cycle has alias landings that sweep through
  spectrum, producing formant-like motion

**Character predictions**:
- Short decay: percussive plucked formant peaks, ring briefly each
  cycle, vocal/brassy attack
- Long decay: formant sustains, blurs into next cycle, sustained
  vocal/resonant character
- Bypass: current static behavior

**Why paradigm-coherent**: the formant IS the alias mechanic, not
bolted alongside.

**Open questions**:
- AD or AHD or full ADSR? Probably AD with adjustable decay.
- Decay range: in samples, ms, or as a fraction of pitch period?
  Fraction of pitch period auto-scales to V/Oct.
- Should the envelope amplitude also be applied as a VCA on the
  Mirror output (Squish proper)? Or only on the divisor?
- Multiple destinations: envelope → divisor AND → Push AND →
  Source morph, with per-destination depth?

**Alternative reading (true Mangrove clone)**: add a third
oscillator at independent frequency (Formant Freq knob), envelope
shapes its amplitude, mix into output. More familiar character,
adds knobs, less paradigm-defining. Could be later "vocal mode"
layered on top of Option 1 above.

---

## Open: the user's "still valleys" framing

User language: "the answer is to bring the influence of >Nyquist
material down unto the lower frequency or more still valleys of
param combos."

Two readings:
1. Cross-couple alias activity to audible-band modulation (thread 1)
2. Concentrate alias spectral energy into the audible low-frequency
   range, so even at static settings the >Nyquist content lands
   audibly (per-cycle envelope from thread 4 does this)

Both probably apply. Thread 1 adds the temporal motion; thread 4
adds the per-cycle spectral motion. They compound.

---

## Reading list (gather thoughts here)

- **Chaos in synthesis**: Don Buchla's 266e Source of Uncertainty,
  the Buchla Music Easel three-section chaos generator, Hordijk's
  Benjolin
- **Arnold tongues / mode locking**: classical literature on coupled
  nonlinear oscillators, circle map dynamics
- **Devil's staircase**: standard fractal-geometry literature
- **1/f synthesis**: Voss & Clarke 1975 paper, Schroeder's chapter
  in Fractals/Chaos/Power Laws
- **Mannequins Mangrove**: schematic if available, design
  philosophy from Whimsical Raps
- **Aliasing as deliberate synthesis**: Forss/Loubet on Mirage
  granular aliasing, Aphex Twin synth design talks

---

## Decision points (what we'd want to commit to before more code)

1. Do we accept the alias-activity feedback (thread 1) as a v1
   feature, or is it v1.1?
2. Same question for the devil's staircase (thread 2).
3. Same question for pink-noise motion (thread 3).
4. Same question for Mangrove envelope (thread 4).
5. If we ship more than one of these, do they have a unified UI
   surface (one "Motion" ply with sub-display routing) or separate
   plies per thread?
6. Stereo (Phase 3 of original design doc) — defer until after
   motion mechanics are worked out, or do in parallel? Currently
   thinking defer, since the stereo Δφ mapping depends on the final
   shape of the Sync Threshold knob (devil's staircase changes that
   shape substantially).

---

## Practical next steps (no implementation, just to set up the next session)

1. **Capture Fold-outlet audio at various param settings**. Confirm
   alias intensity correlates with knob distance from lock zones.
   Drives thread 1 design.
2. **Listen to thread 4 (envelope → divisor) mentally**: imagine
   F0=110 with envelope retriggered each mod wrap, decay ~30 ms,
   sweeping divisor 1 → 16 → 1. What does that sound like? Pluck +
   alias swept formant?
3. **Read one or two of the reading-list items** before next
   coding session, so the implementation choices are informed.
4. **Sketch a unified "Motion" ply** for the UI — could be a single
   knob from 0 (current static behavior) to 1 (all motion
   mechanisms maximally engaged), with sub-display picking which
   mechanism dominates.
