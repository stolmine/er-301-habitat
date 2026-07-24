# Release notes stub: Ngoma modal engine (spreadsheet 2.8.3.7x line)

Draft user-facing copy for the next release. Keep naming generic per
feedback_no_third_party_branding (no instrument or manufacturer names in any
user-facing text). No em dashes anywhere in release copy.

---

## Ngoma: new synthesis core

Ngoma's voice has been rebuilt from the ground up around a measured modal
engine. Same unit, same controls, same cube; a much deeper sound.

- **Modal lattice core.** The oscillator/FM core is replaced by a 14-mode
  additive lattice: a carrier with intermod sidebands above AND below the
  fundamental. The sub-side modes carry roughly 30 percent of the sideband
  energy; that is the body and punch the old core approximated with membrane
  partials.
- **Cubed-ramp decay, two decay classes.** Modes decay on a cubed linear ramp
  (not an exponential), with tone modes ringing long and sidebands short. The
  spectrum darkens over each hit the way struck drums actually do.
- **Grit is noise-FM now.** Grit frequency-modulates every mode with one
  shared noise source (four measured regimes across the throw), so repeated
  hits decorrelate naturally instead of stacking a noise bed on top. Past
  0.75 it also collapses the decay toward the snap; at the very top it hands
  over to noise with an attack burst.
- **Character and Shape do more.** Shape detunes the lattice (the second
  oscillator overlay); Character opens the fold: it kills one family of
  sidebands and raises the odd-harmonic family, a spectrum swap rather than a
  brightness knob.
- **Clipper is now the heart of the dynamics.** The output stage is an
  always-limiting soft clipper, and the Clipper sub-control (on Level) rides
  its drive. Default is full drive: the signature dense, pinned-peak heft.
  Backing it off progressively releases the bank's real dynamics: crest grows
  from 2.5 to 7.7 across the throw, and Shape/Grit start moving level and
  transient shape, not just spectrum. An equal-loudness makeup keeps
  perceived level flat over most of the throw (bottom sits about 3 dB down by
  design). Note: at low Clipper the peaks are hot by intent (that is the
  released crest); trim Level if you are routing straight to an output.
- **Attack has a real job.** The engine's native onset is an instant jump
  (authentic to the modeled behavior, and the default). The Attack sub-param
  now applies a true linear ramp to the modal bank when you want a softer
  front.
- **Honest dials.** Decay, Sweep Time, and Hold drive the new engine directly
  in the seconds shown on the dial. Sweep spans the same 0..72 semitone throw
  mapped onto the measured pitch-envelope law.

**Presets:** existing Ngoma presets load (schema 5). Every control keeps its
name and range, but the laws behind them changed, so presets will sound
different. That is the point of the release; re-tune to taste. New default:
Clipper ships at 1.0 (the classic dense sound); it used to default to 0.

**Under the hood:** the per-sample modal loop is NEON-vectorized on hardware
(4-quad SoA kernel, polynomial sine), and the engine runs at 1x rate (a sine
bank makes no supra-mode harmonics; the clipper is the only nonlinearity).
Zero suspect NEON hints; emulator and hardware run lane-identical math.

## Retained

Trigger, V/Oct + Octave, Punch, EQ, one-knob compressor, Level, the cube
graphic (now polling the live carrier envelope), and CV on every sub-param.
