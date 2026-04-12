u# er-301-habitat

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

Output: `testing/<arch>/<package>-<version>.pkg`

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
| **kryos** | Kryos | Spectral freeze (WIP) -- do not install |

### Peaks / Dead Man's Catch

Based on code by Émilie Gillet and Tim Churches (MIT License). These still need some testing for hardware parity. If you're willing, just sound off.

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
| **biome** | NR | Gate sequencer inspired by the Noise Engineering Numeric Repetitor |
| | 94 Discont | 7-mode waveshaper (fold, tanh, softclip, hardclip, sqrt, rectify, crush) |
| | Latch Filter | Switched-capacitor S&H into SVF with V/Oct tracking |
| | Canals | Linked resonant filter inspired by Three Sisters -- crossover/formant modes |
| | Gesture | Continuous gesture recorder/looper -- 5/10/20s buffer, movement-detected auto-write |
| | Gated Slew | Slew limiter with gate-controlled activation |
| | Tilt EQ | One-knob spectral tilt filter |
| | DJ Filter | Bipolar LP/HP sweep filter |
| | Gridlock | Priority gate router with latching output |
| | Integrator | Running accumulator with leak and reset |
| | Spectral Follower | Adaptive threshold envelope follower with bandpass detector |
| | Quantoffset | Quantizer with CV offset |
| | PSR | Pingable scaled random |
| | Bletchley Park | Codescan wavetable oscillator -- reads arbitrary binary files as waveforms |
| | Station X | Codescan FIR filter -- reads binary files as filter kernels |
| | Fade Mixer | 4-input crossfader with BranchMeter controls |
| | Varishape Voice | Simple synth voice -- POLYBLEP oscillator (tri/saw/square), gate-triggered decay envelope |
| | Varishape Osc | Raw POLYBLEP oscillator -- continuously variable sine/tri/saw/square/pulse, V/Oct, sync |
| | Pecto | Comb resonator -- 16 tap patterns, 4 resonator types (raw/guitar/clarinet/sitar), V/Oct, xform gate randomization |
| | Transport | Gated clock generator -- BPM control, 4 ppqn (16th note) output, toggle run/stop with phase reset |
| **spreadsheet** | Excel | 64-step CV tracker sequencer with math transforms |
| | Ballot | 64-step gate sequencer with chaselight display and algorithmic transforms |
| | Etcher | CV-addressed piecewise transfer function -- 8 depth-controlled transforms, CV gate input |
| | Tomograph | Parallel resonant filter bank with scale distribution |
| | Petrichor | Multitap delay -- 8 taps, per-tap SVF/pitch, granular reverse, drift, grid/stack distribution, macro presets, gate-triggered randomization |
| | Parfait | 3-band multiband saturator -- 7 shapers per band, SVF morph filter, compressor, per-band FFT spectrum display |
| | Rauschen | Parametric noise and chaos generator -- 11 algorithms (White, Pink, Dust, Particle, Crackle, Logistic, Henon, Clocked, Velvet, Gendy, Lorenz), post-generator SVF morph filter with V/Oct, 3D phase space visualization |
| | Impasto | 3-band multiband compressor -- per-band FFT spectrum with GR ceiling contour, sidechain input, G-Bus speed control, auto makeup |
| | Helicase | 2-op FM oscillator -- OPL3 carrier + modulator with 16 fold shapes, JF-style phase-receptivity sync, lin/expo FM, lo-fi/hi-fi toggle, k-means phase-space viz |
| | Larets | Stepped multi-effect -- 10 effects (stutter/reverse/bitcrush/downsample/filter/pitch shift/distortion/shuffle/delay/comb) with clock-locked buffer tricks, CPR single-band compressor, bipolar global param offset, 16-step sequencer with xform gate |
| **scope** | Scope, Scope 2x, Scope Stereo | Inline signal visualization -- stereo-aware passthrough with waveform display |
| | Spectrogram | Inline FFT spectrum analyzer -- 256-point pffft, stereo passthrough, peak hold + RMS gradient |
| **catchall** | Sfera | Z-plane morphing filter -- 32 configs, audio-reactive ferrofluid visualization (experimental) |
| | Lambda | Seeded procedural synth -- PRNG wavetable + filter bank generation (experimental) |
| | Flakes | Granular shimmer/freeze -- feedback looper with self-modulating delay (experimental) |

## Changelog

### v2.1.0

**Larets + Helicase polish pass.** See [RELEASE-2.1.0.md](RELEASE-2.1.0.md) for full details. spreadsheet v2.0.0 -> v2.1.0; all other packages unchanged.

Larets: CPR single-band compressor, Parfait-style Mix ply with auto-makeup, grain pitch shift (Dattorro two-grain overlap), hard-clip distortion with extended drive, clock-locked stutter (beat repeat), fresh-per-loop shuffle, inverted bitcrush direction with 12-bit ceiling, 1-16 loop length with momentary-hold floor, bipolar global param offset, unified step-list type readout (`addName` + `useHardSet`), new reverse/bitcrush/comb visualizations, tape stop and gate effects removed.

