# MI (`mi`) — v1.0.4

Ports of Mutable Instruments modules to the ER-301. All DSP is based on code by
Émilie Gillet (MIT License); the ER-301 unit wrappers, UI and 48 kHz adaptations
are by stolmine. Nine units: three full macro-instruments (Plaits, Clouds,
Rings), three extracted sub-engines (Stratos, Commotio, Warps), and three
control-rate generators (Grids, Marbles T, Marbles X).

Package title in `toc.lua` is **MI**; every unit sits in the **MI** category.

---

## Plaits

<mnemonic: Pl> · Category: MI

The Plaits macro-oscillator with all 24 synthesis engines, from virtual analog
through wavetables, speech, physical models and drum voices. It has two
personalities selected from the unit menu: **osc**, a free-running oscillator
whose output is always on, and **trig**, where a gate fires the internal
decay/LPG envelope so the unit behaves like a self-contained voice. Pitch comes
from a V/oct input summed with a semitone offset, with C4 as 0 V.

**Engines** (fader index → name shown on the fader). The unit reorders the
firmware list so the original 16 engines come first and the v1.2 additions
follow:

| # | Label | Engine | | # | Label | Engine |
|---|---|---|---|---|---|---|
| 0 | `VA` | Virtual analog | | 12 | `Modal` | Modal resonator |
| 1 | `WvShp` | Waveshaping | | 13 | `Kick` | Bass drum |
| 2 | `2opFM` | 2-operator FM | | 14 | `Snare` | Snare drum |
| 3 | `Formt` | Formant / grain | | 15 | `HiHat` | Hi-hat |
| 4 | `Harm` | Harmonic / additive | | 16 | `VA+Flt` | Virtual analog + VCF |
| 5 | `WvTbl` | Wavetable | | 17 | `PhsDst` | Phase distortion |
| 6 | `Chord` | Chord | | 18 | `6opFM1` | 6-op FM (bank 1) |
| 7 | `Speech` | Speech synthesis | | 19 | `6opFM2` | 6-op FM (bank 2) |
| 8 | `Swarm` | Swarm / unison | | 20 | `6opFM3` | 6-op FM (bank 3) |
| 9 | `Noise` | Filtered noise | | 21 | `WvTrrn` | Wave terrain |
| 10 | `Partcl` | Particle noise | | 22 | `StrMch` | String machine |
| 11 | `String` | Plucked string | | 23 | `Chip` | Chiptune |

**Controls** — osc view (default), left to right

| Control | Type | Range / Options | Default | What it does |
|---|---|---|---|---|
| V/oct | Pitch | — | 0 | 1 V/oct pitch input, C4 at 0 V |
| Fundamental | GainBias | -48 … 48 (semitones) | 0 | Coarse/fine pitch offset added to V/oct |
| Engine | EngineSelector | 0 … 23, integer | 0 (`VA`) | Picks the synthesis engine; fader shows the engine name |
| Harmonics | GainBias | 0 … 1 | 0.5 | Per-engine harmonic content (detune, ratio, chord, bank…) |
| Timbre | GainBias | 0 … 1 | 0.5 | Primary per-engine timbre axis |
| Morph | GainBias | 0 … 1 | 0.5 | Secondary per-engine morph axis |

**Controls** — trig view (menu: "trig (enveloped)")

Same as above, preceded by **Gate** and followed by two more:

| Control | Type | Range / Options | Default | What it does |
|---|---|---|---|---|
| Gate | Gate | — | — | Trigger/gate input; fires the internal envelope + LPG |
| Decay | GainBias | 0 … 1 | 0.5 | Length of the internal decay envelope |
| LPG Colour | GainBias | 0 … 1 | 0.5 | Low-pass gate character, from VCA-like to filtered/pingy |

**Menu**

- *Mode:* `osc (free-running)` / `trig (enveloped)` — switches the view and the
  internal `Trig Mode` option. The choice is serialised with the preset.
- *Output:* mono chains → `main` / `aux`; stereo chains → `main+aux` (default),
  `aux+aux`, `main+main`.

