# TODO

## Mutable Instruments Ports

- [x] Plaits (macro-oscillator — all 24 engines)
- [x] Clouds (granular processor — granular, delay, spectral modes)
- [x] Rings (modal/sympathetic string resonator)
- [x] Grids (single channel, no accent — pattern generator with clock/reset inputs)
- [x] Warps (meta-modulator — 6 xmod algorithms, vocoder pending)
- [x] Stratos (Clouds reverb engine extracted as standalone unit)
- [x] Commotio (Elements exciter — bow/blow/strike at native 48kHz)
- [x] Marbles T (probabilistic gate generator) -- 7 models, jitter, deja vu, T1/T2 crossfade
- [x] Marbles X (random CV generator) -- spread, bias, steps, deja vu, X1/X2/X3 selector, 3 control modes

### Marbles Fixes
  - [x] Both units: explicit clock Gate controls with Comparator filtering (replaced chain passthrough)
  - [x] Marbles T: silence output until first clock edge (fixes noise pop on insert)

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
- [x] Adaptive labelling for mode control (Gran/Delay/Spect)
- [ ] Gain compensation toggle — auto-scale output to match input level
- [ ] Further NEON optimization — ShyFFT butterflies, SRC polyphase FIR

## Warps Improvements

- [ ] Vocoder: implement 2x upsample/downsample for correct 96kHz filter bank
- [ ] Easter egg (frequency shifter): test and verify
- [ ] Consider exposing carrier/modulator drive as separate controls

## Stratos Improvements

- [x] Defaults match Clouds' fixed reverb settings

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
- [x] Gesture Sequencer — continuous gesture recorder/looper
  - od::Sample buffer (5/10/20s via menu), waveform display in context views
  - Auto-write from movement detection with tunable sensitivity (Low/Med/High)
  - Erase gate zeros buffer under playhead (write takes priority)
  - Output slew (0-10s), toggle run, trigger reset
  - Write indicator diamond graphic
- [x] Excel — 64-step CV tracker sequencer
  - Per-step offset, length (ticks), deviation (random)
  - Scrollable step list with live editing, shift+scroll for rapid multi-step edits
  - Expandable overview ply: playhead, loop, total ticks, fader controls for length/loop/scope
  - Math transform gate: 9 functions, scope selector, snapshot save/restore, shift-toggle sub-display
  - Clock/reset gate inputs, global slew, V/Oct scaled output (offset 1 = 1 octave)
  - Config: offset range (2Vpp/10Vpp), batch step lengths, randomize/clear offsets

### Verified on hardware
  - [x] Canals/Discont/LatchFilter true stereo (dual DSP instances, shared params)
  - [x] Clouds adaptive mode labelling (Gran/Delay/Spect via ModeSelector)
  - [x] Stratos defaults matched to Clouds reverb (hardSet in Lua)
  - [x] Excel overview: spinner, right-justified text, title bar consistency

### Uncommitted changes needing hardware test
  - [ ] Clouds (Clouds.lua)
  - [ ] Marbles T (MarblesT.lua)
  - [ ] Marbles X (MarblesX.lua)
  - [ ] Plaits (PlaitsVoice.cpp, PlaitsVoice.h, Plaits.lua)
  - [ ] Gesture (GestureSeq.lua)

### Excel/Ballot/Etcher Edit Buffer Sync
  - [ ] Excel: reload edit buffer after xform operations (selected step readout goes stale on transform)
  - [ ] Ballot: same issue -- reload after ratchet/transform changes
  - [ ] Etcher: reload after segment transforms (rotate, reverse, random)
  - Pattern: track mLastLoadedStep, call loadStep(mLastLoadedStep) after any bulk data change (same fix as Filterbank)

