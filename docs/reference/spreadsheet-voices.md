# Spreadsheet: Sources, Voices & Sequencers (`spreadsheet`), v2.8.5.1

The generative half of the `spreadsheet` package: three pattern/shape sequencers
(Excel, Ballot, Etcher), two synthesis sources (Rauschen, Helicase), and four
voices (Ngoma, JF, Visadhara, Mirror). Everything here is an original design by
stolmine; the clean-room voices are noted per unit, and JF/Visadhara's 4-lane
NEON voice pattern is adapted from tomf's `polygon` (`er-301-custom-units`) with
his permission.

---

## Excel

*mnemonic: Ex* · Category: Spreadsheet

A 64-step CV tracker. Each step carries an offset (the voltage it outputs), a
length in clock ticks, and a deviation (per-step random jitter). It is a
step-value sequencer rather than a pitch sequencer. Nothing is quantized, so it
drives V/Oct, filter cutoff, or anything else equally well. A Transform ply
applies arithmetic to the whole pattern on a gate, which is what separates it
from a plain step editor: you can add, multiply, rotate, invert or randomize the
sequence while it plays.

**Controls**

| Control | Type | Range / Options | Default | What it does |
|---|---|---|---|---|
| `Steps` | step-list display + editor | 64 steps | n/a | The pattern. Turn to scroll the step cursor; press a sub button to edit that step's field. |
| `Sequence` | info display | n/a | n/a | Overview of length / loop / transform scope. Enter expands it. |
| `Clock` | Gate | n/a | n/a | Advances the sequence on each rising edge. |
| `Reset` | Gate | n/a | n/a | Rising edge jumps to step 1 and clears the tick counter. Cuts nothing else. |
| `Slew Time` | GainBias | 0 … 10 s (dial); clamped 0 … 1000 s in DSP | 0.0 s | One-pole glide on the output. Below 1 ms it is bypassed entirely. |
| `Transform` | custom Gate + params | n/a | n/a | Fires the transform on a rising edge. Shift toggles the sub-display between gate and parameter pages. |
| `Seq Length` | GainBias (expansion) | 1 … 64 | 16 | Number of active steps. |
| `Loop Length` | GainBias (expansion) | 0 … 64 (clamped to Seq Length) | 0 | 0 = off. Otherwise confines play to an aligned window of this size. |
| `Xform Scope` | ModeSelector (expansion) | `ofst`, `len`, `dev`, `all` | `ofst` | Which per-step field the transform rewrites. |

**Step fields** (edited on the `Steps` ply sub-display)

| Field | Range | Default | What it does |
|---|---|---|---|
| `offset` | −5 … +5 (10 Vpp mode) or −1 … +1 (2 Vpp mode) | 0.0 | The step's output value. One unit = 1 V. Hand-entered values are not clamped in DSP; the range bound is only applied during transforms. |
| `length` | 1 … 16 on the dial; DSP enforces ≥ 1 with no upper bound | 1 | How many clock ticks the step holds before advancing. |
| `dev` | 0 … 1 | 0.0 | Per-step random jitter. Re-rolled once when the step is entered and held for its duration; adds a bipolar random offset of up to ±dev volts. |

**Transform functions**: selected on the Transform ply's `func` readout.

| Index | Name | Effect (factor is an integer ≥ 1) |
|---|---|---|
| 0 | `add` | Adds factor to every step in scope. |
| 1 | `sub` | Subtracts factor. |
| 2 | `mul` | Multiplies by factor. |
| 3 | `div` | Divides by factor (integer division on lengths). |
| 4 | `mod` | Modulo factor. Factor 1 zeroes all float fields. |
| 5 | `rev` | Reverses the active steps. Factor ignored. |
| 6 | `rot` | Rotates right by factor steps. |
| 7 | `inv` | Negates. On lengths this collapses everything to 1. |
| 8 | `rnd` | Randomizes: bipolar random × factor (lengths become 1…factor). |

Offsets are clamped to the current offset range after a transform, deviations to
0…1, lengths to ≥ 1.

**Sub-display / expanded**: The `Steps` ply's sub-display shows `offset`,
`length`, `dev` with a "Step N" title. With no readout focused, turning the
encoder scrolls the step cursor; with one focused, shift + encoder scrolls while
keeping focus. The `Sequence` ply's sub-display carries `length`, `loop`,
`scope`. Enter on `Sequence` opens the expansion with `Seq Length`,
`Loop Length` and `Xform Scope` as full faders. The `Transform` ply has two
sub-display pages toggled by tapping shift: the gate page (`input`, `thresh`,
`fire`) and the parameter page (`func`, `factor`, `fire!`).

**Menu**: *Set All Step Lengths*: `1 tick`, `2 ticks`, `4 ticks`. *Offset
Range*: `10Vpp (-5 to +5)`, `2Vpp (-1 to +1)`; switching rescales all stored
offsets proportionally. *Offsets*: `Randomize all offsets`, `Clear all offsets`.
*Snapshot*: `Save snapshot`, `Restore snapshot`. The snapshot is one-shot:
restoring consumes it, and it is not saved with the preset.

**I/O**: Pure generator; the chain input is ignored. One CV output, duplicated
to both channels on a stereo chain. Mono gate branches: `clock`, `reset`,
`slew`, `seqLength`, `loopLength`, `xform`, `xformScope`. No V/Oct input.

---

## Ballot

*mnemonic: Bl* · Category: Spreadsheet

A 64-step gate sequencer with a chaselight display. Each step has an on/off
gate, a length in ticks, and a velocity that scales the gate's output amplitude.
The Ratchet ply subdivides a step into up to 8 retriggers on demand, and the
Transform ply applies pattern algorithms (Euclidean, Nord-rack tables, a Grids
approximation, necklaces, inversion, rotation and density) to the running
sequence.

**Controls**

