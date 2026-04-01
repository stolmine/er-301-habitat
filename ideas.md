# Habitat Ideas

## The Spreadsheet Paradigm

The ballot/excel list UI pattern generalizes to any unit with N identical nodes, each with a set of parameters displayed as rows in a spreadsheet. The user scrolls through rows, edits per-node values, and the DSP processes all nodes in a loop. This is a strong foundation for multi-channel audio generators, effects, and sequencers.

### Candidate units for the spreadsheet paradigm

- **Multitap delay** — N delay taps, each with time/feedback/filter/level/pan
- **Fixed filter bank (FFB)** — N bands, each with freq/gain/Q/type
- **Fade mixer** — N inputs with crossfade position, level, pan
- **Frames-like interpolator** — N keyframes with parameter snapshots, morph between them
- **Polyphonic sample player** — N voices with sample assignment, pitch, envelope, filter
- **Step sequencer** — N steps with pitch/gate/probability/ratchet per step

---

## Multitap Delay

Rainmaker-inspired multitap delay. 16 taps with per-tap SVF filtering, based on builtin granular delay (gives pitch shifting for free). Two independent spreadsheet lists — taps and filters — keep the UI clean within the 3-params-per-row sub-display constraint.

### Architecture: Two Independent Lists

**Tap list** and **filter list** are separate spreadsheets. Filters act where taps are present — filter N applies to tap N. Filters without a corresponding active tap are inactive (saves CPU). Reuses FFB filter code for the SVF implementation.

### Tap distribution

Taps are distributed across the master time window without requiring a clock:

- Base: `tap_time[i] = master_time * pow((i+1) / N, skew_exp)`
- **Skew = 0:** even spacing
- **Skew > 0:** taps bunch early, spread late (decelerating echo)
- **Skew < 0:** taps spread early, bunch late (accelerating echo)

**Stack** groups coincident taps at shared time positions:

| Stack | Positions | Taps per position |
|---|---|---|
| 1 | 16 | 1 |
| 2 | 8 | 2 |
| 4 | 4 | 4 |
| 8 | 2 | 8 |
| 16 | 1 | 16 |

Stacked taps share timing but keep individual level/pan/filter from their spreadsheet rows. Stack=2 with opposing pans gives instant stereo spread at half the mental overhead.

### UI layout (7 plies)

| Ply | Control | Sub-display / Submenu (enter for gain/bias) |
|---|---|---|
| 1 | **V/Oct pitch** | no submenu |
| 2 | **Tap list** | time / level / pan per tap, spreadsheet nav |
| 3 | **Filter list** | cutoff / Q / type per tap, spreadsheet nav |
| 4 | **Overview/viz** | submenu: tap count, skew, stack |
| 5 | **Master time** | submenu: time (max 2s), feedback, feedback tone (tilt EQ) |
| 6 | **Xform gate** | submenu: target 1, target 2, overloaded factor |
| 7 | **Mix** | submenu: input level, output level, tanh saturation |

All submenu params expandable to gain/bias controls on enter.

### Tap list parameters (sub-display columns)

| Parameter | Range | Notes |
|---|---|---|
| Time | 0 – 1 | position within master time window (pre-skew) |
| Level | -inf – 0dB | per-tap output level, 0 = inactive |
| Pan | L – R | stereo output placement |

Pitch shift accessible via V/Oct master control.

### Filter list parameters (sub-display columns)

| Parameter | Range | Notes |
|---|---|---|
| Cutoff | 20Hz – 20kHz | SVF cutoff frequency |
| Q | 0.1 – 20 | resonance |
| Type | LP / HP / BP / notch | per-filter, switchable |

### Master time submenu

- **Time** — max delay window, 2s ceiling for CPU/RAM budget
- **Feedback** — global amount, always taken from last tap with level > 0
- **Feedback tone** — tilt EQ on the feedback path (darken or brighten repeats)

### Xform gate

Gate input that triggers parameter transformation. Submenu:

- **Target 1** — parameter to modulate (from a list of all delay params)
- **Target 2** — second parameter to modulate
- **Factor** — overloaded macro control: low values move both targets in tandem, mid values diverge them, high values randomize along the upper range

### Overview / visualization

Tap positions shown on a timeline. Considering: gradient display, particles on trigger, or bright spots at tap positions with intensity = level and shade = filter cutoff.

Submenu: tap count (1–16), skew, stack.

### Performance budget (48kHz, 128-sample frames)