**I/O** — Mono or stereo. The chain input is deliberately sunk (gain 0) so
upstream signal never bleeds through. Out1 carries the main output, Out2 the aux
output in a stereo chain (subject to the Output menu). Modulation inlets for FM,
Timbre, Morph, Harmonics and the FM/Timbre/Morph CV *amount* parameters exist in
the DSP object and are exposed as branches, but have no on-screen control in
this release.

---

## Clouds

<mnemonic: Cl> · Category: MI

The Clouds granular texture processor, running its granular, looping-delay and
spectral playback modes. Audio is continuously recorded into the buffer; Freeze
holds the buffer so you can play the frozen material, and the trigger input
fires individual grains. The internal reverb is disabled — use **Stratos** after
it if you want the Clouds reverb.

**Controls** (expanded view order)

| Control | Type | Range / Options | Default | What it does |
|---|---|---|---|---|
| Mode | ModeSelector | 0 … 2, integer: `Gran`, `Delay`, `Spect` | 0 (`Gran`) | Playback mode: granular, looping delay, spectral |
| Trigger | Gate | — | — | Fires a single grain |
| Freeze | Gate (toggle) | — | off | Stops recording; the buffer becomes fixed material |
| Position | GainBias | 0 … 1 | 0.5 | Read position in the buffer |
| Size | GainBias | 0 … 1 | 0.5 | Grain size |
| Density | GainBias | -1 … 1 | 0.0 | Grain rate; negative side is the deterministic/rhythmic half, positive the stochastic half (mapped to 0…1 internally) |
| Texture | GainBias | 0 … 1 | 0.5 | Grain window shape, from smooth to rectangular |
| Pitch | GainBias | -48 … 48 (semitones) | 0.0 | Transposition of grain playback |
| Dry/Wet | GainBias | 0 … 1 | 0.5 | Blend of input and processed signal |
| Feedback | GainBias | 0 … 1 | 0.0 | Feeds output back into the buffer |
| Spread | GainBias | 0 … 1 | 0.5 | Stereo spread of grains |

**Menu**

- *Quality:* `normal` / `hifi` — `normal` records 16-bit stereo, `hifi` records
  16-bit mono (which doubles the available buffer time). See verification notes.
- *Preamp:* `unity` / `x2` / `x3` — input gain of ×1, ×2, ×3.

**I/O** — Stereo-aware. In a stereo chain In1/In2 feed the left/right buffer
inputs and Out1/Out2 carry the stereo output; in a mono chain In1 feeds both
sides and only Out1 is connected. Trigger and Freeze are branch inputs (Freeze
is a toggle comparator, so a gate latches it). Spectral mode is forced to mono
internally regardless of the Quality setting.

---

## Rings

<mnemonic: Rn> · Category: MI

The Rings resonator. Feed it audio as an exciter, or leave the internal exciter
on and strum it. Six resonator models cover modal bars, sympathetic strings,
plucked strings, an FM voice and a reverberated string. Base pitch is MIDI 48
(C3) plus the Freq offset plus V/oct. The "String Synth" easter egg replaces the
resonator with the four-voice string ensemble.

**Models** (fader label → model)

| # | Label | Model |
|---|---|---|
| 0 | `Modal` | Modal resonator |
| 1 | `SympSt` | Sympathetic strings |
| 2 | `String` | Plucked / inharmonic string |
| 3 | `FM` | FM voice |
| 4 | `SympQ` | Quantised sympathetic strings |
| 5 | `Str+Rv` | String with reverb |

**Controls**

