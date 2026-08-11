# Spreadsheet — Effects (`spreadsheet`) — v2.8.5.1

The effects half of the **Spreadsheet** package (author: stolmine) — multiband and filterbank processors, multitap and network delays, granular/rhythmic manglers, resonators, reverbs, and character filters. All units live under the **Spreadsheet** category on the ER-301.

Two units carry third-party lineage: **Colmatage** implements Nick Collins' BBCut breakbeat-cutting algorithm (by way of the Livecut plugin), and **Fabula** is a Dattorro/Griesinger figure-8 recirculating-allpass tank. **Canals** moved out of the `biome` package into `spreadsheet` in v2.6.1 — patches saved against `biome.Canals` will not load and must be re-pointed at `spreadsheet.Canals`. **Larets** has been true stereo since v2.5.1. **Fabula** (v2.7.0) and **Vitrail** (v2.8.0) are the newest units here.

This document covers only the effects subset; sequencers, generators, and voices are documented separately.

---

## Tomograph

<mnemonic: Tm> · Category: Spreadsheet · Keywords: filter, bank, resonator, EQ, spectral, effect, tomograph

A parallel bank of up to 16 state-variable filters whose centre frequencies are snapped to a musical scale. Pick a scale, pick how many bands, and the unit distributes the bands across 60 Hz–16 kHz by maximising their spacing in that scale; Macro Q takes the whole bank from gentle EQ colour to a ringing chord of resonators. Each band can be edited individually (frequency, gain, filter type), and Rotate walks the whole selection through the scale for chord-like sweeps. Use it as a spectral EQ, a formant/resonator body, or a pitched noise-tuner.

**Controls**

| Control | Type | Range / Options | Default | What it does |
|---|---|---|---|---|
| Bands | BandListControl (list + per-band readouts) | band 1…16 (selected via encoder) | band 1 | Selects and edits one band at a time. Sub-buttons: `freq` 20–16000 Hz, `gain` 0–4, `type` 0–2 (`peak` / `lp` / `res`). Shift + encoder scrolls bands while a readout is focused. |
| Response | FilterResponseControl (radial response display) | — | — | Live composite response / per-band energy display. Its sub-display carries Band Count, V/Oct Offset and Slew. |
| Scale | ModeSelector (discrete) | `chr`, `maj`, `min`, `h.min`, `M.pnt`, `m.pnt`, `whole`, `dor`, `phry`, `lyd`, `mixo`, `loc`, plus up to 64 user `.scl` slots loaded from `front/scales` | `chr` (0) | Chooses the pitch set the band frequencies snap to. Redistributes bands on change. |
| Rotate | GainBias | -16…16 (integer) | 0 | Rotates the selected scale degrees, shifting all bands to the next/previous set of degrees. |
| Macro Q | GainBias | 0…1 | 0.5 | Global resonance. Maps quadratically to Q ≈ 1…100, with Q falling off band-by-band up the bank. `lp` bands get a Q floor of 5, `res` bands a floor of 20 so they always ring. |
| Mix | MixControl | 0…1 | 0.5 | Dry/wet blend of the bank against the input. Sub-display carries Input Level, Output Level and Saturation. |
| Band Count | GainBias (expansion) | 2…16 (integer) | 8 | How many bands are active. Changing it redistributes frequencies. |
| V/Oct Offset | GainBias (expansion) | -2…2 | 0.00 | Transposes the whole bank by ±2 octaves. The branch input is scaled ×10, so 1 V = 1 octave. |
| Slew | GainBias (expansion) | 0…5 s | 0.00 s | Glide time for band frequencies when the distribution changes (scale, rotate, band count, V/Oct). |
| Input Level | GainBias (expansion) | 0…4 | 1.00 | Gain into the bank. |
| Output Level | GainBias (expansion) | 0…4 | 1.00 | Gain after the mix stage. |
| Saturation | GainBias (expansion) | 0…1 | 0.00 | Blends in a tanh-saturated copy of the mixed signal (drive rises with amount) before the output gain. |

**Sub-display / expanded** — Enter on `Bands` gives per-band `freq` / `gain` / `type`, with the title showing "Band N" and the current type name. Enter on `Response` gives `bands` / `V/Oct` / `slew`. Enter on `Mix` (shift toggles the param page) gives `input` / `out` / `tanh`.

**Menu**

- Bands: `Init bands (log spacing)` — resets all bands to log-spaced 100 Hz–10 kHz, gain 1.0, type peak.
- Bands: `Randomize bands` — random frequency 60 Hz–16 kHz and gain 0–3 per band.
- Bands: `Rescan .scl files` — re-reads Scala files from `front/scales` into the custom scale slots.
- Macro Filter Type: `All peaking`, `All lowpass`, `All resonator` — sets every band's type at once.

**I/O** — One audio input, one audio output; instantiated stereo the unit runs two independent filterbanks (left/right) sharing all parameters. Every control has a modulation branch. The V/Oct Offset branch is the pitch input (1 V/oct); there are no gate or trigger inputs.

---

## Petrichor

<mnemonic: Pt> · Category: Spreadsheet · Keywords: delay, multitap, echo, feedback, filter, pitch, petrichor

An eight-tap delay where every tap has its own level, pan, pitch shift and state-variable filter. A single Master Time plus Grid, Stack and Skew lays the taps out rhythmically; Drift wobbles them, Reverse plays grains backwards, and V/Oct transposes the whole tail. Macro presets fill all eight taps at once with common level / pan / pitch / filter patterns, and a gate input re-randomises a chosen group of parameters on demand. Ranges from a clean stereo multitap to shimmering, granular, half-broken echo fields.

**Controls**

