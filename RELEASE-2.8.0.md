# er-301-habitat v2.8.0

Release date: 2026-08-06

Package updates: spreadsheet 2.8.3 -> 2.8.4, biome 2.2.1 -> 2.2.2, scope
1.2.1 -> 1.2.7, catchall 0.4.0 -> 0.4.1, and **house 0.1.1 ships for the first
time**. mi and peaks unchanged. Firmware updated to 9.6.0; a corresponding reinstall is encouraged.

## New package: house

A package of eight units, shipping publicly for the first time. Six reverb ports
plus two character processors, most originally by Chris Johnson (Airwindows):
kWoodRoom, WoodenBox, CreamCoat, BrightAmbience3, Verbity, Galactic. TickerTape
and Lacquer are original designs based on his work.

Most of them were optimized this cycle with no change to their sound. Galactic
and BrightAmbience3 have not had that pass yet and remain heavier.

**kWoodRoom** -- Regen / Time / Tone / Reflect / Position / Mix. A 6x6 feedback
network with cross-feedback inside the matrix, so it is internally stereo. The
woody, roomy one, and the heaviest of the six.

**WoodenBox** -- Box / Reso / Mix. A 4x4 network with an intentional left/right
swap through the tank. Small, boxy, resonant; the most obviously "a space" of the
set at short settings.

**CreamCoat** -- Box / Regen / DeRez / Predelay / Wetness. Bright ambience with
the engine's divisor mechanic exposed as a DeRez knob, so you can grind the
reflection resolution down deliberately. Submix-style wet/dry: Wetness at 0.5
sums full wet and full dry rather than crossfading.

**BrightAmbience3** -- Position / Size / Brightness / Wetness. Sparse prime-tap
delay summation with resonant filter feedback, giving a bright gated halo. Size
is the CPU dial: it sums up to 487 sparse taps at the top.

**Verbity** -- Bigness / Longness / Darkness / Wetness. Three cascaded 4x4
networks with a sub-low "thunder" chase underneath. The most conventionally
hall-like, and the one that goes darkest.

**Galactic** -- Replace / Brightness / Detune / BigDim / Wetness. Three cascaded
networks with a modulated predelay and full left-right cross-coupling at the
feedback stage. The lush option, and the one that wanders.

**TickerTape** -- Drive / Tape / Bias / Mix. Not a reverb: a chain of console
saturation into tape rot into console desaturation. Original design built from
Airwindows parts.

**Lacquer** -- Drive / Cut / Polish / Mix. Also not a reverb: gritty trajectory
distortion inside a downsample shell, with clean averaging inside a 2x upsample
bracket. A mixed-rate character processor, and the heaviest unit in the package.
Original design built from Airwindows parts.

## New units elsewhere

**Vitrail** (spreadsheet) -- a dual switched-capacitor character filter. Two
cores on their own drifting clocks with a shared resonance loop; the aliasing,
clock combs and breathing self-oscillation emerge from the mechanism rather than
being tabulated. 50 routing combinations across series and parallel. Its Clock
Src ply carries a tunnel visualization driven from the audio engine rather than
decoration: cutoff sets how fast you travel, the two clocks drifting against each
other rotates the tunnel, resonance steps its cross-section from a circle down to
a triangle, and imbalance between the cutoffs banks the whole thing.

**Expo D** and **Expo AD** (biome) -- simple exponential envelopes with
continuously variable curve, defaulting to fully exponential. Time controls use
the built-in ADSR map.

**Fade Mixer 6** and **Fade Mixer 8** (biome) -- wider siblings of the 4-input
crossfader.

**Spectrogram 3 / 4 / 6** (scope) -- wide-ply-span spectrum displays. The 4 and 6
ply units use a larger FFT to match, so the extra width is real resolution
rather than a stretched image.

## Fixes

- Fixed a bug where changing a Scope unit's timebase or gain would alter the
  built-in scope, and keep affecting it after the unit was deleted.
- Fixed a crash where Pecto could take down the audio thread after running for a
  while. Present since v2.3.0.
- Fixed Fade Mixer's mute and solo buttons, which did nothing.
- Fixed mid-range mix values losing volume on Network, Fabula, Petrichor and
  Colmatage.

## Discrete controls now step consistently

Picking from a list used to be fiddly: a fast turn would jump several entries
and overshoot what you wanted. Anything that selects from a named set -- filter
types, shaper types, algorithms, patterns, modes, curves, macros -- now steps one
entry per turn regardless of how fast you spin it, on both the faders and the
sub-display readouts.

Controls that set a count rather than pick from a set -- step counts, tap counts,
ticks, clock divisions, semitones -- are unchanged, since sweeping those quickly
is the point.

## Other changes

**Fade Mixer** gains a Smooth/Snap option in its config menu, turning the family
into N-to-1 switches as well as crossfaders, with a 3 ms declick ramp so the
switch does not step the waveform. Smooth is bit-identical to before.

**Larets** gains a random/sequential step-advance toggle in its overview
sub-display, replacing the skew readout there (skew is unchanged and still
reachable in the expansion). Random never plays the same step twice in a row.

**Constant Random** was reworked beyond the slew change above: rate now steps
0.1 Hz on coarse rather than 1 Hz, bottoms out at a true 0 Hz pause, and level
adopts the built-in oscillator convention -- bipolar, defaulting to 0.5, so it
swings +/-5 V rather than +/-10 V and can invert.

**Mix controls** across every package now share the built-in dial map, so a
coarse detent moves 0.01. Impasto and Parfait were ten times coarser than that.

**Ngoma** beyond the engine swap: NEON'd to roughly 10% CPU mono.

**Rauschen** gains a Cellular algorithm driven by a 1D cellular automaton, with a
Reseed action in the unit menu.

## Attribution

The `house` package is built from the work of **Chris Johnson (Airwindows)**,
whose plugins are released under the MIT licence. kWoodRoom, WoodenBox,
CreamCoat, BrightAmbience3, Verbity and Galactic are all
ports of his designs, and his Spiral saturator is reused widely across the rest
of the catalog. The ports carry his algorithms; any bugs in them are ours.