| Control | Type | Range / Options | Default | What it does |
|---|---|---|---|---|
| Strum | Gate | — | — | Rising edge excites the resonator |
| V/oct | Pitch | — | 0 | 1 V/oct pitch input |
| Freq | GainBias | -48 … 48 semitones | 0 | Pitch offset from the C3 base note |
| Model | EngineSelector | 0 … 5, integer | 0 (`Modal`) | Resonator model; fader shows the model name |
| Structure | GainBias | 0 … 1 (clamped 0…0.9995) | 0.5 | Harmonic structure / inharmonicity |
| Brightness | GainBias | 0 … 1 (clamped 0…0.9995) | 0.5 | Exciter brightness and resonator tone |
| Damping | GainBias | 0 … 1 (clamped 0…0.9995) | 0.5 | Decay time of the resonance |
| Position | GainBias | 0 … 1 (clamped 0…0.9995) | 0.5 | Excitation point along the resonator |
| Out Mix | MixControl | -1 … 1 | 0.0 | Crossfade between the odd (main) and even (aux) outputs; the fader label changes with the mono/stereo state |

The **Out Mix** label track: mono chain shows `Main`, `Main>`, `Equal`, `<Aux`,
`Aux`; stereo chain shows `M:L`, `M>L`, `Equal`, `A>L`, `A:L` (in stereo the
control swaps which output lands on which side rather than summing).

**Menu** — all four settings are serialised with the preset

- *Polyphony:* `1 voice` / `2 voices` / `4 voices` (default 1); the header shows
  the current setting.
- *Resolution:* `low (16)` / `medium (32)` / `full (60)` partials — default
  `medium (32)`. Lower resolution costs less CPU.
- *Exciter:* toggles `internal` / `external` (default internal). External means
  the chain input is the only excitation.
- *String Synth:* toggles the easter-egg string ensemble on/off (default off).

**I/O** — In1 is the exciter input. Out1 is the main resonator output; in a
stereo chain Out2 carries the aux output and the unit's internal `Stereo` option
is set, which changes how Out Mix behaves (see above).

---

## Grids

<mnemonic: Gr> · Category: MI

Topographic drum pattern generator. Clock it from the chain input and it walks a
32-step pattern read out of the Grids drum map; **Map X** and **Map Y** move
continuously across a 5×5 grid of interpolated patterns, and **Density** sets
how many of the map's hits pass the threshold. The unit emits one gate stream
for the selected instrument channel.

**Controls**

| Control | Type | Range / Options | Default | What it does |
|---|---|---|---|---|
| Grids | GridsCircle | — | — | Two-ply circular step display; follows the running pattern |
| Map X | GainBias / readout | 0 … 1 | 0.5 | Horizontal position in the drum map |
| Map Y | GainBias / readout | 0 … 1 | 0.5 | Vertical position in the drum map |
| Density | GainBias / readout | 0 … 1 | 0.5 | Fill amount — threshold applied to the map's step levels |
| Channel | GainBias | 0 … 2, integer | 0 | Instrument channel: 0 = BD, 1 = SD, 2 = HH |
| Reset | Gate | — | — | Rising edge returns to step 0 |
| Chaos | GainBias | 0 … 1 | 0.0 | Random perturbation added to step levels, re-rolled at each pattern start |
| Width | GainBias | 0 … 1 | 0.5 | Output gate length as a fraction of the measured clock period |

**Sub-display / expanded** — Pressing the circle focuses it and opens a
sub-display with three readouts and sub-buttons `x`, `y`, `fill`, editing Map X,
Map Y and Density directly (0–1, two decimals). Pressing a sub-button a second
time while focused opens the decimal keyboard for that value.

**Menu** — none (`onShowMenu` returns an empty menu).

**I/O** — The chain input is the clock, filtered through a gate-mode comparator
so noisy G-jack signals and hot-unplugging don't false-trigger. Reset is a
separate branch input. The output is a unipolar gate (0 or 1) copied to every
output channel of the chain. Before a second clock edge has been seen the gate
length falls back to 48 samples (1 ms).

---

## Warps

<mnemonic: Wp> · Category: MI

The Warps meta-modulator. The chain input is the carrier and a branch supplies
the modulator; the **Algorithm** fader crossfades continuously through six
cross-modulation algorithms rather than stepping between them, so the in-between
positions are usable. The "Freq Shifter" easter egg replaces the whole
algorithm chain with the frequency shifter.