### Excel/Ballot Improvements
  - [ ] Xform gate control: enter-menu context view with GainBias faders (and CV input) for transform function and params
  - [ ] Pretty up Excel:
    - Animated fire circle in xform math sub-display
    - Transform ply: function icons, gate activity indicator
  - [ ] Output scope visibility: graph compiler may not schedule objects with no chain inlets. Investigate od/units/GraphCompiler.cpp. Commotio has same issue.
  - [ ] Addressable variant: CV address input instead of clock for random access
  - [ ] Expanded variant: CV inputs for sequence length and loop length
  - [ ] Probability variant: per-step probability of firing as separate unit
  - [ ] Snap-to-scale mode for offset values
  - [ ] Playhead/cursor coupling: auto-scroll display follows playback, manual nav decouples
  - [ ] Excel Gate Seq: 64-step gate sequencer with chaselight grid display, ratchet, algorithmic transforms (euclidean, NR, grids, necklace), velocity-controlled gate amplitude

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

## Serialization Audit

- [x] Rings: persist Polyphony, Resolution, Easter Egg, Int Exciter options
- [x] Warps: persist Easter Egg option
- [x] Ballot (GateSeq): persist RatchetLen, RatchetVel options
- [x] Excel (TrackerSeq): persist offsetRange10v config
- [x] Excel/Ballot: persist transform func/params/scope (ParameterAdapter Bias values)
- [x] Verify OptionControl-based serialization works (confirmed via Clouds)
- [x] GestureSeq: od::Sample buffer with waveform display, serialization via Sample.Pool
- [x] GestureSeq: context views with waveform on offset/slew/erase (FeedbackLooper pattern)

## OptionControl Indexing Audit

- [x] Fix off-by-one: add boolean=true to Plaits output, MarblesX control mode, MarblesT model, GestureSeq sensitivity
- [x] Clouds quality labels renamed normal/hifi
- [ ] MarblesT: 7 models but only 3 visible with OptionControl (max 3 choices) -- needs fader or paged UI

## Passthrough Audit

- [x] Marbles T/X: explicit clock Gate with Comparator
- [x] Grids, NR: Comparator on chain clock input
- [x] GestureSeq: sunk chain input, explicit run gate
- [x] Plaits: removed raw chain-to-Level connection
- [ ] Review remaining units for clean delineation between passthrough and non-passthrough behavior

## Ratchet / Strum

Produces a burst of gates from a single trigger input.

- [ ] Clock input, gate input, clock output
- [ ] Count: number of gates in burst (1-16)
- [ ] Spacing: time between gates (ms or tempo-synced)
- [ ] Acceleration: speed up or slow down across the burst
- [ ] Velocity curve: amplitude across burst (flat, decaying, crescendo)
- [ ] Gate length: per-pulse gate width
- [ ] When gated, outputs clock multiplied by fader value (e.g. 2x, 3x, 4x)
- [ ] When not gated, passes clock through unchanged

## Kryos (spectral freeze)

- [ ] Debug hang on load -- test in emulator first to isolate hardware vs code issue
- [ ] If emulator works: hardware-specific issue (alignment, memory, toolchain)
- [ ] If emulator hangs: DSP bug in process() or constructor
- [ ] High priority: blocks multiband spectral freeze unit (per-band freeze gates on crossover engine)

## stolmine (original units)

