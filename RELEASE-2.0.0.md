# er-301-habitat v2.0.0

Release date: 2026-04-09

Requires firmware: v0.7.0-txo (er-301-stolmine)

67 commits since v1.4.1. 10 new units, major updates to Petrichor and Etcher, package reorganization.

## New Units

### Impasto (spreadsheet) - Multiband Compressor

3-band multiband compressor with per-band FFT spectrum display and gain reduction visualization.

- LR4 crossover (24dB/oct)
- Per-band feedforward compression (adapted from tomf's CPR algorithm)
- Speed control follows SSL G-Bus attack/release breakpoints (0.1ms-30ms attack, 0.1s-1.2s release)
- Threshold uses cubic fader scaling for usable compression range across full throw
- Sidechain input split through same crossover for frequency-aware detection
- Per-band FFT spectrum with Catmull-Rom gain reduction ceiling contour
- Band level control via GainBias with dotted level indicator on spectrum
- Peak-hold GR readout per band
- Auto makeup gain, drive with tone EQ, skew, dry/wet mix
- Dual-instance stereo
- Layout: drive, sidechain, lo, mid, hi, skew, mix

### Parfait (spreadsheet) - Multiband Saturator

3-band multiband saturation with per-band FFT spectrum display.

- LR4 crossover (24dB/oct), weight/skew band distribution
- 7 shapers per band: tube, diode, tri-fold, half-rect, crush, sine, fractal
- Per-shaper gain management and safety limiter
- SVF morph filter per band (off/LP/BP/HP/notch) with threshold labels
- Single-knob compressor with sidechain HPF
- 256-point pffft FFT spectrum per band with Catmull-Rom spline and per-pixel gradient
- BandControl with cycling shift sub-display, DriveControl, ParfaitMixControl
- Expansion views for all plies
- Fast math (IEEE 754 log2/exp2) for compressor and sine fold
- 13% idle, 25% active CPU on am335x

### Rauschen (spreadsheet) - Parametric Noise and Chaos Generator

11 algorithms with X/Y parameter controls, post-generator SVF morph filter with V/Oct tracking, and 3D rotating phase space visualization.

Algorithms:
- White: decimation + bit crush
- Pink: white-to-brown tilt (X), spectral thinning (Y)
- Dust: log-scaled sparse impulses
- Particle: random impulses into resonant bandpass, frequency spread control
- Crackle: chaotic attractor with abs() fold, energy injection control
- Logistic: bifurcation map (r=3.45-4.0), iteration rate for pitched chaos
- Henon: 2D chaotic map, periodic islands through full chaos
- Clocked: sample-and-hold noise with interpolation crossfade
- Velvet: sparse bipolar impulses with variable pulse width
- Gendy: Xenakis stochastic synthesis with Levy-like jumps
- Lorenz: 3D chaotic attractor with sub-stepped integration

Post-generator filter morphs through off/LP/BP/HP/notch with V/Oct tracking. Phase space viz shows 3D rotating attractor with auto-scaling and phosphor decay.

### Pecto (biome) - Comb Resonator

16 tap patterns (uniform/fibonacci/early/late/middle + randomized variants), 4 resonator types (raw/guitar/clarinet/sitar with amplitude-dependent delay mod). Gate-triggered xform randomization with target/depth control. Adaptive ModeSelector labels. Dual-instance stereo, 2s buffer.

### Transport (biome) - Gated Clock

Toggle run/stop, BPM fader (1-300), 4 ppqn output (16th notes). Phase resets on start/stop. BPM-to-Hz conversion in C++.

### Varishape Osc (biome) - POLYBLEP Oscillator

Raw variable-shape oscillator extracted from VarishapeVoice. Continuously variable sine-triangle-saw-square-pulse via single shape macro. V/Oct, f0, sync.

### Spectrogram (scope) - Inline Spectrum Analyzer

256-point pffft FFT with stereo passthrough and mono mixdown for analysis. Catmull-Rom spline rendering with peak hold and RMS gradient. 2-ply display.

### Flakes (catchall) - Granular Shimmer/Freeze

C++ rewrite of Joe's Shards preset. Feedback looper with self-modulating delay, freeze gate, warble LFO, noise injection. Dual-instance stereo.

### Lambda (catchall, experimental) - Seeded Procedural Synth

PRNG generates wavetable frames and SVF filter bank from a seed number. Scan parameter morphs through generated timbres. Waveform oscilloscope display.

### Sfera (catchall, experimental) - Z-Plane Morphing Filter

32 hand-curated z-plane configs with 2D morphing across 128 cubes. 7 cascaded Cytomic SVF sections. Audio-reactive ferrofluid visualization with spin parameter, directional lighting, and envelope-driven dynamics.

## Major Updates

### Petrichor (spreadsheet) - Multitap Delay

- Granular pitch shift with per-tap pitch and grain size
- Grid parameter (1/2/4/8/16 taps-per-beat), stack parameter
- Drift (sinusoidal time jitter), reverse probability
- 5 tap macros (volume/pan/cutoff/type/Q distributions)
- Gate-triggered randomization via Bias ref pattern
- Perlin noise contour overview graphic with energy-modulated rendering
- Read-ahead prefetch, grain bypass for unity pitch, fast math
- Int16 delay buffer (20s), mono mixdown on mono chains

### Etcher (spreadsheet) - Transfer Function

- 8 depth-controlled transforms: random, rotate, invert, reverse, smooth, quantize, spread, fold
- Transform gate with CV input (same TransformGateControl UI as Excel and Ballot)
- Symmetric skew fix (linear shift replacing asymmetric pow() curve)

### Bletchley Park (biome) - Codescan Oscillator

- Scan restricted to random 4096-byte region per insert for finer timbral control
- Each instance explores a different neighborhood of the binary data

## Bug Fixes

- VarishapeOsc: crash on delete (SWIG class size mismatch)
- VarishapeVoice: same crash fix
- Etcher: crash on delete (13 private members hidden from SWIG)
- Parfait: ARM hang from pffft NULL work buffer
- Pecto: ARM ICE from density randomization

## UI Improvements

- ThresholdFader and addThresholdLabel adopted across spreadsheet units
- Spreadsheet list focus indication: dim selection when unfocused
- Reversed fine/coarse on sub-display readouts fixed across 15 controls
- Shift-toggle sub-display: value-snapshot pattern for shift+home zeroing

## Package Changes

- New **catchall** package (v0.1.0) for experimental/WIP units
- **biome** v1.0.1 to v2.0.0 (17 to 20 units)
- **spreadsheet** v1.0.1 to v2.0.0 (5 to 8 units)
- **scope** v1.0.0 to v1.1.0 (added Spectrogram)
- All 14 packages rebuilt against current firmware

## Install

Copy `.pkg` files from `testing/am335x/` to SD card at `/mnt/ER-301/packages/`.

Remove old packages before installing:

```
sudo rm /mnt/ER-301/packages/*-stolmine.pkg
```

Delete installed libs from rear card to force reinstall:

```
sudo rm -rf /mnt/v0.7/libs/biome/
sudo rm -rf /mnt/v0.7/libs/spreadsheet/
sudo rm -rf /mnt/v0.7/libs/scope/
sudo rm -rf /mnt/v0.7/libs/stolmine/
```
