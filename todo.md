# TODO

## Mutable Instruments Ports

- [x] correct categories, plaits still shows up in its own

- [x] Clouds (granular processor)
- [ ] Rings (resonator)
- [x] Grids (single channel, no accent — pattern generator with clock/reset inputs)
- [x] Warps (meta-modulator) — 6 xmod algorithms working, vocoder pending 96kHz SRC
- [x] Extract Clouds reverb engine, create a standalone unit named Stratos
- [ ] Marbles X algorithm extracted to single channel unit
- [ ] Marbles Y algorithm extracted to single channel unit

## Warps Improvements

- [ ] Vocoder: implement 2x upsample→process→downsample for correct 96kHz filter bank operation
- [ ] Easter egg (frequency shifter): test and verify
- [ ] Consider exposing carrier/modulator drive as separate controls

## Plaits Improvements

- [x] Add fundamental frequency control on top of V/Oct
- [x] Fix engine switch crash on am335x (define TEST for stmlib)
- [x] Engine name display on fader (EngineSelector subclass)
- [x] Osc/Trig mode toggle with dynamic view switching
- [x] Output routing config menu (mono/stereo options)
- [x] Engine order: original 16 first, v1.2 additions after

## Teletype NR Ops

- [x] Implement as single channel gate sequencer
- [x] Reuse circle graphic from tomf's euclid unit

## Sequencer Suite

- [ ] Ideate and build a decent sequencer suite — ER-301 is lacking here

## Filterbank

- [ ] based on disting ex filterbank
- [ ] controls for filter type, band gain, band res, band freq, band spread/arrangement

## Kryos (spectral freeze)

- [ ] Debug hang on load — test in emulator first to isolate hardware vs code issue
- [ ] If emulator works: hardware-specific issue (alignment, memory, toolchain)
- [ ] If emulator hangs: DSP bug in process() or constructor

## Monokit

- [ ] Gladiola — saturation/discontinuity unit
- [ ] HD2 clone — Monokit synth voice

## Effects

- [ ] Tilt EQ
- [ ] 3 Sisters clone

## 4ms SMR

- [ ] Port of 4ms Spectral Multiband Resonator — 6 resonant bandpass filters with rotation/spread

## Community Package Compatibility

- [x] tomf: sloop, lojik, strike, polygon — build against v0.7.0 (darwin build fixes only)
- [x] SuperNiCd: Accents — build against v0.7.0 (darwin build + hardcoded path fixes)
- [ ] Build all community packages for am335x on Linux machine

## TXo I2C Output (separate repo: er-301-stolmine)

- [x] I2C master HAL extension
- [x] TXo CV and TR units with passthrough
- [x] Gain control and V/Oct mode
- [x] Emulator monitor (txo-monitor.py)
- [ ] Hardware testing on real ER-301 + TXo