| Control | Type | Range / Options | Default | What it does |
|---|---|---|---|---|
| V/oct | Pitch | — | 0 | Transposes all tap playback (granular pitch shift). Internally scaled ×10, so 1 V = 1 octave. |
| Taps | TapListControl (list + per-tap readouts) | tap 1…8 | tap 1 | Selects and edits one tap. Sub-buttons: `level` 0–1, `pan` -1…1, `pitch` -24…+24 semitones. Shift + encoder scrolls taps. |
| Grain / Taps / Stack | DelayInfoControl (overview display) | — | — | Live tap/energy overview. Its sub-display carries Grain Size, Tap Count and Stack. |
| Master Time | TimeControl | 0.01 s … buffer size (default 5 s) | 0.50 s | Base delay time. Taps land at `masterTime × (group+1) / grid`, so total span is `masterTime × groups / grid`. Sub-display carries Grid, Reverse and Skew. |
| Feedback | FeedbackControl | 0…0.95 | 0.30 | Feedback amount around the tap bank. Sub-display carries Feedback Tone. |
| Randomize | TransformGateControl (trigger) | target 0…20 (see below) | target `all`, depth 0.50, spread 0.50 | Trigger input; on each rising edge (or the `fire` sub-button) it randomises the selected target group. `depth` sets deviation, `sprd` biases the result toward the middle of the range vs. the current value. |
| Mix | MixControl | 0…1 | 0.50 | Equal-power dry/wet blend. Sub-display carries Input Level, Output Level and Saturation. |
| Filters | FilterListControl (expansion) | filter 1…8 | filter 1 | Per-tap SVF: `freq` 20–10000 Hz, `Q` 0–1, `type` `off` / `lp` / `bp` / `hp` / `ntch`. |
| Tap Count | GainBias (expansion) | 1…8 (integer) | 4 | Number of active taps. |
| Feedback Tone | GainBias (expansion) | -1…1 | 0.00 | Damping in the feedback path: negative = dark, positive = bright (`dark` / `warm` / `neut` / `brt` / `bright` labels). |
| Grain Size | GainBias (expansion) | 0…1 | 0.50 | Grain length for pitch/reverse playback: 0 ≈ 5 ms, 1.0 ≈ 300 ms, Hann window at 50 % overlap. |
| Grid | ModeSelector (expansion, discrete) | `1`, `2`, `4`, `8`, `16` | `1` | Divides Master Time — higher grid packs the taps into a tighter subdivision. |
| Stack | ModeSelector (expansion, discrete) | `1`, `2`, `4`, `8` | `1` | Number of taps sharing each time position (stacked at the same delay), clamped to the tap count. |
| Skew | GainBias (expansion) | -2…2 | 0.00 | Exponent on tap spacing (2^skew) — bunches taps toward the start or the end of the span. |
| Drift | GainBias (expansion) | 0…1 | 0.00 | Slow per-tap sinusoidal wobble of the tap positions. |
| Reverse | GainBias (expansion) | 0…1 | 0.00 | Probability that any given grain plays backwards. |
| Volume Macro | MacroControl (expansion) | `full`, `off`, `20%`, `40%`, `60%`, `80%`, `asc`, `desc`, `even`, `odd`, `sine` | `full` | Writes a level pattern across all active taps. |
| Pan Macro | MacroControl (expansion) | `cntr`, `left`, `rght`, `L>R`, `R>L`, `e.L`, `o.L`, `c.1`, `c.2`, `c.4`, `c.8` | `cntr` | Writes a pan pattern across all active taps. |
| Pitch Macro | MacroControl (expansion) | `0`, `+12`, `-12`, `+7`, `-7`, `asc`, `desc`, `asc8`, `ds8`, `e12`, `e-12`, `e+7`, `maj`, `min` | `0` | Writes a per-tap semitone pattern (unison, octaves, fifths, ascending/descending, chord cycles). |
| Cutoff Macro | MacroControl (expansion) | `dflt`, `asc`, `desc`, `even`, `odd`, `sine` | `dflt` | Writes per-tap filter cutoffs (200 Hz–10 kHz patterns; `dflt` = 10 kHz on all). |
| Q Macro | MacroControl (expansion) | `off`, `20%`, `40%`, `60%`, `80%`, `full`, `asc`, `desc`, `even`, `odd`, `sine` | `off` | Writes per-tap filter Q. |
| Type Macro | MacroControl (expansion) | `off`, `LP`, `BP`, `HP`, `ntch`, `e.LP`, `e.BP`, `e.HP`, `o.LP`, `o.BP`, `o.HP`, `cycl`, `c2LB`, `c2LH`, `c4`, `c8` | `off` | Writes per-tap filter types (all-one-type, even/odd splits, cycling and clustered patterns). |
| Input Level | GainBias (expansion) | 0…4 | 1.00 | Gain into the delay. |
| Output Level | GainBias (expansion) | 0…4 | 1.00 | Final output gain. |
| Saturation | GainBias (expansion) | 0…1 | 0.00 | Blends in a tanh-saturated copy of the mixed output. |

Randomize targets, in order: `all`, `taps`, `delay`, `filt`, `level`, `pan`, `pitch`, `cut`, `Q`, `type`, `time`, `fdbk`, `tone`, `skew`, `grain`, `drift`, `rev`, `stack`, `grid`, `count`, `reset`. Note that the DSP currently clamps the target index to 16, so `stack`, `grid`, `count` and `reset` do nothing when fired (see verification notes).

**Sub-display / expanded** — Enter on `Taps` gives `level` / `pan` / `pitch` plus the Filters list, Tap Count and all six macros. Enter on the overview gives `grain` / `taps` / `stack`. Enter on `Master Time` gives `grid` / `rev` / `skew`. Enter on `Feedback` gives `tone`. Enter on `Mix` gives `input` / `out` / `tanh`. Shift on `Master Time`, `Feedback`, `Mix` and `Randomize` toggles between the normal and the param sub-page.

**Menu** — Buffer Size: `2 sec`, `5 sec`, `10 sec`, `20 sec`. Reallocating also rescales the Master Time fader throw and clamps the current time to the new maximum. Buffer is int16.

**I/O** — Mono in, stereo out (`Out` / `OutR`). Instantiated stereo, both inputs feed the same delay line and the taps' pan positions produce the stereo image; instantiated mono the right output is folded back to mono. Every fader has a modulation branch; `V/oct` is the pitch input (1 V/oct) and `Randomize` is a trigger input.

---

## Parfait

<mnemonic: Pf> · Category: Spreadsheet · Keywords: distortion, saturation, multiband, shaper, effect, parfait

A three-band saturator. The signal is split by two 24 dB/oct Linkwitz-Riley crossovers whose positions you set with per-band Weight and a global Skew, and each band gets its own waveshaper, DC bias, level and morphing state-variable filter. A shared Drive with tilt-EQ Tone feeds all three, and the output stage adds an optional compressor and tanh limiter. Good for adding weight to lows without fizzing the highs, and equally happy as a full-on destruction box.

**Controls**

| Control | Type | Range / Options | Default | What it does |
|---|---|---|---|---|
| Drive | DriveControl | 0…16 | 1.00 | Global input drive into all three bands. Sub-display carries Tone Amount and Tone Freq. |
| Band Low | BandControl (spectrum display + fader) | 0…2 | 1.00 | Level of the low band. Shift cycles its sub-display between normal, shaper (`amt`/`bias`/`type`) and filter (`wt`/`freq`/`mrph`). |
| Band Mid | BandControl | 0…2 | 1.00 | Level of the mid band, same sub-displays. |
| Band High | BandControl | 0…2 | 1.00 | Level of the high band, same sub-displays. |
| Skew | GainBias | -1…1 | 0.00 | Shifts both crossover points in log-frequency space: positive bunches them low, negative bunches them high. |
| Mix | ParfaitMixControl | 0…1 | 1.00 | Dry/wet blend. Sub-display carries Compress, Output Level and Output Saturation. |
| Band Low/Mid/High Amount | GainBias (expansion) | 0…1 | 0.50 | Shaper drive and dry/wet for that band — 0 is clean, 1 is fully shaped. |
| Band Low/Mid/High Bias | GainBias (expansion) | -1…1 | 0.00 | DC offset into the shaper, pushing it off-centre for even harmonics. |
| Band Low/Mid/High Type | ModeSelector (expansion, discrete) | `Off`, `Tube`, `Diode`, `Fold`, `Half`, `Crush`, `Sine`, `Fractal` | `Off` | Waveshaper for that band (see list below). |
| Band Low/Mid/High Weight | GainBias (expansion) | 0.1…4 | 1.00 | Relative width of that band; the three weights divide the 20 Hz–20 kHz log span and set the two crossovers. |
| Band Low/Mid/High Filter Freq | GainBias (expansion) | 20…20000 Hz | 1000 Hz | Cutoff of that band's post-shaper morphing SVF. |
| Band Low/Mid/High Filter Morph | ThresholdFader (expansion) | 0…1, labelled `off`, `LP`, `L>B`, `BP`, `B>H`, `HP`, `H>N`, `ntch` | 0.00 (`off`) | Sweeps the band filter continuously from bypass through lowpass, band-pass, highpass to notch. |
| Band Low/Mid/High Filter Q | GainBias (expansion) | 0.5…20 | 0.50 | Resonance of that band's filter. |
| Tone Amount | GainBias (expansion) | -1…1 | 0.00 | Tilt EQ before the bands: negative tilts dark, positive tilts bright. |
| Tone Freq | GainBias (expansion) | 50…5000 Hz | 800 Hz | Pivot frequency of the tilt EQ. |
| Compress | GainBias (expansion) | 0…1 | 0.00 | Output compressor amount; raising it lowers the threshold and increases the ratio together. |
| SC HPF | GainBias (expansion) | 0 or 1 (integer) | 0 | Enables a high-pass filter on the compressor's sidechain so low end doesn't pump the whole signal. |
| Output Level | GainBias (expansion) | 0…4 | 1.00 | Final output gain. |
| Output Saturation | GainBias (expansion) | 0…1 | 0.00 | Blends in a tanh-saturated copy of the output. |

Shaper types, in list order:

