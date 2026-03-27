# TODO

## Mutable Instruments Ports

- [x] Plaits (macro-oscillator — all 24 engines)
- [x] Clouds (granular processor — granular, delay, spectral modes)
- [x] Rings (modal/sympathetic string resonator)
- [x] Grids (single channel, no accent — pattern generator with clock/reset inputs)
- [x] Warps (meta-modulator — 6 xmod algorithms, vocoder pending)
- [x] Stratos (Clouds reverb engine extracted as standalone unit)
- [x] Commotio (Elements exciter — bow/blow/strike at native 48kHz)
- [ ] Marbles X algorithm extracted to single channel unit
- [ ] Marbles Y algorithm extracted to single channel unit

## Peaks / DMC Ports

- [x] Initial port of 14 units (drums, modulation, sequencers, generators)

Refinements:
- [x] Tap LFO: removed phase reset on clock edge — free-runs at tracked tempo
- [x] Tap LFO: renamed gate to "Clock", added separate "Reset" input
- [x] Tap LFO: rate knob acts as clock multiplier when synced
- [x] FmLfo/WsmLfo: removed clock input (free-running), reset-only
- [x] Mini Sequencer / Mod Sequencer: added reset input (GATE_FLAG_AUXILIARY_RISING)
- [x] PLO: continuous /16x–16x pitch multiplier, corrected param labels (WSM Rate/Depth)
- [ ] Tap LFO: add frequency counter display on clock input (research ER-301 SDK)
- [ ] PLO: recalculate phase_increment continuously between clock pulses for smoother pitch response
- [ ] Step position visualization — adapt tomf's polygon voice indicator or simple bar display
  - Apply to Mini Sequencer, Mod Sequencer, and future 101 Sequencer

## Controls Audit

- [x] Bipolar parameter correctness — fixed Peaks/DMC bipolar faders and defaults
- [x] Warps algorithm range clamped to match Lua fader
- [x] Modulation gain scaling — verified default gainMap [-10,10] is correct for both polarities

## Clouds Improvements

- [x] Fix CPU spike on trigger — capped grain spawns per block
- [x] NEON envelope rendering and spectral frame transforms
- [x] Spectral mode enabled (2048-point mono)
- [x] switching to mode 2 while freeze is active causes crash — pffft bump allocator reset fix
- [x] Trig input fires on rising edge only — one grain per tick
- [x] Spectral mode CPU overrun — halved hop ratio (4→2), restored pffft NEON FFT path
- [ ] Adaptive labelling for mode control (ModeSelector pattern)
- [ ] Gain compensation toggle — auto-scale output to match input level
- [ ] Further NEON optimization — ShyFFT butterflies, SRC polyphase FIR

## Warps Improvements

- [ ] Vocoder: implement 2x upsample/downsample for correct 96kHz filter bank
- [ ] Easter egg (frequency shifter): test and verify
- [ ] Consider exposing carrier/modulator drive as separate controls

## Stratos Improvements

- [ ] Defaults should match Clouds' fixed reverb settings

## Commotio Improvements

- [x] Disconnected chain input passthrough (pure generator, no exciter bleed)
- [ ] Split versions: standalone units for each exciter algo (bow, blow, strike)
- [ ] User sample loading: allow custom buffers in strike sample player
- [ ] UI: sub-displays for timbre/meta pairs (tomf pattern) or expanded controls (polygon pattern)
- [ ] NEON optimization pass

## Sequencer Suite

- [ ] 101 Sequencer — 64-step CV sequencer inspired by the SH-101
  - Gate input for step advance, reset input
  - Address fader (0-63): moves editing cursor through steps
  - Optional UI tracking: couple address fader to playhead on command
  - Sequence length fader (1-64)
  - Loop length fader: when > 0, loops N steps from current playhead position
  - Each step has its own pitch/CV fader
  - Dynamic viewport: selected step's fader appears in view, switches with address
  - Global math transforms: add/subtract/multiply/divide/mod/randomize all steps
  - Integer factor fader for transform operations (e.g. add 7, div 3)
  - Global pitch/CV scaling and snap-to-scale mode
- [ ] Gesture Sequencer — record fader gestures into a clocked step buffer
  - Offset fader (CV value source) + gate input (punch in/out)
  - Gate engaged: record/overdub fader position to buffer at clock rate
  - Gate disengaged: record null (rest), preserving existing data
  - Clock input for step advance, reset input
- [x] Excel — 64-step CV tracker sequencer (implemented)
  - Per-step offset, length (ticks), slew
  - Scrollable step list with live editing, shift+scroll for rapid multi-step edits
  - Sequence info ply: playhead, loop status, total tick count
  - Clock/reset gate inputs, global slew, V/Oct scaled output (offset 1 = 1 octave)
  - Config: offset range (2Vpp/10Vpp), batch set all step lengths (1/2/4)

