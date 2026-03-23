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

Output: `testing/<arch>/<package>-0.1.0.pkg`

## Install

Copy `.pkg` to `~/.od/rear/` (emulator) or the ER-301's front SD card `ER-301/packages/` (hardware).

## Packages

### Mutable Instruments Ports

Based on code by Émilie Gillet (MIT License).

| Package | Unit(s) | Description |
|---------|---------|-------------|
| **plaits** | Plaits | Macro-oscillator — all 24 synthesis engines, V/Oct, engine selector |
| **clouds** | Clouds | Granular processor — granular, looping delay, and spectral modes. NEON-optimized envelope rendering and spectral frame transforms |
| **stratos** | Stratos | Clouds reverb engine extracted as standalone effect |
| **rings** | Rings | Modal/sympathetic string resonator with NEON-vectorized SVF bank |
| **warps** | Warps | Meta-modulator — 6 crossmodulation algorithms with auto gain compensation |
| **grids** | Grids | Topographic drum pattern generator |
| **commotio** | Commotio | Elements exciter section (bow/blow/strike) at native 48kHz |
| **kryos** | Kryos | Spectral freeze (WIP) | WILL CRASH DO NOT INSTALL

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
| **nr** | NR | Gate sequencer inspired by the Noise Engineering Numeric Repetitor |