- `Off` — passthrough, no shaping.
- `Tube` — asymmetric soft clip (different curve above and below zero, even harmonics).
- `Diode` — arctan-style soft knee with a squared term.
- `Fold` — three-pass triangle wavefolder with gain between passes.
- `Half` — half-wave rectified soft clip (negative half muted).
- `Crush` — mu-law companded bit reduction (8 steps).
- `Sine` — sine wavefolder, decoupled from Drive, roughly 1–2 folds at full amount.
- `Fractal` — iterated cubic polynomial clamped to its stable region.

**Sub-display / expanded** — Enter on `Drive` gives `tone` / `freq`. Enter on any band ply gives that band's Amount / Bias / Type / Weight / Filter Freq / Filter Morph / Filter Q. Enter on `Mix` gives `comp` / `out` / `tanh` plus SC HPF. On each band ply, shift cycles the sub-display: normal fader → `Amt / Bias / Type` → `Wt / Freq / Morph` → back.

**Menu** — none.

**I/O** — One audio input, one audio output; instantiated stereo the unit runs two saturators (left/right) sharing all parameters. Every control has a modulation branch, including all per-band parameters (CV on a per-band branch reaches the audio, not just the display). No V/Oct, gate or trigger inputs.

---

## Impasto

<mnemonic: Im> · Category: Spreadsheet · Keywords: compressor, multiband, dynamics, sidechain, effect, impasto

A three-band compressor with an FFT spectrum display: the incoming signal is driven, optionally tilted, split into LO/MID/HI, compressed per band, and recombined against a dry/wet mix. The two crossover points are not frequency knobs — they are placed by the per-band Weight balance and then pushed around by a single bipolar **Skew** control, so you steer where the bands sit rather than dialling exact hertz. There is a dedicated sidechain input that can be switched in to key all three detectors, and each band's spectrum ply shows its live gain-reduction contour.

**Controls** (main view, left to right)

| Control | Type | Range / Options | Default | What it does |
|---|---|---|---|---|
| Drive | DriveControl (fader + CV branch) | 0 – 4 | 1.00 | Input gain into the whole chain, ahead of the tilt EQ and crossover. |
| Sidechain | CompSidechainControl (fader + audio branch) | 0 – 4 | 1.00 | Fader is the sidechain input gain. The branch itself is the sidechain audio inlet; a MiniScope on the sub-display shows what is arriving. |
| LO Band | CompBandControl (spectrum + fader + CV branch) | 0 – 2 | 1.00 | Output level of the low band, drawn over that band's slice of the FFT spectrum with its gain-reduction ceiling. |
| MID Band | CompBandControl | 0 – 2 | 1.00 | Output level of the mid band. |
| HI Band | CompBandControl | 0 – 2 | 1.00 | Output level of the high band. |
| Skew | GainBias | −1 – +1 | 0.00 | Bipolar shift of both crossover points along the 20 Hz – 20 kHz log axis. Negative pushes the splits up, positive pulls them down. |
| Mix | CompMixControl (GainBias) | 0 – 1 | 1.00 | Dry/wet blend of the compressed result against the untouched input. |

**Sub-display / expanded**

- **Drive** — sub-display holds `tone` / `freq` readouts (label "Tone / Freq"). Expanding the ply gives **Drive**, **Tone** (0 – 1, default 0.00 — tilt-EQ amount, positive tilt brightens) and **Tone Freq** (20 – 20000 Hz, default 800 Hz — the tilt pivot).
- **Sidechain** — sub-buttons `side` (open the sidechain branch), `enable` (toggle the sidechain on/off; indicator lit = on) and `gain` (the input-gain readout). With the sidechain disabled, each band keys off its own audio.
- **LO / MID / HI Band** — shift on the ply swaps the sub-display to `thresh` / `ratio` / `speed` readouts. Expanding gives that band's ply plus **Threshold** (0 – 1, default 0.50; cubed internally, so 1.00 = 0 dB and 0.50 ≈ −18 dB), **Ratio** (1 – 20, default 2.0), **Attack** (0.0001 – 0.1 s, default 0.0010 s) and **Release** (0.001 – 1 s, default 0.050 s). **Speed** (0 – 1, default 0.30) is reachable only from the sub-display readout; it is a G-Bus style single knob that interpolates attack/release together from 30 ms/1.2 s at 0 down to 0.1 ms/0.1 s at 1.
- **Mix** — shift on the ply swaps the sub-display to `auto` (auto-makeup toggle, indicator lit = on) and `output`. Expanding gives **Mix** plus **Output** (0 – 2, default 1.00).

**Menu** — none beyond the framework defaults.

**Caveat** — the per-band **Attack** and **Release** faders are present, save with the patch, and move, but the DSP derives attack and release solely from **Speed**; they have no audible effect in the shipping build. Per-band **Weight** determines crossover placement but has no on-screen control, so in practice the crossovers move only via **Skew** (or CV into the hidden weight branches).

**I/O** — Mono or stereo depending on the chain. On a stereo chain two compressor instances run, one per channel, sharing every parameter and the single mono sidechain branch (detectors are therefore not linked across L/R). Drive, Tone, Tone Freq, Skew, Mix, Output and every per-band Threshold / Ratio / Speed / Attack / Release / Weight / Level have their own mono CV branch. The Sidechain ply's branch carries audio, not CV — the sidechain input gain has no CV branch of its own. No V/Oct, gate or trigger inputs.

**Bands and crossovers** — exactly 3 bands (LO/MID/HI) split by two Linkwitz-Riley 4th-order (24 dB/oct) crossovers. Crossover placement is computed from the three per-band Weight values (normalised cumulative split, mapped as 20·1000^x Hz) then displaced by Skew, and clamped to 30 Hz … 0.33·samplerate with a 10 Hz minimum gap between the two splits. Sidechain options are binary: **enable** (sidechain input keys all three detectors, scaled by the Sidechain fader) or disabled (each band self-keys); the **auto** toggle on Mix adds threshold-derived makeup gain per band.

---

## Larets

<mnemonic: Lr> · Category: Spreadsheet · Keywords: effect, step, sequencer, multi, processor, larets

A clock-driven stepped multi-effect: a 16-step sequencer where every step holds one effect type, one parameter value and a tick length, and the audio passing through is mangled by whichever step is current. Ten effects are on offer, several of them buffer tricks locked to the measured clock period (beat-repeat, reverse, shuffle), so the results stay musical rather than arbitrary. A transform gate can randomise, rotate or reverse the step program on the fly, and a single-band compressor tidies up the level jumps.

Larets is **true stereo** (since release v2.5.1, which took the spreadsheet package 2.7.0 → 2.7.1): the C++ object owns separate L and R buffers, filter and grain state, while the sequencer, the shuffle random pick and the compressor detector are shared so the stereo image stays coherent.

**Controls** (main view, left to right)

| Control | Type | Range / Options | Default | What it does |
|---|---|---|---|---|
| Clock | LaretClockControl (comparator + branch) | trigger | — | Advances the sequencer. Also measures the clock period, which sets the loop length for stutter / reverse / shuffle / pitch. |
| Steps | LaretStepListControl | 16 steps | type off, param 0.50, ticks 1 | The step editor. Scroll to select a step; sub-buttons edit its type, param and tick length. |
| Overview | LaretOverviewControl (fader + CV branch) | −1 – +1 | 0.00 | Whole-sequence visualiser; the fader itself is **Skew**. |
| Param Offset | GainBias | −1 – +1 | 0.00 | Bipolar global offset added to every step's param before the effect runs (clamped to 0–1). One knob to sweep the whole program. |
| Transform | TransformGateControl (gate + branch) | gate | — | On a rising gate (or the `fire!` sub-button) applies the selected transform to the step program. |
| Mix | LaretsMixControl (GainBias) | 0 – 1 | 1.00 | Dry/wet blend of the effect chain against the input. |

**Effect types** — 11 entries in the type selector (0 = off plus 10 effects). Names are exactly as they appear in the readout:

| # | Name | What it does |
|---|---|---|
| 0 | off | Passes the input through untouched. |
| 1 | stt | Stutter / beat-repeat: snapshots the last musical fraction of a clock period (1/16, 1/8, 1/4, 1/2, 1 — chosen by param) and loops it for the step. |
| 2 | rev | Reverse: plays the last clock period backwards; param sets playback rate 0.5× – 2×. |
| 3 | bit | Bitcrush: quantises amplitude from 12-bit at param 0 down to ~2.5-bit at param 1. |
| 4 | dec | Downsample: sample-and-hold decimation, factor 1 – 32 by param. |
| 5 | flt | Filter: 2-pole state-variable lowpass, ~40 Hz – 20 kHz by param, with cutoff rising over the step's progress. |
| 6 | pch | Pitch shift: two-grain overlap shifter, param maps to −12 – +12 semitones. |
| 7 | drv | Distortion: hard clip against a unit ceiling, drive 1× – 20× by param, with makeup gain. |
| 8 | shf | Shuffle: beat-repeat as `stt`, but the slice is picked at random from a wider window each loop (same pick on L and R). |
| 9 | dly | Delay: plain tap up to 0.5 s by param, no feedback. |
| 10 | cmb | Comb: input plus a short delayed copy at 0.7 gain, delay ~20 samples – 20 ms by param. |

**Transform functions** — 7 entries on the Transform ply's `func` readout, applied with a probability/amount given by `depth`:

| # | Name | What it does |
|---|---|---|
| 0 | all | Randomises type, param and ticks on each selected step. |
| 1 | t+p | Randomises type and param. |
| 2 | type | Randomises effect types only. |
| 3 | prm | Randomises params only. |
| 4 | tick | Randomises tick lengths (1 – 4). |
| 5 | rot | Rotates the whole program; depth scales the rotation distance. |
| 6 | rev | Reverses the step order. |

**Sub-display / expanded**

- **Clock** — sub-buttons `input` (open the clock branch), `div` (**Clock Division**) and `reset` (open the Reset branch); MiniScope on the incoming clock. Expanding gives **Clock**, **Reset** (trigger input) and **Clock Division** (1 – 16, default 1).
- **Steps** — sub-buttons `type` (0 – 10, names as above), `param` (0 – 1) and `ticks` (1 – 16); the label shows "Step N". Encoder alone scrolls steps; shift + encoder scrolls steps while a readout is focused. The type readout uses discrete stepping so a fast turn cannot skip an entry.
- **Overview** — shift on the ply swaps the sub-display to `rand` (step-advance toggle; lit = random, dark = sequential), `steps` and `loop`. Expanding gives **Overview**, **Skew** (−1 – +1, default 0.00 — tilts effective tick lengths across the sequence), **Step Count** (1 – 16, default 8) and **Loop Length** (1 – 16, default 16; wraps at min(loop, step count)).
- **Transform** — gate sub-display: `input` / `thresh` / `fire`. Shift toggles to the parameter sub-display: `func`, `depth` (0 – 1, default 0.50) and `fire!`.
- **Mix** — shift toggles the sub-display to `out` (**OutputLevel**, 0 – 4, default 1.00), `comp` (compressor amount, 0 – 1, default 0.00 — 0 is bypass, 1 is an aggressive limiter at −40 dB / 20:1 / 1 ms) and `auto` (auto-makeup toggle).

**Menu** — "Set All Tick Lengths" header plus five tasks: **1 tick**, **2 ticks**, **4 ticks**, **8 ticks**, **16 ticks**. Each writes that tick length to all 16 steps.

**I/O** — Mono or stereo; on a stereo chain both In L/In R are patched and both Out L/Out R are driven independently. **Clock** and **Reset** are trigger inputs (rising edge); **Transform** is a gate input (fires on the rising edge). CV branches on Step Count, Skew, Mix, Output Level, Compress Amount, Clock Division, Loop Length and Param Offset.

---

## Blanda

<mnemonic: Bl> · Category: Spreadsheet · Keywords: mixer, scan, morph, fade, three, input, blanda

A three-input scan mixer. Each input owns a bell-shaped response centred on its own position along a 0–1 scan axis, and a single **Scan** control sweeps a playhead across that axis, fading the inputs in and out as it passes through their bells. Widening or collapsing the bells with **Focus** decides whether you get a smooth crossfade, hard isolated zones, or all three inputs sounding at once. There is no filtering on board — patch your own into the three inputs.

**Controls** (main view, left to right)

| Control | Type | Range / Options | Default | What it does |
|---|---|---|---|---|
| Input 1 | MixInputControl (fader + audio branch) | 0 – 2 | 1.00 | First audio inlet; the fader is its Level. Sub-display carries Solo/Mute. |
| Input 2 | MixInputControl | 0 – 2 | 1.00 | Second audio inlet. |
| Input 3 | MixInputControl | 0 – 2 | 1.00 | Third audio inlet. |
| Scan | ScanSkewControl (GainBias) | 0 – 1 | 0.50 | Playhead position along the scan axis. This is the morph control. |
| Focus | FocusShapeControl (GainBias) | −1 – +1 | 0.00 | Global bell width. −1 collapses every bell to a spike (hard zones), +1 widens them fourfold (heavy overlap). |
| Output Level | GainBias | 0 – 2 | 1.00 | Final output gain. |

**Curves and shapes** — Blanda has no discrete mode enum; three continuous shapers do the work:

- **Bell shape** — per input, `Shape` morphs the response from a triangle (0.00) to a plateau/near-rectangle (1.00): coefficient = max(0, 1 − (d/w)^γ) with γ = 1 + 3·shape.
- **Focus** — bell half-width = 0.22 · 4^focus, so −1 gives ≈0.055 and +1 gives ≈0.88 of the scan axis, floored at 1/512 so the playhead can never fall between bells.
- **Skew** — a bipolar macro that warps every input's Offset through x^(2^skew). Monotonic and ordering-preserving: positive skew bunches the bells toward the low end of the scan axis, negative toward the high end.

Parameters are read at block rate rather than per sample, so very fast **Scan** CV will step at block boundaries rather than glide.

**Sub-display / expanded**

- **Input 1 / 2 / 3** — default sub-display shows the patched source mnemonic plus **Solo** and **Mute** panels (all three inputs are enrolled in the unit mute group, and solo/mute state is saved with the patch). Shift swaps to `level` / `wght` / `ofst` readouts. Expanding gives the input ply plus **Level N** (0 – 2, default 1.00), **Weight N** (0 – 2, default 1.00 — scales that bell's width relative to Focus) and **Offset N** (0 – 1; defaults 0.17 / 0.50 / 0.83 for inputs 1/2/3 — the bell's centre on the scan axis).
- **Scan** — shift swaps the sub-display to a single centred `skew` readout ("Skew"). Expanding gives **Scan** plus **Skew** (−1 – +1, default 0.00).
- **Focus** — shift swaps the sub-display to `in1` / `in2` / `in3` readouts (label "Bell Shape"). Expanding gives **Focus** plus **Shape 1**, **Shape 2**, **Shape 3** (each 0 – 1, default 0.00).

**Menu** — none beyond the framework defaults.