Helicase: single brightness measure (mean |Δ| / RMS), blob gray ceiling raised to white, sync phase threshold exposed on expansion GainBias (CV modulatable without touching the sync main view).

### v2.0.1

**Plaits v1.4.0** -- fixed hard crash on engine switch above index 7-10. Shared arena allocator re-initialized on engine change (same fix as Clouds mode switch).

### v2.0.0

**10 new units, major updates.** See [RELEASE-2.0.0.md](RELEASE-2.0.0.md) for full details.

New units: Impasto (multiband compressor), Parfait (multiband saturator), Rauschen (11-algorithm noise/chaos generator), Pecto (comb resonator), Transport (gated clock), Varishape Osc (POLYBLEP), Spectrogram (FFT analyzer), Flakes (granular shimmer), Lambda (seeded procedural synth), Sfera (z-plane morphing filter).

Major updates: Petrichor (granular pitch, grid/stack, macros, xform gate), Etcher (8 transforms + CV gate), Bletchley Park (restricted scan range).

New catchall package for experimental/WIP units. Bug fixes for SWIG class layout crashes (VarishapeOsc, VarishapeVoice, Etcher). biome v2.0.0, spreadsheet v2.0.0, scope v1.1.0.

### v1.4.0

**Package split: stolmine -> biome + spreadsheet**
- biome: NR, 94 Discont, Latch Filter, Canals, Gesture, Gated Slew, Tilt EQ, DJ Filter, Gridlock, Integrator, Spectral Follower, Quantoffset, PSR, Bletchley Park, Station X, Fade Mixer
- spreadsheet: Excel, Ballot, Etcher, Tomograph, Petrichor

**New units: biome**
- Gated Slew: slew limiter with gate-controlled activation
- Tilt EQ: one-knob spectral tilt
- DJ Filter: bipolar LP/HP sweep
- Gridlock: priority gate router with latching output
- Integrator: running accumulator with leak and reset
- Spectral Follower: adaptive threshold envelope follower with bandpass detector
- Quantoffset: quantizer with CV offset
- PSR: pingable scaled random (C++ rewrite)
- Bletchley Park: codescan wavetable oscillator, reads arbitrary binary files as waveforms, hex address scan label, DC blocker, file chooser menu
- Station X: codescan FIR filter, reads binary files as filter kernels, DC blocker, file chooser menu
- Fade Mixer: 4-input crossfader with BranchMeter controls
- Varishape Voice: simple synth voice with POLYBLEP oscillator (tri/saw/square morph), gate-triggered decay envelope, V/Oct pitch

**New unit: spreadsheet**
- Petrichor: Rainmaker-inspired multitap delay, 8 taps, 20s int16 buffer, per-tap SVF filtering (LP/BP/HP/notch), granular pitch shift with reverse, grid/stack tap distribution, tap/filter/volume/pan/cutoff/Q/type macro presets, gate-triggered randomization with 21 targets, feedback tone damping, V/Oct pitch, info overview display

**Petrichor**
- fast_tanh Pade approximant replaces tanhf in audio path
- Encoder fix: RaindropControl no longer swallows encoder events when no readout focused
- Max taps capped to 8 for CPU stability
- DelayInfoGraphic overview: taps, time, grid, stack, grain, xform target

**Bletchley Park + Station X**
- Hardware file loading via od::FileReader
- ScanControl with adaptive hex address label
- DC blocker (~20Hz one-pole highpass) on output
- Level control with VCA, control layout matches SingleCycle
- File path serialization, deferred load for boot stability
- Fixed std::string ABI hang on ARM (replaced with char array)

**Build**
- Requires er-301-stolmine firmware with FrameBuffer::readPixel moved to end of class for vanilla compatibility
- Install script supports dev, release, and third-party package sets
- All packages rebuilt against txo.8.6 firmware

### v1.3.2

**Gesture**
- Auto-write: recording driven by offset movement detection, no manual write gate needed
- Erase gate: zeros buffer under playhead when held (write takes priority)
- Run is now toggle mode, reset is trigger mode
- Write indicator: diamond graphic lights up when auto-write is active

### v1.3.1

**Serialization**
- Fixed save/load across all units (onSerialize/onDeserialize replaced with proper serialize/deserialize overrides)
- Plaits: trig mode now persists correctly
- Rings: polyphony, resolution, easter egg, internal exciter options now persist
- Warps: easter egg option now persists
- Excel: offset range config, transform function/factor/scope now persist
- Ballot: ratchet toggles, transform function/params/scope now persist
- Excel/Ballot: step data and xform label correctly restored on load

**Gate filtering**
- Marbles T/X: replaced chain passthrough with explicit clock Gate controls (Comparator filtered)
- Marbles T: silence output until first clock edge (fixes noise pop on insert)
- Grids, NR: added Comparator filtering on chain clock input
- GestureSeq: sunk chain input, added explicit run Gate control
- Plaits: removed raw chain-to-Level connection that caused signal passthrough

**Excel**
- Removed 0.1x output scaling (1 offset = 1V)

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

