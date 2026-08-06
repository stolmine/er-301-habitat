# er-301-habitat v2.8.0

Release date: 2026-08-06

Package updates: spreadsheet 2.8.3 -> 2.8.4, biome 2.2.1 -> 2.2.2, scope
1.2.1 -> 1.2.7, catchall 0.4.0 -> 0.4.1, and **house 0.1.1 ships for the first
time**. mi and peaks unchanged. Firmware unchanged.

## Read this first: two changes that alter existing patches

**Ngoma's voice engine has been replaced.** The oscillator/FM/membrane core is
gone, replaced by a measured modal engine. Presets move to schema 5: old patches
load, but the laws changed and they will not sound the same. The Clipper default
also flipped from 1.0 to 0.0, so Ngoma now ships clean and the knob adds heft
rather than removing it.

**Constant Random's Slew is now a time in seconds**, not a 0-1 amount. There is
no migration path, because the old 0-1 range overlaps valid new second values: a
stored slew of 0.5 used to mean 38 ms and now means half a second. Existing
patches using it need re-dialling.

## New: house

A package of eight units, shipping publicly for the first time. Six reverb ports
plus two character processors, all originally by Chris Johnson (Airwindows):
kWoodRoom, WoodenBox, CreamCoat, BrightAmbience3, Verbity, Galactic, TickerTape
and Lacquer.

Most of them went through a hybrid-float conversion this cycle -- Cortex-A8 has
no double-precision NEON, so full-double DSP falls back to scalar VFPv3 and runs
several times slower than it needs to. kWoodRoom, WoodenBox, Verbity, Lacquer and
TickerTape's Console0/ChromeOxide chain were converted with the tone verified
identical against a reference build (1 LSB, correlation 1.0000000), for f64
operation-count reductions of 75-81%. Galactic and BrightAmbience3 are not yet
converted and remain heavier.

## New units elsewhere

**Vitrail** (spreadsheet) -- a dual switched-capacitor character filter. Two
cores on their own drifting clocks with a shared resonance loop; the aliasing,
clock combs and breathing self-oscillation emerge from the mechanism rather than
being tabulated. 50 routing combinations across series and parallel. Its Clock
Src ply carries a tunnel visualization driven entirely from the DSP: travel
tracks cutoff (which is the switched-cap clock rate), rotation tracks the A/B
clock drift, resonance steps the tunnel's cross-section from a circle down
through a 12-gon, octagon, hexagon and square to a triangle, and cutoff imbalance
banks the whole thing.

**Expo D** and **Expo AD** (biome) -- simple exponential envelopes with
continuously variable curve, defaulting to fully exponential. Time controls use
the built-in ADSR map.

**Fade Mixer 6** and **Fade Mixer 8** (biome) -- wider siblings of the 4-input
crossfader.

**Spectrogram 3 / 4 / 6** (scope) -- wide-ply-span spectrum displays. The 4 and 6
ply units are backed by a 512-point FFT (256 real bins) rather than stretching
128 bins across a wider canvas.

## Fixes

**Scope no longer leaks its timebase into the built-in scope.** Adjusting a Scope
unit's timebase changed the firmware's own signal monitoring, and the change
persisted after the unit was deleted. The probe is served from a shared pool that
the firmware's MiniScope also draws from and never re-initialises, so releasing
it with a modified decimation handed the setting to the next consumer. Scope now
restores the pool default before releasing.

**Pecto no longer overflows the audio task stack.** Its process() reserved a
1072-byte stack frame from five per-frame scratch arrays, against an audio task
stack of 2048 bytes total for the whole chain. This had been present since
v2.3.0, silently corrupting memory past the stack; recent firmware added the
canaries that caught it. The buffers moved to the heap and the frame is now 92
bytes. A build-time check (`tools/check-audio-stack.sh`) now guards the class.

**Fade Mixer's mute and solo work.** They previously did nothing -- the controls
escaped to the parent chain's mute group where they were unregistered. Mute and
solo are now unit-local and gate audio per input.

## Discrete controls now step consistently

Scrolling through a list of options used to inherit the encoder's acceleration,
so a fast turn jumped several entries and landing on a specific one was fiddly.
Every control that addresses a named set -- filter types, shaper types,
algorithms, patterns, modes, scopes, grids, curves, macros -- now steps one entry
at a time, acceleration-independent, with the fine setting costing more travel
rather than a smaller step.

This covers 15 controls in spreadsheet and 3 in biome, across both the
mode-selector faders and the sub-display readouts. Controls that address a
*count* rather than a set -- step counts, tap counts, ticks, clock divisions,
semitones -- deliberately keep their old behaviour, because sweeping quickly is
the point there.

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
CreamCoat, BrightAmbience3, Verbity, Galactic, TickerTape and Lacquer are all
ports of his designs, and his Spiral saturator is reused widely across the rest
of the catalog. The ports carry his algorithms; any bugs in them are ours.

Elsewhere in this release: the Mutable Instruments units are based on code by
**Emilie Gillet** (MIT), the Peaks and Dead Man's Catch units on code by
**Emilie Gillet** and **Tim Churches** (MIT), and Colmatage's cut procedures
descend from **Nick Collins**' BBCut library by way of **Remy Muller**'s Livecut.

## Known issues

- Ngoma has three documented differences from its reference: missing sub-partials
  in the body, Character not reaching the fully-overfolded extreme, and Shape's
  harmonic trajectory diverging across the throw.
- house's Galactic and BrightAmbience3 are still full-double and heavier than
  their converted siblings.
- Kryos remains unreleased; it hangs on load.
