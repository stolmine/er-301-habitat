# TODO

## Bugs

### Excel
- [x] Playhead stuck past new bound on stepCount reduce ("14/1"). mStep modulo-wraps into the new range each audio block once mCachedSeqLength updates. Same fix applied to GateSeq (Ballot).
- [x] Offset range switch now scales all stored offsets proportionally (×5 on 2V→10V, ×0.2 on 10V→2V) and reloads the edit buffer. Readout updates live; pattern shape is preserved and round-trip reversible.

### Ballot
- [x] Ratchet settings (RatchetLen, RatchetVel toggle options) + RatchetMult now persist. Options rebased to 1/2 convention (0 was CHOICE_UNKNOWN sentinel), enableSerialization() in C++ constructor, mult round-tripped via ParameterAdapter Bias target/hardSet.
- [ ] Gate width 0 produces a trigger instead of silence. May be intentional — decide if this is desired behavior or a bug.

### Pecto
- [ ] CPU spikes on certain settings (high density + long comb + sitar/clarinet resonator). Profile on am335x.
- [ ] Zipper noise on V/Oct and comb size changes. Needs per-block interpolation or one-pole smoother on delay length.

### Tomograph
- [x] Overview viz: distended bottom lobe / off-position band spokes on hardware. Root cause was runtime sinf/cosf from package .so miscomputing on am335x; firmware-compiled graphics unaffected. Fixed with 72-entry cos/sin LUT in FilterResponseGraphic (77ca47a). LUT memory saved for future package graphics.
- [ ] FilterListControl type label doesn't update when macros change filter type externally (readout is correct, label text stale).

### Helicase
- [ ] Discontinuity transfer functions 7, 12, 15 click at zero crossings. Value discontinuity in fold/wrap/ring-fold cases needs interpolation or smoothing.

### Impasto
- [x] Auto gain + sidechain enable only affect one channel in stereo. Toggle handlers fan out to both op instances; deserialize re-syncs opR to op (52b3213).
- [ ] Impasto menu: stereo option re-labels to "mono" / "stereo" but the saved option state can disagree with actual instantiation if toggled without re-insert.

### Kryos
- [ ] Debug hang on load — test in emulator first to isolate hardware vs code issue.

### Etcher
- [x] Skew loaded at 1.0 instead of 0.0 (neutral). `initialBias = 1.0` on the GainBias overrode `skew:hardSet("Bias", 0.0)` in onLoadGraph. Changed to 0.0.

### Larets
- [ ] Stutter vs shuffle viz distinction: both read similar at a glance. Stutter should show boxed loop window, shuffle should show rearranged fragment blocks.
- [x] Playhead stuck past new bound on stepCount reduce. Same mStep modulo-wrap as Excel/Ballot, applied at top of audio block after stepCount refresh. Etcher audited: mActiveSegment is recomputed from input CV each sample, so no stale-playhead vector exists there.

### Step-list units (Excel, Ballot, Larets, Etcher)
- [x] Count reduction below current cursor: graphic now clamps mSelectedStep to listLen-1 at top of draw() (viewport follows automatically). Lua controls reconcile currentStep on onCursorEnter so edit-buffer params track the clamped step.

### Serialization & stale-label inventory (spreadsheet package)

Progress this session:
- [x] Excel (TrackerSeq): complete -- per-step state, offsetRange10v, xform func+paramA/B/scope via ParameterAdapter target/hardSet, xform funcLabel refreshed in deserialize.
- [x] Ballot (GateSeq): RatchetLen/Vel options rebased to 1/2 convention + enableSerialization() in C++ constructor; RatchetMult ParameterAdapter round-tripped; xform funcLabel + ratchet len/vel labels refreshed in deserialize.
- [x] Etcher: all top-level ParameterAdapter Biases + per-segment state round-tripped. No options, no stale labels.
- [x] Petrichor (MultitapDelay): 23 top-level ParameterAdapter Biases + tune Offset + xformGate Threshold round-tripped via Excel pattern. Tap state already covered.
- [x] Parfait (MultibandSaturator): 33 ParameterAdapter Biases (9 global + 24 per-band) round-tripped; BandMute0/1/2 Parameters got enableSerialization() in C++ constructor.
- [x] Rauschen: 7 top-level ParameterAdapter Biases round-tripped; algo fader label refreshed via ModeSelector:updateLabel() in deserialize.
- [x] Impasto (MultibandCompressor): stereo opR sync for AutoMakeup/EnableSidechain options in deserialize (52b3213). Options have enableSerialization() in the Lua control init, not in C++ constructor -- working per user but minor inconsistency.
- [x] Helicase: LinExpo + HiFi options enableSerialization() in C++ constructor; 12 ParameterAdapter Biases + tune Offset + syncComparator Threshold round-tripped; lin/expo overview label refreshed via updateLinExpo() in deserialize.
- [x] Larets: AutoMakeup option enableSerialization() in C++ constructor. Labels are all dynamic (track currentStep, chain mnemonic) -- no stale-label surface.

