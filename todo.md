# TODO

## Release

- [x] Version bump biome and spreadsheet packages to v1.0.1 for hotfix released 2026-04-05. Rebuild and reupload.
- [x] v2.0.0 released 2026-04-09. biome 2.0.0, spreadsheet 2.0.0, catchall 0.1.0. Full rebuild against current firmware.

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
- [x] Shift-toggle sub-display audit: value-snapshot pattern for shift+home zeroing applied to all 5 Petrichor controls (FeedbackControl, TimeControl, MixControl, TransformGateControl, RatchetControl). Review: Filterbank MixControl and any future shift-toggle controls.
- [ ] Sub-display readout audit: ensure all expansion-only controls have matching readouts on parent control's shift sub-display (per feedback_expansion_subdisplay convention).
- [x] Spreadsheet list focus indication: WHITE when focused, GRAY5 when unfocused. Applied to all 4 list graphics + 5 Lua controls.

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

## Biome

- [x] Varishape Voice -- simple synth voice for quick testing. Replaces varishape osc.
  - Varishape oscillator core with shape control
  - V/Oct, f0, level (same layout as SingleCycle/Bletchley Park)
  - Gate input with built-in decay envelope (AD or just D)
  - Envelope controls amplitude; gate triggers attack, release on gate off or decay end
  - Goal: drop in a voice, patch a gate and a pitch, hear sound immediately
- [x] Bug: Bletchley Park loses file association when "moved to mixer" -- fixed, deserialize restores path
- [x] Transport -- gated clock generator. Toggle run/stop, BPM fader (1-300), 4 ppqn output (16th notes). Phase resets on start/stop.
- [x] Pecto -- comb resonator. 16 tap patterns (uniform/fibonacci/early/late/middle/ess/flat/rev-fib + 8 randomized variants), 4 slopes, 4 resonator types (raw/guitar/clarinet/sitar), xform gate randomization, dual-instance stereo, 2s buffer, adaptive labels, MixControl + TransformGateControl reuse.
- [x] Pecto: density capped at 24 (CPU/UI), sorted tap reads for cache locality, xform randomization respects cap, TI ARM ICE workaround (O1 on recomputeTaps)

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

### Verified on hardware (2026-04-01)
  - [x] Clouds quality labels (normal/hifi), OptionControl boolean fix
  - [x] Marbles T/X: OptionControl boolean fix
  - [x] Plaits: output mode cleanup (3 modes), OptionControl boolean fix
  - [x] Gesture: sensitivity OptionControl boolean fix
  - [x] Filterbank: full unit verified on hardware

### Excel/Ballot/Etcher Edit Buffer Sync
  - [x] Excel: added reload after setAllStepLengths (C++ transforms already had it)
  - [x] Ballot: added reload after 6 "Set All" tasks (lengths + velocities). C++ transforms and randomize/clear already had it.
  - [x] Etcher: already clean -- all presets and clears call reloadEditBuffer()

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

- [x] Tilt EQ
- [x] DJ Filter -- bipolar LP/HP sweep with resonance
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

Spreadsheet-style parallel fixed filter bank. Mono and stereo.

### Implemented

- [x] Parallel SVF topology: 2-16 bands, input summed through all bands with sqrt(bandCount) normalization
- [x] 3 filter types: peak (BP), LP (resonant, Q floor 5), resonator (BP, Q floor 20)
- [x] Q scaling: 1-100 range with frequency-dependent boost and progressive decay across bands
- [x] Scale-based frequency distribution: 12 built-in scales + up to 64 Scala files auto-loaded from /scales/
- [x] Greedy selection algorithm: picks N bands maximizing min log-distance from scale degrees
- [x] Rotate: circular shift of band-to-frequency assignment, CV-modulatable
- [x] V/Oct offset: post-distribution pitch shift, 10x ConstantGain for 1V/Oct tracking
- [x] Slew: per-frame exponential smoothing, 0-5 seconds direct
- [x] Mix (default 0.5), input level, output level, tanh saturation
- [x] Radial overview graphic (2 ply): Gaussian bump display with frequency-mapped angles, live energy modulation, rotation animation, 16-level Q-driven gradient fill
- [x] Band list spreadsheet: per-band freq/gain/type editing with sub-display readouts
- [x] MixControl: shift-toggle sub-display (input/output/tanh) via removeSubGraphic/addSubGraphic swap
- [x] Edit buffer auto-reload after distributeFrequencies (readouts stay in sync)
- [x] Custom scales: ModeSelector fader with builtins + .scl files, rescan from menu
- [x] Serialization for all band data and global params