**I/O** — Three mono audio inputs, patched as branches on the Input 1/2/3 plies (they are not the unit's chain input). The mix is summed to a single mono output; on a stereo chain that same output is copied to both channels. Every parameter — the nine per-input Level/Weight/Offset values, the three Shapes, Scan, Focus, Skew and Output Level — has its own mono CV branch. No V/Oct, gate or trigger inputs.

---

## Colmatage

<mnemonic: BC> · Category: Spreadsheet · Keywords: effect, rhythm, breakbeat, slicer, stutter, warp, colmatage

A clock-driven breakbeat cutter. Feed it a loop and a clock, and it re-cuts the incoming audio into phrases of blocks, each block filled with repeats that accelerate, ritard, or run straight. Negative duty cycle reads the slice backwards. The algorithm descends from Nick Collins' BBCut library (ICMC 2002), by way of the WarpCut/Livecut implementation by Rémy Muller (GPLv2).

**Controls** (main view, left to right)

| Control | Type | Range / Options | Default | What it does |
|---|---|---|---|---|
| Clock | Gate (trigger) + sub-display | — | — | Clock input. One trigger per beat; the unit divides each beat into `Subdivision` units. |
| Block Size | Custom (GainBias + mosaic overview) | 0 – 1 | 0.5 | Weights how long each cut block is, from short blocks (0) to blocks near `Block Max` (1). Control graphic is the phrase/block mosaic overview. |
| Density | GainBias | 0 – 1 | 0.5 | Probability a given block gets cut at all. At 0 blocks pass through straight; at 1 every block is repeated/mangled. |
| Repeats | Custom (GainBias) | 2 – 64 | 8 (view) / 4 (DSP init) | Number of repeats packed into a cut block. |
| Duty Cycle | Custom (GainBias) | -1 – +1 | 1.00 | Length of each repeat as a fraction of its slot — below 1 leaves gaps (gated stutter). **Negative values read the slice in reverse.** |
| Mix | Custom (GainBias) | 0 – 1 | 1.00 | Equal-power (sqrt-law) dry/wet blend between input and the cut output. |

**Expanded sub-parameters** — press Enter on a main control to expand it into full faders:

| Parent | Control | Type | Range | Default | What it does |
|---|---|---|---|---|---|
| Clock | Reset | Gate (trigger) | — | — | Restarts the phrase. |
| Clock | Subdivision | GainBias | 6 – 32, snapped to 6/8/12/16/24/32 | 8 | Units per beat. Only the six listed values are used; intermediate settings snap to the nearest. |
| Block Size | Phrase Min | GainBias | 1 – 8 | 2 | Minimum phrase length in bars. |
| Block Size | Phrase Max | GainBias | 1 – 8 | 4 | Maximum phrase length in bars. |
| Block Size | Block Max | GainBias | 1 – 16 | 8 | Largest block, in beats (converted internally to `beats × Subdivision / 4` units). |
| Repeats | Repeats | GainBias | 2 – 64 | 4 | Same parameter as the main-view control. |
| Repeats | Ritard Bias | GainBias | 0 – 1 | 0.50 | Probability that a geometric series runs as a ritard (slowing) rather than an accel (speeding up). |
| Repeats | Blend | GainBias | 0 – 1 | 0.50 | Probability a block uses the geometric accel/ritard series rather than even, equal-length repeats. |
| Repeats | Accel | GainBias | 0.500 – 0.999 | 0.900 | Ratio of the geometric series. Lower = more extreme acceleration; near 1 = nearly even. |
| Duty Cycle | Duty Cycle | GainBias | -1 – +1 | 1.00 | Same parameter as the main-view control. |
| Duty Cycle | Amp Min | GainBias | 0 – 1 | 0.80 | Low end of the random per-cut amplitude range. |
| Duty Cycle | Amp Max | GainBias | 0 – 1 | 1.00 | High end of the random per-cut amplitude range. Cuts ramp between two draws across a block. |
| Duty Cycle | Fade | GainBias | 0 – 0.100 s | 0.005 s | Per-cut edge fade, to keep slice boundaries from clicking. |
| Mix | Input Level | GainBias | 0 – 4 | 1.00 | Gain into the cutter. |
| Mix | Output Level | GainBias | 0 – 4 | 1.00 | Gain out of the cutter. |
| Mix | Saturation | GainBias | 0 – 1 | 0.00 | Amount of tanh saturation on the output. |

**Sub-displays** — Clock shows a mini-scope for the clock input plus a `div` readout (Subdivision) and a `reset` button. Block Size, Repeats, Duty Cycle and Mix each carry a three-readout sub-display reachable by holding Shift: `Phrase Min / Phrase Max / Block Max`, `Rit / Blend / Accel`, `AMin / AMax / Fade`, and `Input / Output / Sat` respectively. The Block Size control graphic is a live mosaic overview of the current phrase, block and cut position.

**Menu** — none.

**I/O** — Mono in (In1), same signal to Out1 and (in a stereo chain) duplicated to Out2 — the processing is mono. Clock and Reset are trigger-mode comparator inputs with their own CV branches. All 18 parameters are CV-modulatable via ParameterAdapters.

---

## Pecto

<mnemonic: Pc> · Category: Spreadsheet · Keywords: comb, resonator, filter, delay, karplus, pecto

A multi-tap comb resonator with up to 24 taps whose spacing is chosen from 16 tap patterns and shaped by an amplitude slope. Feedback runs through one of four resonator characters — raw, guitar (Karplus-Strong damping), clarinet (odd-harmonic soft clip), or sitar (jawari buzz via amplitude-dependent delay modulation). Feedback is bipolar, so negative values invert each loop for a hollow, differently-tuned comb. V/Oct tracks the comb pitch and a Randomize gate re-rolls the whole voicing.

**Controls** (main view, left to right)

| Control | Type | Range / Options | Default | What it does |
|---|---|---|---|---|
| V/Oct | Pitch | ±12 octaves (clamped in DSP) | 0 | Shifts comb pitch; higher pitch shortens the base delay. |
| Comb Size | GainBias | 0.001 – 2.000 s | 0.100 s | Base comb delay in seconds — the fundamental of the resonance. Buffer is allocated to 2 s. |
| Density | Custom (GainBias) | 1 – 24 | 8 | Number of active comb taps. |
| Feedback | GainBias | -0.99 – +0.99 | 0.50 | Regeneration around the longest tap. Negative inverts phase each pass. |
| Randomize | Transform gate + sub-display | target: see below; depth 0 – 1 | depth 0.50 | Fire (or gate) to randomize the selected parameter(s) by `depth`. |
| Mix | Custom (GainBias) | 0 – 1 | 0.50 | Dry/wet blend. |

**Randomize targets** — `all`, `size`, `fdbk`, `res`, `dens`, `patt`, `slope`, `mix`, `reset`. `all` re-rolls every voicing parameter; `reset` returns them to defaults. Depth 0 leaves values alone, depth 1 fully randomizes across each parameter's range.

**Expanded sub-parameters**

| Parent | Control | Type | Range / Options | Default | What it does |
|---|---|---|---|---|---|
| Density | Density | GainBias | 1 – 24 | 8 | Active tap count. |
| Density | Pattern | ModeSelector (discrete) | 16 entries, see below | `unif` | Distribution of tap positions across the comb length. |
| Density | Slope | ModeSelector (discrete) | `flat`, `rise`, `fall`, `hump` | `flat` | Amplitude envelope applied across the taps. |
| Density | Resonator | ModeSelector (discrete) | `raw`, `gtr`, `clar`, `sitr` | `raw` | Character of the feedback path. |
| Mix | Mix | Custom (GainBias) | 0 – 1 | 0.50 | Dry/wet blend. |
| Mix | Input Level | GainBias | 0 – 4 | 1.00 | Gain into the comb. |
| Mix | Output Level | GainBias | 0 – 4 | 1.00 | Gain out of the comb. |
| Mix | Saturation | GainBias | 0 – 1 | 0.00 | tanh saturation on the output. |

**The 16 tap patterns** — entries 0–7 are the base distributions, 8–15 are the same distributions with a seeded ±10% per-tap perturbation (the `r.` family). Shift on the encoder jumps between the two families.

`unif` (uniform, evenly spaced) · `fib` (Fibonacci / golden-ratio spacing) · `early` (clustered toward the start) · `late` (clustered toward the end) · `mid` (clustered toward the centre) · `ess` (smoothstep S-curve: sparse at the ends) · `flat` (all taps at one position — unison/chorus) · `rfib` (Fibonacci mirrored about 0.5) · `r.un` · `r.fi` · `r.ea` · `r.la` · `r.mi` · `r.es` · `r.fl` · `r.rf`

**The 4 resonator types**

| Option | Character |
|---|---|
| `raw` | Direct wire — bright, metallic, undamped. |
| `gtr` | Guitar: one-pole lowpass damping in the feedback loop (Karplus-Strong). Highs decay fast, lows ring. |
| `clar` | Clarinet: soft-clip drive in the feedback path for odd-harmonic emphasis. |
| `sitr` | Sitar: amplitude-following modulation of the write position — jawari buzz and pitch wobble that follow playing level. |

**Sub-displays** — Density carries a `Patt / Slope / Res` three-readout sub-display (hold Shift, then `patt` / `slope` / `res`), stepping by whole named entries. Mix carries `Input / Output / Sat`. Randomize carries a target selector plus a `depth` readout and a manual fire.

**Menu** — none.

**I/O** — Mono in / mono out, or true stereo: in a stereo chain a second independent Pecto instance is instantiated for In2 → Out2, with all parameters tied across both. V/Oct is a pitch input (ConstantOffset branch). XformGate is a trigger-mode comparator input. All parameters are CV-modulatable.

---

## Network

<mnemonic: Nw> · Category: Spreadsheet · Keywords: reverb, multitap, delay, spatial, network, geometry, glitch

A non-traditional reverb built as a macro spatial simulation rather than a tank. A field of up to 64 virtual reflectors is generated on a phyllotaxis (golden-angle) spiral; per-tap delay comes from each reflector's distance to a listener, and per-tap L/R pan comes from its azimuth. Motion walks the listener around an orbit, so the whole field slews coherently with Doppler-like coupling. Connectivity sparsely recycles taps into a feedback bus with allpass diffusion, and the Glitch macro progressively swaps taps into mute / stutter / crush / scrub / reverse behaviour — taking the unit from a lush room to a disintegrating one.

**Controls** (main view, left to right)

| Control | Type | Range / Options | Default | What it does |
|---|---|---|---|---|
| Glitch | Custom (GainBias + 3D field viz) | 0 – 1 | 0.000 | Character macro, lush → glitch. Raises the per-tap probability of each glitch mode, brightens the feedback lowpass, and (with Motion) drives reflector respawns. Control graphic is the listener-centred sphere overview. |
| Size | GainBias | 0 – 1 (clamped to ≥0.01 in DSP) | 0.500 | Scales the maximum tap delay — the apparent scale of the space. |
| Density | GainBias | 0 – 1 | 0.500 | Fraction of the 64 reflectors that are active (1 – 64 taps). |
| Motion | GainBias | 0 – 1 | 0.500 (view) / 0.0 (DSP init) | Rate at which the listener walks the orbit. Also gates glitch-pattern reshuffling — patterns freeze at Motion 0 and shuffle on each revolution. |
| Connectivity | GainBias | 0 – 1 | 0.000 | Fraction of active taps recycled into the feedback bus. Also scales allpass diffusion strength and per-tap pitch detune. |
| Decay | GainBias | 0 – 1 | 0.500 | Feedback gain scaler for the recycled taps — how long the space rings. |
| Dry/Wet mix | GainBias | 0 – 1 | 0.500 | Dry/wet blend. |

**Glitch modes** — glitch is not a selector; it is a probability macro. Each active tap gets at most one effect per cycle, drawn from: `NORMAL` (untouched), `MUTE`, `STUTTER`, `CRUSH` (bit/sample-rate reduction), `SCRUB` (buffer-position jump, depth scaling with Size), `REVERSE`. Effects stack across taps via the shared feedback bus, so high settings still layer. Stutter is the dominant character, mute second.

**Sub-display / expanded** — Glitch replaces the normal bar fader with a live 3D overview: a listener-centred sphere with per-tap trails, sonar pings whose ring crossings track each reflector's real world-space distance, and radial displacement of taps by glitch amount. No expanded sub-parameter pages — every control is a top-level fader.

**Menu** — none.

**I/O** — Mono in (In1) → stereo out. Out1 (left) and OutR → Out2 (right) are written directly from per-tap pan derived from reflector azimuth; in a mono chain only the left output is connected. Delay buffer is allocated to 1 second. No gate, trigger or V/Oct inputs. All seven exposed parameters have CV branches.

**Not exposed on the front panel** — the DSP also carries `Seed` (hashed to a uint32; regenerates the reflector field when changed) and `InputLevel`. `Seed` is tied to a ParameterAdapter but has no view control and no CV branch in the shipping unit, so the field is fixed at its built-in default seed.

---

## Canals

<mnemonic: Ca> · Category: Spreadsheet · Keywords: filter, resonant, svf, crossover, formant, canals

A linked three-band resonant filter in the Three-Sisters idiom: one frequency control moves a LOW / CENTRE / HIGH trio that spreads apart as you open Span, with a Quality control that runs from anti-resonance through high Q into self-oscillation. It mirrors the hardware's input arrangement — a main ALL input that feeds all three bands, plus per-band inputs that override ALL for whichever band you patch. The three bands are available as separate sub-outputs at the same time as the morphed main output, so it works equally as a filter, a crossover, or a formant voice.

**Moved package in v2.6.1.** Canals used to live in `biome`; it now lives in `spreadsheet` and `biome.Canals` was removed. Patches saved against `biome.Canals` will not load — re-point them at `spreadsheet.Canals`. (Verified in RELEASE-2.6.1.md.) The spreadsheet version is a full rebuild, not a re-hosting of the old one.

**Controls** (main view order: in, mode, V/oct, freq, span, qual, out)

| Control | Type | Range / Options | Default | What it does |
|---|---|---|---|---|
| (overview ply, button `in`) | Custom overview | — | — | Routing view for the three per-band inputs; see below. |
| Mode | ModeSelector (discrete) | `Xover`, `Formnt` | `Xover` | Picks the band topology. `Xover` is the crossover arrangement (LOW = lowpass, CENTRE = band, HIGH = highpass). `Formnt` re-taps the outer bands (LOW takes the highpass tap, HIGH the lowpass tap) and puts CENTRE on the centre frequency itself, giving the stacked-formant behaviour. |
| V/oct | Pitch | — | — | 1 V/oct transposition of the whole band group (1 V = 12 semitones). |
| Fundamental | GainBias | -48 to +48 semitones | 0.00 | Centre frequency, in semitones relative to middle C (261.63 Hz). Result is clamped to 20 Hz – 20 kHz. |
| Span | GainBias | 0 to 1 | 0.25 | How far LOW and HIGH spread either side of the centre, 0 to 48 semitones each way. |
| Quality | GainBias | -1 to 1 | 0.00 | Resonance. Negative values give anti-resonance (notching) with Butterworth damping; 0 to ~0.9 raises Q up to roughly 50; the top decile crosses into self-oscillation. |
| Output | ModeSelector (discrete) | 0 to 3, named `LOW`, `CTR`, `HIGH`, `ALL` | 0.0 (`LOW`) | Continuous morph across the band mix on the main output: 0–1 crossfades LOW→CENTRE, 1–2 CENTRE→HIGH, 2–3 fades LOW and CENTRE back in alongside HIGH until all three are equal at `ALL`. Only affects the main output; the per-band sub-outputs are unaffected. |

**Sub-display / expanded** — The `in` ply is a routing overview: a three-stripe graphic (LOW / CENTRE / HIGH) showing each band's post-routing input, with an "ALL" overlay on any band currently falling back to the main input. Its sub-display holds three MiniScopes, one per band input; sub-buttons 1/2/3 dive into the corresponding LOW / CENTRE / HIGH subchain. There are no level or bias controls there — the per-band inputs pass at unity, so any shaping happens inside the subchain you dive into.

Normalling is automatic: as soon as a per-band subchain has a unit or an assigned input source, that band takes its own input instead of ALL. Emptying the subchain reverts it to ALL.

**Menu** — `Routing` header, and `ALL Input` (`enabled` / `disabled`, default `enabled`). Disabling it makes every unpatched band silent instead of falling back to the main input.

**I/O** — Mono DSP core (the hardware is mono); on a stereo chain both output channels carry the same signal. For true stereo, place two Canals in parallel. Inputs: main In (ALL), plus three mono input branches (LOW / CENTRE / HIGH) reached from the `in` ply, plus a V/oct input and CV branches on Fundamental, Span, Quality, Output and Mode. Five sub-outputs: `Out`, `Out R`, `LOW`, `CENTRE`, `HIGH` — the last three are always the direct per-band taps regardless of the Output morph position, and are derived from the left-side instance. Cutoff modulation runs at 2× oversampling.

---

## Fabula

<mnemonic: Fa> · Category: Spreadsheet · Keywords: reverb, room, hall, dattorro, algorithmic, tank, allpass, space, freeze, fabula

A smooth, long-decay algorithmic room built on a Dattorro/Griesinger figure-8 recirculating-allpass tank. Each delay line is modulated by its own Brownian random walk rather than an LFO, so the tail drifts and breathes instead of chorusing on a fixed period, and a discrete early-reflection network sits in parallel with the diffuse tail so the room reads as present rather than as a pure wash. Freeze is continuous, ramping the tank into a self-sustaining cloud that stays alive under the modulation. An Xform gate re-rolls the whole room to a new space on a trigger.

**New in v2.7.0** — verified against RELEASE-2.7.0.md, which introduces Fabula as the only new unit of that release (spreadsheet 2.8.2 → 2.8.3).

**Controls** (main view order: size, pre, ER, frz, xform, mix)

| Control | Type | Range / Options | Default | What it does |
|---|---|---|---|---|
| Size | Custom overview (GainBias + fabric viz) | 0 to 1 | 0.35 | Room size — scales the tank delay lengths. Main dial of the unit; its fader is replaced by the fabric waterfall visualization. |
| Predelay | GainBias | 0 to 1 | 0.04 | Delay before the tank, 0 up to the full predelay buffer (16384 samples, ≈341 ms at 48 kHz). |
| Early Reflections | GainBias | 0 to 1 | 0.40 | Level of the discrete early-reflection network, and a macro that biases the room toward smaller/shorter/warmer as it rises. At 0 the ER network contributes nothing. |
| Freeze (living hold) | GainBias | 0 to 1 | 0.00 | Continuous hold. As it rises the tank input mutes and the tail ramps to a self-sustaining cloud, locking in stages (left then right) while the Brownian modulation keeps it moving. Smoothed (~30 ms glide). |
| Randomize | TransformGateControl (trigger) | trigger | — | Rising edge re-rolls the room. Destructive: the affected control biases visibly move and the new room serializes. Sub-parameters: target (below) and `depth` 0 to 1 (default 0.50), the blend from the current settings toward the random ones. Can also be fired manually from the control's sub-display. |
| — Randomize target | Sub-parameter of Randomize | `noFrz`, `all`, `size`, `dcay`, `damp`, `diff`, `ER`, `pre`, `frz`, `reset` | `noFrz` | Which parameter(s) a re-roll touches. `noFrz` = everything except Freeze; `all` = everything; the single-name entries touch just that one; `reset` restores defaults. |
| Dry/Wet | MixHpfControl (GainBias) | 0 to 1 | 0.40 | Dry/wet balance. Hosts the wet highpass as a sub-parameter. |
| Decay | GainBias (under Size) | 0 to 1 | 0.55 | Tail length — tank feedback. |
| Damp | GainBias (under Size) | 0 to 1 | 0.25 | High-frequency damping in the tank; higher is darker. |
| Diffusion | GainBias (under Size) | 0 to 1 | 0.45 | Scales the six allpass coefficients — smear versus discrete echo character. |
| Wet Highpass | GainBias (under Dry/Wet) | 20 to 500 Hz | 60 Hz | Highpass corner on the wet signal — how much low body the room keeps. Deliberately not a Randomize target. |

**Sub-display / expanded** — Size's main graphic is the "fabric" waterfall: stacked flat spectrum contours from a 16-band analyzer on the wet output, scrolling and dimming with age. Tap-shift on Size reveals compact Decay / Damp / Diffusion readouts; pressing enter expands them to full faders. Tap-shift on Dry/Wet reveals the Wet Highpass readout; enter expands it to a full fader. Both routes bind the same parameters, so they stay in sync. Randomize's sub-display carries the target name, the depth readout, and a manual fire button. The analyzer idles whenever Fabula's display is off screen.

**Menu** — none.

**I/O** — Internally stereo: cross-feedback lives inside the tank. On a stereo chain In1/In2 feed the tank's L/R inputs and Out L/Out R drive Out1/Out2; on a mono chain only the left side is wired. Every listed parameter has its own mono CV branch, plus a gate/trigger input on Randomize. Mod and mod-rate are baked in and not exposed.

---

## Vitrail

<mnemonic: Vt> · Category: Spreadsheet · Keywords: filter, switched capacitor, comb, aliasing, resonant, self-oscillation, character, clock, vitrail

A dual switched-capacitor character filter. Two switched-capacitor cores are always in the path, each running on its own slowly drifting clock (±5% wander) with a shared resonance loop, and each clock pulls the other's frequency down slightly. The grit, the comb notches, and the breathing quality of the self-oscillation are all consequences of the switched-capacitor mechanism rather than added effects. Routing chooses each core's filter tap and whether the two cores cascade or sum, giving 50 distinct combinations.

**New in v2.8.0** — verified against RELEASE-2.8.0.md, which lists Vitrail under "New units elsewhere" (spreadsheet 2.8.3 → 2.8.4).

**Controls** (main view order: rout, clk, cutA, cutB, res, gain)

| Control | Type | Range / Options | Default | What it does |
|---|---|---|---|---|
| Routing | ModeSelector (discrete, normalized) | 50 options (see below) | `LP>LP` | Picks filter A's tap, filter B's tap, and whether they cascade (`A>B`) or sum (`A+B`). The parameter carries a normalized 0–1 value so a 0–1 CV sweeps the whole list; the selector displays and steps indices. Coarse moves one entry per step, shift jumps a whole 5-entry family. |
| Clock Src | VitrailClockControl (discrete) | `A`, `B`, `Both` | `A` | Which clock tunes the cores. Hosts the tunnel visualization. |
| Cutoff A | GainBias | 0 to 1 | 0.50 | Core A cutoff, exponentially mapped ~30 Hz to ~5 kHz (the curve reaches its top at ≈0.8 and holds). Clock A runs at 25× this. Also pulls clock B down by up to 6%. |
| Cutoff B | GainBias | 0 to 1 | 0.50 | Core B cutoff, same mapping — the interference/comb partner. Detuning A against B is where the comb structure comes from. |
| Resonance | GainBias | 0 to 1 | 0.20 | Q law plus the shared-loop gain; at the top the loop breaks into self-oscillation. |
| Gain | GainBias | 0 to 8 | 1.00 | Input drive into the softclip ahead of the filters. |

Routing options, in list order — 25 series entries (`A>B`, cascade) followed by 25 parallel entries (`A+B`, sum), each running filter A across LP/BP/HP/AP/N with filter B across the same five:

- **Series:** `LP>LP, LP>BP, LP>HP, LP>AP, LP>N, BP>LP, BP>BP, BP>HP, BP>AP, BP>N, HP>LP, HP>BP, HP>HP, HP>AP, HP>N, AP>LP, AP>BP, AP>HP, AP>AP, AP>N, N>LP, N>BP, N>HP, N>AP, N>N`
- **Parallel:** `LP+LP, LP+BP, LP+HP, LP+AP, LP+N, BP+LP, BP+BP, BP+HP, BP+AP, BP+N, HP+LP, HP+BP, HP+HP, HP+AP, HP+N, AP+LP, AP+BP, AP+HP, AP+AP, AP+N, N+LP, N+BP, N+HP, N+AP, N+N`

(`N` is the notch tap; `AP` is the allpass-style tap.)

**Sub-display / expanded** — The Clock Src ply carries the tunnel visualization in place of its fader, driven from the audio engine rather than the UI frame rate: forward travel tracks cutoff, the rotation is the A-against-B clock drift, the cross-section morphs from a circle toward a triangle with resonance, and imbalance between the two cutoffs banks the view. The mode name is drawn as an overlay label on the tunnel.

**Menu** — `Vitrail` header, and `Aliasing` (`LO` / `HI`, default `LO`). `HI` applies mild high-frequency smoothing after the switched-capacitor stage, taming the alias content; `LO` leaves it raw.

**I/O** — Mono core: one In, one Out; on a stereo chain the single output is duplicated to both channels. Cutoff A, Cutoff B, Resonance and Gain are audio-rate modulatable inlets, each with its own mono branch — following the package convention, their mod gain defaults to 0, so a patched inlet does nothing until you raise that control's gain. Routing and Clock Src each have a CV branch. No V/oct or gate inputs.

<!-- VERIFICATION NOTES

Package version
- The title version 2.8.5.1 was supplied by the assignment. It is not recorded
  anywhere in the repo: `mods/spreadsheet/assets/toc.lua` has no version field
  and no version string exists under `mods/spreadsheet/`. The release notes
  track the package at 2.8.4 -> 2.8.5 (RELEASE-2.8.1.md); the trailing `.1` is
  unverified. There is no RELEASE-2.8.5.md, so nothing was cross-checked
  against 2.8.5.1-specific notes.
- Release-version and package-version numbering are independent. Confirmed
  mappings: release v2.6.1 -> spreadsheet 2.8.1 (Canals move), v2.7.0 ->
  spreadsheet 2.8.3 (Fabula), v2.8.0 -> spreadsheet 2.8.4 (Vitrail), v2.5.1 ->
  spreadsheet 2.7.1 (Larets stereo).

Verified claims
- Canals moved biome -> spreadsheet in v2.6.1; biome.Canals removed; saved
  patches must be re-pointed. RELEASE-2.6.1.md lines 14, 116-118, 177-178.
- Larets true stereo as of v2.5.1. RELEASE-2.5.1.md lines 12, 86-121.
- Fabula new in v2.7.0 (only new unit that release); Vitrail new in v2.8.0.
- Colmatage attribution: Colmatage.cpp:4 -- "Algorithm lineage: Nick Collins'
  Colmatage via Livecut (Remy Muller, GPLv2)". README.md:417 credits Nick
  Collins / BBCut library (ICMC 2002).
- Mnemonics are set as `args.mnemonic` in each unit's own .lua, not in toc.lua
  or init.lua.

Stale README / release prose
- README.md:111 says Network is "32-tap stereo". Code: kMaxNetworkTaps = 64,
  activeTaps = round(density x 64). Documented as 64.
- README.md:190 says Network's glitch macro has "8 mutually-exclusive modes
  (G1-G8)". Network.h:50-56 defines 6 modes (NORMAL, MUTE, STUTTER, CRUSH,
  SCRUB, REVERSE) selected probabilistically, not exclusively. Documented from
  code.
