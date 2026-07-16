# er-301-habitat v2.7.0

Release date: 2026-07-15

Package updates: spreadsheet 2.8.2 -> 2.8.3. biome, catchall, mi, peaks,
scope unchanged. Firmware unchanged from v2.6.1.

## Highlights

One new unit: **Fabula**, an algorithmic room reverb, joins the spreadsheet
package. It is the smooth, lush, long-decay room the catalog did not have,
distinct from the resonant and multitap units (Pecto, Network, Tomograph).
No other units changed.

## Fabula (algorithmic room reverb)

Fabula is a Dattorro/Griesinger figure-8 recirculating-allpass tank. The
recirculating tank runs at half the sample rate (band-limited, plate-like top
end); the input diffusion, predelay, and early-reflection network stay at full
rate. The result is a believable, smooth room with a long, dense tail.

What gives it its character, versus a textbook Dattorro:

- Organic modulation. Each delay line is modulated by an independent
  Brownian random walk rather than a plain sine LFO, so the tail drifts and
  breathes instead of chorusing on a fixed period.
- A discrete early-reflection network in parallel with the diffuse tail, so
  the room reads as present and immediate, not just a wash.
- Living Freeze, a continuous 0..1 hold. As you raise Freeze the tail ramps
  to a self-sustaining cloud with the tank input muted, locking in stages
  (left then right) so the freeze "sets" progressively while the Brownian
  modulation keeps it alive.

### Controls

- Size. The main dial, with a custom "fabric" waterfall overview graphic that
  ripples to the wet signal's spectrum. Under Size (tap-shift, or press enter
  to expand to full faders) sit Decay, Damp, and Diffusion.
- Predelay, Early (early-reflection amount), Freeze.
- Mix (dry/wet). Under Mix sits a tunable wet Highpass (20 Hz to 500 Hz,
  default 60 Hz) that sets how much low body the reverb keeps. Press enter on
  Mix to expand the highpass to a full fader.
- Xform, a gate that re-rolls the room. On a trigger (or the manual fire
  button) it randomizes a curated set of the room parameters to a new space.
  A Target chooses the scope, a Depth sets how far the re-roll moves from the
  current settings. The default Target is everything except Freeze, so a
  re-roll reshapes the room without unexpectedly freezing it; other targets
  cover all parameters, any single parameter, or a reset to defaults.

### Interface

- The Size and Mix controls expand on enter. Pressing enter opens a fader
  strip of their sub-parameters (Size -> Decay/Damp/Diffusion, Mix -> Highpass)
  so you can dial them like any top-level fader, in addition to the compact
  tap-shift sub-display readouts.
- The overview "fabric" graphic is a stack of flat spectrum contours that
  scroll and dim with age, driven by a 16-band analyzer on the wet output.

## Compatibility

Firmware unchanged from v2.6.1. No patch migration needed; existing patches
load unchanged. Fabula is a new unit, so nothing that already worked changes.

## Acknowledgements

Fabula's recirculating-tank saturator and nested-allpass diffusion math are
derived from Airwindows by Chris Johnson (MIT License).
