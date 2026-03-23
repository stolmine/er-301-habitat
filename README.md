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
| **kryos** | Kryos | Spectral freeze (WIP) |

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

---

## Building tomf's Community Packages (sloop, lojik, strike, polygon)

These packages live in a separate repo and need two build system fixes for macOS Apple Silicon / gcc-15.

### Setup

```bash
cd ~/repos
git clone https://github.com/tmfset/er-301-custom-units.git
cd er-301-custom-units

# Point at the ER-301 SDK (our custom fork)
rm -rf er-301
ln -s ../er-301 er-301
```

### Required Edits to `scripts/mod-builder.mk`

The darwin section needs two fixes:

1. **Replace `-march=native`** (doesn't work on Apple Silicon with gcc-15):

```makefile
# BEFORE:
CFLAGS.darwin = -Wno-deprecated-declarations -march=native -fPIC

# AFTER:
ifeq ($(shell uname -m),arm64)
  CFLAGS.darwin = -Wno-deprecated-declarations -march=armv8.2-a -fPIC
else
  CFLAGS.darwin = -Wno-deprecated-declarations -march=native -fPIC
endif
```

2. **Replace linker flags** (gcc-15 doesn't accept raw ld flags):

```makefile
# BEFORE:
LFLAGS = -dynamic -undefined dynamic_lookup -lSystem

# AFTER:
LFLAGS = -shared -Wl,-undefined,dynamic_lookup
```

### Build

```bash
make sloop
make lojik
make strike
make polygon
```

All four build cleanly against v0.7.0 with no source code changes.

For hardware (on a Linux x86 machine with the TI SDK):

```bash
make sloop ARCH=am335x
make lojik ARCH=am335x
make strike ARCH=am335x
make polygon ARCH=am335x
```

---

## Building Accents (SuperNiCd / Joe Filbrun)

```bash
cd ~/repos
git clone https://github.com/SuperNiCd/Accents.git
cd Accents

# Point at the ER-301 SDK
ln -s ../er-301 er-301
```

### Required Edits to `Makefile`

1. **Add darwin arch detection** — replace `ARCH ?= linux` with:

```makefile
ifndef ARCH
  SYSTEM_NAME := $(shell uname -s)
  ifeq ($(SYSTEM_NAME),Linux)
    ARCH = linux
  else ifeq ($(SYSTEM_NAME),Darwin)
    ARCH = darwin
  else
    $(error Unsupported system $(SYSTEM_NAME))
  endif
endif
```

2. **Change SDK path** — replace `SDKPATH ?= ../er-301` with:

```makefile
SDKPATH ?= er-301
```

3. **Add darwin build section** after the linux section:

```makefile
ifeq ($(ARCH),darwin)
INSTALLPATH.darwin = $(HOME)/.od/rear
ifeq ($(shell uname -m),arm64)
CFLAGS.darwin = -Wno-deprecated-declarations -march=armv8.2-a -fPIC
else
CFLAGS.darwin = -Wno-deprecated-declarations -march=native -fPIC
endif
LFLAGS = -shared -Wl,-undefined,dynamic_lookup
include $(SDKPATH)/scripts/darwin.mk
endif
```

4. **Fix hardcoded include paths** in three source files:

```bash
sed -i 's|#include </home/joe/Accents/Bitwise.h>|#include "Bitwise.h"|' Bitwise.cpp
sed -i 's|#include </home/joe/Accents/Accents/DXEG.h>|#include "DXEG.h"|' DXEG.cpp
sed -i 's|#include </home/joe/Accents/PointsEG.h>|#include "PointsEG.h"|' PointsEG.cpp
```

### Build

```bash
make
```

Builds cleanly against v0.7.0.
