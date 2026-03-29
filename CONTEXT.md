# ER-301 Habitat: Agent Context

This document captures institutional knowledge for agents working on this repo.

## Project

Custom unit packages for the ER-301 sound computer (Eurorack DSP module by Orthogonal Devices). Builds `.pkg` files loadable on hardware or emulator. 12 mod packages: Mutable Instruments ports, Peaks/DMC ports, and original units.

## Build Environment

- Arch Linux, TI ARM toolchain at `~/ti`
- `er-301` symlink points to `~/repos/er-301-stolmine` (custom firmware fork)
- Cross-compile: `make ARCH=am335x` (Cortex-A8 target)
- Emulator: `make` (native linux)
- Output: `testing/<arch>/<package>-<version>.pkg`
- Install script: `./install-packages.sh` copies to SD card with `-stolmine` suffix
- Every firmware recompile invalidates ALL packages -- must rebuild all
- `make clean ARCH=am335x` wipes ALL package builds, always follow with full `make ARCH=am335x`

## SD Card Layout

- Front SD card mounted at `/mnt` -- holds `.pkg` files at `/mnt/ER-301/packages/`
- Rear SD card -- holds installed (extracted) packages at `/v0.7/libs/<package>/`
- If a bad package causes boot loop, delete from rear card: `sudo rm -rf /mnt/v0.7/libs/<package>/`
- User cannot use sudo -- ask them to run sudo commands themselves

## ER-301 SDK Patterns

### Unit Structure
- C++ DSP class (`.h`/`.cpp`) with `process()` override
- Lua UI/graph wiring (`.lua`) in `assets/` directory
- SWIG binding (`.cpp.swig`) connects C++ to Lua
- `toc.lua` registers units with the package system

### SWIG Rules
- Constructor/destructor MUST be outside `#ifndef SWIGLUA`
- `process()`, Inlets, Outlets, Parameters go inside `#ifndef SWIGLUA`
- The `%{ %}` block `#undef SWIGLUA` for C++ compilation; `%include` sees `SWIGLUA` defined
- If using macros to generate classes, use dual-macro pattern (one for SWIG, one for full class)

### Serialization
- `od::Parameter` and `od::Option` are NOT auto-serialized
- `OptionControl` ViewControl calls `enableSerialization()` automatically
- `GainBias` ViewControl enables serialization on Gain/Bias params automatically
- Task-based menu items do NOT enable serialization
- For custom state: override `serialize()`/`deserialize()` on the Unit, calling `Unit.serialize(self)` / `Unit.deserialize(self, t)` as super
- `onSerialize`/`onDeserialize` do NOT exist as SDK hooks -- they are never called

### Gate Inputs
- Built-in units route gate inputs through `app.Comparator` (threshold + hysteresis filtering)
- Raw chain input (`connect(self, "In1", op, "Clock")`) passes unfiltered signal -- causes noise from G jacks and hot-unplug
- Generators should sink chain input: `ConstantGain(0)` connected to `In1`
- Clock/gate consumers should use Comparator: `connect(self, "In1", clock, "In"); connect(clock, "Out", op, "Clock")`

### Audio Thread
- Small stack -- use heap for work buffers in `process()`. Put buffers in Internal struct.
- Use `kMaxFrameLength = 256` for fixed-size heap buffers

### ViewControls
- Expanded view expects ViewControl objects, not raw Graphics
- `self.subGraphic` is a field read by the framework -- set in init, don't add/remove dynamically
- Mode toggle faders go at far left of expanded view

### Output Crossfade
- For multi-output units, use C++ `od::Parameter` + internal buffer, not SDK `CrossFade` graph
- Unconnected outlets may alias a shared zero buffer, discarding writes

### V/Oct Scaling
- `FULLSCALE_IN_VOLTS = 10`, correct factor is `* 120` (10 * 12 semitones)
- Plaits exception: `* 12` in C++ with 10x `ConstantGain` in Lua (ARM compiler workaround -- changing to `* 120` in C++ crashes on engine switch due to `-O3 -ffast-math` code generation)

### stmlib on Cortex-A8
- Must define `TEST` symbol for am335x builds
- Without it, stmlib uses STM32 Cortex-M4 inline ASM (ssat, usat, vsqrt.f32) that crashes on Cortex-A8

### od::Sample Buffer Pattern
- Use `od::Sample` for buffers that need waveform display (not raw `float*`)
- Create via `Sample.Pool.create{type="buffer", channels=1, secs=duration}` in Lua
- Pass to C++ via `setSample(sample.pSample)`
- Mark `mpSample->setDirty()` on write for display refresh
- Inherit from `od::TapeHead` to get `mpSample`, `mCurrentIndex`, `mEndIndex`, and `TapeHeadDisplay` compatibility
- Serialize with `pool.serializeSample()` / `pool.deserializeSample()`

## Unit Category Conventions
- MI ports: use standard SDK categories ("Synthesizers", "Timing", etc.), not "Mutable Instruments"
- Use standard ER-301 categories: Essentials, Synthesizers, Oscillators, Audio Effects, Filtering, Timing, Envelopes, Mapping and Control, Measurement and Conversion, Containers, Experimental

## Attribution
- Always credit Emilie Gillet (never use deadname) when working with Mutable Instruments source
- Replace deadname in any copied MI source files

## Writing Style
- No em dashes in READMEs, release notes, changelogs, or commit messages
- User prefers concise instructions and direct commands over explanations