**Algorithms** — the fader spans 0 … 0.625, with the six named algorithms landing
on 0, 0.125, 0.25, 0.375, 0.5 and 0.625:

| Position | Label | Algorithm |
|---|---|---|
| 0.000 | `XFade` | Crossfade |
| 0.125 | `Fold` | Wavefolder |
| 0.250 | `AnaRM` | Analogue ring modulator |
| 0.375 | `DigRM` | Digital ring modulator |
| 0.500 | `XOR` | Bitwise XOR |
| 0.625 | `Compar` | Comparator |

The vocoder region above 0.7 in the original module is deliberately excluded
pending a 96 kHz sample-rate conversion.

**Controls**

| Control | Type | Range / Options | Default | What it does |
|---|---|---|---|---|
| mod | BranchMeter | — | 1.0 gain | Modulator sub-chain and its level (clamped at -59.9 dB); part of the unit's mute group |
| Algorithm | AlgoSelector | 0 … 0.625 | 0 (`XFade`) | Crossfades through the six algorithms; fader shows the nearest algorithm name |
| Timbre | GainBias | 0 … 1 | 0.5 | Per-algorithm modulation parameter |
| Drive | GainBias | 0 … 1 | 0.5 | Input drive applied to both channels, with the module's gain compensation |

**Menu**

- *Freq Shifter:* `enable` / `disable` — the easter egg frequency shifter,
  default off. Serialised with the preset.

**I/O** — In1 is the carrier (via a unity ConstantGain clamped at -59.9 dB); the
modulator arrives through the `mod` branch. Out1 is the main output, Out2 the
aux output in a stereo chain. Internally Warps renders in 96-sample blocks
through a FIFO, so there is a small fixed latency.

---

## Stratos

<mnemonic: St> · Category: MI

The reverb from Clouds, extracted as a standalone stereo effect. It is a dense
diffusion reverb with an adjustable input gain stage, useful both as a plain
reverb and — with Time near 1.0 — as a smearing ambience. The unit defaults to a
long, fairly diffuse, gently damped setting rather than a neutral one.

**Controls**

| Control | Type | Range / Options | Default | What it does |
|---|---|---|---|---|
| Amount | GainBias | 0 … 1 | 0.54 | Wet amount / reverb level |
| Time | GainBias | 0 … 1 | 0.98 | Reverb decay time |
| Diffusion | GainBias | 0 … 1 | 0.7 | Density of the diffusion network |
| Damping | GainBias | 0 … 1 | 0.6 | Low-pass damping inside the tank |
| Input Gain | GainBias | 0 … 1 | 0.2 | Level into the reverb; drive it up for saturation |

**Menu** — none.

**I/O** — Stereo-aware. Stereo chains map In1/In2 → Out1/Out2; mono chains feed
In1 to both reverb inputs and connect only Out1. No trigger or freeze inputs.

---

## Commotio

<mnemonic: Co> · Category: MI

The exciter section of Elements, running at the ER-301's native 48 kHz, as a
standalone gate-driven noise/texture source. It layers three excitation types —
a bowed friction model, a blown/breath model, and a struck/particle model — each
with its own level and timbre, shaped by a common envelope. Pair it with Rings
(set to external exciter) to rebuild an Elements-style voice.

**Controls**

| Control | Type | Range / Options | Default | What it does |
|---|---|---|---|---|
| Gate | Gate | — | — | Gate input; opens the envelope and drives the exciters |
| Bow Level | GainBias | 0 … 1 | 0.0 | Amount of bowed friction excitation |
| Bow Timbre | GainBias | 0 … 1 | 0.5 | Bow character / friction curve |
| Blow Level | GainBias | 0 … 1 | 0.0 | Amount of breath/blown excitation |
| Blow Timbre | GainBias | 0 … 1 | 0.5 | Blown noise colour |
| Blow Meta | GainBias | 0 … 1 | 0.5 | Blown model morph (noise ↔ pitched flow) |
| Strike Level | GainBias | 0 … 1 | 0.5 | Amount of struck/particle excitation |
| Strike Timbre | GainBias | 0 … 1 | 0.5 | Mallet hardness / strike colour |
| Strike Meta | GainBias | 0 … 1 | 0.5 | Strike model morph across the mallet types |
| Envelope | GainBias | 0 … 1 | 0.5 | Shape of the multistage excitation envelope |
| Damping | GainBias | 0 … 1 | 0.5 | Resonator damping value fed to the exciters |
| Brightness | GainBias | 0 … 1 | 0.5 | Resonator brightness value fed to the exciters |