Remaining gaps:
- [x] Tomograph (Filterbank): `scale` ParameterAdapter Bias added to serialize/deserialize; ModeSelector fader label refreshed via `updateLabel()` in deserialize so the scale name matches the restored index on load.
- [x] Petrichor macro fader labels: 6 MacroControls now get `updateLabel()` called in a loop at end of MultitapDelay:deserialize, so vol/pan/pitch/cutoff/Q/type preset names match the restored Bias on load.
- [x] Impasto (MultibandCompressor): `mAutoMakeup.enableSerialization()` and `mEnableSidechain.enableSerialization()` moved to the C++ constructor for consistency with Ballot/Helicase/Larets. Also added full Excel-pattern round-trip for all 28 ParameterAdapter Biases (7 global + 21 per-band) so sidechain input gain fader and all band threshold/ratio/speed/attack/release/weight/level values persist. opR option sync on deserialize retained for legacy patches.

Cross-cutting patterns established this session (saved to memory):
- od::Option toggles must use 1/2 values (never 0; it's CHOICE_UNKNOWN sentinel).
- `enableSerialization()` in the C++ constructor is the safest placement -- the flag is set before any Lua or framework save/load path runs.
- For N>2 or CV-modulatable controls, use ParameterAdapter + Bias, round-trip via target()/hardSet("Bias", v) in Lua.
- Any control whose label is derived from a restorable value (ModeSelector fader name, toggle ON/off badge, discrete function name) needs an explicit `updateLabel()` or equivalent call at the end of the unit's deserialize -- the framework restores the underlying value but does not trigger label refresh.

## Release

- [x] Version bump biome and spreadsheet packages to v1.0.1 for hotfix released 2026-04-05. Rebuild and reupload.
- [x] v2.0.0 released 2026-04-09. biome 2.0.0, spreadsheet 2.0.0, catchall 0.1.0. Full rebuild against current firmware.
- [x] v2.1.0 released 2026-04-12. spreadsheet 2.1.0 -- Larets and Helicase shipped as first-release public units.
- [x] Test procedures draft: `docs/test-procedures.md` -- per-unit checklists covering insert, every control, sub-displays, expansion views, menus, CV behavior, stereo routing, save/load round-trip, and edge cases for all units across spreadsheet/biome/catchall/scope/stolmine packages. Global sanity pass appended. Runs in a couple of hours pre-release.

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
- [ ] Shift button behavior audit across spreadsheet package. The shift button is used heavily (mode toggles on RatchetControl / TransformGateControl, drive-param expansion on DriveControl, param-mode swap on HelicaseOverviewControl, shift-home zeroing via value-snapshot pattern, etc.) and one unit in particular (RatchetControl/TransformGateControl family) has been tuned to feel right. Codify the rules into a single reference document and cross-check every shift-aware spreadsheet control against them so the package behaves in harmony with the 301's existing UI language. Rules to capture: press vs. hold vs. release semantics; whether shift-tap toggles a display mode or modifies the next input; how shiftDeferred / shiftSnapshot guard against accidental mode flips while editing readouts; when shift+home zeros vs. restores; shift+encoder fine-edit conventions; interaction with focus / unfocus; how mode-switched sub-graphics should advertise state (label text change vs. badge). Per-control checklist afterward.

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

- [ ] Drum Kit Sequencer (working name "Kit" or similar). Monophonic unit, single CV outlet, meant to sit on the V/Oct of a mono drum voice and drive it through a kit-style pattern. Typical use: one voice chain (OSC + noise + env + VCA + filter) tuned so that different pitches (or different values of whichever param the outlet is patched to) produce different drum characters -- low CV for kicks, higher for snares with noise balance tweaked, highest for hats. Currently this requires hand-dialing an Excel sequence and mentally mapping step values back to drum roles.

  Design goals, distinct from Excel:
  - Step UI is drum-oriented: each step has a drum-role tag (kick / snare / hat / tom / rest / custom) that drives a per-role CV preset, not a raw number. You build a pattern by stamping tags into steps rather than dialing offsets per step.
  - Role presets are user-editable and recallable (save/load "my kick" = -5V, "my snare" = -2V, etc.) so one sequencer is reusable across differently-tuned voices.
  - Visual step list reads as a drum grid rather than a fader bank -- kick icon, snare icon, rest, etc. Reads the pattern at a glance.
  - Global gate/trig output alongside CV so the voice can be enveloped on the same edges (or integrate trigger generation with Ballot's gate behavior -- possibly just a tight pairing).
  - Xform gate for random swaps within a role set (e.g. randomize which steps are snares vs. ghost notes).

  Difference from Excel: Excel is a generic CV sequencer that happens to work for pitch; Kit is a sequencer whose UI paradigm is "drum pattern" and whose values come from a named-role preset bank rather than being dialed per step.

  Difference from Control-Forge-alike: CF is time-interpolated multistage (envelope-shaped). Kit is step-triggered on clock edge, single outlet, no ramping.

  Open design questions: how many roles (6 covers most kits -- kick/snare/hat/tom/clap/rest), whether roles map to preset CV values or to small envelope segments (e.g. a role could be a 50ms CV ramp that triggers a pitch-drop on a kick), whether the preset bank is per-patch or global.

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
  - [ ] Excel Gate Seq: 64-step gate sequencer with chaselight grid display, ratchet, algorithmic transforms (euclidean, NR, grids, necklace), velocity-derived gate amplitude
  - [x] Ballot ratchet settings persist -- rebased options to 1/2 convention, enableSerialization in C++ constructor, RatchetMult round-tripped via ParameterAdapter Bias target/hardSet (see Bugs/Ballot).

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

- [ ] See Bugs section. If emulator works: hardware-specific issue. If emulator hangs: DSP bug.
- [ ] High priority: blocks multiband spectral freeze unit (per-band freeze gates on crossover engine)

## Microsound / electroacoustic family (research first)

Audio units, not visualizers. Aesthetic target: clicks and cuts / microsound / electroacoustic glitch in the vein of Raster-Noton artists (Alva Noto, Ryoji Ikeda, Carsten Nicolai), and the interacting-oscillator-lattice feel of Ciat-Lonbarde Plumbutter (Peter Blasser). Not a single unit -- a set of related DSP tools that share a sensibility.

- [ ] Research phase (depth-first before unit design):
  - Raster-Noton production methods: what techniques recur across the catalog (sine-wave rhythm cells, micro-edit stutter, spectral masking, DC-click percussion, click-and-drone layering)? Target artists to read about: Alva Noto, Carsten Nicolai / Frank Bretschneider, Ryoji Ikeda. Read interviews, look for tooling references.
  - Peter Blasser's Plumbutter schematics (Ciat-Lonbarde, paper circuit lineage). Schematics should be public on ciat-lonbarde.net / blasser's site. Understand Rollz (rhythm generator via charge-bucket pulse logic), Gongs (tuned LC resonators hit by percussive pulses), Lattice (cross-coupling between oscillators that produces organic interlocking rhythms), and the stereo "butter" electroacoustic smear on the output.
  - Extract 2-3 DSP kernels that make sense for ER-301: e.g. a charge-bucket pulse divider, a tuned-resonator bank hit by pulses rather than audio, a lattice-style coupling matrix between oscillators, a click-train + DC-pulse percussion generator.
- [ ] Candidate unit directions (pick after research):
  - Click / tick generator: sine-wave pulses, DC clicks, configurable density and spectral character. Think Ikeda's "+ / -" catalog.
  - Resonator bank hit by pulses (not audio-excited): 4-8 tuned resonators, percussive excitation, stereo spread. The Gongs kernel.
  - Charge-bucket rhythm generator: interacting pulse dividers with cross-feedback, produces polyrhythmic-but-musical patterns from simple counter logic. The Rollz kernel.
  - Lattice oscillator: 4-6 tuned oscillators with cross-coupling matrix, outputs their sum plus modulated resonance. Settles into organic interlocking patterns.

## stolmine (original units)

- [x] NR — gate sequencer (migrated from standalone package)
- [x] 94 Discont — 7-mode waveshaper (fold, tanh, softclip, hardclip, sqrt, rectify, crush)
- [x] Latch Filter — switched-capacitor S&H → SVF with V/Oct tracking
- [x] Stereo processing for Canals, 94 Discont, and Latch Filter (dual DSP instances)
- [x] 2-op FM oscillator (Helicase — OPL3 waveforms, discontinuity folder, phase-receptivity sync)
- [ ] X-op FM voice (4-op or beyond, Akemie's Castle / TX81Z style, build on Accents XOXO voice)
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
- [ ] Combined Petrichor + Pecto unit (cut down, CPU-efficient). The two live together musically -- multitap delay into comb resonator, or comb-bed for a delay network -- but they currently compete heavily for CPU when stacked on the same chain, so users have to choose one. Explore a single DSP object that shares buffer, grain, and filter infrastructure between the two. Candidates for the cut-down: fewer taps, fewer comb tap patterns, single filter type per tap, one resonator model instead of four (or audio-rate xfade if needed). Goal is a unit that costs closer to one of them than both summed, while preserving the shared musical character. Design sketch first, then prototype DSP.

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
- [x] Overview viz lobe positioning issue -- fixed via LUT trig (see Bugs/Tomograph).

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
- [ ] See Bugs section: FilterListControl type label stale on macro changes.
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
- [ ] Tap pitch macro: CV-modulatable continuous pitch offset applied to every tap's pitch param (like the filter-cutoff macro above but for pitch). Existing tap macros cover volume/pan/cutoff/type/Q but not pitch. Semitones ideally, -24..+24 range, applied additively on top of per-tap pitch values so the per-tap structure is preserved.
- [x] Serialization audit -- all 23 top-level ParameterAdapter Biases (masterTime/feedback/mix/tapCount/feedbackTone/vOctAdapter/{vol,pan,pitch,cutoff,q,type}Macro/xform{Target,Depth,Spread}/grainSize/skew/drift/reverse/stack/grid/{input,output}Level/tanhAmt) plus tune Offset and xformGate Threshold now round-tripped via Excel target/hardSet pattern. Per-tap state already covered. Reload restores full patch.
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
- [ ] See Bugs section: auto gain + sidechain stereo drift.
- [x] Repo-wide stereo audit complete. Dual-instance (opR) units: Canals, Discont, LatchFilter, Pecto (biome); Flakes, Sfera (catchall); Filterbank, MultibandCompressor/Impasto, MultibandSaturator/Parfait (spreadsheet). All params and gate inputs properly mirrored to both instances. MI ports (Rings, Clouds, Warps, Plaits, Stratos, Commotio) use native stereo inside a single op -- not dual-instance. **Only Impasto has the option-toggle stereo drift** (see entry above). Pecto's `setTopLevelBias` only targets `op` but `applyRandomize` runs on both ops with NULL-guarded pointers and shared ParameterAdapter Bias targets, so the effect is identical on both channels -- not a bug.

### Spectral Gate
- [ ] Same crossover, per-band: threshold, attack/release, level
- [ ] Global: crossover freqs, input gain, mix

### Larets (Stepwise Multi-Effect, dblue Glitch / Infiltrator-style)
- [ ] Sequenced effect chain: each step applies a different effect to the audio
- [ ] Effect types: stutter/repeat, reverse, bitcrush, downsample, filter sweep, pitch shift, tape stop, gate, distortion, buffer shuffle
- [ ] Per-step: effect type, depth/amount, probability, duration (or sync to clock)
- [ ] Global: step count, clock/rate, dry/wet, randomize
- [ ] Spreadsheet paradigm: step list with per-step params, list graphic showing effect sequence
- [ ] Clock input for tempo sync, gate input for trigger/reset
- [ ] Buffer capture for stutter/reverse/shuffle effects (short circular buffer, ~1s)
- [ ] Could share some infrastructure with Ballot (step sequencer) and Petrichor (buffer management)

### Larets Polish

- [x] Overview viz: scale Y by 2x, then auto-normalize (0.9 / peak) with 8%/frame smoothing so waveform always fills the frame.
- [x] Pitch shift: two-grain Dattorro-style overlap pitch shifter (sin^2 windows, 180 deg phase offset). Adapted idea from SDK MonoGrainDelay.
- [x] Distortion makeup: 1/sqrt(drive) to level-match other effects.
- [x] Compressor at full: -40 dB threshold, 20:1 ratio, 1 ms attack. Actual limiter territory.
- [x] Bitcrush viz: quantize in display-pixel space (min 3 px step), segments always visible.
- [x] Stutter: snapshot at step start, loop a musical fraction of the clock period (1/16, 1/8, 1/4, 1/2, 1 tick). Beat-repeat feel.
- [x] Shuffle: chunk-quantized swaps across a one-tick snapshot, min ~43 ms chunks.
- [x] Type readout parallax: replaced separate integer readout + label pair with a single Readout using `addName` (same `(int)(v+0.5f)` rounding as storeStep). Also called `useHardSet()` on the step-list readouts so the parameter's target and value stay in sync -- the actual off-by-one was softSet's 50-step ramp, not rounding, which made the displayed target name disagree with the storeStep-visible interpolated value.
- [x] Global param offset: bipolar (-1..1) top-level ply between overview and xform. Non-destructive, applied per-sample at the point the step param is read. Pitch and filter moved off their step-transition caches so CV-modulated offset tracks smoothly.
- [x] Comb viz rework: envelope fill with concentric inner waveform copies, nested inside the envelope so nothing escapes frame bounds. Up to 7 inner rings at low param (tight comb), collapsing to a single contour at max.
- [x] Gate removed. Tried a 64-pattern clock-locked bank (duty gates, Euclidean, NR primes) with 1/16-tick base slot, but Larets steps are too short in samples for rhythmic gating to land -- sub-tick pattern slots slip into AM buzz well before anything musical emerges. Better to reclaim the slot.
- [ ] Stutter vs shuffle viz distinction: both currently use column-level segment swaps / clustered groups and read similar at a glance. Stutter should telegraph "fixed loop of recent audio" (maybe a boxed window with repeated contour fragments), shuffle should telegraph "chunks rearranged" (fragment blocks visibly shifted or color-coded by source).
- [x] Shuffle fresh per loop: rewritten as a beat-repeat (stutter-style snapshot, musical-fraction loop length) that picks a new random start offset into a two-tick buffer window on every loop wrap.
- [x] Bitcrush viz: replaced staircase with hash-based particle fill between the waveform contour and centerY. Density `area * (0.15 + fxParam * 0.55)`, brightness scales with crush, motion via `mVizPhase` in the per-frame hash seed. Waveform silhouette drawn on top in WHITE so the shape stays readable.
- [x] Distortion rework: hard clip against a unit-ceiling limiter (was tanh soft knee) for brighter character and visibly squared-off waveform. Drive range extended to 1-20 so even moderate input clips at high settings. Makeup `1/drive^0.7` keeps the step sitting slightly louder than bare input at max.
- [x] Overview expansion: added dedicated GainBias faders for step count and loop length alongside skew. Expansion now matches the sub-display.
- [x] Loop length rework: range now 1-16 with default 16 (effective wrap clamped to step count). Minimum 1 gives a momentary-hold single-step repeat. Old patches with loopLength=0 migrate to 16. Overview readouts also got `useHardSet` for target/value consistency.
- [x] Bitcrush range + direction: inverted so param=0 is clean (12-bit, `2^12`=4096 levels) and param=1 is most crushed (`2^2.5`≈5.66 levels, matches old param=0.1). Now agrees with the viz (low param = many thin bands, high param = few big bands).
- [x] Tape stop removed. Deceleration behavior never felt musical in this stepped context; cleaner to reclaim the slot than to keep tuning it.
- [x] Reverse viz rework: sweep a highlighted band right-to-left across the waveform at the playback rate. Reads visually as "playing backwards".

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

### Varishape Oscillator -- COMPLETE
- [x] Continuously variable waveshape: sine > triangle > saw > square > pulse
- [x] Single shape parameter + PWM, V/Oct, sync input

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
- [ ] Overview viz: more reactive. Workable as-is but feels like it smooths over quick parameter wiggles. Candidates: faster brightness slew (0.1 -> 0.2), faster centroid slew (0.10 -> 0.2), briefer min/max contraction (currently 0.01 -- maybe 0.03 for quicker re-zoom), per-cluster LFO coupled to brightness so breathing speeds up with timbral richness. Per-cluster noise could also respond to disc index/type rather than being purely seeded.
- [ ] See Bugs section: discontinuity shapes 7/12/15 click at zero crossings.
- [x] Phase-receptivity sync threshold modulatable: added a dedicated GainBias expansion fader ("phase") alongside the unmodified sync ply. Uses the already-existing `syncThreshold` ParameterAdapter + mono branch. No custom control needed -- sync main view unchanged, expansion driven entirely by `views.sync = { "sync", "syncPhase" }`.

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
