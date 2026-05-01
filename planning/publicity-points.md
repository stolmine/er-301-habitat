# Habitat publicity points

Working doc for talking points + example-chain selection. Pre-v2.5 release. The headline shape: habitat is a curated synthesis-and-effects collection for ER-301 with three centers of gravity — original DSP voices/effects (Spreadsheet), a clean consolidation of the major Mutables ports (MI), and a deep utility + oddities bench (Biome).

## Headline pitch (one or two sentences)

> Habitat turns the ER-301 into a fully-stocked instrument: a complete Mutable Instruments port set, a half-dozen original synthesis voices and effects, and a deep bench of unusual utilities — all in three packages, all hardware-tuned, all free and open source.

Alt framing: "Three packages, ~40 units, including everything Mutables ever made and a long list of original DSP nobody else has."

## Spreadsheet (tier-1 originals + sequencer pair)

The flagship package. Mix of original synthesis voices, original effects, and the sequencing pair Excel/Ballot.

### Voices
- **JF** — six harmonically-coupled slope engines from a single chassis, clean-room from the Mannequins technical map. Goes from sub-octave drone to bell-stack overtone series via a single INTONE knob; bipolar through-zero linear FM in Sound range; CV-able OUT crossfader scans across the six voices on the primary outlet. Multi-output: MIX + 6 per-voice taps. **Demo angle:** show the INTONE morph audibly (overtone CW → unison detune → undertone minor-triad), then patch a slow LFO to OUT for a "voice-morphing" lead.
- **Helicase** — OPL3-style 2-op FM with a discontinuity shaper for digital grit. Phase-receptivity sync between carrier and modulator (the JF SHIFT mode lives here first). Custom orbital + phase-space viz. **Demo angle:** the shape morph at audio rate is genuinely interesting, lots of FX-pedal-territory tones without being thin.
- **Ngoma** — analog-style macro drum voice. Six top-level controls (Trig / V/Oct / Character / Sweep / Decay / Level) over 14 hidden parameters; pitch-morphing membrane mode ratios produce wide-spread sub-bass at low pitch and tight cymbal-sheen clusters at high pitch. **Demo angle:** sweep V/Oct from kick to snare to hat range over a static Character setting.
- **Rauschen** — multi-mode noise generator (gendy / dust / crackle / chaos / particle). Phase-space viz makes the variance visible. **Demo angle:** parallel three Rauschens at different chaos modes and morph between them with Blanda.

### Effects
- **Pecto** — 24-tap multitap comb. Karplus / sitar / clarinet resonator types; Doppler-style baseDelay smoother gives smooth tape-like glide on V/Oct + size sweeps. **Demo angle:** drive Pecto with white noise + V/Oct ramp → harmonic resonator that sweeps without zipper noise. Or feed back into itself for self-oscillating drone.
- **Petrichor** — sister multitap delay (6% stereo CPU on Cortex-A8, NEON 3-pass gather). Per-tap pitch-shift and feedback character. **Demo angle:** ambient verb-replacement, longer windows than Stratos covers.
- **Tomograph** — graphical filterbank / parallel resonator. Visual interaction is the selling point.
- **Etcher** — segmented transfer-curve waveshaper / mapper. Drag points, get a CV→CV table. **Demo angle:** quantize a wandering LFO to a chord-friendly contour. Pedagogically interesting — a waveshaper with a literal drawing on the screen.
- **Parfait** — multiband saturator with per-band drive + spectrum viz.
- **Impasto** — multiband compressor. Sidechain input.
- **Larets** — step-sequenced multi-effect (each step picks an effect treatment). One of the more unusual structures in the set.
- **Blanda** — 3-input mixer / scan-morph fader. Three-source crossfader on a single knob.
- **Colmatage** — BSP-tile breakbeat slicer. Striking full-screen graphic.

### Sequencers
- **Excel** — 64-step CV sequencer, tracker-style, with offset range scaling and pattern recall.
- **Ballot** — 64-step gate sequencer with ratchets, Euclidean fill, transform/randomize.

**Spreadsheet demo arc suggestion:** Excel → Helicase (V/Oct) → Pecto (resonator) → Stratos. Then swap Helicase for JF for a hex-voiced version. Both work; both highlight different package strengths.

## MI (Mutable Instruments — consolidated)

Single package, 9 units. The story is **consolidation + completeness** — every Mutables module that mattered, in one install, behavior-faithful at native rates.

- **Plaits** — all 24 engines. Macro-oscillator headliner.
- **Clouds** — granular + delay + spectral + pitch-shift modes.
- **Rings** — modal/sympathetic resonator, NEON-vectorized for hardware.
- **Marbles T + X** — separate units for trigger generator and CV generator (cleaner than combined).
- **Grids** — drum pattern generator (single channel, no accent — drives 3 voices via channel select).
- **Warps** — meta-modulator, 6 xmod algorithms.
- **Stratos** — Clouds reverb engine extracted as a standalone unit.
- **Commotio** — Elements exciter (bow / blow / strike) at native 48 kHz.

**Demo angle:** "Everything from the late-2010s eurorack consolidation era, in one package, free." That's the headline. Plus the package consolidates a previously-fractured 8-package layout into one with shared `stmlib`.

**MI demo arc suggestion:** Marbles T (clock + density) → Plaits → Clouds → Stratos. Classic patch, drives the point home.

## Biome (utilities + oddities)

The bench. Not headliners individually, but the *collection* is the story — habitat ships utilities that nobody else writes for ER-301, and a few oddballs that do something genuinely strange.