- Per tap: ~20-25 ops/sample (granular delay read + SVF + level/pan)
- 16 taps: ~16% CPU — comfortable
- Memory: 16 taps x 2s max delay = ~6MB DDR (256MB available)

### Reference

- Intellijel Rainmaker for conceptual reference (tap grouping, distribution)
- FrozenWasteland multitap concepts (GPLv3 — algorithm reference only, clean-room)
- Builtin granular delay for delay line + pitch shift implementation
- FFB unit for SVF filter implementation (reuse directly)

---

## Fixed Filter Bank (FFB)

Classic studio FFB with spreadsheet control over every band.

### Per-band parameters

| Parameter | Range | Notes |
|---|---|---|
| Frequency | 20Hz – 20kHz | center frequency |
| Gain | -24dB – +24dB | boost/cut |
| Q / Resonance | 0.1 – 20 | bandwidth control |
| Type | LP / HP / BP / peak / notch | per-band |
| Solo / Mute | on/off | for dialing in |

### Configurations

- **Classic FFB mode** — fixed logarithmic spacing, gain-only control per band
- **Parametric mode** — full control over every band's frequency and Q
- 8, 12, or 16 bands — selectable

---

## Fade Mixer

N-input crossfader with a single position control that morphs between sources.

### Per-input parameters

| Parameter | Range | Notes |
|---|---|---|
| Input source | audio input | assigned channel |
| Position | 0 – 1 | where this input is placed on the fade axis |
| Curve | linear / equal-power | crossfade shape |
| Level trim | -inf – 0dB | pre-fade level |

### Global parameters

- **Fade position** — single CV-controllable position that sweeps through all inputs
- **Fade width** — how much overlap between adjacent sources
- **Output level** — master gain

---

## Spectral Envelope Follower

Inlet > BPF > envelope follower. Tracks energy in a tunable frequency band. Useful for kick detection, sibilance tracking, driving parameters from specific spectral content.

### Controls (4 plies, no submenus)

| Control | Range | Notes |
|---|---|---|
| Cutoff | 20Hz – 20kHz | center frequency of tracked band |
| Bandwidth | 0.1 – 4 octaves | width of band, Q derived internally |
| Attack | 0.1ms – 500ms | envelope rise time |
| Decay | 0.1ms – 5000ms | envelope fall time |

CPU: under 1% (one biquad + one-pole envelope follower).

---

## Multimode Filter

Single filter unit extracted from FFB codebase. Exposes LP, peaking, and resonator modes from the same SVF core.

### Controls

| Control | Range | Notes |
|---|---|---|
| Cutoff | 20Hz – 20kHz | filter frequency |
| Q / Resonance | 0.1 – 20 | |
| Gain | -24dB – +24dB | boost/cut (peaking mode) |
| Type | LP / peak / resonator | mode select |

CPU: negligible (single SVF).

---

## Ratchet / Strum

Produces a burst of gates from a single trigger input.

### Parameters

- **Count** — number of gates in the burst (1–16)
- **Spacing** — time between gates (ms or tempo-synced)
- **Acceleration** — speed up or slow down across the burst
- **Velocity curve** — how amplitude changes across the burst (flat, decaying, crescendo)
- **Gate length** — per-pulse gate width

---

## Traffic (Priority Gate Router)

Multiple gate inputs compete to set an output voltage. Highest-priority active gate wins.

### Per-input parameters (spreadsheet)

| Parameter | Range | Notes |
|---|---|---|
| Gate input | gate/trigger source | |
| Priority | 1 – N | tie-breaking order |
| Output voltage | -5V – +5V | voltage emitted when this gate wins |
| Slew | 0 – 1000ms | portamento to this voltage |

---

## Spectrogram

FFT-based visual display unit. Passes audio through unchanged, renders frequency content.

- FFT size: 256 / 512 / 1024
- Display: scrolling waterfall or static spectrum
- Useful as a diagnostic insert anywhere in a chain

---

## Port Candidates

### MIT-compatible (direct port OK)

- **Stages LFO** — Mutable Instruments Stages (pichenettes/eurorack, MIT)
- **Loom** — sequencer/pattern generator (ianjhoffman/RigatoniModular, MIT)
- **Airwindows** — large library of effects (Chris Johnson, MIT)

### Algorithm reference only (GPLv3 — clean-room reimplementation required)

- Noise Plethora (Befaco, CC-BY-NC-SA)
- FrozenWasteland (almostEric, GPLv3)
- CellaVCV (victorkashirin, GPLv3)
- Geodesics (MarcBoule, GPLv3)
- NOI-VCVRACK (LeNomDesFleurs, GPLv3)
- Plateau / Amalgam (ValleyAudio, GPLv3)
- Reformation / Venom (DaveBenham, GPLv3)