### Remaining

- [ ] Band list expansion: gate controls for randomize freq, gain, type across all bands
- [x] Fine/coarse reversed on sub-display readouts. Fixed `Encoder.Coarse` -> `Encoder.Fine` across all 15 controls package-wide.
- [ ] Default gain/Q review: unit could use better defaults for immediate audibility on load

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

Rainmaker-inspired multitap delay. 8 taps (capped for CPU), 20s max int16 buffer (~1.875MB). Per-tap SVF filtering, granular pitch shift with reverse. Info display overview.

### Architecture
- Single shared int16 circular buffer, all taps read at different positions
- Per-tap SVF: LP/BP/HP/notch, FFB's Q scaling
- Granular delay engine for pitch shifting with per-grain reverse probability
- Grid-based tap distribution (Rainmaker-style): time * (i+1)/grid with skew, stack grouping
- Mono mixdown on mono chains (L+R sum)

### UI layout (7 plies)
1. V/Oct pitch (Pitch control, ConstantOffset)
2. Tap list (TapListControl: level/pan/pitch, expand: filters + stack + macros + tapCount)
3. Overview (Perlin contour viz, always-visible SD: grain/taps/stack, expand: overview + grainSize + tapCount + stack)
4. Master time (TimeControl, shift SD: grid/rev/skew, expand: time + grid + drift + reverse + skew)
5. Feedback (FeedbackControl, shift SD: tone, expand: feedback + feedbackTone)
6. Xform gate (TransformGateControl, gate trigger for randomization)
7. Mix (MixControl, shift SD: input/output/tanh, expand: mix + inputLevel + outputLevel + tanhAmt)

### Implemented
- [x] C++ skeleton: MultitapDelay class, Internal struct, buffer alloc, basic process loop
- [x] Tap list: TapListControl + TapListGraphic (level/pan/pitch sub-display)
- [x] Filter list: FilterListControl in tap expansion view (cutoff Hz/Q/type with formatFreq)
- [x] Auto-distribution: tap times from pow((i+1)/N, 2^skew), dirty-check optimization
- [x] Per-tap filtering: SVF with Q 1-30, Q-compensated feedback path, 5 types (off/lp/bp/hp/notch)
- [x] Granular pitch shift: 3 grains per tap, sine LUT envelope, per-tap semitone pitch (-24 to +24) + master V/Oct
- [x] V/Oct pitch: builtin Pitch control (ConstantOffset) with ParameterAdapter bridge
- [x] Grain size param (5-100ms) on TimeControl shift SD and expansion
- [x] TimeControl: shift SD grain/skew/tapCount, expansion view
- [x] FeedbackControl: shift SD tone, expansion view
- [x] MixControl: shift SD input/output/tanh, expansion view
- [x] Feedback stabilization: one-pole tone damping, tap-count normalization, tanhf limiter
- [x] Feedback tone: bipolar (-1 dark to +1 bright) variable one-pole on feedback path
- [x] Serialization for all tap and filter data
- [x] Shift tap vs hold on all shift-toggle controls (fine/coarse preserved)