### Excel Improvements
  - [ ] Output scope visibility: OutputScope shows "no signal" despite working output. Commotio has the same issue. Investigate pUnit:getOutput(0) for generator units that don't consume In1
  - [ ] Randomize all offsets (config menu item)
  - [ ] Math transform gate (ply 6): non-destructive transforms with function/bound/probability
  - [ ] Addressable variant: CV address input instead of clock for random access
  - [ ] Expanded variant: CV inputs for sequence length and loop length
  - [ ] Probability variant: per-step probability of firing as separate unit
  - [ ] Snap-to-scale mode for offset values
  - [ ] Playhead/cursor coupling: auto-scroll display follows playback, manual nav decouples

### Sequencer UI Research

- Bar graph custom Graphic (2-4 ply): 8-16 steps as vertical bars, paged navigation
- MondrianList tracker: scrollable step rows with sub-display per-step editing
- Dual-zone: miniature 64-step overview (L) + focused step controls (R)
- NRCircle ring: good for short patterns, crowded at 64 steps
- Playhead/cursor coupling: auto-scroll follows playback, manual nav decouples

## Scope

- [x] Inline scope unit — Scope (1 slot), Scope 2x (2 slot), Scope Stereo
- [x] Passthrough audio with MiniScope waveform display
- [x] Stereo passthrough on Scope and Scope 2x (adapt to chain channel count)
- [ ] Channel focus display switching — show L or R based on channel button selection
  - MiniScope supports runtime watchOutlet(), but SDK has no channel focus callback for custom ViewControls
  - Need to find where channel button events are dispatched in Lua UI layer
- [ ] Timebase and gain controls (requires custom graphic or MiniScope subclass)
- [ ] Research headerless unit display (SDK hardcodes header — not currently possible)

## Ratchet

- [ ] Clock input, gate input, clock output
- [ ] When gated, outputs clock multiplied by fader value (e.g. 2x, 3x, 4x)
- [ ] When not gated, passes clock through unchanged
- [ ] Integer fader for multiplication factor

## Kryos (spectral freeze)

- [ ] Debug hang on load — test in emulator first to isolate hardware vs code issue
- [ ] If emulator works: hardware-specific issue (alignment, memory, toolchain)
- [ ] If emulator hangs: DSP bug in process() or constructor

## stolmine (original units)

- [x] NR — gate sequencer (migrated from standalone package)
- [x] 94 Discont — 7-mode waveshaper (fold, tanh, softclip, hardclip, sqrt, rectify, crush)
- [x] Latch Filter — switched-capacitor S&H → SVF with V/Oct tracking
- [ ] Stereo passthrough for Canals, 94 Discont, and Latch Filter (currently mono only)
- [ ] HD2-style FM oscillator pair (dual osc with FM index + feedback)
- [ ] Additional filter models from monokit (MoogFF, DFM1, BMoog)

## Polyphonic Sample Playback

- [ ] Polyphonic sample player with voice management (reference: Polygon voice allocation)
- [ ] Manual grain triggering (reference: Manual Grains)

## Effects

- [ ] Tilt EQ
- [x] Canals (Three Sisters clone) — linked resonant filter with crossover/formant modes

## Canals Improvements (Three Sisters fidelity)

- [ ] Custom SistersSvf primitive with tanh-saturating integrators (OTA-style nonlinearity)
  - Fast tanh approximation tuned for AM335x (rational, piecewise cubic, or x/(1+|x|))
  - Multi-output per sample (LP/BP/HP simultaneously, no template mode selection)
- [ ] Cross-coupled filter topology — serial signal decomposition instead of 6 independent SVFs
- [ ] Tune Q curve and saturation character to match analog behavior
- [ ] Bench CPU cost on hardware; NEON vectorize if needed

## Filterbank

- [ ] Based on disting ex filterbank
- [ ] Controls for filter type, band gain, band res, band freq, band spread/arrangement

## 4ms SMR

- [ ] Port of 4ms Spectral Multiband Resonator — 6 resonant bandpass filters with rotation/spread

## TXo I2C Output (separate repo: er-301-stolmine)

- [x] I2C master HAL extension
- [x] TXo CV and TR units with passthrough
- [x] Gain control and V/Oct mode
- [x] Emulator monitor (txo-monitor.py)
- [ ] Hardware testing on real ER-301 + TXo
- [ ] Recompile core package for TXo firmware — core unit failed to load errors

## Release / Compatibility

- [x] Release latest Habitat updates (v0.2.0)
- [x] Vanilla ER-301 firmware compatibility for Habitat packages

## Rings Improvements

- [x] Crossfade control for main/aux outputs on mono and stereo chains
- [ ] NEON optimization check for sympathetic string and FM modes (currently only modal is vectorized)

## NEON Optimization Audit

- [ ] Repo-wide NEON check — identify hot DSP paths without SIMD and assess vectorization opportunities

## Video

- [ ] Intro video for Habitat packages (see video.md for script)