### Utilities (work-horse / "you'll use these every patch")
- **Transport** — clock + transport gate utility. Master clock generator + downbeat / first-beat detection.
- **Gridlock** — gate priority router. Routes one of N gate inputs to one output based on which is currently active.
- **Integrator** — accumulator. CV adds into a held value, configurable bleed. Useful for sequencer step-tracking, drift sources.
- **Quantoffset** — quantizer with offset. Snap CV to scale, transpose by interval.
- **Constant Random** — sample-and-hold utility with hold-vs-pick probability fader. Single-fader randomness.
- **PSR (Pingable Scaled Random)** — clock-driven random with bipolar scaling.
- **Tilt EQ** — single-knob tone shaper. Useful as the "smile" or "frown" master EQ.
- **DJ Filter** — single-knob dual filter (LP one direction, HP the other). Pure utility.
- **Gated Slew** — slew limiter with rise/fall asymmetry + gate-conditioned passthrough.
- **Fade Mixer** — crossfader for two sources. Cheap, focused.
- **Latch Filter** — gate-latched filter / sample-and-hold filter.

### Oddities (worth showing because they're unusual)
- **Canals** — three-sisters-style resonant filter. Three positions on one knob: LOW / CTR / HIGH / ALL output crossfade pattern is direct ancestor of JF's OUT crossfader.
- **94 Discont** — discontinuity-based waveshaper. Same shaper family as Helicase's grit but as a standalone effect. Lots of clipping and folding in a single fader.
- **Spectral Follower** — bandpass-filtered envelope follower. CV out responds to specific frequency bands of the input — usable as a synth-driven CV source.
- **Bletchley Park** — codescan oscillator (wavetable scanned via a non-trivial trajectory). Generates digital textures unlike the standard ER-301 oscillators.
- **Station X** — codescan filter / FIR convolution with the same family of trajectory-scanning code.
- **Varishape Voice + Osc** — polyBLEP-morphing oscillator across sine/tri/saw/square/pulse. The voice variant adds envelope. Cleaner than Plaits' classic-osc engine for traditional analog patches.
- **NR** — gate sequencer (one of the early biome additions; Euclidean-flavored).
- **Gesture** — gesture recorder / looper for CV. Record a knob movement, loop it back.

**Biome demo arc suggestion:** chain a Constant Random → Quantoffset → Plaits's V/Oct, then run Plaits through DJ Filter on a slow LFO sweep. Shows three biome utilities doing real work in a 10-second patch.

## Suggested patch demos for video / asset capture

Each is short, fits on one ply-row screen, demonstrates one strong angle:

1. **"JF demo"** — JF in Sound/Cycle, INTONE sweep from CCW to CW, then mode-switch to Sustain + clock from Marbles T. Shows the multi-out unit's musical range.
2. **"Spreadsheet self-contained"** — Excel + Helicase + Pecto + Parfait. No external sources. Quick demo that the package alone is a complete instrument.
3. **"MI consolidation"** — Marbles T → Plaits → Clouds → Stratos. Familiar Mutables-era patch shape, single package.
4. **"Habitat utilities at work"** — biome chain with Constant Random → Quantoffset → V/Oct, plus Tilt EQ + DJ Filter on the audio path. Show that you can build a working voice from utilities alone.
5. **"Hex-voiced drone"** — JF in Shape range, INTONE moving slowly, OUT crossfader on an Excel quantize step pattern, MIX into Pecto with high feedback. Demonstrates the multi-output framework's musical payoff.
6. **"Oddities tour"** — Bletchley Park → Station X → 94 Discont. Pure biome-oddity chain, intentionally weird.

## Talking points for written copy

Audiences are mostly other ER-301 users + eurorack synth folks who follow the platform.

- **Free + open source** — release notes, PR, source. Don't bury this.
- **Hardware-tuned** — most habitat units have NEON paths and have been profiled on Cortex-A8. Pecto went 50% → 6% stereo CPU through a documented optimization arc.
- **Comparable scope to a commercial expansion** — count the units (~40), highlight the originals are ours (Pecto, Helicase, Ngoma, JF, Rauschen, Larets, Etcher, Tomograph, Petrichor, Parfait, Impasto, Blanda, Colmatage all original) and the MI ports are clean-room.
- **MI consolidation** — rare in the ER-301 ecosystem; we're the only place that ships everything Mutables made under one roof for ER-301.
- **Multi-output framework** — first JF-class six-voice unit on ER-301. Mention the SDK extension we contributed (extended max-out cap to 99) and the proven NEON polyphony pattern adapted from tomf's polygon.
- **Sound-design-friendly** — the unit catalog is curated, not exhaustive. Each unit has a specific musical purpose and cross-references with the others (Pecto resonator pairs with Helicase oscillator pairs with Petrichor delay pairs with Stratos reverb).
- **Hardware-verified release-quality** — every unit ships only after hardware testing. Document the test-procedures.md flow if asked.

## What NOT to highlight in copy

- Alembic (catchall, experimental tier) — unless there's a v2.5 cycle promotion. Currently doesn't survive quicksave round-trip.
- D8 visible-highlight bug pinned for next release — known internally, not a publicity point.
- Internal bisect arcs and hardware crash debugging history — interesting to engineers, not to musicians.

## Open prep tasks (track elsewhere if needed)

- Photo/video of ER-301 with habitat patches running. Highlight the Colmatage BSP tile field and SOM-sphere viz on Alembic — the strongest visual moments in the catalog.
- Short demo audio clips (15–30s) per package. Patch presets shipped with the package would help users reproduce.
- Comparison sheet: habitat units vs the analog/commercial-eurorack inspirations (Mannequins JF → habitat JF; SAWS / TX81Z / OPL3 → Helicase; Eurorack drum voices → Ngoma; Mutables Clouds → MI Clouds). Frame as "how to think about each unit musically."