### Remaining
- [x] Tap macros: volume (11: full/off/20-80%/asc/desc/even/odd/sine), pan (11: center/L/R/L>R/R>L/evens/odds/cluster), cutoff (6), Q (11: off/20-80%/full/asc/desc/even/odd/sine), type (16: all/evens/odds/cyclical/cluster)
- [ ] FilterListControl type label doesn't update when macros change filter type externally (readout is correct, label text stale)
- [x] Stack parameter (groups coincident taps, in overview expansion)
- [x] Xform gate: single target selector (17 positions) + depth + fire. Gate fires randomization via Bias ref pattern.
  - Targets: rnd all, rnd taps, rnd delay, rnd filters, rnd level, rnd pan, rnd pitch, rnd cutoff, rnd Q, rnd type, rnd time, rnd fdbk, rnd tone, rnd skew, rnd grain, rnd count, reset
  - Depth (0-1): max deviation amount
  - Sub-display: target / depth / fire. Gate input on comparator.
- [ ] Xform spread parameter (0-1): 0 = perturb around current values, 1 = full param range (deferred)
- [x] Raindrop overview graphic: Perlin contour viz with tap-energy hotspots, domain warp, adaptive thresholds, feedback-controlled slew
- [x] Drift parameter (per-tap sinusoidal time jitter)
- [x] Reverse parameter (per-grain playback direction probability)
- [x] Grid parameter (Rainmaker-style taps-per-beat spacing, 1/2/4/8/16)
- [x] Stack parameter (groups coincident taps, 1/2/4/8/16)
- [x] 20s delay buffer via int16 storage (2x memory efficiency)
- [x] Mono mixdown (L+R sum on mono chains via setMono)
- [x] Overview always-visible sub-display (grain/taps/stack) with addName readout formatting
- [x] Shift+home zeroing fix across all 5 sub-display controls (value-snapshot comparison)
- [ ] Improve Petrichor engine: audio quality issues at higher tap counts, granular artifacts, investigate feedback path and grain overlap. Currently capped at 8 taps for CPU stability.
- [x] Bug: tap distribution fixed -- formula was `(i+1)/grid` causing taps to pile up when grid < tapCount. Now `(i+1)/grid` with taps at multiples of masterTime when grid=1, packed tight at high grid.
- [x] Bug: feedback fixed -- tanh was compressing every feedback cycle. Now linear with tanh safety limiter only above 1.5 amplitude.
- [x] Grain bypass for unity pitch (direct buffer read when tap pitch is 0)
- [x] Fast math: LCG for inner loop rand(), fast_exp2 for pitch/skew, fast sine for drift, sqrt pan
- [x] Read-ahead prefetch (__builtin_prefetch on next tap position)
- [x] Buffer size tiers (2s/5s/10s/20s menu, time fader + xform clamped to buffer)
- [ ] Stereo optimization: dual-instance pattern doubles CPU; investigate shared buffer or interleaved processing for Petrichor and Pecto on stereo chains
- [ ] Macro filter cutoff offset: CV-modulatable continuous shift of all per-tap cutoffs. On feedback shift SD + expansion (feedback + tone + filterOffset).
- [ ] Cross-feedback matrix (stretch: tap N feeds tap M)
- [ ] Perlin noise LUT: RaindropGraphic has a working tileable 64x64 LUT approach (bake Perlin at init, bilinear sample at runtime). The firmware repo has Voronoi and Perlin screensavers that could adopt this pattern for efficient noise generation. See RaindropGraphic.h for the implementation.

## Fade Mixer

- [x] 4-input crossfader with BranchMeter controls (gain, meter, solo/mute). Equal-power crossfade, CV fade position, output level. Chain passthrough summed with crossfaded mix.

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
- [x] Audio-rate input works fine as-is
- [x] Transforms: 8 depth-controlled ops (random, rotate, invert, reverse, smooth, quantize, spread, fold) via TransformGateControl with CV gate input
- [ ] Additional presets: sine wave, triangle, custom user presets via save/restore
- [x] Fix skew asymmetry: symmetric linear shift (same approach as Parfait), bipolar -1 to +1
- [x] Fix SWIG crash on delete: private members were inside #ifndef SWIGLUA, moved outside

