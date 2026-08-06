ER-301-HABITAT TODO (generated — DO NOT EDIT)
============================================

*Generated from `planning/ledger.toml` by `scripts/dev render`. Edit the
ledger, not this file. Status/verification are gate-enforced (`scripts/dev
check`): a `done` item must have a real test or its named artifact.*

**172 items** — 44 done, 19 wip, 107 todo, 2 blocked. *Rendered 2026-08-05.*

## DSP

| | id | item | verify |
|---|---|---|---|
| ~ | `anamnesis-insert-crash` | Anamnesis am335x insert: data-abort in Event_post (heap corruption from insert path) | manual · 2026-07-12 |
| ~ | `biome-expo-envelopes` | Biome: simple exponential envelopes - Expo D and Expo AD with expo-variation controls | manual *(attested)* · 2026-07-22 |
| ~ | `ca-noise-texture` | Cellular-automata granular noise texture source (Vivary) | manual · 2026-07-16 |
| ~ | `fabula-am335x` | Fabula reverb am335x port: hybrid-float + pow2-mask + zipper-fix | manual · 2026-07-12 |
| ~ | `fdn-reverb` | NEON FDN reverb (Householder core + per-line filters + spectral flavor overlay) | manual · 2026-07-16 |
| ~ | `ngoma-character-campaign` | Pin down Ngoma Character control - mechanism from firmware, fix the engine to match hardware | manual · 2026-07-23 |
| ~ | `ngoma-grit-noise-persistence` | Grit noise bed decays too fast vs hardware - separate short envelope (60ms fixed / 150ms cap) | manual · 2026-07-23 |
| ~ | `ngoma-grit-tuning` | Ngoma grit: noise phase-mod too weak below 0.8 - re-fit depth vs re-pinned hardware | manual · 2026-07-23 |
| ~ | `ngoma-shape-fm-campaign` | Ngoma/Tessera Shape does not reproduce hardware's FM-ish activity (even at low clipping) | manual *(attested)* · 2026-07-22 |
| ~ | `ngoma-shape0-level-campaign` | Ngoma shape-0 fundamental level structure (oscillator phase coherence at low r) | manual · 2026-07-23 |
| ~ | `trinity-fm-unit` | Profile Modbap Trinity FM voice + build ER-301 FM drum unit | manual · 2026-07-20 |
| ~ | `vitrail-unit` | Vitrail - dual switched-capacitor character filter unit | manual · 2026-07-18 |
|   | `alembic-comb-retrofit` | Alembic: Doppler smoother + idx-wrap-ulp guard retrofit on the comb; fix direct sample-swap retrain | manual · 2026-07-09 |
|   | `alembic-userbias-derivations` | Alembic Phases 7-8: per-region user-bias sub-params + Order 2/3 derivations + sample excitation | manual · 2026-07-09 |
|   | `biome-utility-dsp-units` | biome DSP utility units: simple allpass, dome filter, frequency shifter, bitcrush/downsample | manual · 2026-07-09 |
|   | `canals-audiorate-and-cpu` | Canals: audio-rate Span-sweep aliasing (halfband decimator) + hardware CPU/NEON pass | manual · 2026-07-09 |
|   | `canals-response-tuning` | Canals: SPAN/volume/low-band/Q/soft-clip response tuning against hardware capture corpus | manual · 2026-07-09 |
|   | `compound-dsp-voice-profile` | Profile + emulate on-hand compound DSP module (workflow shakedown) | manual · 2026-07-17 |
|   | `drywet-crossfade-audit` | Audit all wet/dry units for the linear-crossfade center dip; switch decorrelated wets to equal-power | manual · 2026-07-15 |
|   | `fabula-diffusion-makeup` | Fabula: AGC / intelligent gain control to make up volume lost to diffusion | manual · 2026-07-14 |
|   | `fabula-dry-under-hpf` | Fabula: dry passthrough underneath the 200Hz wet HPF | manual · 2026-07-14 |
|   | `fabula-xform` | Fabula: xform (transform/randomize) control, modeled on Pecto/Petrichor | manual · 2026-07-14 |
|   | `flakes-shards-fidelity` | Flakes: tune self-modulation engine to Shards, steeper LP on feedback, evaluate manual-loops fidelity | manual · 2026-07-09 |
|   | `fx-delay-units` | Combined Petrichor+Pecto (CPU-efficient) + character-forward Lofi delay | manual · 2026-07-09 |
|   | `helicase-sync-polyblep` | Helicase: PolyBLEP at the carrier reset discontinuity on hard sync | manual · 2026-07-09 |
|   | `house-atom-library` | House: AW tone-shaping atom library (first-15) + RotCoat/composition component atoms | manual · 2026-07-09 |
|   | `house-harness-patterns` | House: harness patterns — Channel-Buss containment, envelope-driver, reduced-rate domain, kWoodRoom atom refactor | manual · 2026-07-09 |
|   | `house-hybrid-float-retrofits` | House: hybrid-float retrofits for kWoodRoom (29%→~12-15%) and WoodenBox (14%→~6-8%) | manual · 2026-07-09 |
|   | `house-suppress-customs-optimize-ports` | House: suppress the original units, keep + optimize (NEON) the ports | manual · 2026-07-21 |
|   | `house-xyz-engine` | XYZ engine — cryptic 3-param original reverb (X morph, Y saturate+undersample, Z meta-routing) | manual · 2026-07-09 |
|   | `mi-clouds-warps-improvements` | MI: Clouds gain-comp + further NEON (Clouds ShyFFT/SRC, Rings sympathetic/FM); Warps vocoder + drive | manual · 2026-07-09 |
|   | `mirror-promotion` | Mirror: final habitat name + Voss-McCartney 1/f drift + wavetable frame-inventory audition | manual · 2026-07-09 |
|   | `multimode-drum-voice-profile` | Profile + emulate multi-engine digital drum voice (per mode) | manual · 2026-07-17 |
|   | `multiout-nlc-chaos` | NLC chaotic modulation sources (clean-room): Sloth, Chua, Hyperchaos, Squid Axon, + lower-pri | manual · 2026-07-09 |
|   | `ngoma-cpu-optimization` | Ngoma: CPU optimization round 2 (~33% mono Cortex-A8 idle is steep) | manual · 2026-07-09 |
|   | `ngoma-known-residuals` | Ngoma parked 2026-07-23 (2.8.3.82): stands on its own, ~10% mono - three known misses vs hardware | manual · 2026-07-23 |
|   | `ngoma-sound-tuning` | Ngoma: careful sound-tuning pass (param cross-couplings, per-partial decay, mode ratios, sub-octave) | manual · 2026-07-09 |
|   | `open303-port` | Port Open303 (MIT-licensed acid bassline emulation) as an ER-301 mono voice unit | manual · 2026-07-11 |
|   | `parfait-shaper-oversampling` | Parfait: per-band shaper oversampling to kill aliasing audible as mix is lowered at high drive | manual · 2026-07-09 |
|   | `pecto-control-refinements` | Pecto: Doppler slew sub-param, bipolar feedback, density cap at 12 | manual · 2026-07-09 |
|   | `pecto-neon-gather-opt` | Pecto: further NEON gather optimization for greedy comb mode | manual · 2026-07-09 |
|   | `petrichor-audio-quality` | Petrichor: audio-quality/feedback overhaul + grid-independent skew + tap-timing analysis | manual · 2026-07-09 |
|   | `petrichor-stereo-and-crossfeed` | Petrichor/Pecto: shared-buffer stereo optimization + cross-feedback matrix (stretch) | manual · 2026-07-09 |
|   | `research-audiorate-param-mod` | Research: how firmware native units accept clean audio-rate parameter modulation | manual · 2026-07-09 |
|   | `spectrum-ply-versions` | Spectrum (Spectrogram) 2/3/4/6-ply versions with higher spectral resolution | manual · 2026-07-21 |
|   | `station-x-character` | Station X (codescan FIR): improve output character (currently mostly noise-like filtering) | manual · 2026-07-09 |
|   | `stolmine-original-units` | stolmine originals backlog: X-op FM voice + additional monokit filter models | manual · 2026-07-09 |
|   | `units-2d-wavetable-osc` | 2D/3D Wavetable Oscillator — X/Y-addressable wavetable with stacked pseudo-3D frame viz | manual · 2026-07-09 |
|   | `units-spectral-processing` | Spectral Mask + Spectral Gate — sidechain-keyed / crossover per-band spectral processors | manual · 2026-07-09 |
| ✗ | `kryos-load-hang` | Kryos (spectral freeze): hangs hardware on load — isolate emu vs hardware | manual · 2026-07-09 |
| ✓ | `biome-flakes` | Flakes — granular shimmer/freeze feedback looper (C++ rewrite of Shards) | manual *(attested)* · 2026-07-09 |
| ✓ | `biome-pecto` | Pecto — comb resonator (16 tap patterns, 4 slopes, 4 resonator types, NEON 3-pass, zipper-clean) | manual *(attested)* · 2026-07-09 |
| ✓ | `fx-tilt-djfilter` | Tilt EQ + DJ Filter shipped (bipolar LP/HP sweep with resonance) | manual *(attested)* · 2026-07-09 |
| ✓ | `house-aw-reverbs` | House package — 6 faithful Airwindows reverb ports (kWoodRoom…Galactic), hardware-validated | manual *(attested)* · 2026-07-09 |
| ✓ | `house-rotcoat` | RotCoat (codename) — first original-design reverb: multi-world per-line reduced-rate FDN | manual *(attested)* · 2026-07-09 |
| ✓ | `network-gain-compensation` | Gain compensation for Network (Fabula's makeup model, tap-weight-energy basis) | manual *(attested)* · 2026-07-15 |
| ✓ | `ngoma-hardware-hang` | Ngoma hangs hardware on load (post-2.5.1 regression) — am335x-specific, bisect pending hardware test | manual *(attested)* · 2026-07-22 |
| ✓ | `spreadsheet-canals` | Canals — Three Sisters clone, DSP refresh + 4-input normalling topology; moved biome→spreadsheet | manual *(attested)* · 2026-07-09 |
| ✓ | `spreadsheet-filterbank` | Filterbank (FFB/Tomograph) — parallel fixed filter bank, 2-16 bands, Scala scales, radial viz | manual *(attested)* · 2026-07-09 |
| ✓ | `spreadsheet-helicase` | Helicase — 2-op FM oscillator (OPL3 waveforms, discontinuity folder, phase-receptivity sync) | manual *(attested)* · 2026-07-09 |
| ✓ | `spreadsheet-impasto` | Impasto — 3-band multiband compressor (LR4 crossover, per-band CPR, sidechain, FFT viz) | manual *(attested)* · 2026-07-09 |
| ✓ | `spreadsheet-jf` | JF — hex-voiced slope-engine voice (clean-room Just Friends map), shipped as open secret | manual *(attested)* · 2026-07-09 |
| ✓ | `spreadsheet-larets` | Larets — stepwise multi-effect (dblue-Glitch style) with true internal stereo | manual *(attested)* · 2026-07-09 |
| ✓ | `spreadsheet-mirror` | Mirror — aliasing-paradigm complex oscillator (wavetable formant + 4-stage crusher, true stereo) | manual *(attested)* · 2026-07-09 |
| ✓ | `spreadsheet-parfait` | Parfait — 3-band multiband saturator (7 shapers, SVF morph, comp, FFT spectrum viz) | manual *(attested)* · 2026-07-09 |
| ✓ | `spreadsheet-petrichor` | Petrichor (Multitap Delay) — 8-tap Rainmaker-style, per-tap SVF + granular pitch, xform, viz | manual *(attested)* · 2026-07-09 |
| ✓ | `spreadsheet-rauschen` | Rauschen — parametric noise, 11 algorithms (White…Lorenz) with SVF morph + phase-space viz | manual *(attested)* · 2026-07-09 |
| ✓ | `spreadsheet-visadhara` | Visadhara — clean-room BIA-style drum voice (Skin/Liquid/Metal configs) + Corona viz | manual *(attested)* · 2026-07-09 |
| ✓ | `tessera-into-ngoma` | Port Tessera's Trinity-derived innovations into Ngoma | manual *(attested)* · 2026-07-22 |

## Units

| | id | item | verify |
|---|---|---|---|
| ~ | `fademixer-snap-mode` | Fade Mixer: Smooth/Snap config option so the family works as N-to-1 switches too | manual *(attested)* · 2026-08-05 |
|   | `bionic-lester-clone` | Profiling-informed clone of a switched-capacitor dual filter (target: Bionic Lester Mk1) | manual · 2026-07-15 |
|   | `blanda-scan-mixer` | Blanda — 3-input scan mixer (Morph-4-style continuous bell-scan, not a crossover) | manual · 2026-07-09 |
|   | `etcher-io-range-and-presets` | Etcher: change I/O ranges to ±1V + add sine/triangle/user presets + hardware deviation verify | manual · 2026-07-09 |
|   | `fademixer-6-8-plus-mutesolo-fix` | FadeMixer 6/8-input versions + fix broken mute/solo | manual · 2026-07-21 |
|   | `jf-phase6-ship` | JF Phase 6: polish + ship (trig-LUT audit, CPU profile, test procedures, vanilla-compat, FM consolidation) | manual · 2026-07-09 |
|   | `larets-effect-breakout` | Break out Larets effects into individual units where the catalog lacks standalone coverage | manual · 2026-07-15 |
|   | `larets-feature-additions` | Larets: add reset input, random step selection, ring-mod + chorus effects, width + panning effects | manual · 2026-07-15 |
|   | `mi-commotio-marbles` | MI: Commotio split-units + sample loading + UI + NEON; Marbles T model picker (7 models, 3 visible) | manual · 2026-07-09 |
|   | `multiout-framework-quadlfo` | Multi-output framework + Quadrature LFO proof-of-concept driver + candidate audit refresh | manual · 2026-07-09 |
|   | `multiout-generative-units` | Multi-output generative units: poly clocked burst, coupled CV+gate env, multichannel seq, + candidates | manual · 2026-07-09 |
|   | `ngoma-preset-library` | Ngoma: per-drum preset library (kick/snare/hat/tom) + default tuning | manual · 2026-07-09 |
|   | `polyphonic-sample-playback` | Polyphonic sample playback unit(s) - synth-style poly + drum-machine-style per-voice sample loader | manual · 2026-07-15 |
|   | `porcelain-microsound-family` | Porcelain microsound/electroacoustic family — DSP kernel set (Raster-Noton + Plumbutter) | manual · 2026-07-09 |
|   | `port-gplv3-cleanroom` | GPLv3 algorithm-reference clean-room reimplementations (reference only, not code ports) | manual · 2026-07-09 |
|   | `port-mit-direct` | MIT-compatible direct ports backlog: Stages LFO, Loom, Open303, Faust reverb, ProCo Rat, AW-remaining | manual · 2026-07-09 |
|   | `switch-1-to-n-unit` | 1-to-N switch unit: output selection + output count + passthrough, on the multi-out framework | manual · 2026-08-05 |
|   | `units-4ms-smr` | 4ms Spectral Multiband Resonator port — 6 resonant bandpass filters with rotation/spread | manual · 2026-07-09 |
|   | `units-buffer-shuffler` | Buffer Shuffler / Groovebox — BBCut-style beat-slicing buffer manipulation | manual · 2026-07-09 |
|   | `units-device-randomizer-control-forge` | Device Randomizer + Control Forge-alike — chain-neighbor randomizer and multistage envelope | manual · 2026-07-09 |
|   | `units-external-comm` | External communication units: I2C output to Crow + clock-sync derived from audio | manual · 2026-07-09 |
|   | `units-ffb-scale-family` | FFB-scale-distribution family: Tone Cluster / Drone, Comb Bank, Harmonic Series Manipulator, Waveguide | manual · 2026-07-09 |
|   | `units-grain-cloud` | Grain Cloud — N-grain (8-16) sample-buffer granular unit | manual · 2026-07-09 |
|   | `units-phaser-flanger` | Phaser / Flanger Designer — N allpass stages with per-stage depth/rate/feedback | manual · 2026-07-09 |
|   | `units-poly-sample-player` | Polyphonic sample player — N-voice manual-grain-based sample playback with voice management | manual · 2026-07-09 |
|   | `units-som` | SOM — 64-node Kohonen self-organizing map with localized learning + icosphere scan viz | screenshot: insert; confirm 6-dim 64-node map learns timbral prototypes from audio/CV features, Scan icosphere ply (Fibonacci sphere, 64 Voronoi cells, BMU glow, CV-modulatable scan) + Plasticity ply (localized learning around scan position), reconstructed output interpolated from neighbors; usable structure in 1-3s, converged ~15s · 2026-07-09 |
| ✓ | `biome-constant-random` | Constant Random — always-running S&H random CV source (rate + slew) in biome | manual *(attested)* · 2026-07-09 |
| ✓ | `biome-varishape` | Varishape oscillator + Varishape Voice shipped in biome (POLYBLEP sine→square morph) | manual *(attested)* · 2026-07-09 |
| ✓ | `catchall-alembic` | Alembic — sample-trained SOM 4-op PMM matrix synth (dual FM filter, comb, Ferment chaos macro) | manual *(attested)* · 2026-07-09 |
| ✓ | `fabula-promote-spreadsheet` | Promote Fabula from the zaum package to the spreadsheet package | manual *(attested)* · 2026-07-14 |
| ✓ | `fade-mixer` | Fade Mixer — 4-input equal-power crossfader with BranchMeter gain/solo/mute controls | manual *(attested)* · 2026-07-09 |
| ✓ | `mi-ports-shipped` | Mutable Instruments port suite: Plaits/Clouds/Rings/Grids/Warps/Stratos/Commotio/Marbles T+X | manual *(attested)* · 2026-07-09 |
| ✓ | `peaks-dmc-ports` | Peaks/DMC port suite: 14 drum/modulation/sequencer/generator units with clock+reset refinements | manual *(attested)* · 2026-07-09 |
| ✓ | `porcelain-chime-v0` | Chime v0 — first porcelain (microsound/electroacoustic) unit: coupled pulse-excited resonator bank | manual *(attested)* · 2026-07-09 |
| ✓ | `spreadsheet-colmatage` | Colmatage — block-cutting rhythmic effect unit shipped in spreadsheet | manual *(attested)* · 2026-07-09 |
| ✓ | `spreadsheet-ngoma` | Ngoma — analog macro drum voice (sine/tri core, pitch-sweep, grit, shape FM, comp, EQ, xform) | manual *(attested)* · 2026-07-09 |
| ✓ | `utility-modulation-units` | Shipped utility/modulation units: Gated Slew, Integrator, PSR, Gridlock, Spectral Follower, Codescan | manual *(attested)* · 2026-07-09 |

## UI / interaction

| | id | item | verify |
|---|---|---|---|
| ~ | `anamnesis-viz-opt` | Anamnesis viz: shared per-frame field cache + bounded metaball build (CM4/am335x perf) | manual · 2026-07-11 |
| ~ | `mix-control-standards` | Mix-control standards: detents standardized on the built-in map; crossfade-law half still open | manual *(attested)* · 2026-08-05 |
| ~ | `vitrail-viz` | Vitrail: tunnel visualization stacked on the Clock Src control | screenshot: Vitrail shows a custom overview graphic that reads clearly at 128x64 / 4-bit depth, telegraphs the current routing pair and the drifting dual-clock character, and does NOT capture the encoder (draw-path structure, not CPU) *(attested)* · 2026-08-05 |
|   | `alembic-phase9-polish` | Alembic Phase 9: naming/defaults/mnemonic polish + serial stacked-waveform viz | screenshot: confirm final names/defaults/control labels + sphere-viz refinements, plus a stacked/cascading render of mWavetableLUT[64][256] with active reagent-scan frames lit and neighbors dimmed · 2026-07-09 |
|   | `control-descriptions-drop-parentheticals` | Shorten Lua control descriptions that include parentheticals (they overflow the allotted space) | manual · 2026-07-22 |
|   | `control-step-standards` | Adopt built-in dial-map standards across habitat; inventory every control vs the framework registry | manual · 2026-07-16 |
|   | `controls-bias-modrange-audit` | Bias + mod-input range audit across all packages (CV can't reach full param range at 10x gainMap) | manual · 2026-07-09 |
|   | `controls-discrete-encoder-rollout` | Discrete-control stepping standard: one feel for every integer/discrete control (surveyed) | manual · 2026-08-05 |
|   | `controls-fader-response-audit` | Repo-wide fader response audit (dial-map shape, coarse/fine steps, encoder velocity, bias range) | manual · 2026-07-09 |
|   | `controls-shift-audit-impl` | Shift-button behavior audit — implementation phase (spec locked in planning/shift-handling.md) | manual · 2026-07-09 |
|   | `controls-subdisplay-and-viz` | Sub-display readout audit for expansion-only controls + bias indicator line on visualizer plies | manual · 2026-07-09 |
|   | `fabula-overview-caret` | Fabula overview: default-focus a sub-readout on re-entry WITHOUT breaking navigation (renderer focus==self gate) | manual · 2026-07-14 |
|   | `filterbank-defaults-and-randomize-gates` | Filterbank: better default gain/Q for immediate audibility + band-list randomize gate controls | manual · 2026-07-09 |
|   | `helicase-polish` | Helicase: wider carrier-shape set, more-reactive overview viz, am335x CPU profile | manual · 2026-07-09 |
|   | `impasto-stereo-option-state` | Impasto: saved stereo option state can disagree with instantiation if toggled without re-insert | manual · 2026-07-09 |
|   | `larets-stutter-shuffle-viz` | Larets: distinguish stutter vs shuffle viz (both read similar at a glance) | screenshot: confirm stutter telegraphs a boxed fixed-loop window with repeated contour fragments, and shuffle telegraphs rearranged fragment blocks (shifted or source-color-coded) · 2026-07-09 |
|   | `mirror-step-and-viz` | Mirror: fine/coarse/super step audit on all controls + custom overview-ply viz | screenshot: confirm each fader's coarse/fine/super steps match musical granularity (Hz = octave/cents/sub-Hz; 0..1 = 0.1/0.01/0.001; Mirror knob approaches the Nyquist-flip region smoothly); confirm a custom overview viz (concentric fire wheels / rubber-band sphere / L-vs-R Lissajous) renders · 2026-07-09 |
|   | `mod-gain-default-zero` | Mod gain defaults to 0 on all controls of all units (CV opt-in) - catalog-wide standardization | manual · 2026-07-22 |
|   | `ngoma-viz-and-docs` | Ngoma: cube viz refinement + documentation refresh | screenshot: confirm cube scaled 0.85x with breathing room, state-driven face-fill textures (Character/Grit/Shape/ampEnv), exaggerated parallax + face-shrink/edge-expand-on-hit; drum-voice.md updated or superseded by the codex reference · 2026-07-09 |
|   | `peaks-refinements` | Peaks: Tap LFO clock frequency-counter display, PLO continuous phase-increment, step-position viz | manual · 2026-07-09 |
|   | `pecto-expansion-views` | Pecto: add per-control expansion views so submenu params open as full faders on enter (impasto pattern) | manual · 2026-07-14 |
|   | `petrichor-tap-macros` | Petrichor: CV-modulatable macro filter-cutoff offset + tap-pitch macro + xform spread param | manual · 2026-07-09 |
|   | `rate-time-control-octave-maps` | Rate/time controls on wide-ratio LINEAR dial maps swing far too wide (Constant Random fixed; audit open) | manual · 2026-08-05 |
|   | `scope-channel-focus` | Scope: channel focus display switching (show L or R by channel-button selection) | manual · 2026-07-09 |
|   | `scope-goniometer` | Goniometer / Lissajous — XY stereo-field scope with correlation readout | screenshot: insert; confirm goniometer (L+R vs L-R) or arbitrary-XY Lissajous display with a +1..-1 correlation readout; negligible DSP · 2026-07-09 |
|   | `ui-editmode-border-expanded` | Edit-mode border vanishes on expanded-graphic + M-hold (user report, needs repro) | manual · 2026-07-09 |
|   | `ui-filterlist-type-label-stale` | FilterListControl type label doesn't refresh when macros change filter type (Tomograph + Petrichor) | manual · 2026-07-09 |
|   | `ui-subdisplay-selection-indicator` | Sub-display selection indicator missing on paramMode ply-to-ply navigation (PINNED) | manual · 2026-07-09 |
|   | `units-artsy-visualizers` | Artsy visualizers / generative sound+visual units (Norns/Paratek-inspired eye candy) | screenshot: build generative visual + audio units: glitchy audio/CV-driven visuals and a pseudo-3D serial-wavetable-frame view · 2026-07-09 |
|   | `viz-offscreen-gate-all` | Bring the offscreen-viz gate to all spreadsheet units with a DSP-side visualization | manual · 2026-07-15 |
| ✗ | `scope-headerless-research` | Scope: research headerless unit display | manual · 2026-07-09 |
| ✓ | `controls-optioncontrol-hardening` | Controls hardening: OptionControl boolean/indexing fixes, bipolar correctness, passthrough Comparators | manual *(attested)* · 2026-07-09 |
| ✓ | `scope-spectrogram` | Spectrogram — inline spectrum analyzer (256-pt pffft, peak-hold + RMS gradient, 2-ply) | manual *(attested)* · 2026-07-09 |
| ✓ | `scope-units` | Scope unit family — inline passthrough scopes with timebase, Y-gain, and voltmeter readout | manual *(attested)* · 2026-07-09 |
| ✓ | `vitrail-ux-fixes` | Vitrail UX: cutoff-mod-gain default + routing-control encoder tick scaling | manual *(attested)* · 2026-07-22 |

## Sequencing / timing

| | id | item | verify |
|---|---|---|---|
| ~ | `larets-random-step-advance` | Larets: random step-advance mode (sequential/random, never the same step twice in a row) | manual *(attested)* · 2026-08-05 |
|   | `ballot-gate-width-zero` | Ballot: gate width 0 produces a trigger instead of silence — decide desired behavior | manual · 2026-07-09 |
|   | `colmatage-xform-ply` | Colmatage: add an xform ply (func + paramA/B/scope) for algorithmic cutting randomization | manual · 2026-07-09 |
|   | `seq-101-sequencer` | 101 Sequencer — 64-step SH-101-style CV sequencer with address fader + math transforms | manual · 2026-07-09 |
|   | `seq-automata-chess` | Automata Sequencer (Chess) — grid sequencer driven by board-game piece movement rules | manual · 2026-07-09 |
|   | `seq-compass-port` | Compass — port of the Norns generative sequencer (feasibility research first) | manual · 2026-07-09 |
|   | `seq-drum-kit` | Drum Kit Sequencer ('Kit') — drum-role-tagged monophonic CV sequencer for a mono drum voice | manual · 2026-07-09 |
|   | `seq-excel-ballot-improvements` | Excel/Ballot improvements: xform context view, viz polish, output-scope visibility, variants, snap-scale, gate-seq | manual · 2026-07-09 |
|   | `seq-ratchet-strum` | Ratchet / Strum — gate-burst generator from a single trigger (count/spacing/accel/velocity/gate-length) | manual · 2026-07-09 |
| ✓ | `seq-ballot-gateseq` | Ballot — gate sequencer with ratchet (RatchetLen/Vel/Mult) persistence | manual *(attested)* · 2026-07-09 |
| ✓ | `seq-excel-tracker` | Excel — 64-step CV tracker sequencer (per-step offset/length/deviation, xform gate, V/Oct out) | manual *(attested)* · 2026-07-09 |
| ✓ | `seq-gesture-recorder` | Gesture Sequencer — continuous gesture recorder/looper with od::Sample buffer + waveform viz | manual *(attested)* · 2026-07-09 |

## Tooling / build

| | id | item | verify |
|---|---|---|---|
|   | `audit-passthrough-remaining` | Review remaining units for clean passthrough vs non-passthrough delineation | manual · 2026-07-09 |
|   | `dsp-neon-audit` | Repo-wide NEON audit — identify hot DSP paths without SIMD and assess vectorization | manual · 2026-07-09 |
|   | `tooling-keyword-metadata-audit` | Normalize per-unit picker `keywords` metadata across all packages | manual · 2026-07-09 |
|   | `voice-profiling-workflow` | Hardware voice profiling workflow (manual + MIDI + record + sox null-test) | manual · 2026-07-17 |
| ✓ | `tooling-build-defenses` | am335x build defenses: graphic-vtable lint + NEON-hint lint + -fno-tree-vectorize | manual *(attested)* · 2026-07-09 |

## Documentation

| | id | item | verify |
|---|---|---|---|
| ~ | `branding-attribution-policy` | Branding + attribution policy: generic externally, precise internally | manual *(attested)* · 2026-08-05 |
| ~ | `release-v2-8-0` | v2.8.0 release: hardware test matrix, then ship 6 packages (house pending a call) | manual *(attested)* · 2026-08-05 |
|   | `diffusion-makeup-model-notate` | Notate Fabula's diffusion-makeup gain model as a reusable pattern for tap-based units (main target: Network) | manual · 2026-07-14 |
|   | `docs-intro-video` | Intro video for Habitat packages | manual · 2026-07-09 |
| ✓ | `control-expansion-views-pattern` | Canonical pattern: control expansion views (expand sub-params to full faders on ENTER) | manual *(attested)* · 2026-07-14 |
| ✓ | `readme-airwindows-attribution` | Add an attribution line in the README for Airwindows' creator Chris Johnson | manual *(attested)* · 2026-07-14 |

## Infrastructure

| | id | item | verify |
|---|---|---|---|
|   | `alembic-serialization` | Alembic Phase 6: serialize sample ref + trained per-node state + user biases | manual · 2026-07-09 |
|   | `infra-cross-package-dep-audit` | Cross-package dependency audit — no require/%import/#include reaching into another package's tree | manual · 2026-07-09 |
|   | `rauschen-header-only-migration` | Migrate Rauschen to header-only (COMDAT vtable) - drop Rauschen.cpp | manual · 2026-07-17 |
|   | `reconcile-notes-memory-codebase` | Reconcile all notes/TODOs/ledger/memory against current codebase state (drift sweep) | manual · 2026-07-22 |
| ✓ | `infra-ledger-regime` | Ledger + BDG regime stood up (gate, render, hooks, blessed entrypoint) | manual *(attested)* · 2026-07-09 |
| ✓ | `infra-serialization-audit` | Spreadsheet-package serialization + stale-label audit (ParameterAdapter Bias round-trip, updateLabel) | manual *(attested)* · 2026-07-09 |
| ✓ | `infra-vanilla-compat` | Habitat packages load on vanilla ER-301 firmware (non-stolmine) | manual *(attested)* · 2026-07-09 |
| ✓ | `mi-package-consolidation` | 8 separate MI port packages consolidated into single `mi` package (shared stmlib, ABI-safe) | manual *(attested)* · 2026-07-09 |