| Control | Type | Range / Options | Default | What it does |
|---|---|---|---|---|
| `Steps` | chaselight display + editor | 64 steps | n/a | The pattern. Turn to scroll; sub buttons edit the focused step. |
| `Sequence` | info display | n/a | n/a | Length / loop / gate width overview. Enter expands. |
| `Clock` | Gate | n/a | n/a | Advances the sequence and measures the clock period (used for gate length and ratchet spacing). |
| `Reset` | Gate | n/a | n/a | Jumps to step 1, clears the tick counter, and cuts any sounding gate. |
| `Ratchet` | custom Gate + params | n/a | n/a | Held high at a step's gate-fire moment, that step retriggers. Shift toggles gate/parameter sub-pages. |
| `Transform` | custom Gate + params | n/a | n/a | Fires the pattern transform on a rising edge. |
| `Seq Length` | GainBias (expansion) | 1 … 64 | 16 | Number of active steps. |
| `Loop Length` | GainBias (expansion) | 0 … 64 (clamped to Seq Length) | 0 | 0 = off; otherwise loops an aligned window. |
| `Gate Width` | GainBias (expansion) | 0 … 1 (dial); DSP clamps 0.01 … 1.0 | 0.5 | Gate duration as a fraction of the step's total time. |

**Step fields** (edited on the `Steps` ply sub-display)

| Field | Range | Default | What it does |
|---|---|---|---|
| `on/off` | ON / OFF | OFF | Whether the step fires. |
| `length` | 1 … 16 on the dial; DSP enforces ≥ 1 | 1 | Ticks the step holds. Also scales gate duration. |
| `vel` | 0 … 1 | 1.0 | Gate amplitude. Velocity 0 produces a silent gate; 1.0 gives a full-level gate. |

**Ratchet parameters** (Ratchet ply parameter page)

| Field | Range | Default | What it does |
|---|---|---|---|
| `mult` | 1 … 8 | 1 | Sub-gates per step. Must be > 1 for ratcheting to happen at all. Spacing is the measured clock period ÷ mult. |
| `len` | `len:off` / `len:ON` | off | On: each sub-gate is shortened to gate length ÷ mult so they don't overlap. Off: every sub-gate keeps the full gate length. |
| `vel` | `vel:off` / `vel:ON` | off | On: sub-gates decay in level across the ratchet. Off: all sub-gates use the step velocity. |

**Transform functions**: selected on the Transform ply's `func` readout. `prm A`
and `prm B` are the two parameters.

| Index | Name | Effect on gates | `prm A` | `prm B` |
|---|---|---|---|---|
| 0 | `euc` | Euclidean distribution of hits across the active steps. | Hits (0 … steps) | Rotation (0 … steps−1) |
| 1 | `nr` | 16-bit table pattern, repeating every 16 steps. | Table index (0 … 31) | Mask variant (0 … 3) |
| 2 | `grd` | Grids-style deterministic hash pattern, roughly 50% density. | X (0 … 255) | Y (0 … 255) |
| 3 | `nkl` | Necklace: Euclidean with rotation derived from the index. | Density | Index → rotation |
| 4 | `inv` | Inverts every gate. | n/a | n/a |
| 5 | `rot` | Rotates the pattern right. | Rotation amount | n/a |
| 6 | `den` | Per-step coin flip at the given density. | Density percent (0 … 100) | n/a |

Under the `len` and `vel` scopes only `rot`, `inv` and (for velocity) `den` do
anything; the pattern generators are gate-only.

**Sub-display / expanded**: `Steps` sub-display: `on/off`, `length`, `vel`,
titled "Step N". `Sequence` sub-display: `length`, `loop`, `width`; Enter opens
the expansion with those three as full faders. `Ratchet` and `Transform` each
have a gate page (`input`, `thresh`, `fire`) and a parameter page (`mult` /
`len` / `vel` for Ratchet, `func` / `prm A` / `prm B` for Transform), toggled by
tapping shift.

**Menu**: *Set All Gate Lengths*: `1 tick`, `2 ticks`, `4 ticks`. *Set All
Velocities*: `25%`, `50%`, `100%`. *Randomize*: `Randomize gates`, `Randomize
lengths`, `Randomize velocities`. *Clear / Reset*: `Clear all gates`, `Reset
lengths to 1`, `Reset velocities to 100%`. *Snapshot*: `Save snapshot`,
`Restore snapshot` (one-shot, not preset-saved).

**I/O**: Pure generator. One gate output, duplicated to both channels on a
stereo chain. Mono branches: `clock`, `reset`, `ratchet`, `xform`, `xformScope`,
`seqLength`, `loopLength`, `gateWidth`. Gate high level is the step velocity, so
patch it into a VCA or an envelope's trigger as an accented gate.

---

## Etcher

*mnemonic: Et* · Category: Spreadsheet

A CV-addressed piecewise transfer function. The incoming signal scans left to
right across a chain of up to 32 segments; each segment has an offset (its
output value), an interpolation curve, and a weight that sets how much of the
input span it occupies. Use it as a waveshaper, a CV mapper, a quantizer
(staircase preset), or, with a ramp on the input, as a scanning envelope
generator. Eight depth-controlled transforms rewrite the shape on a gate.

**Controls**

| Control | Type | Range / Options | Default | What it does |
|---|---|---|---|---|
| `Input` | GainBias | −1 … +1 | 0.0 | The scan position. Clamped to ±1 per sample and remapped to 0…1 across the curve. |
| `Segments` | segment-list display + editor | up to 32 | n/a | The breakpoint list. Turn to scroll; sub buttons edit the focused segment. |
| (curve display) | 2-ply transfer-curve graphic | n/a | n/a | Draws the current transfer function with the live input/output dot. Enter expands. |
| `Skew` | GainBias | −1 … +1 | 0.0 | Bunches the interior boundaries. Positive bunches them low, negative high; symmetric, strongest mid-range. |
| `Transform` | custom Gate + params | n/a | n/a | Fires the shape transform on a rising edge. |
| `Level` | GainBias | −1 … +1 | 1.0 | Output scaling, applied before deviation. Negative inverts. Not clamped itself; the final output is clamped to ±1. |
| `Deviation` | GainBias (expansion) | 0 … 1 | 0.0 | Per-segment randomization, re-rolled on each boundary crossing and held while the input stays in that segment. |
| `Dev Scope` | ModeSelector (expansion) | `ofst`, `crv`, `wgt`, `all` | `ofst` | What deviation randomizes. |
| `Segments` | GainBias (expansion) | 4 … 32 on the dial; DSP clamps 2 … 32 | 16 | Number of active segments. |

