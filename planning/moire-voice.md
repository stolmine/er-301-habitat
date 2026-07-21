# Moire - the moving intermod-lattice voice

Working name **Moire** (the audible result of moving one lattice against another is
interference/beating - a moire pattern). Rename freely.

## Origin

The Trinity RE proved BLOCK's spectrum is an intermod lattice `f(h,k) = fc*(h + k*r)`: a core
oscillator's harmonics `h` cross-modulated by a second oscillator detuned by `r`, producing
uniform sidebands, with ~30% of the energy in the sub-modes (k<0, below fc). The hardware sets
`r` **statically** from the Shape knob. Moire's whole idea: make `r` **playable and
audio-rate-modulatable**, so the entire lattice moves - partials slide through each other,
harmonic <-> inharmonic morph, sub-modes swelling and vanishing. The hardware never lets you do
this; it is the original instrument hiding inside the mechanism we reverse-engineered.

This is NOT a Trinity clone: no fitted amp table, no fold, no grit, no envelope - a different
instrument built from the discovered principle.

## v0 - bare bones (this build)

Continuous **oscillator** (drone), not triggered. Rationale: the point is to HEAR `r` move, and
a triggered hit decays before you can sweep it. Trigger + envelope is a later wrapper (Tessera's
machinery drops in unchanged).

**Signal path (per sample):**
```
fc = f0 * 2^voct
r  = clamp(spreadBias + spreadCV, 0, 2)
y  = sum over 15 partials of  amp[m] * sine(phase[m]),  where f[m] = fc*(h[m] + k[m]*r)
     (a partial is muted when |f| < 20 Hz or |f| >= Nyquist; phase still advances)
out = softclip(y * norm) * level
```

**Lattice (v0):** odd harmonics h in {1,3,5}, sidebands k in {-2,-1,0,+1,+2} = 15 partials.
- At r=0 the k-siblings collapse onto each harmonic (phases start aligned -> clean odd-harmonic
  tone, soft square-ish).
- As r opens, sidebands emerge and beat; sub-modes (k<0) dip toward DC and reflect back up.
- Amplitudes: simple rolloff (carrier loudest, falling with h and |k|). NOT the Trinity fit.

**Controls (v0):**
- `V/oct` (inlet) - pitch, 10x-gain + `2^voct` (Mirror/Plaits convention)
- `Pitch` (Fundamental Hz param) - base pitch
- `Spread` (r) - the star. Inlet-backed (audio-rate) via a ConstantOffset: knob = bias,
  CV branch adds. Range 0..2.
- `Level` - output gain

**am335x:** sine LUT (no runtime trig), class-member `phase[15]`, `no-tree-vectorize` pragma,
`__builtin_sqrtf` softclip. Target 0 NEON hints, both arches.

## Deliberately deferred (bolt on after the first listen)

- Trigger + AR/AHD envelope (-> percussion voice) - Tessera's code reuses directly.
- Decorrelation / liveness (the common-mode noise-FM: one shared LP-noise Hz-jitter on every
  partial) as a first-class "alive" macro.
- Per-partial / two-class decay spread.
- Bipolar `r` (mirror the lattice), wider `rMax`, animated-`r` internal LFO.
- A fold/waveshaper stage (the additive-vs-waveform hybrid).
- Viz (phase-space or the lattice itself).

## Open choices to revisit after hearing v0

- Lattice size/shape (15 is a guess; more k = denser, more h = brighter baseline).
- Amp rolloff law (perceptual, tune by ear).
- Whether r=0 phase-alignment is the right default or should vary.
