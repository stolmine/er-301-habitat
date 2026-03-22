# TODO

## Mutable Instruments Ports

- [ ] Clouds (granular processor)
- [ ] Rings (resonator)
- [ ] Grids (single channel, no accent — pattern generator with clock/reset inputs)
- [ ] Warps (meta-modulator)
- [ ] Extract Clouds reverb engine, create a standalone unit named Stratos
- [ ] Marbles X algorithm extracted to single channel unit
- [ ] Marbles Y algorithm extracted to single channel unit

## Plaits Improvements

- [x] Add fundamental frequency control on top of V/Oct
- [x] Fix engine switch crash on am335x (define TEST for stmlib)

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

## 4ms SMR

- [ ] Port of 4ms Spectral Multiband Resonator — 6 resonant bandpass filters with rotation/spread