### Other ideas

- **ProCo Rat emulation** — distortion circuit modeling
- **Polyphonic sample playback** — manual grains based

---

## More Spreadsheet Unit Ideas

### Tone Cluster / Drone Machine

N oscillators (8-16) in a spreadsheet. Covers both chord/cluster use and slow-evolving drone textures depending on parameter choices. Reuses FFB's scale-aware distribution model — oscillators are placed on scale degrees relative to root, with the same spread/arrangement logic that spaces FFB bands.

| Per-voice | Range | Notes |
|---|---|---|
| Pitch | V/Oct or ratio | absolute or relative to root |
| Level | -inf – 0dB | per-voice mix |
| Detune / Drift | 0 – 50 cents | slow random drift amount for drone mode |

Global: root pitch (V/Oct), scale select, spread/arrangement (from FFB), waveform select, master drift rate, output level. Could share oscillator code from Plaits or use simple wavetable/saw/sine per voice.

### Comb Bank

N comb filters (8-16) in parallel or series, Karplus-Strong / resonator territory. Reuses FFB's scale-aware distribution model — combs tuned to scale degrees relative to root.

| Per-comb | Range | Notes |
|---|---|---|
| Pitch / Delay | 20Hz – 20kHz | tuned comb frequency |
| Feedback | -100% – +100% | negative = inverted comb |
| Level | -inf – 0dB | per-comb mix |

Global: input level, damping, master pitch (V/Oct), scale select, spread/arrangement (from FFB). Very cheap per voice — one delay line read + multiply + add.

### Phaser / Flanger Designer

N allpass stages with per-stage control. Build custom modulation effects from first principles.

| Per-stage | Range | Notes |
|---|---|---|
| Depth | 0 – 100% | modulation depth |
| Rate | 0.01 – 20Hz | LFO rate |
| Feedback | -100% – +100% | per-stage feedback |

Global: master rate, master depth, wet/dry. Stages in series. Could get exotic phasers impossible with fixed hardware.

### Multiband Compressor

N bands (4-8) with crossover splitting and per-band dynamics.

| Per-band | Range | Notes |
|---|---|---|
| Threshold | -60dB – 0dB | compression onset |
| Ratio | 1:1 – inf:1 | compression amount |
| Attack/Decay | combined or separate | envelope speed |

Global: crossover frequencies (auto-spaced or manual), makeup gain, mix. UI challenge: showing gain reduction per band in the overview — could do a bar graph or per-band meter in the viz ply.

### Multiband Distortion / Saturation

Same crossover splitting as multiband comp, but per-band saturation instead of dynamics.

| Per-band | Range | Notes |
|---|---|---|
| Drive | 0 – 100% | saturation amount |
| Type | tanh / fold / clip / asymmetric | distortion character |
| Level | -inf – 0dB | post-distortion band level |

Global: crossover frequencies, input gain, mix. Could share the crossover/splitting code with multiband comp — same frontend, different per-band processing.

### Spectral Gate

N bands with crossover splitting (shared with multiband comp/distortion). Per-band gate instead of compression — bands below threshold get silenced.

| Per-band | Range | Notes |
|---|---|---|
| Threshold | -60dB – 0dB | gate onset |
| Attack/Release | combined or separate | gate envelope |
| Level | -inf – 0dB | post-gate band level |

Global: crossover frequencies, input gain, mix. Uses same crossover engine as multiband comp and multiband distortion. Good for noise removal, transient isolation, creative gating.

### Grain Cloud

N grains (8-16) reading from a single sample buffer, each with independent parameters. Multiplies the builtin manual grains concept by N — spreadsheet defines a frozen granular texture.

| Per-grain | Range | Notes |
|---|---|---|
| Position | 0 – 1 | location in sample buffer |
| Pitch | -2 – +2 octaves | per-grain transposition |
| Level | -inf – 0dB | per-grain mix level |

Global: sample select, spray/jitter (randomizes positions), grain size, master pitch (V/Oct), pan spread. Each grain is essentially a manual grain instance — base the DSP on the builtin.

### Multiband Spectral Freeze (blocked on Kryos)

N bands with independent freeze gates. Freeze selective frequency ranges while others pass through. Depends on solving the Kryos hang-on-load bug first — once that's resolved, extend the freeze DSP across the crossover engine's band splitting.

