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
| | Canals | Linked resonant filter inspired by Three Sisters — crossover/formant modes |
| **scope** | Scope, Scope 2x, Scope Stereo | Inline signal visualization — stereo-aware passthrough with waveform display |

## Changelog

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