**Segment fields** (edited on the `Segments` ply sub-display)

| Field | Range | Default | What it does |
|---|---|---|---|
| `offset` | −1 … +1 | 0.0 | The segment's value. |
| `curve` | 0 = step, 1 = linear, 2 = cubic | 1 (linear) | How it interpolates toward the next segment's offset. Step holds flat (staircase/quantizer); cubic is Catmull-Rom through the neighbours. The last segment always holds flat regardless. |
| `weight` | 0.1 … 4.0 | 1.0 | Relative width. A segment's share of the input span is its weight over the sum of all active weights. |

**Transform functions**: `depth` (0 … 1, default 0.5) is a blend amount toward
the transformed shape unless noted.

| Index | Name | Effect |
|---|---|---|
| 0 | `rnd` | Blends offsets toward random values and weights toward random widths. |
| 1 | `rot` | Rotates offset/curve/weight left. Depth selects the rotation *distance*, not strength; the rotation is always full-strength. |
| 2 | `inv` | Blends offsets toward their negatives. Depth 0.5 collapses the shape flat. |
| 3 | `rev` | Mirrors offsets and weights end-for-end. Curves only swap above depth 0.99. |
| 4 | `smo` | Smooths offsets with a 3-point wrap-around average. |
| 5 | `qnt` | Quantizes offsets to 2 … 16 levels. Depth sets both the level count and the blend. |
| 6 | `sprd` | Normalizes the offset range out to the full −1 … +1. |
| 7 | `fold` | Reflect-folds offsets back into ±1 after a depth-dependent pre-gain. |

**Deviation scopes**: `ofst` adds a bipolar random offset after Level; `crv`
randomly overrides the segment's curve type; `wgt` shifts the segment's end
boundary, stretching or compressing its span (and so its local slope); `all`
does all three.

**Sub-display / expanded**: `Segments` sub-display: `offset`, `curve`,
`weight`, titled "Seg N". The curve ply's sub-display carries `dev`, `scope`,
`segs`; Enter on it opens the expansion with `Deviation`, `Dev Scope` and
`Segments` as full faders. `Transform` has the usual gate page (`input`,
`thresh`, `fire`) and parameter page (`func`, `depth`, `fire!`).

**Menu**: *Presets*: `Linear ramp`, `S-curve`, `Staircase (quantizer)`,
`Random`. *Clear*: `Reset all segments`. Etcher has **no snapshot**: transforms
are destructive with no undo.

**I/O**: The scan position comes from the `Input` ply's branch, not the chain
input; patch your CV there. One output, duplicated to both channels on a stereo
chain. Mono branches: `input`, `skew`, `level`, `segCount`, `deviation`,
`devScope`, `xform`.

---

## Rauschen

*mnemonic: Rn* · Category: Spreadsheet

A parametric noise and chaos generator. Twelve algorithms span ordinary
white/pink noise, sparse impulse textures, chaotic maps and attractors, Xenakis
stochastic synthesis, and a cellular-automaton wavetable voice. Two macro
knobs, Param X and Param Y, mean something different in every algorithm, so
the unit is really twelve small generators behind one control surface. A morphing
state-variable filter sits after the generator, and a rotating 3-D phase-space
plot shows the signal's structure.

**Controls**

| Control | Type | Range / Options | Default | What it does |
|---|---|---|---|---|
| `Algorithm` | ModeSelector | 12 entries, see below | `White` | Picks the generator. Steps one entry per turn regardless of speed. |
| (viz) | phase-space display | n/a | n/a | Read-only 3-D delay embedding of the output, slowly rotating with phosphor persistence. Resets when the algorithm changes. |
| `Param X` | GainBias | 0 … 1 | 0.5 | First macro. Per-algorithm meaning; see the table. |
| `Param Y` | GainBias | 0 … 1 | 0.5 | Second macro. Per-algorithm meaning. |
| `Filter Cutoff` | custom GainBias | 20 Hz … 20 kHz | 10000 Hz | Cutoff of the post-generator filter. Shift opens a Morph / Q sub-page. |
| `Level` | GainBias | 0 … 1 | 0.5 | Output gain. |
| `Filter Morph` | ThresholdFader (expansion) | `off`, `LP`, `L>B`, `BP`, `B>H`, `HP`, `H>N`, `ntch` | 0.1 (`LP`) | Sweeps the filter response continuously; the fader shows the name rather than a number. At or below 0.005 the filter is bypassed entirely. |
| `Filter Q` | GainBias (expansion) | 0.5 … 20 | 0.5 | Filter resonance. |

**Algorithms** (index order)

| # | Name | What it generates | Param X | Param Y |
|---|---|---|---|---|
| 0 | `White` | White noise, sample-and-held and quantized. | Decimation, 1 … 64 samples of sample-and-hold. | Bit crush, 256 down to 2 levels. Inactive near 0. |
| 1 | `Pink` | 3-pole pink noise blended toward a doubly-integrated brown. | Tilt from pink toward brown. | Spectral thinning via a 1 … 64 sample hold. |
| 2 | `Dust` | Random bipolar impulses with silence between. | Density, 1 Hz … 10 kHz (logarithmic). | Impulse amplitude, 0.1 … 1.0. |
| 3 | `Particle` | Random impulses exciting a resonant bandpass. Bypasses the post filter. | Density (impulse probability). | Frequency spread: up to 48 semitones of random detune around the cutoff. |
| 4 | `Crackle` | Chaotic crackle attractor. | Chaos, 1.0 … 2.0. | Energy, 0.01 … 0.3. |
| 5 | `Logistic` | The logistic map, audio-rate. | Growth rate, 3.45 … 4.0; periodic through chaotic. | Iteration hold, 1 … 512 samples: noise at the bottom, pitched at the top. |
| 6 | `Henon` | The Hénon map, mirror-folded. | Map parameter *a*, 1.15 … 1.45. | Coupling *b*, 0 … 0.6. At 0 it degenerates to a 1-D map. |
| 7 | `Clocked` | Clocked sample-and-hold noise. | Clock rate, 0.5 Hz … 1 kHz (logarithmic). | Blend from stepped to linearly interpolated. |
| 8 | `Velvet` | Velvet noise: one raised-cosine pulse of random sign per window. | Density, 10 Hz … 10 kHz. | Pulse width. |
| 9 | `Gendy` | Xenakis stochastic synthesis over 16 moving breakpoints, with occasional Lévy jumps. | Amplitude chaos. | Duration chaos, roughly 10 Hz … 20 kHz. |
| 10 | `Lorenz` | The Lorenz attractor. | Rho, 22 … 100 (chaos sets in around 24.7). | Traversal speed, roughly 50 Hz … 1 kHz. |
| 11 | `Cellular` | A 1-D cellular automaton used as a wavetable: the CA row *is* the waveform, with a read head, generational evolution, grain overlap and a pitch-tracked feedback comb. | Character: selects a rule family (chaos / structure / edge-of-chaos gliders) and a rule within it. | "Life": drives an emergent per-instance field controlling resolution, evolution rate, reset, overlap, feedback and pitch. |