- README.md:267 says Petrichor has a "20s int16 buffer". Default is 5 s
  (MultitapDelay.lua `self.bufferSeconds = 5.0`); 20 s is one of four menu
  options (2/5/10/20).
- BandListControl.lua:181 help text reads "Filter type (0=peak, 1=bpf,
  2=allpass)". Actual types are peak / lp / res (FTYPE_PEAK/LP/RESON). The help
  string is wrong on two of three entries.
- Fabula's lineage is given as Dattorro/Griesinger in RELEASE-2.7.0.md but
  Dattorro/Gardner in the Fabula.lua header comment. Followed the release notes.

Likely bug found
- Petrichor's Randomize target list has 21 entries and the Lua-side
  applyRandomize handles all 21, but the gate/fire path runs through C++
  MultitapDelay::applyRandomize, which clamps the target index to 16
  (MultitapDelay.cpp:477). Targets `stack`, `grid`, `count` and `reset` are
  therefore unreachable when fired.

Conflicting defaults in source (both recorded in the tables)
- Colmatage Repeats: main-view control initialBias = 8, expanded fader for the
  same parameter initialBias = 4, DSP/adapter default 4.
- Network Motion: view initialBias = 0.5, graph hardSet("Bias", 0.0), DSP
  default 0.0f.