- [x] NR — gate sequencer (migrated from standalone package)
- [x] 94 Discont — 7-mode waveshaper (fold, tanh, softclip, hardclip, sqrt, rectify, crush)
- [x] Latch Filter — switched-capacitor S&H → SVF with V/Oct tracking
- [x] Stereo processing for Canals, 94 Discont, and Latch Filter (dual DSP instances)
- [ ] HD2-style FM oscillator pair (dual osc with FM index + feedback)
- [ ] 4-op FM voice (Akemie's Castle / TX81Z style, build on Accents XOXO voice)
- [ ] Additional filter models from monokit (MoogFF, DFM1, BMoog)

## Polyphonic Sample Playback

Spreadsheet paradigm: N voices with per-voice params.

- [ ] Polyphonic sample player with voice management (reference: Polygon voice allocation)
- [ ] Per-voice params: sample assignment, pitch, envelope, filter
- [ ] Manual grain triggering (reference: Manual Grains)

## Effects

- [ ] Tilt EQ
- [x] Canals (Three Sisters clone) — linked resonant filter with crossover/formant modes

## Canals Improvements (Three Sisters fidelity)

- [x] Custom SistersSvf primitive with soft-clip saturating integrators
  - Multi-output per sample (LP/BP/HP simultaneously)
  - Threshold-based soft clip: linear below |2|, compressed above
- [x] Per-sample processing with cascaded filter topology
- [ ] Q tuning: longer resonant decay with less gain boost. Want musical ping without volume spike.
- [ ] Gain compensation at high Q: attenuate input or output proportional to resonance
- [ ] Tune soft clip threshold and curve for best saturation character
- [ ] Investigate how SDK builtin filters handle audio-rate modulation cleanly (parameter interpolation? per-sample coefficient update?)
- [ ] Bench CPU cost on hardware; NEON vectorize if needed

## Filterbank (FFB)

Spreadsheet-style parallel fixed filter bank. Input on inlets (mono and stereo).

### Architecture

- Parallel topology: each band processes input independently, outputs summed
- 2-16 bands (integer fader, default 8). Fixed-size internal arrays (16 max) -- reducing band count hides bands, increasing reveals them with previous settings intact. Only scale snap overwrites frequencies.
- Pre-filter drive on input, tanh saturation/limiting on output

### Expanded view (6 plies, left to right)

1. **Band list** (spreadsheet, 1 ply) -- 3-char freq label per row (120, 440, 1k2, 4k0, 12k)
2. **Overview** (custom graphic, 1 ply) -- vertical orientation (freq Y-axis low-to-high, amplitude X-axis), spectral contour of composite filter response, active band highlight synced with list
3. **Scale** (ModeSelector fader) -- uses Scala files and built-in scales from firmware (Scales.lua registry + user .scl files from /scales/). Re-snaps all band freqs on change, user can manually override individual freqs after
4. **Rotate** (GainBias int fader) -- remaps band-to-frequency assignment by N positions. CV-modulatable for expressive patching
5. **Macro Q** (GainBias fader) -- global Q applied to all bands
6. **Mix** (GainBias fader) -- dry/wet balance

### Band sub-display (3 buttons)

- col1: freq (log-scaled readout, 20Hz-20kHz)
- col2: gain (-24 to +24dB)
- col3: filter type (peak/bpf/allpass, integer)

### Overview sub-display + expansion

- col1: band count (int fader, 2-16)
- col2: skew (pushes band resolution toward low or high frequencies)
- col3: slew (transition time when bands move on scale change or freq edit)

### Mix sub-display + expansion

- col1: input level (pre-filter drive)
- col2: output level
- col3: tanh amount (saturation + implicit limiting)

### Scale-to-band distribution (Lua-side, reuses firmware scale infrastructure)

- Source: er-301/mods/core/assets/Quantizer/Scales.lua (16 built-in scales as cents tables) + Scala.lua (.scl file loader for user scales in /scales/)
- Algorithm:
  1. Get cents table from scale
  2. Generate all degree frequencies across 60Hz-16kHz: freq = baseFreq * 2^(cents/1200)
  3. Greedy selection: pick N bands maximizing min log-distance
  4. Skew warps distance metric (favors low or high freqs)
  5. Sort ascending, apply rotate (circular shift)
  6. Call op:setTargetFreq(i, hz) for each band (slew in C++ handles transition)

### Filter core: stmlib::Svf with Rings-style Q scaling

- Base Q from macro Q fader (0-1 maps to 0-500)
- Q increases with frequency: 1.0 + normalizedFreq * q (from Rings resonator.cc)
- Progressive Q decay across bands: q *= q_loss (brightness-dependent, woody character)
- stmlib::Svf in BAND_PASS mode with FREQUENCY_FAST

### Menu

- Bands: init bands (reset all to defaults), randomize bands
- Macro filter type: one option per type to set all bands (all peak, all bpf, all allpass)
- Scale: load from file (opens browser for .scl files)

### Remaining

- [ ] Band list expansion: gate controls for randomize freq, gain, type across all bands
- [ ] Default mix/gain settings: unit should be transparent on load so user can immediately hear filtering. Review initial mix, input level, output level, band gain, macro Q defaults.
- [ ] Overview graphic: line curve is lopsided -- certain bands get disproportionate visual weight. Investigate evaluateResponse weighting and Q scaling across bands. May need per-band normalization or log-amplitude display.
- [x] BPF removed: 3 filter types remain (peak/LP/resonator)
- [x] Custom scales: .scl files from /scales/ auto-loaded at init, appear on scale fader alongside builtins (up to 64 custom, 128 degrees each)
- [ ] Replace skew with V/Oct offset: shift all band frequencies by a CV-modulatable pitch offset instead of warping the distribution. More musically useful and patchable.
- [ ] Radial graphic: shapes too uniform across scale/rotate changes. Investigate better data source for polygon radius (per-band Q, filter state energy, or raw spectral magnitude).

## 4ms SMR

- [ ] Port of 4ms Spectral Multiband Resonator — 6 resonant bandpass filters with rotation/spread

## TXo I2C Output (separate repo: er-301-stolmine)

- [x] I2C master HAL extension
- [x] TXo CV and TR units with passthrough
- [x] Gain control and V/Oct mode
- [x] Emulator monitor (txo-monitor.py)
- [x] Hardware testing on real ER-301 + TXo
- [x] Recompile core package for TXo firmware — core unit failed to load errors

## Release / Compatibility

- [x] Release latest Habitat updates (v0.2.0)
- [x] Vanilla ER-301 firmware compatibility for Habitat packages

## Rings Improvements

- [x] Crossfade control for main/aux outputs on mono and stereo chains
- [ ] NEON optimization check for sympathetic string and FM modes (currently only modal is vectorized)

## V/Oct Scaling Audit

- [x] V/Oct scaling: * 120 (FULLSCALE_IN_VOLTS=10 * 12 semitones) confirmed working for Plaits, Rings, Canals, LatchFilter
- [x] Plaits V/Oct: scaling done via 10x ConstantGain in Lua graph (C++ stays * 12). Changing C++ to * 120 causes hard crash on engine switch due to compiler code generation issue. Workaround is stable.

## NEON Optimization Audit

- [ ] Repo-wide NEON check — identify hot DSP paths without SIMD and assess vectorization opportunities

## Multitap Delay

Rainmaker-inspired multitap delay. 16 taps with per-tap SVF filtering, based on builtin granular delay (gives pitch shifting for free). Two independent spreadsheets (taps + filters) keep UI clean within 3-params-per-row constraint.

### Tap distribution

Taps distributed across master time window without requiring clock:
- Base: `tap_time[i] = master_time * pow((i+1) / N, skew_exp)`
- Skew=0: even spacing. Skew>0: bunch early. Skew<0: bunch late.
- Stack groups coincident taps (1/2/4/8/16 per position). Stacked taps share timing but keep individual level/pan/filter.

### UI layout (7 plies)

1. V/Oct pitch
2. Tap list (time/level/pan per tap)
3. Filter list (cutoff/Q/type per tap, reuses FFB SVF code)
4. Overview/viz (submenu: tap count, skew, stack)
5. Master time (submenu: time max 2s, feedback, feedback tone tilt EQ)
6. Xform gate (submenu: target 1, target 2, overloaded factor)
7. Mix (submenu: input level, output level, tanh saturation)

### Remaining

- [ ] Tap list: time (0-1 within window), level, pan
- [ ] Filter list: cutoff, Q, type (LP/HP/BP/notch) -- reuse FFB SVF code
- [ ] Tap distribution: skew + stack parameters
- [ ] Master time with feedback and feedback tone (tilt EQ on feedback path)
- [ ] Xform gate with dual targets and overloaded factor
- [ ] Overview viz: tap positions on timeline (gradient, particles, or bright spots)
- [ ] V/Oct pitch shifting via builtin granular delay
- [ ] Cross-feedback matrix (stretch: tap N feeds tap M)
- [ ] Budget: 16 taps ~16% CPU, ~6MB DDR for 16x2s delay lines
- [ ] Reference: stmlib delay_line.h, stmlib filter.h, FrozenWasteland (GPLv3, algorithm ref only)

## Fade Mixer

N-input crossfader with single CV position control.

- [ ] Per-input (4 inputs on top level, refer to warps for inserting mono branches): position on fade axis, curve (linear/equal-power), level trim -- sub-display params with gate control on shift
- [ ] Global: fade position (CV-controllable), fade width (overlap), output level

## Etcher (Transfer Function Designer, inspired by MI Frames)

CV-addressed piecewise transfer function. Input voltage maps to output voltage
through user-defined segments. Not a sequencer -- a voltage-to-voltage mapper.
Waveshaper, LFO sculpting, response curves, quantizer-like steps.

Input: GainBias branch (no inlet). Output: 10Vpp (-5V to +5V).

### Implemented

- [x] Segment list (spreadsheet): offset, curve (step/linear/cubic), weight (0.1-4.0)
- [x] Weight-normalized boundaries with skew (power curve warping)
- [x] Catmull-Rom cubic interpolation
- [x] 2-ply overview: transfer function polyline, segment boundaries, active highlight, playhead + dot
- [x] Deviation: snapshots on segment transition, scope (offset/curve/weight/all)
- [x] Presets: linear ramp, S-curve, staircase, random
- [x] Serialization for all segment data + global params
- [x] 16 default, 32 max segments

### Remaining

- [ ] Change input and output ranges to +/-1V. Most 301 controls are scaled for -1/1 or 0/1, so this ensures broader compatibility within the system. Users can scale externally if needed.
- [ ] Verify deviation behavior on hardware across all scopes
- [ ] Consider: audio-rate input option (inlet mode) for true waveshaping
- [ ] Transforms: adapt Excel transform pattern for segment data (rotate, reverse, random, etc.)
- [ ] Additional presets: sine wave, triangle, custom user presets via save/restore

## Traffic-like (Priority Gate Router)

Multiple gate inputs compete to set output voltage. Highest-priority active gate wins.

- [ ] just 3 gate inputs, each with a bipolar fader representing its assigned output value. priority going from left to right

## Spectrogram

FFT-based visual display unit, audio passthrough.

- [ ] FFT size: 256 / 512 / 1024
- [ ] Display: scrolling waterfall or static spectrum
- [ ] Diagnostic insert anywhere in a chain

## Spectral Envelope Follower

Inlet > BPF > envelope follower. Tracks energy in a tunable frequency band.

- [ ] Cutoff (20Hz-20kHz), bandwidth (0.1-4 octaves), attack (0.1-500ms), decay (0.1-5000ms)
- [ ] Under 1% CPU (one biquad + one-pole envelope)
- [ ] Use cases: kick detection, sibilance tracking, spectral-driven modulation

## Tone Cluster / Drone Machine

N oscillators (8-16) in spreadsheet. Reuses FFB's scale-aware distribution for placing oscillators on scale degrees.

- [ ] Per-voice: pitch (V/Oct or ratio), level, detune/drift (0-50 cents)
- [ ] Global: root pitch (V/Oct), scale select, spread/arrangement (from FFB), waveform, master drift rate
- [ ] Could share oscillator code from Plaits or use simple wavetable/saw/sine per voice

## Comb Bank

N comb filters (8-16) in parallel, Karplus-Strong / resonator territory. Reuses FFB scale distribution.

- [ ] Per-comb: pitch/delay (20Hz-20kHz), feedback (-100% to +100%), level
- [ ] Global: input level, damping, master pitch (V/Oct), scale select, spread (from FFB)
- [ ] Very cheap per voice (one delay line read + multiply + add)

## Harmonic Series Manipulator

N BPFs locked to harmonic ratios of a fundamental. Reshapes harmonic content -- FFB but harmonically locked.

- [ ] Per-harmonic: harmonic (1-32 integer ratio), level (-inf to +12dB), Q (0.1-20)
- [ ] Global: fundamental (V/Oct), input gain, mix
- [ ] Uses FFB filter code with freq = fundamental * harmonic number

## Crossover Engine Family (shared infrastructure)

Multiband comp, multiband distortion, spectral gate all share crossover/band-splitting frontend. Build once, swap per-band processing.

### Multiband Compressor
- [ ] N bands (4-8), per-band: threshold, ratio, attack/decay
- [ ] Global: crossover freqs (auto or manual), makeup gain, mix

### Multiband Distortion
- [ ] Same crossover, per-band: drive, type (tanh/fold/clip/asymmetric), level
- [ ] Global: crossover freqs, input gain, mix

### Spectral Gate
- [ ] Same crossover, per-band: threshold, attack/release, level
- [ ] Global: crossover freqs, input gain, mix

## Grain Cloud

N grains (8-16) reading from a single sample buffer. Extends builtin manual grains x N.

- [ ] Per-grain: position (0-1 in buffer), pitch (-2 to +2 octaves), level
- [ ] Global: sample select, spray/jitter, grain size, master pitch (V/Oct), pan spread

## Phaser / Flanger Designer

N allpass stages with per-stage control. Build custom modulation effects from first principles.

- [ ] Per-stage: depth, rate (0.01-20Hz), feedback (-100% to +100%)
- [ ] Global: master rate, master depth, wet/dry. Stages in series.

## Utilities

### Gated Slew
- [ ] One-pole slew that only acts when gate is high. Raw signal passes when gate low.
- [ ] Controls: rise time, fall time, gate input

### Goniometer / Lissajous
- [ ] XY scope display: goniometer (L+R vs L-R stereo field) or Lissajous (arbitrary XY)
- [ ] Correlation readout (+1 mono to -1 out of phase)
- [ ] Pure visualization, negligible DSP

### Integrator / Location Tracker
- [ ] Running accumulator with configurable leak/decay
- [ ] Shape: linear, exponential (Cold Mac location), logarithmic
- [ ] Controls: rate, decay/leak, shape, gate reset

### Pingable Scaled Random
- [ ] C++ rewrite of SuperNiCd's pingable scaled random for performance
- [ ] Clock input triggers new random values, scaled/quantized to range

### Codescan Oscillator
Reads the ER-301's own firmware binary as a wavetable. Inspired by the Buchla 259e and IME Kermit MkIII codescan modes.

- [ ] Phase accumulator indexes into firmware .text region, treating raw machine code bytes as sample values
- [ ] Scan position (GainBias): offset into firmware address space, CV-modulatable at audio rate
- [ ] Pitch (V/Oct)
- [ ] Byte-to-sample mapping: signed 8-bit normalized to -1/+1, with interpolation between samples
- [ ] Sweeping scan produces timbral animation -- tight loops sound buzzy/tonal, varied data regions sound noisy/chaotic
- [ ] Timbres unique to each firmware build

### Codescan Filter
Reads firmware binary as FIR tap weights, applied as convolution on the input signal. Companion to Codescan Oscillator.

- [ ] Read N consecutive firmware bytes as FIR kernel (16-64 taps), normalized to -1/+1
- [ ] Scan position (GainBias): offset into firmware address space, CV-modulatable
- [ ] Kernel length control (fewer taps = comb/resonance, more taps = complex spectral shaping)
- [ ] Mix (dry/wet)
- [ ] Different firmware regions produce wildly different impulse responses -- sweeping scan reshapes the filter in real time

### Varishape Oscillator
- [ ] Continuously variable waveshape: sine > triangle > saw > square > pulse
- [ ] Single shape parameter + PWM, V/Oct, sync input

### Control Forge-alike
- [ ] Multistage envelope generator, spreadsheet paradigm (each row = stage)
- [ ] Per-stage: mode (rise/fall/sustain/random/discontinuous), time, level, curve shape
- [ ] Loop points, per-stage submenu

### Waveguide
- [ ] Physical modeling: delay line + filter in feedback loop
- [ ] Exciter input, tuned to V/Oct. Pair with comb bank for extended body modeling.

### Parametric Noise
- [ ] Continuously variable spectral tilt and bandpass (not just white/pink/brown)

### Z-Plane Filter (research)
- [ ] Rossum Morpheus-style: morph between pole/zero configurations in z-plane

### TZFM Complex Oscillator
- [ ] Carrier + modulator with through-zero FM, wavefolding, sync
- [ ] Buchla-inspired territory

## Buffer Shuffler / Groovebox

Beat-slicing / buffer manipulation unit. BBCut-style stutter, shuffle, reverse, repeat on audio buffers.

- [ ] Record or assign buffer, slice by transients or fixed grid
- [ ] Per-slice: reverse, repeat count, pitch, level
- [ ] Algorithmic shuffle modes (BBCut-style probability-driven rearrangement)
- [ ] Clock-synced playback with gate-triggered stutter/glitch
- [ ] Could evolve into a groovebox framework: seq + slices + effects

## Automata Sequencer (Chess)

Grid-based sequencer where placement rules from board games drive step generation. Users place pieces (chess-inspired) with idiosyncratic movement patterns; highlighted cells become active steps. Intersections produce emergent parameter combinations.

- [ ] Grid display (8x8 or similar), pieces placed with movement patterns (rook=row/col, bishop=diagonal, knight=L-shape)
- [ ] Piece count limited (as in chess) to force creative placement
- [ ] Voices (3-6) with per-voice level, triggered by grid intersections
- [ ] Horizontal/vertical line placement as simplified input mode
- [ ] Signal input feedback: use CV to influence step placement or piece movement
- [ ] Framework: generator (seq+voice), processor (seq-driven param changes), sequencer (complex stepwise CV)
- [ ] Inspiration: Folktek Matter, Plumbutter -- freaky sequencing paradigm with generative character
- [ ] Research: other grid games / cellular automata as step generators

## I2C / External Communication

- [ ] I2C output to Crow (complement existing TXo support)
- [ ] Clock sync to audio (derive clock from audio transients or zero crossings)
- [ ] Input already exists via sc.cv etc for Crow > ER-301

## Compass (Norns port)

- [ ] Port of Compass for ER-301 -- generative sequencer with interesting visual display
- [ ] Research feasibility within ER-301 UI constraints

## Artsy Visualizers / Sound Generators

- [ ] Norns-inspired generative visual + audio units
- [ ] Glitchy eye candy: generative visuals driven by audio/CV (ref: Paratek modules)
- [ ] Pseudo-3D waveform viz: wavetable frames stacked serially for 3D view

## Port Candidates

### MIT-compatible (direct port)

- [ ] Stages LFO -- Mutable Instruments Stages (pichenettes/eurorack, MIT)
- [ ] Loom -- sequencer/pattern generator (ianjhoffman/RigatoniModular, MIT)
- [ ] Airwindows -- large library of effects (Chris Johnson, MIT). Focus on timbral/spatial/lo-fi effects, reverbs, distortion over mixing utilities

### Algorithm reference only (GPLv3, clean-room reimplementation)

- [ ] Noise Plethora (Befaco, CC-BY-NC-SA)
- [ ] FrozenWasteland (almostEric, GPLv3)
- [ ] Plateau / Amalgam (ValleyAudio, GPLv3)
- [ ] Geodesics (MarcBoule, GPLv3)
- [ ] CellaVCV (victorkashirin, GPLv3)
- [ ] NOI-VCVRACK (LeNomDesFleurs, GPLv3)
- [ ] Reformation / Venom (DaveBenham, GPLv3)

### Other

- [ ] ProCo Rat emulation -- distortion circuit modeling
- [ ] Polyphonic sample playback -- manual grains based

## Video

- [ ] Intro video for Habitat packages (see video.md for script)