**Sub-display / expanded**: The `Filter Cutoff` ply's normal sub-display carries
the cutoff; tapping shift switches to a "Morph / Q" page with `mrph` and `Q`.
The `mrph` readout shows the same eight response names as the fader. Enter on
`Filter Cutoff` opens the expansion with `Filter Cutoff`, `Filter Morph` and
`Filter Q` as full faders.

**Menu**: *Cellular*: `Reseed field`. Re-rolls the Cellular algorithm's
per-instance emergent field (both textures and pitches) without deleting the
unit. No effect on the other eleven algorithms.

**I/O**: Pure generator; the chain input is ignored. One output, duplicated to
both channels on a stereo chain. Mono branches for `algo`, `paramX`, `paramY`,
`filterFreq`, `filterQ`, `filterMorph`, `level`; every macro is CV-addressable.
A V/Oct inlet exists in the DSP (it transposes the filter cutoff, and with it
Particle's bandpass centre) but **no view control is bound to it**, so it is not
patchable from the UI in this version.

---

## Helicase

*mnemonic: Hx* · Category: Spreadsheet

A two-operator FM oscillator with an OPL3-inspired waveform set. Carrier and
modulator each pick from eight shapes; the carrier then passes through a
"discontinuity folder" offering sixteen further transfer shapes, from soft sine
reshaping to Serge triangle folds and Chebyshev harmonics. Modulator feedback,
linear or exponential FM, and a phase-receptive sync that fires only when the
modulator reaches a chosen point in its cycle round it out. Three custom
visualizations (a clustered phase-space plot, the live folder transfer curve,
and a rotating modulator ribbon) show what each section is doing.

**Controls**

| Control | Type | Range / Options | Default | What it does |
|---|---|---|---|---|
| `V/Oct` | Pitch | n/a | n/a | 1 V per octave into the carrier. |
| `Fundamental` | GainBias | 0.1 Hz … just under Nyquist | 110.0 Hz | Base carrier pitch. Goes low enough to use as an LFO. |
| `Overview` | custom (ModMix) | 0 … 1 | 0.5 | Phase-space display; the fader is Mod Mix. Shift opens a mix / lin-exp / carrier page. |
| `Shaping` | custom (Mod Index) | 0 … 10 | 1.0 | Folder transfer-curve display; the fader is Mod Index. |
| `Modulator` | custom (Ratio) | 0.5 … 16 | 2.0 | Modulator ribbon display; the fader is Ratio. |
| `Sync` | custom Gate | n/a | n/a | Phase-receptive hard sync on the carrier. |
| `Level` | GainBias | 0 … 1 | 0.5 | Output gain. |
| `Mod Mix` | GainBias (Overview expansion) | 0 … 1 | 0.5 | How much raw modulator is added on top of the folded carrier. Additive, not a crossfade; the carrier is always present. |
| `Carrier Shape` | GainBias (Overview expansion) | 0 … 7 | 0 (sine) | Carrier waveform. Fractional values morph between adjacent shapes in hi-fi only. |
| `FM Mode` | OptionControl (Overview expansion) | `lin`, `exp` | `exp` | Exponential FM tracks pitch; linear FM keeps a constant sideband spacing. |
| `Mod Index` | GainBias (Shaping expansion) | 0 … 10 | 1.0 | FM depth. |
| `Disc Index` | GainBias (Shaping expansion) | 0 … 1 | 0.0 | Dry/wet between the raw and folded carrier. At 0 the folder is bypassed. |
| `Disc Type` | GainBias (Shaping expansion) | 0 … 15 | 0 | Which fold shape. Fractional values morph between adjacent types. |
| `Ratio` | GainBias (Modulator expansion) | 0.5 … 16 | 2.0 | Modulator frequency as a multiple of the carrier. Continuous, not quantized. |
| `Feedback` | GainBias (Modulator expansion) | 0 … 1 | 0.0 | Modulator self-feedback through a soft clip. |
| `Mod Shape` | GainBias (Modulator expansion) | 0 … 7 | 0 (sine) | Modulator waveform. |
| `Fine Tune` | GainBias (Modulator expansion) | −100 … +100 cents | 0.0 | Fine carrier detune. |
| `Phase Threshold` | GainBias (Sync expansion) | 0 … 1 | 0.0 | Where in the modulator's cycle a pending sync is allowed to fire. |

**Carrier / Mod Shape** (0 … 7): `sine`, `half-sine`, `abs-sine`,
`quarter-sine`, `alternating` (sine then silence), `camel` (rectified sine then
silence), `square`, `log saw` (falling ramp).

**Disc Type** (0 … 15): the sixteen fold shapes. Types 0-7 reuse the OPL3
waveforms as *transfer functions* applied to the carrier; types 8-15 are direct
wavefolders.

| # | Name | What it does |
|---|---|---|
| 0 | sine | Soft sine reshaper. |
| 1 | half-sine | Positive half only; negative clipped to zero. |
| 2 | abs-sine | Rectified, giving octave doubling. |
| 3 | quarter-sine | Rectified sine only over the outer quarters, silent between. |
| 4 | alternating | Sine over the first half of the range, zero over the second. |
| 5 | camel | Rectified sine over the first half, zero over the second. |
| 6 | square | Hard two-level sign function. |
| 7 | log saw | Falling ramp, band-limited at the wrap. |
| 8 | triangle fold | Serge-style bounce off the rails. |
| 9 | sine fold | Buchla-style sine wavefolder. |
| 10 | hard fold | Sharp V-shaped reflection. |
| 11 | staircase | Quantizes to six steps. |
| 12 | wrap | Phase wrap-around, band-limited. |
| 13 | asymmetric fold | Triangle fold with extra input gain, giving unequal slopes. |
| 14 | chebyshev T3 | Third-harmonic generator. |
| 15 | ring fold | Rectified sine with rounded corners. |

**Sub-display / expanded**: `Overview`: `mix`, `l/e` (a lin/exp toggle, not a
readout; press to switch), `carr`. `Shaping`: `index`, `disc`, `type`.
`Modulator`: `ratio`, `fdbk`, `shape`. `Sync`: `input`, `phase`, `fire` (press
and release to manually trigger sync). Enter on any of these opens the expansion
listed in the control table above.

**Menu**: *Mode*: `Quality`, either `lo-fi` or `hi-fi`, default `lo-fi`. Hi-fi
turns on 2× oversampling across the whole chain, enables fractional morphing
between adjacent carrier and modulator shapes, and drops the lo-fi ~8-bit
quantization of the folded carrier and modulator. Lo-fi is cheaper to run, and
its grit is also intended as part of the sound.

**I/O**: Pure generator; the chain input is ignored. One output, duplicated to
both channels on a stereo chain. `V/Oct` inlet (1 V/oct) and a trigger-mode
`Sync` inlet. Every parameter above has its own mono branch. A 20 Hz DC blocker
runs only above 1 Hz carrier frequency, so the unit is usable as an LFO.

---

## Ngoma

*mnemonic: NG* · Category: Spreadsheet

A macro drum voice built on a 16-mode modal lattice rather than the usual
sine-plus-pitch-envelope. A carrier of odd harmonics is cross-modulated by a
second oscillator, and each mode gets its own amplitude and decay from a fitted
model, so one Character knob sweeps a continuous territory from kick through tom
and snare to cymbal. The output chain (variable clipper, bipolar DJ EQ, and a
one-knob compressor) is part of the instrument, not a polish stage.

> The voice engine was **replaced in v2.8.0**. Presets from before that release
> will not sound the same, and the Clipper default flipped to 0.0 (clean).

**Controls**

| Control | Type | Range / Options | Default | What it does |
|---|---|---|---|---|
| `Trigger` | Gate (trigger mode) | n/a | n/a | Fires the voice. The whole mode bank is baked on the rising edge, so V/Oct is sampled per hit; mid-hit pitch changes do not bend it. |
| `V/Oct` | custom Pitch | n/a | n/a | 1 V per octave. Base pitch is 110 Hz; the result is clamped to 8 Hz … 8 kHz. |
| `Character` | custom GainBias | 0 … 1 | 0.5 | The macro. Morphs both oscillators sine → triangle over the lower half, then applies increasing wavefold drive over the upper half, painting odd fold harmonics as it goes. |
| `Sweep` | custom GainBias | 0 … 1 | 0.25 | Pitch-envelope depth. Starting pitch multiplier runs from about 1.1× at zero up to roughly 56× at full. |
| `Decay` | custom GainBias | 0.01 … 2.0 s | 0.25 s | Master decay time. Also tilts the spectrum and sets the noise-bed and grit-gate times. |
| `Level` | custom GainBias | 0 … 1 | 0.5 | Final output gain, after clipper, EQ and compressor. |
| `Octave` | GainBias (V/Oct expansion) | −4 … +4, integer | 0 | Octave offset added to V/Oct. |
| `Shape` | GainBias (Character expansion) | 0 … 1 | 0.0 | Detune ratio of the second oscillator, which sets every cross-modulated mode's frequency. Silent until roughly 0.07. |
| `Grit` | GainBias (Character expansion) | 0 … 1 | 0.0 | Three things at once: noise FM on the modes, a voice gate that chokes the tail (and above about 0.9 kills it outright with a noise burst), and a noise bed that starts around halfway. |
| `Punch` | GainBias (Character expansion) | 0 … 1 | 0.4 | A roughly 3 ms multiplicative transient boost before the drive stage. |
| `Sweep Time` | GainBias (Sweep expansion) | 0.001 … 0.5 s | 0.04 s | Pitch-envelope decay time. Also sets the onset-bloom time. |
| `Hold` | GainBias (Decay expansion) | 0 … 0.5 s | 0.0 s | Freezes every mode's envelope at full for this long before the decay starts. |
| `Attack` | GainBias (Decay expansion) | 0 … 0.05 s | 0.0 s | Linear rise on the modal bank only; the noise burst is unaffected. 0 is the hard, authentic jump. |
| `Clipper` | GainBias (Level expansion) | 0 … 1 | **0.0 (clean)** | Morphs through measured threshold/gain curves. Because makeup gain is compensated, this changes crest and density rather than loudness, and it is what generates the intermodulation lattice. |
| `EQ` | GainBias (Level expansion) | −1 … +1 | 0.0 | Bipolar DJ filter. Negative sweeps a lowpass down, positive sweeps a highpass up; bypassed near zero. |
| `Compressor` | GainBias (Level expansion) | 0 … 1 | 0.0 | One-knob compressor: threshold 0 → −40 dB, ratio 1:1 → 20:1, attack 10 → 1 ms, fixed 200 ms release, auto makeup. Bypassed at 0. |

**Sub-display / expanded**: `V/Oct`: `oct` ("Octave Offset"), stepping in whole
octaves. `Character`: `shape`, `grit`, `punch` ("Shape / Grit / Punch"), the
page shown by default. `Sweep`: `time` ("Sweep Time"). `Decay`: `hold`, `atk`
("Hold / Attack"). `Level`: `clip`, `eq`, `comp` ("Clipper / EQ / Comp"). Enter
on any ply opens the matching expansion with those parameters as full faders;
they are the same underlying values, so editing either surface moves the other.
Shift + a sub button opens keyboard entry.

The `Character` ply replaces its fader with a rotating 3-D cube. Character morphs
it toward a sphere and then folds it radially; Shape squashes it; Grit jitters
the vertices, speeds the rotation and dithers the faces; each hit pulses its
scale and flashes the face shading.

**Menu**: none.

**I/O**: Pure generator. One output, duplicated to both channels on a stereo
chain. Trigger and V/Oct inlets, plus a mono branch for every parameter above;
all fourteen are CV-addressable.

---

## JF

*mnemonic: JF* · Category: Spreadsheet

Six harmonically-coupled slope generators in one unit. A single TIME control sets
the base rate and INTONE morphs the six voices' frequency relationships
continuously from an undertone series, through a narrow detuned unison, to a
1:2:3:4:5:6 overtone stack. RAMP and CURVE shape the slopes themselves. A Range
switch moves the whole thing between control rates (envelopes, LFOs) and audio
rates (a six-operator additive voice), and every voice has its own output.

This is a clean-room implementation from a public technical map; no DSP source
was used. The 4-lane NEON voice pattern is adapted from tomf's `polygon` with his
permission.

**Controls**

| Control | Type | Range / Options | Default | What it does |
|---|---|---|---|---|
| `V/Oct` | Pitch | n/a | n/a | 1 V per octave on top of TIME. |
| `TIME` | GainBias | 0 … 1 | 0.5 | Base rate. In `shape` this spans roughly a 60-second period up to 1 kHz; in `sound` it spans 20 Hz to well past audio range. |
| `INTONE` | GainBias | −1 … +1 | 0.0 | Morphs the six voices' frequency ratios. Full CCW is an undertone series (1N at 1, 6N at 1/6); noon is a slight detune spread; full CW is the overtone series 1:2:3:4:5:6. Voice 1N is always the anchor. |
| `RAMP` | GainBias | −1 … +1 | 0.0 | Rise/fall asymmetry. CCW is fall-heavy, noon is a symmetric triangle, CW is rise-heavy. Bypassed in `sust` mode. |
| `CURVE` | GainBias | −1 … +1 | 0.0 | Continuous slope-shape morph across five anchors: rect → log → linear (noon) → exponential → sine. Bypassed in `sust` mode. |
| `FM Depth (CW: TIME / CCW: INTONE)` | GainBias | −1 … +1 | 0.0 | Signed destination selector. CW routes linear FM to TIME, applied equally to all six voices (through-zero). CCW routes it to INTONE, weighted per voice so 1N is untouched and 6N is affected most. Noon is no FM. |
| `FM Input` | GainBias | −1 … +1 | 0.0 | The FM signal itself. Carries bias, so a DC offset works in `shape`; `sound` AC-couples the inlet. |
| `OUT (primary outlet selector)` | GainBias | 0 … 6 | 0.0 | What the primary outlet carries. 0 = the mix, 1 … 6 = voice 1N … 6N. |
| `Trigger 1N (IDENTITY)` … `Trigger 6N` | Gate ×6 (gates view) | n/a | n/a | Per-voice trigger inlets. |

**Sub-display / expanded**: Standard GainBias sub-displays throughout; there
are no custom multi-parameter pages. The unit has **two main views**, switched
from the menu: the default globals page (the eight controls above) and a gates
page carrying the six per-voice trigger plies.

**Menu**: *View*: `Globals (default)`, `Gates (1N..6N)`. *Range*: `shape` /
`sound`. `shape` is control rate with a unipolar 0…1 output and a DC-coupled FM
inlet; `sound` is audio rate with a bipolar output and an AC-coupled inlet, and
the mix becomes a soft-clipped sum instead of an index-scaled maximum. *Mode*:
`trans` (attack-release; retriggers ignored while a slope is running), `sust`
(gate-following rise-and-fall), `cycle` (free-running, with rising edges hard
syncing). *OUT mode*: `smooth` (crossfades between adjacent OUT positions) /
`snap` (lands exactly on one source).

**I/O**: Multi-output, 8 channels. Sub-outs are labelled `mix`, `mix R`, `1N`,
`2N`, `3N`, `4N`, `5N`, `6N`. Outputs 1 and 2 both carry the primary outlet, so a
plain stereo chain hears the same thing on both sides; outputs 3-8 are the six
individual voices. Note that the primary outlet carries whatever **OUT** selects;
the true mix is what OUT position 0 gives you, not a separate signal.

Inlets: `V/Oct`, `FM In`, and six per-voice triggers. The triggers **cascade
right to left**: an unpatched cell inherits the trigger of the nearest patched
neighbour to its right. A voice with no patched source anywhere to its right
stays silent. Patch only 6N and all six voices fire together.

---

## Visadhara

*mnemonic: Vx* · Category: Spreadsheet

An additive percussion macro voice. Eight tonal oscillators plus a noise
oscillator run through a wavefolder and a shared attack-decay envelope. Spread
sets how the partials are spaced (harmonic through prime), Harmonic trades
detuned unison fatness against spread-out partials, and Morph blends the
waveshape from sine through square. A continuous Mode control crossfades between
three engines: Skin (plain additive), Liquid (additive with per-voice pitch
bends), and Metal (paired three-operator phase modulation).

Clean-room implementation based on a public technical manual. The 4-lane NEON
voice pattern descends from the same source as JF's.

**Controls**

| Control | Type | Range / Options | Default | What it does |
|---|---|---|---|---|
| `Trigger` | Gate | n/a | n/a | Fires the envelope on all voices and resets every phase for a coherent attack peak. |
| `V/Oct` | custom Pitch | n/a | n/a | 1 V per octave. Base pitch is 110 Hz. |
| `Mode` | custom GainBias | 0 … 2 (0 = Skin, 1 = Liquid, 2 = Metal) | 0.0 (Skin) | Crossfades the three engines. Liquid adds a punchy per-voice pitch bend of up to two octaves; Metal swaps in two three-operator phase-modulation pairs. |
| `Fold` | GainBias | 0 … 1 | 0.0 | Threshold-reflection wavefolder, drive 1× … 6× with output compensation. Above 0.75 it adds a square pulse component. |
| `Attack` | GainBias | −1 … +1 | 0.0 | Bipolar and three-way. Above about +0.05 it is a slow rise up to roughly 0.2 s. Near zero it is instant. Below about −0.05 it stays instant but cross-injects the *other* engine's bus (the Metal bus into Skin/Liquid, or the additive bus into Metal) by the knob's magnitude. |
| `Decay` | GainBias | 0 … 1 | 0.5 | Envelope decay, quadratically tapered. |
| `Level` | GainBias | 0 … 1 | 0.7 | Output gain before the master saturator. |
| `Octave` | ThresholdFader (V/Oct expansion) | `Bass`, `Alto`, `Tenor` | `Alto` | Coarse register: Bass is −2 octaves, Alto is 0, Tenor is +2. CV-addressable. |
| `Spread` | GainBias (Mode expansion) | 0 … 1 | 0.0 | Partial spacing: interpolates from a harmonic series (1,2,3,…8) to a prime series (1,2,3,5,7,11,13,17). In Metal it also boosts the second operator pair's FM index. |
| `Harmonic` | GainBias (Mode expansion) | 0 … 1 | 0.5 | Trades distribution against detune. At 0 all eight voices sit on the fundamental with full detune, giving a fat chorused sub. At 1 they spread onto the Spread series with detune collapsed to zero. |
| `Morph` | GainBias (Mode expansion) | 0 … 1 | 0.0 | Equal-power waveshape blend: sine → triangle → saw → square. Applies to the additive voices and to every Metal operator. |

**Sub-display / expanded**: `V/Oct`: `oct` ("Octave"), which shows `Bass` /
`Alto` / `Tenor` rather than a number. `Mode`: `sprd`, `harm`, `mrph` ("Spread /
Harm / Morph"), shown by default. Enter on `V/Oct` adds the `Octave` fader;
Enter on `Mode` adds `Spread`, `Harmonic` and `Morph` as full faders, driving the
same values as the sub readouts.

The `Mode` ply replaces its fader with the Corona graphic: petal polygons
orbiting on a tilted, slowly tumbling plane. Spread sets the petal count, Mode
sets how many sides each polygon has (three at Skin, eight at Metal, morphing
continuously), Harmonic sets the orbital radius, Morph turns polygons into stars,
and Fold inverts the whole image. Each trigger sends a pair of radial shockwaves
outward.

**Menu**: *Mode crossfade*: `smooth` (tent crossfade between adjacent modes) /
`snap` (hard select the nearest mode). Default `smooth`.

**I/O**: Pure generator. One output, duplicated to both channels on a stereo
chain. Trigger and V/Oct inlets, plus a mono branch for every parameter. The
whole engine runs 2× oversampled.

---

## Mirror

*mnemonic: Mr* · Category: Spreadsheet

A complex oscillator built around deliberate aliasing. The audible source is a
16-frame wavetable envelope retriggered by an internal sync edge; the Mirror knob
then runs it through a four-stage crusher (pre-saturation, divider-clocked
sample-and-hold, bit quantization, and a zero-order-hold reconstruction that
flips polarity at the top of the knob) with **no anti-aliasing anywhere**. The
resulting fold-down partials are the instrument. Sync Threshold sets a
carrier-to-modulator lock ratio with sticky plateaus at Fibonacci ratios, so the
unit moves between locked, pitched tones and smooth chaos, and it uses that same
axis to open the stereo image.

**Controls**

| Control | Type | Range / Options | Default | What it does |
|---|---|---|---|---|
| `V/oct` | Pitch | n/a | n/a | 1 V per octave on the modulator, which is what you hear as pitch. |
| `Shape (overview)` | custom GainBias | 0 … 1 | 0.13 | Position across the 16 wavetable frames, interpolated. Ordered roughly simple to exotic: square gate, saw down, triangle, exponential decay, half-sine bell, Gaussian, pluck, anti-pluck, two-peak and three-peak lobes, damped sine, inverse exponential, full sine cycle, damped square, sinc, triple impulse. |
| `Mod Depth` | GainBias | 0 … 1 | 0.5 | Modulator into carrier phase and into the envelope rate; up to ±3 octaves of envelope-rate modulation at full. |
| `Sync` | GainBias | 0 … 1 | 0.0 | Carrier/modulator lock ratio, anchored at 1, 2, 3, 5, 8 and 13 across the knob's travel. Lock zones are sticky and the transitions between them are smooth chaos. It multiplies the envelope rate too, and sets stereo width: zero at the locks, widest at the chaos midpoints. |
| `Mirror` | GainBias | 0 … 1 | 0.0 | The crusher. One knob drives all four stages: sample-and-hold divisor 1 … 64, bit depth 16 down to 2, pre-saturation drive, and above 0.85 a Nyquist polarity flip. |
| `Level` | GainBias | 0 … 1 | 0.5 | Output gain, before the soft clip. |
| `Fundamental` | GainBias (Shape expansion) | 0.1 Hz … just under Nyquist | 110.0 Hz | Modulator rate; the perceived pitch at V/Oct 0. |
| `Formant` | GainBias (Shape expansion) | 0.1 Hz … just under Nyquist | 110.0 Hz | Wavetable envelope rate. Tracks V/Oct and is multiplied by the lock ratio. When it is slower than the sync rate, sync edges get absorbed, which is what produces the pitch-division/undertone series. |
| `Feedback` | GainBias (Shape expansion) | 0 … 1 | 0.0 | Crusher output back into the envelope rate; up to ±2 octaves at full. Independent per channel. |

**Sub-display / expanded**: The `Shape` ply's custom page carries `freq`,
`form`, `fbck` ("Freq / Form / Fbck") and is shown by default; tapping shift
reveals the stock Shape fader sub-display. Enter on `Shape` opens the expansion
with `Shape` (keeping its graphic) plus `Fundamental`, `Formant` and `Feedback`
as full faders.

The `Shape` ply's graphic is a phase-space phosphor scope, plotting each output
sample against the next and mirroring every point about the vertical centreline,
so the image is always left-right symmetric. It does not tumble; rotation would
destroy the mirror identity.

**Menu**: none.

**I/O**: Multi-output, 8 channels. Sub-outs: `Out`, `Out R`, `Clean`, `Drive`,
`Held`, `Fold`, `Sync`, `Mod`.

- `Out` / `Out R`: the two main channels. The right channel is a fully
  independent pipeline; it differs from the left only by a sync-derived envelope
  phase offset, which is why stereo width tracks Sync Threshold.
- `Clean`: the bandlimited wavetable envelope before the crusher.
- `Drive`: after pre-saturation, before the sample-and-hold.
- `Held`: the quantized stair-step value, before reconstruction.
- `Fold`: the alias residual alone (crusher output minus clean). Built for
  self-patching.
- `Sync`: a one-sample gate on each internal sync edge.
- `Mod`: the raw modulator sine.

`Clean`, `Drive`, `Held` and `Fold` are left-channel taps only. Inlets are
`V/Oct` and an audio-rate `FM` input; the FM input has **no view control** and is
reachable only through the input picker. The output DC blocker disengages below
1 Hz, so Mirror works as an LFO or a slow modulation source.

---

<!-- VERIFICATION NOTES

Version: mods/spreadsheet/mod.mk declares PKGVERSION 2.8.5.1. RELEASE-2.8.1.md
(the last release doc, 2026-08-06) says spreadsheet went 2.8.4 -> 2.8.5, so
2.8.5.1 is a post-release point bump with no release doc of its own. Everything
below was read from source at that revision.

DISCREPANCIES vs README.md / release notes:

1. Rauschen has TWELVE algorithms, not eleven. README.md:99 says "11 algorithms"
   and lists only White..Lorenz, omitting Cellular. Cellular was added in v2.8.0
   (RELEASE-2.8.0.md:117) and is index 11; the C++ clamps to 0..11 and the Lua
   algoNames table has 12 entries. The Lua comment in Rauschen.lua also still
   says "Flat 11-entry list" and is stale.

2. Rauschen's V/Oct is not reachable from the UI. Rauschen.lua builds a tune
   ConstantOffset -> op "V/Oct" and calls addMonoBranch("tune", ...), and
   requires Unit.ViewControl.Pitch, but never instantiates it; onLoadViews
   returns no control bound to branches.tune, and tune's Offset is not
   serialized. In DSP, V/Oct transposes the filter cutoff (and with it
   Particle's bandpass centre). README.md:99 advertises "post-generator SVF
   morph filter with V/Oct", which is true of the DSP but not usable in this
   build. Reported as a likely bug rather than documented as a feature.

3. Ngoma's preset schema is now 6, not 5. The task brief and README.md:204 both
   say "preset schema 5". DrumVoice.lua:310 writes `t.schema = 6` with the
   comment "schema 5 = modal engine transplant. schema 6 = Sweep normalized 0..1
   (was 0..72), Level default 0.5." Schema 5 was the v2.8.0 engine swap; schema
   6 is a later rescale within the 2.8.x line. Clipper default 0.0 confirmed.

4. Ngoma Level default: the C++ od::Parameter default is 0.8 but Lua hardSets
   0.5 and the view's initialBias is 0.5, so the effective default is 0.5.
   Documented as 0.5.

5. Visadhara is an EIGHT-voice engine, not six. README.md:108 says "6-voice NEON
   additive", visadhara/README.md says "6-lane voice (two 4-lane bundles, lanes
   6,7 masked off)", and Visadhara.lua's header comment says "6-voice NEON
   additive". The shipped C++ loops `for (int i = 0; i < 8; i++)` over voice
   distribution and pitch-envelope setup, and kVoiceDetune is an 8-element array.
   Documented as eight.

6. Visadhara's Mode is fully implemented, not a stub. Visadhara.lua:148 still
   carries a "Phase 1 stub" comment saying Skin only, with the crossfade between
   modes deferred to Phase 3+. The C++ implements Skin, Liquid and Metal with
   both tent-crossfade and snap dispatch. Stale comment; not repeated in the
   docs above.

7. Mirror's sync lock ratios are Fibonacci {1, 2, 3, 5, 8, 13}, not the
   {1:1, 3:2, 2:1, 5:2, 3:1} listed in Mirror.h's own file header comment
   (lines 10-12). Mirror.cpp:23 and :49 define kR[6] = {1,2,3,5,8,13} at knob
   positions 0.0/0.2/0.4/0.6/0.8/1.0. The header comment is stale; the code is
   documented above.

8. JF's primary outlet carries the OUT-crossfader result, not a fixed mix.
   README.md:107 describes it as "multi-output" which is accurate, but the
   "mix" sub-out label is slightly misleading: mixBuf receives whatever OUT
   selects, and the true mix is only OUT position 0.

9. JF is deliberately unbranded in code. JF.h calls it a "clean-room
   implementation from a public technical map"; the named upstream (the
   Whimsical Raps / Mannequins technical map) appears only in
   planning/just-friends.md, not in shipped source. Described above without the
   third-party name, matching the code's own convention.

COULD NOT VERIFY / NOT DOCUMENTED:

- Ngoma's Character/Grit/Clipper coefficient tables are fitted regressions
  against hardware captures; the described perceptual behaviour comes from the
  code comments and the shape of the tables, not from listening.
- Excel step offsets are not clamped in DSP on hand entry (only during
  transforms), so out-of-range values are reachable via keyboard entry. Noted in
  the table; not treated as a bug since the dial map bounds normal use.
- Ballot fires the step gate AFTER advancing, so with step length 1 the gate
  that sounds belongs to the step following the one that just ended, and step 1's
  gate is not played on the first clock after reset. This looks like an
  off-by-one but may be intentional; left out of the user-facing text and flagged
  here.
- Etcher has no snapshot save/restore, unlike Excel and Ballot. Documented as a
  fact; unclear whether it is an omission or a decision.
- Excel/Ballot snapshots are one-shot (restore consumes the snapshot) and are not
  written into presets. Documented; worth confirming this is intended.

-->