- Pecto CombSize: Pecto.h default 0.01 s, Lua hardSet/initialBias 0.1 s. Lua
  wins at construction; 0.100 s documented.
- Filterbank mMix: C++ 1.0 vs Lua hardSet 0.5. Lua wins.
- MultibandSaturator mSkew: C++ 1.0 vs Lua 0.0, and 1.0 lies outside the
  control's -1..1 range. Lua wins.

Undocumented / not exposed
- Network's Seed and InputLevel DSP parameters are bound to ParameterAdapters
  but have no view control (Seed also has no CV branch), so the reflector field
  is fixed at the built-in seed.
- MultibandSaturator.h declares BandMute0..2 but no view control exposes them.

Derived rather than stated
- Fabula Predelay in ms: kPD = 16384 samples, targetPD = predelay * (kPD-1);
  ~341 ms is arithmetic at 48 kHz. The control itself is unitless 0-1.
- Vitrail cutoff Hz range (~30 Hz to ~5 kHz, saturating near k = 0.8) is derived
  from clockHz().
- Canals V/oct has no initialBias; recorded as `-`.

Could not verify
- On-hardware rendering of the custom graphics (BandListGraphic,
  FilterResponseGraphic, DelayInfoGraphic, SpectrumGraphic, the Colmatage
  mosaic, the Network sphere, the Fabula fabric waterfall, the Vitrail tunnel).
  All described from Lua/C++ intent and header comments only.