## Gridlock (Priority Gate Router)

- [x] 3 gate inputs with descending priority, bipolar CV values, latching output

## Spectrogram

Inline spectrum analyzer, adapted from Parfait's SpectrumGraphic. Stereo passthrough, mono mixdown for FFT analysis. 2-ply display.

- [x] C++ DSP: ring buffer + 256-point pffft FFT (extract from Parfait), stereo passthrough, L+R sum for analysis
- [x] Lua: 2-ply SpectrogramGraphic (full 20-20kHz), Catmull-Rom spline, peak hold + RMS gradient
- [x] Scope package, pffft copied from spreadsheet

## Varishape Oscillator

Raw POLYBLEP oscillator extracted from VarishapeVoice. No envelope, no gate -- pure oscillator.

- [x] C++ DSP: stages::VariableShapeOscillator wrapper, V/Oct, f0, shape, sync, level
- [x] Lua: shape, V/Oct, f0, level (matches VarishapeVoice control order)
- [x] Biome package
- [x] Fix crash on delete: SWIG class size mismatch (private pointers inside #ifndef SWIGLUA)
- [x] VarishapeVoice: same SWIG fix applied

## Flakes (granular shimmer/freeze)

C++ rewrite of Joe's Shards Lua preset. Feedback looper with self-modulating delay.

- [x] C++ DSP: int16 circular buffer (10s), freeze gate, variable delay with feedback, self-modulation engine, warble LFO, noise injection, dry/wet mix
- [x] Controls: Freeze (gate), Depth, Delay, Warble, Noise, Dry/Wet
- [x] Biome package, dual-instance stereo
- [ ] Compare to original Shards and tune self-modulation engine (threshold, envelope times, mod depths) for closer character match
- [ ] Consider adding ladder-style filter or steeper LP on feedback path
- [ ] Evaluate whether Manual Loops / sample playback element is needed for full Shards fidelity

## Spectral Envelope Follower

- [x] BPF (RBJ biquad) + one-pole envelope follower. Center freq, bandwidth, attack, decay.

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

## Parfait (Multiband Saturation)

3-band multiband saturation. Drive, tilt EQ, weight/skew crossover, 7 shapers, SVF morph filter, compressor, FFT spectrum display.

### Implemented
- [x] Drive + tilt EQ (one-pole split, variable freq)
- [x] Weight/skew crossover (Etcher-style normalization, 12dB/oct one-pole)
- [x] 7 shapers per band (tube/diode/fold/half-rect/crush/sine/fractal) with amount/bias, anti-alias LP
- [x] SVF morph filter per band (off/LP/BP/HP/notch, freq, Q)
- [x] Single-knob compressor (program-dependent envelope, SC HPF)
- [x] FFT spectrum display (256-point pffft, peak hold + RMS, Catmull-Rom spline with adaptive tension, per-pixel gradient)
- [x] BandControl with cycling shift sub-display (shaper + filter params)
- [x] DriveControl with tone/freq sub-display
- [x] ParfaitMixControl with comp/output/tanh sub-display
- [x] Dual-instance stereo, all per-band params via Bias refs
- [x] SpectrumGraphic replaces fader (setControlGraphic container pattern)

### Remaining
- [x] Per-shaper gain management -- half rect (tanh soft clip), sine fold (decoupled 1-3x depth), fractal (decoupled 0.5-2x depth, clamped to stable region)
- [x] Safety limiter after band sum (~1.5x soft clip, before comp/output chain)
- [x] Symmetric skew -- replaced pow(accum, exponent) with linear shift in log-freq space, equal octave range both directions
- [x] Expansion views for bands (level, amt, bias, type w/ ModeSelector labels, wt, freq, morph, Q)
- [x] Expansion views for drive (drive, tone amount, tone freq) and mix (mix, comp, SC HPF, output, tanh)
- [x] Filter morph adaptive labels via addThresholdLabel (off/LP/L>B/BP/B>H/HP/H>N/ntch) -- BandControl sub-display + expansion ThresholdFader
- [x] LR4 crossover (24dB/oct) -- 4 cascaded one-pole stages per split point
- [x] Fast math: IEEE 754 fast_log2/fast_exp2 for compressor, fast_sinf for sine fold
- [x] Shaper type 0 = Off (passthrough, default) -- shapers shifted to 1-7
- [x] CPU profiling on am335x -- 13% idle, 25% with filters and saturation active
- [x] Defaults tuning -- current defaults are good

## Crossover Engine Family (shared infrastructure)

Multiband comp and spectral gate share crossover/band-splitting frontend with Parfait. Build once, swap per-band processing.

### Impasto (Multiband Compressor) -- COMPLETE
- [x] 3-band LR4 crossover (shared from Parfait), per-band feedforward compression (CPR algorithm)
- [x] Per-band: threshold (cubic-scaled), ratio, speed (G-Bus breakpoints), band level
- [x] Sidechain input split through same crossover for frequency-aware detection
- [x] FFT spectrum per band with Catmull-Rom GR ceiling contour, peak-hold GR readout
- [x] Band level dotted line indicator, dimmed excess RMS above GR ceiling
- [x] CompBandControl (GainBias + shift-toggle comp params), CompMixControl (auto makeup + output)
- [x] CompSidechainControl (adapted from tomf's SidechainMeter)
- [x] Drive/tone EQ, skew, auto makeup, dry/wet mix
- [x] Dual-instance stereo, option serialization

### Spectral Gate
- [ ] Same crossover, per-band: threshold, attack/release, level
- [ ] Global: crossover freqs, input gain, mix

### Stepwise Multi-Effect (dblue Glitch / Infiltrator-style)
- [ ] Sequenced effect chain: each step applies a different effect to the audio
- [ ] Effect types: stutter/repeat, reverse, bitcrush, downsample, filter sweep, pitch shift, tape stop, gate, distortion, buffer shuffle
- [ ] Per-step: effect type, depth/amount, probability, duration (or sync to clock)
- [ ] Global: step count, clock/rate, dry/wet, randomize
- [ ] Spreadsheet paradigm: step list with per-step params, list graphic showing effect sequence
- [ ] Clock input for tempo sync, gate input for trigger/reset
- [ ] Buffer capture for stutter/reverse/shuffle effects (short circular buffer, ~1s)
- [ ] Could share some infrastructure with Ballot (step sequencer) and Petrichor (buffer management)

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
- [x] Per-sample slew limiter with gate activation. Up/both/down modes. Stereo.

### Goniometer / Lissajous
- [ ] XY scope display: goniometer (L+R vs L-R stereo field) or Lissajous (arbitrary XY)
- [ ] Correlation readout (+1 mono to -1 out of phase)
- [ ] Pure visualization, negligible DSP

### Integrator / Location Tracker
- [x] Running accumulator with rate, leak (decay toward zero), trigger reset. Clipped +/-5V.

### PSR (Pingable Scaled Random)
- [x] C++ rewrite (1 object vs 11 in Lua original). Trigger, scale, offset, quantize levels.

### Bletchley Park (Codescan Oscillator)
- [x] Reads libstolmine.so as wavetable. 256-byte windows, scan position, V/Oct, sync. Timbres unique per build/platform.
- [x] Scan restricted to random 4096-byte region per insert for finer timbral control
- [x] Working on hardware

### Station X (Codescan FIR)
- [x] Reads libstolmine.so as FIR kernel (4-64 taps, normalized). Scan position, dry/wet mix.
- [x] Working on hardware
- [ ] Needs attention on output character (mostly noise-like filtering)

### Lambda (Seeded Procedural Synth) -- catchall package, WIP
- [x] PRNG-seeded DSP: generates oscillator waveform + filter coefficients on load/reseed
- [x] Oscillator: weighted harmonic sum, seeded amplitudes/phases
- [ ] Filter: random pole/zero placement (Cytomic SVF cascade, like Sfera but generated)
- [ ] Scan parameter: morphs through coefficient banks like a wavetable position
- [ ] Behind VCA for amplitude control
- [ ] V/Oct tracking on oscillator
- [ ] Reseed: menu task or gate input, each seed = unique repeatable instrument
- [ ] Seed is serialized so patches recall the same sound
- [ ] Spreadsheet package (procedural generation + scan = complex UI)

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

### Rauschen (Parametric Noise) -- COMPLETE
- [x] 11 algorithms: White, Pink, Dust, Particle, Crackle, Logistic, Henon, Clocked, Velvet, Gendy, Lorenz
- [x] X/Y params with log/quadratic curves on all algos, post-generator SVF morph filter with V/Oct
- [x] Phase space viz (3D rotating x[n]/x[n-1]/x[n-2] with auto-scaling + phosphor decay)
- [x] CutoffControl shift sub-display with addThresholdLabel morph labels
- [x] ThresholdFader for expansion morph fader (reusable pattern)
- [x] Spreadsheet package
- [x] Algorithm tuning: Pink gain normalization + spectral thinning Y, Crackle SC-style abs() fold, Logistic focused on chaotic region with iteration rate Y, Henon wider param ranges with fold, Gendy tripled perturbation with Levy jumps and multi-breakpoint updates, Lorenz sub-stepping for accurate integration

### Helicase (2-Op FM Oscillator) -- COMPLETE
- [x] Carrier + modulator with OPL3 waveforms, self-feedback, discontinuity folder
- [x] 3 custom visualizations: k-means metaball overview, transfer curve, circular ribbon
- [x] V/Oct fix (was 100x scaling), FM scaling fix (use modInc not carrierInc)
- [x] ModMix: additive carrier+modulator blend (not crossfade)
- [x] Carrier shape parameter (OPL3 waveform 0-7) on overview sub-display
- [x] Decimated ring buffer for full attractor capture regardless of pitch
- [x] Overview viz: k-means clustered metaballs from phase space, 6-24 dynamic clusters
- [x] Overview viz: voronoi edges with Z-occlusion, inverted contrast, bead stipple, depth shading
- [x] Overview viz: spectral brightness (zero-crossing rate) and per-cluster noise LFOs
- [x] Overview viz: frame-skip rendering (every other frame cached) for encoder responsiveness
- [x] Modulator ribbon: Catmull-Rom interpolation (384 segments), DC blocker, snapshot+slew
- [x] Transfer curve: reads discIndex/discType live from C++ for CV modulation reactivity
- [x] Lin/expo toggle: replaced ParameterAdapter with od::Option, TZFM at +-100Hz*modIndex
- [x] Per-sample linear freq ramp for clean TZFM pitch tracking
- [x] Overview expansion view: modMix, carrier shape, lin/expo OptionControl
- [x] GUI stack overflow fix: work arrays moved to heap members
- [x] Move to Spreadsheet category
- [x] Research Just Friends run modes for dynamic sync paradigm
- [x] Sync ply: phase-receptivity sync (JF-inspired). Custom HelicaseSyncControl, latched pending edges fire when modulator crosses phase threshold (0.0=hard sync, 0.5=soft sync, 1.0=subharmonic lock)
- [x] Modulator feedback: tanh soft clip to tame runaway at high feedback
- [ ] Sync: PolyBLEP at carrier reset discontinuity
- [x] Discontinuity folder: fold shapes 8-15 (triangle fold, sine fold, hard fold, staircase, wrap, asymmetric fold, Chebyshev T3, ring fold)
- [x] Lo-fi/hi-fi config menu toggle (OPL bit-depth quantization, hard/morphable shapes, fine fader steps)
- [ ] Carrier shape: consider wider selection beyond OPL3 set
- [ ] CPU profiling on am335x

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