| Per-band | Range | Notes |
|---|---|---|
| Freeze gate | on/off | per-band freeze toggle |
| Blend | 0 – 100% | frozen ↔ live mix per band |
| Level | -inf – 0dB | post-freeze band level |

Global: crossover frequencies, input gain, mix. Part of the crossover engine family.

### Crossover Engine Family (shared infrastructure)

Multiband comp, multiband distortion, and spectral gate all share the same crossover/band-splitting frontend. Build once with configurable band count and crossover frequencies, swap per-band processing:

- Comp: envelope follower → gain reduction
- Distortion: waveshaper (tanh/fold/clip)
- Gate: envelope follower → threshold switch

### Harmonic Series Manipulator

N BPFs locked to harmonic ratios of a fundamental. Reshapes harmonic content of existing audio — like an FFB but harmonically locked.

| Per-harmonic | Range | Notes |
|---|---|---|
| Harmonic | 1–32 | integer ratio of fundamental |
| Level | -inf – +12dB | boost/cut this harmonic |
| Q | 0.1 – 20 | selectivity |

Global: fundamental (V/Oct), input gain, mix. Uses FFB filter code with frequencies derived from fundamental × harmonic number.

---

## Utilities

### Gated Slew

One-pole slew limiter that only acts when gate input is high. Raw signal passes through when gate is low.

Controls: rise time, fall time, gate input. Possibly: separate rise/fall enables.

### Goniometer / Lissajous

XY scope display. Two modes (or one unit with mode switch):
- **Goniometer:** L+R vs L-R stereo field visualization, with correlation readout (+1 mono → -1 out of phase)
- **Lissajous:** arbitrary two-input XY plot for phase relationships, FM ratios, envelope shapes

Pure visualization — negligible DSP, just display math.

### Integrator / Location Tracker

Running accumulator inspired by Cold Mac's location circuit. Input adds to a running sum with configurable leak/decay to prevent runaway.

Includes locational tracking mode: rate is inversely proportional to distance from target. Large jumps traverse quickly, small movements crawl — exponential decay toward target where the tail length is constant regardless of step size. Shape parameter crossfades between linear (constant speed), exponential (Cold Mac-style location), and logarithmic (slow start, fast finish).

Controls: integration rate, decay/leak, shape (linear → exponential → logarithmic), gate reset. Useful as slow random walk, positional tracker, gravity-like CV generator, nonlinear slew.

### Pingable Scaled Random (C++ rewrite)

Rewrite of Joe's (SuperNiCd) pingable scaled random in C++ for performance. Clock input triggers new random values, scaled/quantized to range.

### Z-Plane Filter

Rossum Morpheus-style filter emulation — algorithmically derived z-plane filter model. Morph between filter pole/zero configurations in the z-plane. Heavy DSP research item.

### TZFM Complex Oscillator

Through-zero FM complex oscillator arrangement. Carrier + modulator with TZFM, wavefolding, sync. Classic Buchla-inspired territory.

### Pseudo-3D Waveform Viz

Classic wavetable visualization where frames are stacked serially to produce a 3D view. Display-only unit or integrated into wavetable/oscillator units.

### Waveguide

Physical modeling waveguide synthesis. Delay line + filter in feedback loop. Exciter input, tuned to V/Oct. Could pair with comb bank for extended body modeling.

### Parametric Noise

Noise generator with controllable spectral shape. Rather than fixed white/pink/brown, continuously variable spectral tilt and bandpass parameters.

### Control Forge-alike

Multistage envelope generator with submenus and modes per stage. Spreadsheet paradigm — each row is a stage.

| Per-stage | Notes |
|---|---|
| Mode | rise / fall / sustain / random / discontinuous |
| Time | stage duration |
| Level | target level |

Discontinuous mode: multipoint spread within segment (scatter of values rather than smooth transition). Random mode: bounded random walk for duration of stage. Full submenu per stage for curve shape, loop points, etc.

### Glitchy Eye Candy

Visual-only unit(s) for fun. Generative glitch visuals driven by audio or CV. No audio processing, pure display. Reference: Paratek modules.

### Varishape Oscillator

Single oscillator with continuously variable waveshape. Morph between sine → triangle → saw → square → pulse via single shape parameter + PWM. V/Oct, sync input.

---

### Matrix Mixer (exploratory)

N×M grid of level crosspoints. Interesting conceptually but constrained by ER-301's single-unit stereo output limit and unclear whether mono branches can be nested. Worth investigating what's possible within the SDK. If feasible, even a simple N-in stereo-out summing mixer with per-channel level/pan controlled via spreadsheet would be useful.