**Menu** — none.

**I/O** — Mono output on Out1 only. Gate is a branch input. The DSP object also
declares an audio `In` inlet, but the unit does not connect the chain input to
it, so Commotio is a pure source.

---

## Marbles T

<mnemonic: Mt> · Category: MI

The T (timing) half of Marbles: a probabilistic gate generator locked to an
external clock. Seven generative models decide how the incoming clock is turned
into gates; **Deja Vu** and **Length** control the loop-and-mutate memory that
turns random streams into repeating-but-drifting patterns.

**Models** (menu option `Model`, index order)

`Bernoulli`, `Clusters`, `Drums`, `Independent`, `Divider`, `Three States`,
`Markov`.

**Controls**

| Control | Type | Range / Options | Default | What it does |
|---|---|---|---|---|
| Clock | Gate | — | — | External clock input; the generator is silent until the first edge |
| Reset | Gate | — | — | Rising edge resets the generator |
| Jitter | GainBias | 0 … 1 | 0.0 | Randomises gate timing around the clock |
| Deja Vu | GainBias | 0 … 1 | 0.0 | 0 = fresh randomness, 0.5 = loop the stored sequence, 1 = new randomness again; the region around 0.5 loops with mutation |
| Length | GainBias | 1 … 16, integer | 8 | Length of the Deja Vu loop in steps |
| Output | GainBias | 0 … 1 | 0.5 | Crossfades the single output between the T1 and T2 gate streams (also sets the generator's bias between them) |

**Menu**

- *Model:* the seven models listed above.

**I/O** — Clock and Reset are branch inputs (Reset triggers on rise). The gate
output is copied to every output channel of the chain. Output stays silent until
a clock edge has been received, so inserting the unit does not pop.

---

## Marbles X

<mnemonic: Mx> · Category: MI

The X (voltage) half of Marbles: a clocked random CV generator built on a beta
distribution. **Spread** and **Bias** shape the distribution the voltages are
drawn from, **Steps** quantises the result from smooth to hard-stepped, and
Deja Vu/Length loop the sequence. One of the three X outputs (or a blend of two)
is sent to the unit's output.

**Control modes** (menu option `Control Mode`, index order)

`Identical`, `Bump`, `Tilt` — how the three X channels' distributions relate to
each other.

**Controls**

| Control | Type | Range / Options | Default | What it does |
|---|---|---|---|---|
| Clock | Gate | — | — | External clock input |
| Reset | Gate | — | — | Rising edge resets the generator |
| Spread | GainBias | 0 … 1 | 0.5 | Width of the random distribution, from a single value to fully uniform |
| Bias | GainBias | 0 … 1 | 0.5 | Centre of the distribution |
| Steps | GainBias | 0 … 1 | 0.5 | Quantisation, from smooth/continuous to a small number of discrete levels |
| Deja Vu | GainBias | 0 … 1 | 0.0 | Loop-and-mutate amount, as on Marbles T |
| Length | GainBias | 1 … 16, integer | 8 | Length of the Deja Vu loop |
| Output | GainBias | 0 … 1 | 0.0 | Selects/blends the output: 0 = X1, 0.5 = X2, 1 = X3 |

**Menu**

- *Control Mode:* `Identical` / `Bump` / `Tilt`.

**I/O** — Clock and Reset are branch inputs. The CV output is copied to every
output channel of the chain. The generator's ±5 V range is scaled by 0.2 to the
ER-301's ±1 convention, i.e. the unit outputs roughly ±1. Voltage range is fixed
to "full" and the internal scale/quantiser is fixed to scale 0; the Y output and
the T-side ratio controls are not exposed.

<!-- VERIFICATION NOTES

Sources read: mods/mi/assets/*.lua (all 16 files) and the C++ wrappers in
mods/mi/ (PlaitsVoice, Clouds, RingsVoice, Grids, WarpsModulator, Stratos,
Commotio, MarblesT, MarblesX .h/.cpp), plus eurorack/plaits/dsp/voice.cc and
eurorack/clouds/dsp/granular_processor.h for engine order and quality semantics.

Discrepancies / open items:

1. README.md (lines 41-51) still describes plaits, clouds, stratos, rings,
   warps, grids, commotio and marbles as SEPARATE packages. In v2.8.1 they all
   ship inside the single `mi` package (mods/mi/mod.mk, PKGVERSION 1.0.4).
   The top-level directories mods/plaits, mods/rings, mods/warps and
   mods/commotio exist but are EMPTY — all sources live under mods/mi/.

2. Grids view wiring looks like a bug. Grids.lua returns
   `expanded = {"circle"}`, `collapsed = {"circle"}` and a third view named
   `circle` containing circle/channel/reset/mapx/mapy/density/chaos/width — but
   nothing ever calls `switchView("circle")`, so the `circle` view is
   unreachable and Channel, Reset, Chaos and Width have no on-screen control in
   the default expanded view. Map X / Map Y / Density remain editable through
   the circle's sub-display. Documented above as authored; could not run the
   firmware to confirm (er-301/ submodule is not checked out here).

3. Clouds "Quality" menu labels are misleading. `set_quality(0)` = 16-bit
   STEREO, `set_quality(1)` = 16-bit MONO (double buffer time); low-fidelity
   (8-bit mu-law) is never selected. The Lua labels these `normal` (0) and
   `hifi` (1) — so "hifi" is actually the mono/longer-buffer setting, not a
   higher-fidelity one. Clouds.h's comment on mQuality says "0=16bit stereo,
   1=16bit mono", which matches the code but not the labels.

4. Clouds.h comments are stale in two more places: mMode is commented
   "0=granular, 1=stretch, 2=delay, 3=spectral" but Clouds.cpp maps 0=granular,
   1=looping delay, 2=spectral (3 modes, matching the Lua Gran/Delay/Spect);
   mPreamp is commented "0=unity, 1=x2, 2=x4" but the gain table in Clouds.cpp
   is {1, 2, 3} and the Lua labels are unity/x2/x3.

5. Clouds Density: onLoadGraph does `density:hardSet("Bias", 0.5)` while the
   view control declares `initialBias = 0.0` on a [-1,1] map. The view's
   initialBias is what a freshly-inserted unit shows; 0.0 on the UI maps to 0.5
   internally, so the two agree in effect. Documented default is 0.0 (UI).

6. Rings.lua creates the `mix` ParameterAdapter and its branch TWICE
   (lines ~63-67 and ~104-108 of onLoadGraph) — a duplicate addObject/
   addMonoBranch of the same name. Harmless in effect but redundant.

7. MarblesT.lua defines a local `modelNames` table that is never referenced;
   the menu uses an inline `choices` list with the same seven names.

8. Plaits: `FM Amount`, `Timbre CV`, `Morph CV` parameters and the `fm`,
   `timbreMod`, `morphMod`, `harmonicsMod` inlet branches are created but have
   no view control in any view, so they are not reachable from the UI.

9. Commotio's DSP object declares an `In` inlet that the Lua never connects.

10. Warps: algorithm names come from the Lua `algoNames`/`getAlgoName` mapping
    (index = round(value * 5 / 0.625)); the C++ only clamps modulation_algorithm
    to 0…0.625. The exact algorithm identities above are inferred from the
    Warps firmware's algorithm order and the Lua labels.

11. Could not verify on-hardware behaviour of anything here; the er-301
    submodule directory is empty in this checkout, so ViewControl base-class
    semantics (default view selection, Gate/Pitch rendering) were taken from
    the unit code's own usage.
-->
