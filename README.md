# er-301-habitat

Custom units for the ER-301 sound computer.

## Setup

```bash
git clone --recursive git@github.com:stolmine/er-301-habitat.git
cd er-301-habitat

# Symlink the ER-301 SDK (custom firmware fork)
ln -s /path/to/er-301 er-301
```

## Build

```bash
# Emulator (macOS/Linux)
make plaits

# Hardware
make plaits ARCH=am335x

# Build all packages
make
make ARCH=am335x
```

Output: `testing/<arch>/<package>-1.0.0.pkg`

## Install

Copy `.pkg` to `~/.od/rear/` (emulator) or the ER-301's front SD card `ER-301/packages/` (hardware).

## Packages

### Mutable Instruments Ports

Based on code by Émilie Gillet (MIT License).

| Package | Unit(s) | Description |
|---------|---------|-------------|
| **plaits** | Plaits | Macro-oscillator — all 24 synthesis engines, V/Oct, engine selector |
| **clouds** | Clouds | Granular processor — granular, looping delay, and spectral modes. NEON-optimized pffft FFT, rising-edge trigger control |
| **stratos** | Stratos | Clouds reverb engine extracted as standalone effect |
| **rings** | Rings | Modal/sympathetic string resonator with NEON-vectorized SVF bank, main/aux output mix control |
| **warps** | Warps | Meta-modulator — 6 crossmodulation algorithms with auto gain compensation |
| **grids** | Grids | Topographic drum pattern generator |
| **commotio** | Commotio | Elements exciter section (bow/blow/strike) at native 48kHz |
| **marbles** | Marbles T, Marbles X | Random sampler -- probabilistic gate generator (7 models) and random CV generator (beta distribution, deja vu looping, 3 control modes) |
| **kryos** | Kryos | Spectral freeze (WIP) — WILL CRASH, DO NOT INSTALL |

### Peaks / Dead Man's Catch

Based on code by Émilie Gillet and Tim Churches (MIT License).

| Unit | Category | Description |
|------|----------|-------------|
| Bass Drum | Peaks | 808-style kick synthesis |
| Snare Drum | Peaks | 808-style snare synthesis |
| High Hat | Peaks | 808-style hi-hat synthesis |
| FM Drum | Peaks | FM synthesis drum with noise |
| Tap LFO | Peaks | Clock-synced LFO |
| Bouncing Ball | Peaks | Physics-based bounce envelope |
| Mini Sequencer | Peaks | 4-step CV sequencer |
| Number Station | Peaks | Radio transmission noise generator |
| Randomised Envelope | Dead Man's Catch | Stochastic AD envelope |
| Mod Sequencer | Dead Man's Catch | Extended step sequencer |
| FM LFO | Dead Man's Catch | LFO with FM modulation |
| WSM LFO | Dead Man's Catch | LFO with waveshape modulation |
| PLO | Dead Man's Catch | Phase-locked oscillator |
| ByteBeats | Dead Man's Catch | Algorithmic bytebeat generator |

### Original Units

| Package | Unit(s) | Description |
|---------|---------|-------------|
| **stolmine** | NR | Gate sequencer inspired by the Noise Engineering Numeric Repetitor |
| | 94 Discont | 7-mode waveshaper (fold, tanh, softclip, hardclip, sqrt, rectify, crush) |
| | Latch Filter | Switched-capacitor S&H into SVF with V/Oct tracking |
| | Canals | Linked resonant filter inspired by Three Sisters -- crossover/formant modes |
| | Gesture | Continuous gesture recorder/looper -- 5/10/20s buffer, movement-detected auto-write |
| **scope** | Scope, Scope 2x, Scope Stereo | Inline signal visualization — stereo-aware passthrough with waveform display |

## Changelog

### v1.3.0

**New: Marbles**
- Marbles T -- probabilistic gate generator with 7 models, jitter, deja vu, T1/T2 crossfade
- Marbles X -- random CV generator with spread, bias, steps, deja vu, X1/X2/X3 selector, 3 control modes

**stolmine**
- New unit: Gesture -- continuous gesture recorder/looper with 5/10/20s selectable buffer, movement-detected auto-write with holdoff, explicit write gate override

**Build**
- All packages rebuilt against txo.8 firmware

### v1.2.0

**New: Ballot**
- 64-step gate sequencer with chaselight display, per-step gate width/velocity/length
- Algorithmic transforms (euclidean, NR, grids, necklace), randomize gates/lengths/velocities
- Clear/reset menu items, fine encoder scrolling

**Plaits**
- Persist trig mode across save/reload
- Sink chain input to prevent unpatched trigger firing
- V/Oct scaling workaround: 10x ConstantGain in Lua graph to avoid engine switch crash

**Excel**
- Slew fader starts at 0 (instant), range 0-10s
- Overview: step length readout replaces progress bar
- Fine scrolling for step selection via encoder accumulator

**Canals**
- Custom SistersSvf filter core with soft-clip saturating integrators
- Threshold-based soft clip: linear below |2|, compressed above

### v1.1.0

**New: Excel**
- 64-step CV tracker sequencer with per-step offset, length, and deviation
- Scrollable step list with live editing, shift+scroll for rapid multi-step edits
- Expandable overview ply with playhead, loop, total ticks, fader controls
- Math transform gate: 9 functions, scope selector, snapshot save/restore
- Clock/reset gate inputs, global slew, V/Oct scaled output (offset 1 = 1 octave)
- Config: offset range (2Vpp/10Vpp), batch step lengths, randomize/clear offsets

**Clouds**
- Adaptive labelling for mode control (Gran/Delay/Spect)

**Commotio**
- Disconnected chain input passthrough (pure generator, no exciter bleed)

**Stratos**
- Defaults now match Clouds' fixed reverb settings

**stolmine**
- Stereo processing for Canals, 94 Discont, and Latch Filter (dual DSP instances, shared params)

**Build**
- Bumped clouds, commotio, stolmine, stratos to v1.1.0

### v1.0.0

Compatible with both vanilla ER-301 firmware and er-301-stolmine (TXo) firmware.

**Clouds**
- Restored NEON-optimized spectral mode via pffft (lost in submodule migration)
- Fixed crash when switching to spectral mode while freeze is active
- Reduced spectral CPU load by halving FFT hop ratio (4→2) while retaining 2048-point resolution
- Trigger input now fires on rising edge only (one grain per tick, not continuous gate)
- Density control is now bipolar (-1 gridded, +1 random)

**Rings**
- Added output mix control — bipolar crossfade between main and aux outputs
- Mono: -1 = main only, 0 = equal sum, +1 = aux only
- Stereo: smoothly swaps routing between outlets

**Scope**
- Fixed stereo passthrough on Scope and Scope 2x (previously dropped R channel on stereo chains)

**stolmine**
- New package consolidating NR, 94 Discont, Latch Filter, and Canals

**Build**
- All packages bumped to v0.2.0
- Rebuilt against latest firmware with SWIG 4.2.1 compatibility

### v0.1.0

Initial release.

## Acknowledgements

Special thanks to...

tomf - I reused your clever circle graphic for the drum sequencers, and learned a lot from your units

Joe Filbrun - I drew directly from your menu paging schemes in Accents

Brian Clarkson - For making my favorite instrument

Émilie Gillet - For the incredible, generous gift of your code