- Whether Tomograph scales added by "Rescan .scl files" extend the Scale
  selector's range before a view reload (biasMap is built from scaleCount at
  view-load time).

Additional findings (Impasto / Larets / Blanda)
- Impasto: each band's Attack (0.0001-0.1 s) and Release (0.001-1 s) plies are
  wired to BandAttack{i} / BandRelease{i}, but MultibandCompressor.cpp never
  reads mBandBias[b][3] or [4]. Attack and release come only from the Speed
  breakpoint table. The knobs are inert.
- Impasto: BandWeight0..2 have adapters, CV branches and serialization and set
  the crossover positions, but no ViewControl surfaces them.
- Impasto: MultibandCompressor.cpp's header comment still calls the unit
  "Presse" (stale internal name).
- Larets: README.md:228 claims a "momentary-hold loop length". Not in current
  source -- Loop Length is a plain GainBias 1-16 with a CV branch. Stale.
- Larets: C++ default mMix{"Mix", 0.5f} vs Lua hardSet 1.0. Lua wins;
  1.00 documented.
- Larets: the C++ parameter is registered as mCompressAmt{"TanhAmt", ...} while
  the Lua ties it as "CompressAmt". Ties resolve by string name, so this may
  silently fail to bind, which would leave the `comp` readout and its CV branch
  disconnected from the DSP. UNVERIFIED at runtime -- worth checking.
- Larets: xformFunc and xformDepth adapters have no addMonoBranch, so Transform
  func and depth are knob-only (no CV).
- Blanda: Lua dial maps cap Level/Weight/Output Level at 2.0 while Blanda.cpp
  clamps to 4.0. The reachable (Lua) range is documented.
- Blanda: process() reads parameters at block rate, not per sample.
- Blanda: README.md:202's "territory-based ghost occlusion" is a graphics-layer
  claim in the C++ graphic, not in Blanda.cpp; not verified, not documented.

Nothing was run on hardware or in the emulator. All ranges and defaults come
from assets/*.lua dial maps and hardSet calls, with behaviour read from the C++.
-->
