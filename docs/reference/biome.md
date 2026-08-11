# Biome (`biome`) v2.2.3

Biome is the original-units package: gate sequencers, waveshapers, filters, CV
utilities, envelopes, mixers and oscillators, all authored for this catalog
rather than ported wholesale. Two units embed third-party DSP: the Varishape
pair use Émilie Gillet's (Mutable Instruments) variable-shape oscillator and
decay envelope, and 94 Discont / Latch Filter are ports of monokit SuperCollider
implementations. NR's rhythm table derives from the teletype / Noise Engineering
Numeric Repetitor idea.

---

## NR

Mnemonic: NR · Category: Biome

A gate sequencer built on 32 hard-coded 16-bit "prime" rhythm patterns. Prime
picks the base rhythm, Mask ANDs it with one of three window variants, Factor
multiplies it into a denser derived pattern, and Length sets the loop size. The
clock comes from the unit's own chain input, so patch the clock into the chain
that holds NR; the output replaces that signal with gates. The circle display
shows the pattern arcs, ring divisions and the last-played step.

**Controls**

| Control | Type | Range / Options | Default | What it does |
|---|---|---|---|---|
| Pattern | Custom ViewControl (NRCircle) | n/a | n/a | Ring display of the active pattern plus step marker; carries the prime/mask/factor readouts |
| Reset | Gate (trigger) | trigger | n/a | Returns the sequence to step 0 and kills any gate in progress |
| Prime | GainBias | 0-31, integer | 0 | Selects one of 32 base rhythm patterns |
| Mask | GainBias | 0-3, integer | 0 | 0 = no mask; 1/2/3 AND the pattern with 0x0F0F / 0xF003 / 0x01F0 |
| Factor | GainBias | 0-16, integer | 1 | Multiplies the masked pattern and folds the overflow back in, thickening the rhythm |
| Length | GainBias | 1-16, integer | 16 | Number of steps before the sequence wraps |
| Width | GainBias | 0-1 | 0.50 | Gate length as a fraction of the measured clock period (≈1 ms fallback before a period is known) |

**Sub-display / expanded**: The default expanded view is the circle alone;
Enter on it opens the full row (Reset, Prime, Mask, Factor, Length, Width). The
circle's sub-display holds three readouts with sub-buttons `prime` / `mask` /
`factor`; pressing a focused button again opens a numeric keyboard. All three
step one entry per detent.

**I/O**: Clock is the unit's chain input In1 (gate-mode comparator). Reset is a
mono trigger branch. Prime/Mask/Factor/Length/Width each accept CV on their own
branch. The output gate (0 or 1) is copied to every channel of the chain.

---

## 94 Discont

Mnemonic: Dc · Category: Biome

A seven-mode waveshaper and saturator. Amount is drive into the shaper (up to
10x), each mode has its own character from gentle tanh through wavefolding,
rectification and 8-level bitcrush, and Mix crossfades wet against dry. Ported
from a monokit SuperCollider implementation.

**Controls**

| Control | Type | Range / Options | Default | What it does |
|---|---|---|---|---|
| Mode | ModeSelector (discrete) | Fold, Tanh, Soft, Clip, Sqrt, Rect, Crush | Fold | Chooses the shaping function |
| Amount | GainBias | 0-10 | 1.00 | Pre-shaper gain: how far into the nonlinearity the signal goes |
| Mix | GainBias | 0-1 | 1.00 | Dry/wet blend at the output |

**I/O**: Mono or stereo (one DSP instance per channel), audio in/out. Mode,
Amount and Mix each take CV on a mono branch. In a stereo chain Mode and Amount
are tied to both instances but Mix is tied only to the left one, so Mix
currently affects the left channel only.

---

## Latch Filter

Mnemonic: LF · Category: Biome

A switched-capacitor-flavoured filter: the input is sample-and-held at one
eighth of the cutoff frequency, then passed through a state-variable filter at
that same cutoff. The latching adds aliasing grit and a metallic edge that
tracks pitch, so it works as a filter and a sample-rate crusher at once. Cutoff
tracks 1 V/oct exactly, so with high Resonance it can be played as a pitched
resonator. Ported from a monokit SuperCollider implementation.

**Controls**

| Control | Type | Range / Options | Default | What it does |
|---|---|---|---|---|
| V/oct | Pitch | n/a | 0 | 1 V/oct cutoff tracking, applied per sample |
| Fundamental | GainBias | -48 to +48 semitones | 0.0 | Cutoff offset in semitones from middle C (261.63 Hz); cutoff clamped 20 Hz-20 kHz |
| Resonance | GainBias | 0-1 | 0.50 | Maps to filter Q from 0.5 to 20 |
| Mode | ModeSelector (discrete) | LP, HP | LP | Low-pass or high-pass output of the SVF |

**I/O**: Mono or stereo (dual DSP instances, all parameters shared). V/oct,
Fundamental, Resonance and Mode each have a mono CV branch. Audio in/out.

---

## Gesture

Mnemonic: Gs · Category: Biome

A continuous CV gesture recorder and looper. Turn the Offset fader while Run is
on and the movement is detected automatically and written into the loop buffer.
There is no write button, and a diamond indicator lights while writing is
active. The head then loops the buffer forever, with optional output slew and a
held Erase gate that zeroes the buffer under the playhead. Buffer length is
chosen from the menu and the buffer is saved with the patch.

**Controls**

| Control | Type | Range / Options | Default | What it does |
|---|---|---|---|---|
| Run | Gate (toggle) | on/off | off | Runs the playhead; output is 0 while stopped. Writing and erasing only happen while running |
| Reset | Gate (trigger) | trigger | n/a | Jumps the playhead back to the start of the buffer |
| Offset | GainBias | -1 to 1 | 0.000 | The live CV value; moving it auto-arms write and records into the buffer |
| Slew | GainBias | 0-10 s | 0.00 s | One-pole smoothing on the output (bypassed below 1 ms) |
| Erase | Gate | gate | n/a | While held (and not writing), zeroes buffer samples under the playhead |
| write | Custom indicator (display only) | n/a | n/a | Diamond lights when movement-detected write is active |

**Sub-display / expanded**: Enter on Offset, Slew or Erase opens a zoomable
waveform view of the gesture buffer with the playhead. Its sub-display buttons
are `|<<` (reset head) and `> / ||` (play/pause), plus the usual zoom floating
menu with an added "collapse" item.

**Menu**: Buffer: `5 sec`, `10 sec`, `20 sec` (allocate a new buffer),
`Clear Buffer` (zero it). Write sensitivity: `Sens.` = Low / Medium / High
(movement thresholds 0.005 / 0.002 / 0.0005), default Medium.

**I/O**: Pure CV generator: the chain input is sunk, and the single CV output
is copied to every channel. Run, Reset, Erase, Offset and Slew each have a mono
CV branch.

---

## Gated Slew

Mnemonic: GS · Category: Biome

A slew limiter that only limits while its gate is high. With the gate low the
output tracks the input exactly and keeps its internal state synced, so the
moment the gate rises the slew engages from the current value with no jump.
Useful for gating portamento on legato notes, or for smoothing a CV only during
specific sections.

**Controls**

| Control | Type | Range / Options | Default | What it does |
|---|---|---|---|---|
| Gate | Gate (gate mode) | gate | n/a | High = slew active; low = straight pass-through |
| Slew Time | GainBias | 0.003-1000 s (octave scaling, `slewTimes` map) | 1.0 s | Time to traverse one full unit of signal; sets the maximum change per sample |
| Mode | OptionControl | up, both, down | both | Which direction of change gets limited |

**I/O**: Mono or stereo. In stereo, two slew instances share the Slew Time
parameter, the Gate comparator and the Mode option. Gate and Slew Time are mono
CV branches.

---

## Tilt EQ

Mnemonic: TQ · Category: Biome

A one-knob tone control that pivots the spectrum around a fixed ~800 Hz
crossover. Turning positive brightens (boosts highs, cuts lows by the
complementary amount); negative darkens. Total swing is roughly ±6 dB per side
at full deflection, so the two halves stay energy-complementary.

**Controls**

| Control | Type | Range / Options | Default | What it does |
|---|---|---|---|---|
| Tilt | GainBias | -1 to 1 | 0.0 | -1 = dark (lows boosted ~+6 dB, highs cut), 0 = flat, +1 = bright |

**I/O**: Audio In1 (and In2 if stereo) to Out1/Out2; one filter instance per
channel with a shared Tilt. Tilt has a mono CV branch.

---

## DJ Filter

Mnemonic: DJ · Category: Biome

A bipolar single-knob DJ-style sweep filter built on a Cytomic-style SVF. Centre
position is a true bypass (dead zone below |Cut| = 0.01); turning left engages a
lowpass sweeping down toward 20 Hz, turning right a highpass sweeping up toward
20 kHz. The wet/dry blend tracks distance from centre, so the filter fades in as
you sweep rather than snapping.

**Controls**

| Control | Type | Range / Options | Default | What it does |
|---|---|---|---|---|
| Cut | GainBias | -1 to 1 | 0.0 | Negative = lowpass, positive = highpass, centre = bypass. Cutoff maps exponentially over 20 Hz-20 kHz |
| Resonance | GainBias | 0-1 | 0.5 | SVF damping; Q ≈ 0.5 (clean) up to ≈ 10 at maximum |

**I/O**: Audio In1 (and In2 if stereo) to Out1/Out2; one filter instance per
channel with shared Cut and Resonance. Both parameters have mono CV branches.

---

## Gridlock

Mnemonic: GL · Category: Biome

A three-input priority gate router with a latching output. Each gate is paired
with its own value; when a gate goes high the output jumps to that value and
holds it after the gate falls, until another gate fires. Gate 1 wins over Gate
2, which wins over Gate 3, evaluated per sample. Useful as a keyboard-style
priority CV source or a gate-addressed voltage memory.

**Controls**

| Control | Type | Range / Options | Default | What it does |
|---|---|---|---|---|
| Gate 1 (highest priority) | Gate (gate mode) | gate | n/a | Highest priority; while high, output = Value 1 |
| Gate 2 | Gate (gate mode) | gate | n/a | Fires only when Gate 1 is low |
| Gate 3 (lowest priority) | Gate (gate mode) | gate | n/a | Fires only when Gates 1 and 2 are low |
| Value 1 | GainBias | -5 to 5 | 1.0 | Voltage latched by Gate 1 |
| Value 2 | GainBias | -5 to 5 | 0.0 | Voltage latched by Gate 2 |
| Value 3 | GainBias | -5 to 5 | -1.0 | Voltage latched by Gate 3 |

**I/O**: No audio input used. Three mono gate branches and three mono CV
branches. Output is a single mono CV signal copied to Out1 and Out2 in stereo.
Gate threshold is 0.5.

---

## Integrator

Mnemonic: IN · Category: Biome

A running accumulator: the input signal is integrated over time at a scalable
rate, with an optional leak that pulls the accumulated value back toward zero. A
rising edge on Reset zeroes the accumulator. Output is hard-clipped to ±5 V, so
it saturates rather than blowing up.

**Controls**

| Control | Type | Range / Options | Default | What it does |
|---|---|---|---|---|
| Rate | GainBias | 0-100 | 1.0 | Integration gain: how fast the input accumulates |
| Leak | GainBias | 0-1 | 0.0 | 0 = pure integrator (holds), 1 = fast decay toward zero |
| Reset | Gate (trigger mode) | trigger | n/a | Rising edge zeroes the accumulator |

**I/O**: In1 is integrated (In2 ignored). Mono CV branches for Rate and Leak,
mono trigger branch for Reset. Mono output copied to Out1 and Out2 in stereo,
clipped to ±5 V.

---

## Spectral Follower

Mnemonic: SF · Category: Biome

An envelope follower that listens through a tunable bandpass, so it responds
only to energy in a chosen part of the spectrum. An RBJ-cookbook biquad sets the
listening band; a two-time-constant follower with an adaptive threshold decides
whether to attack or decay, so the detector tracks relative level changes rather
than a fixed trip point. Output is a mono CV envelope, not audio.

**Controls**

| Control | Type | Range / Options | Default | What it does |
|---|---|---|---|---|
| Center Freq | GainBias | 20-20000 Hz | 1000 Hz | Bandpass centre frequency |
| Bandwidth | GainBias | 0.1-4 octaves | 1.0 | Bandpass bandwidth |
| Attack | GainBias | 0.0001-0.5 s | 0.005 s | Rise time when band energy exceeds the adaptive threshold |
| Decay | GainBias | 0.0001-5.0 s | 0.050 s | Fall time below threshold |

**I/O**: Audio In1 (In2 ignored). Mono CV branches for all four parameters.
Output is a mono unipolar envelope copied to Out1 and Out2 in stereo.

---

## Quantoffset

Mnemonic: QO · Category: Biome

A CV offset feeding a grid quantizer. The Offset control (with its own gain,
bias and CV branch) supplies the voltage, and Levels sets how many equal steps
the octave is divided into, where 12 gives standard semitones. Output is the
quantized voltage, suitable as a V/Oct source. Note that the unit does not read
its chain input: the only signal source is the Offset control and its branch.

**Controls**

| Control | Type | Range / Options | Default | What it does |
|---|---|---|---|---|
| Offset | GainBias | -1 to 1 | 0.000 | The voltage that gets quantized; has gain, bias and a CV branch |
| Levels | GainBias | 2-128, integer | 12 | Divisions per 1.0 unit (per octave in V/Oct terms) |

**I/O**: Mono CV branches for Offset and Levels. Output is mono V/Oct-compatible
CV copied to Out1 and Out2 in stereo. The chain input In1/In2 is not connected.

---

## PSR

Mnemonic: SR · Category: Biome

A trigger-driven random voltage source: on each rising edge it draws a fresh
bipolar random value, optionally quantizes it to a grid, then scales and offsets
it. The value is held until the next trigger, so it doubles as a pingable
sample-and-hold with no input to sample.

**Controls**

| Control | Type | Range / Options | Default | What it does |
|---|---|---|---|---|
| Trigger | Gate (trigger mode) | trigger | n/a | Rising edge draws a new random value |
| Scale | GainBias | 0 to 5 | 1.00 | Multiplies the raw ±1 random value |
| Offset | GainBias | -5 to 5 | 0.00 | Added after scaling |
| Quant Levels | GainBias | 0-128, integer | 0 | Quantizes the raw value to N levels before scale/offset. Only engages above 1, so 0 and 1 both mean unquantized |

**I/O**: No audio input. Mono output on Out1, mirrored to Out2 in stereo.
Trigger, Scale, Offset and Quant Levels each have a mono CV branch.

---

## Bletchley Park

Mnemonic: CO · Category: Biome

An oscillator that reads arbitrary binary files as its waveform. A 256-byte
window of the loaded file is interpreted as signed 8-bit samples and played back
as a single-cycle wave with linear interpolation; Scan slides that window
through the data, so the timbre is whatever bit patterns happen to live at that
address. Each instance picks a random 4096-byte region on load, so two copies
explore different neighbourhoods. A ~20 Hz DC blocker cleans up the arbitrary
offsets raw binary produces.

**Controls**

| Control | Type | Range / Options | Default | What it does |
|---|---|---|---|---|
| Scan | ScanControl (GainBias subclass) | 0 to 1 | 0.000 | Slides the 256-byte read window through a 4096-byte region of the file. The fader label shows the byte offset in hex, or `no data` |
| V/Oct | Pitch | n/a | n/a | 1 V/oct pitch offset |
| Fundamental | GainBias | `oscFreq` map, Hz; DSP clamps 0.1 Hz to 0.49 × sample rate | 110.0 Hz | Base frequency |
| Sync | Gate (trigger mode) | trigger | n/a | Rising edge resets the phase to 0 |
| Level | GainBias | -1 to 1 | 0.5 | Output level; negative inverts |

**Menu**: `Load File` (file chooser) plus a read-only info line showing the
loaded file's name and size. The path is saved with the patch and reloaded.

**I/O**: Generator, no audio input. Mono output on Out1, mirrored to Out2 in
stereo. V/Oct is a pitch input and Sync is a trigger input; Scan, V/Oct,
Fundamental, Sync and Level each have a mono CV branch. With no file loaded the
output is silent.

---

## Station X

Mnemonic: CF · Category: Biome

A convolution filter whose kernel is read straight out of a binary file. Taps
consecutive bytes at the scan position are interpreted as signed 8-bit
coefficients, normalized by their absolute sum, and convolved with the input.
Scan sweeps the kernel through the whole file, so the filter's character changes
unpredictably as you move through the data; Mix blends against the dry signal.

**Controls**

| Control | Type | Range / Options | Default | What it does |
|---|---|---|---|---|
| Scan | ScanControl (GainBias subclass) | 0 to 1 | 0.000 | Byte offset of the FIR kernel within the file. Fader label shows the offset in hex, or `no data` |
| Taps | GainBias | 4-64, integer | 32 | Kernel length / filter order |
| Mix | GainBias | 0 to 1 | 0.50 | 0 = dry, 1 = fully convolved |

**Menu**: `Load File` (file chooser) plus a read-only line with the loaded
file's name and byte size. The path is saved with the patch.

**I/O**: Audio in/out. In stereo a second independent filter instance handles
In2 → Out2, tied to the same Scan / Taps / Mix and loaded with the same file.
All three parameters have mono CV branches. With no data loaded (or a file under
64 bytes) audio passes through unaltered.

---

## Fade Mixer

Mnemonic: FM · Category: Biome

A four-input crossfading mixer driven by a single Fade control: fade sweeps an
equal-power triangular window across the four inputs, so position 0 is all of
input 1 and position 1 is all of input 4, with smooth blends between. Each input
is a full branch with its own level fader and unit-local Solo/Mute buttons, so
muting acts across this mixer's inputs only rather than the whole chain. A
config-menu option switches Fade from a smooth crossfade to a Snap N-to-1
switch.

**Controls**

| Control | Type | Range / Options | Default | What it does |
|---|---|---|---|---|
| in1 | FadeMuteMeter (BranchMeter subclass) | fader gain | 1.0 | Level for input branch 1; sub-buttons enter the branch, solo, mute |
| in2 | FadeMuteMeter | fader gain | 1.0 | Input branch 2 |
| in3 | FadeMuteMeter | fader gain | 1.0 | Input branch 3 |
| in4 | FadeMuteMeter | fader gain | 1.0 | Input branch 4 |
| Fade | GainBias | 0 to 1 | 0.00 | Crossfade position across the four inputs. Smooth: equal-power triangular window per slot. Snap: the nearest slot takes the whole output |
| Level | GainBias | 0 to 4 | 1.00 | Output level of the mixed signal |

**Sub-display / expanded**: Each input meter's sub-buttons are 1 = enter/show
the branch, 2 = Solo, 3 = Mute, routed to a mute group the unit owns rather than
the parent chain's. The collapsed view is header-only.

**Menu**: `Fade`: `smooth` (default) or `snap`. Smooth is the crossfader; Snap
makes it an N-to-1 switch selecting by nearest centre, with a 3 ms declick ramp
so switching does not step the waveform.

**I/O**: Four mono input branches (`ch1`-`ch4`); Fade and Level have their own
mono CV branches. The unit's chain input In1 is summed with the crossfade
output, so the incoming chain signal passes through and the mix is added. Mono
output on Out1, mirrored to Out2 in stereo. No V/Oct, gate or trigger inputs.

---

## Fade Mixer 6

Mnemonic: F6 · Category: Biome

Identical to Fade Mixer except that it has six inputs (`in1`-`in6`, branches
`ch1`-`ch6`) and Fade sweeps its window across six slots. Same controls, ranges,
defaults, smooth/snap menu option, unit-local mute/solo and I/O behaviour. New
in v2.8.0.

---

## Fade Mixer 8

Mnemonic: F8 · Category: Biome

Identical to Fade Mixer except that it has eight inputs (`in1`-`in8`, branches
`ch1`-`ch8`), the maximum the DSP object supports, with Fade sweeping across
eight slots. Everything else matches Fade Mixer. New in v2.8.0.

---

## Varishape Voice

Mnemonic: VV · Category: Biome

A minimal one-oscillator synth voice: a PolyBLEP variable-shape oscillator run
through a gate-triggered exponential decay envelope and an output VCA. Shape is
a single continuous morph from triangle through saw to square (with pulse width
opening at the top) rather than a waveform selector. The oscillator core and
envelope are Émilie Gillet's (Mutable Instruments, MIT).

**Controls**

| Control | Type | Range / Options | Default | What it does |
|---|---|---|---|---|
| Gate | Gate (gate mode) | gate | n/a | Rising edge fires the decay envelope |
| Shape | GainBias | 0 to 1 | 0.0 | Morphs triangle → saw → square; also opens pulse width above ~0.66 |
| V/Oct | Pitch | n/a | 0 | Exponential pitch offset from Fundamental |
| Fundamental | GainBias | `oscFreq` map; DSP clamps 0.1 Hz to 0.49 × sample rate | 110.0 Hz | Base oscillator frequency |
| Decay | GainBias | 0 to 1 | 0.5 | Envelope decay length; 0 = fast pluck, 1 = long tail |
| Level | GainBias | -1 to 1 | 0.5 | Output VCA gain; negative inverts |

**I/O**: Generator: the chain input is sunk. Mono output copied to Out2 in
stereo. Gate is a gate input, V/Oct a pitch input; Shape, Fundamental, Decay and
Level each have a CV branch. A Sync branch exists in the graph but is not shown
in any view and the DSP never reads it, so sync is non-functional.

---

## Varishape Osc

Mnemonic: VO · Category: Biome

The bare oscillator from Varishape Voice with no envelope: a PolyBLEP
variable-shape core morphing triangle → saw → square with widening pulse width,
plus V/Oct and an output level. BLEP correction keeps it clean high in the
range. Use it as a raw tone source for external envelopes and filters. Core by
Émilie Gillet (Mutable Instruments, MIT).

**Controls**

| Control | Type | Range / Options | Default | What it does |
|---|---|---|---|---|
| Shape | GainBias | 0 to 1 | 0.0 | Morphs triangle → saw → square; widens pulse width past ~0.66 |
| V/Oct | Pitch | n/a | 0 | Exponential pitch offset from Fundamental |
| Fundamental | GainBias | `oscFreq` map; DSP clamps 0.1 Hz to 0.49 × sample rate | 110.0 Hz | Base oscillator frequency |
| Level | GainBias | -1 to 1 | 0.5 | Output gain; negative inverts |

**I/O**: Generator: the chain input is sunk. Mono output copied to Out2 in
stereo. V/Oct is a pitch input; Shape, Fundamental and Level have CV branches.
As with Varishape Voice, the Sync branch is not exposed and the DSP ignores it.

---

## Transport

Mnemonic: Tr · Category: Biome

A run/stop clock generator. BPM sets the tempo and the output runs at 4
ppqn (16th notes) with a 50% duty square. Run/Stop is a toggle, and either edge
resets the phase to zero, so the first tick always lands on the transition.

**Controls**

| Control | Type | Range / Options | Default | What it does |
|---|---|---|---|---|
| BPM | GainBias | 1 to 300 | 120.0 | Tempo; output frequency = BPM × 4 / 60 Hz |
| Run/Stop | Gate (toggle mode) | off / on | off | Toggles the clock. Either edge resets phase; the output is 0 while stopped |

**I/O**: Generator; no audio input used. Mono gate output copied to Out2 in
stereo. BPM and Run each have a CV branch.

---

## Constant Random

Mnemonic: CR · Category: Biome

A free-running random voltage source: it draws a new bipolar random target at
the Rate you set and slews to it over the Slew Time you set. With Slew Time at 0
it is a pure sample-and-hold; longer times turn it into a smooth wandering LFO.
Rate bottoms out at exactly 0 Hz, which pauses it: no new values are drawn and
the output holds still.

**Controls**

| Control | Type | Range / Options | Default | What it does |
|---|---|---|---|---|
| Rate | GainBias | 0 to 100 Hz (coarse step 0.1 Hz); DSP accepts up to 1000 Hz via CV | 5.0 Hz | How often a new random target is drawn. 0 = paused |
| Slew Time | GainBias | 0 s, then 0.003 s doubling to ~786 s (octave scaling) | 0.0 s | Glide time to each new target. 0 = hard jump, i.e. sample and hold |
| Level | GainBias | -1 to 1 | 0.5 | Output scale; 0.5 ≈ ±5 V, 1.0 ≈ ±10 V, negative inverts |

**I/O**: Generator; no input and no clock or trigger input, so timing is
internal only. Mono output copied to Out2 in stereo. Rate, Slew Time and
Level each have a CV branch.

**Changed in v2.8.0 (breaking)**: Slew was a unitless 0-1 amount; it is now a
slew *time in seconds*. Rate changed from a 0.01-100 Hz linear map with 1 Hz
coarse steps to 0-100 Hz with 0.1 Hz steps and a true 0 Hz pause, and Level went
from unipolar 0-1 to bipolar -1…1 defaulting to 0.5. Old patches will not
translate.

---

## Expo D

Mnemonic: ED · Category: Biome

A trigger-fired decay-only envelope. A rising trigger ramps up to the peak over
a fixed 2 ms shaped rise (an anti-click onset that also makes retriggers glide
rather than pop), then decays to zero over the Decay time. Curve continuously
morphs the decay contour from concave through linear to convex, defaulting to
fully exponential. Gate length is ignored: fire and forget. New in v2.8.0.

**Controls**

| Control | Type | Range / Options | Default | What it does |
|---|---|---|---|---|
| Trigger | Gate (gate mode) | gate | n/a | Rising edge restarts the envelope from its current level |
| Decay | GainBias | ADSR map, 0 to 99 s (DSP floors at 0.5 ms) | 0.2 s | Decay segment length |
| Curve | GainBias | -1 to 1 | 1.0 | Decay contour: -1 concave/log, 0 linear, +1 fully exponential |
| Level | GainBias | 0 to 1 | 1.0 | Output scale |

**I/O**: Generator; no audio input used. Mono envelope output copied to Out2 in
stereo. Trigger, Decay, Curve and Level each have a branch; the CV mod gains are
hardset to 0, so CV has to be dialed in deliberately.

---

## Expo AD

Mnemonic: EA · Category: Biome

A trigger-fired attack-decay envelope. A rising trigger runs a shaped attack
from the current level up to the peak, then a shaped decay back to zero; gate
length is ignored. Attack and decay each get their own bipolar curve control
morphing log through linear to exponential, both defaulting to fully
exponential. Retriggering mid-envelope glides up from wherever it was rather
than stepping. New in v2.8.0.

**Controls**

| Control | Type | Range / Options | Default | What it does |
|---|---|---|---|---|
| Trigger | Gate (gate mode) | gate | n/a | Rising edge restarts the attack from the current level |
| Attack | GainBias | ADSR map, 0 to 99 s (DSP floors at 0.5 ms) | 0.01 s | Attack segment length |
| Decay | GainBias | ADSR map, 0 to 99 s (DSP floors at 0.5 ms) | 0.2 s | Decay segment length |
| Attack Curve | GainBias | -1 to 1 | 1.0 | Attack contour: -1 concave/log, 0 linear, +1 fully exponential |
| Decay Curve | GainBias | -1 to 1 | 1.0 | Decay contour, same law |
| Level | GainBias | 0 to 1 | 1.0 | Output scale |

**I/O**: Generator; no audio input used. Mono envelope output copied to Out2 in
stereo. All six controls have branches, with CV mod gains hardset to 0.

<!-- VERIFICATION NOTES

Discrepancies vs README.md / release notes:

- README still lists Canals as a biome unit (line 249 and the 2.x release
  summary). Canals moved to spreadsheet in v2.6.1 and is absent from
  mods/biome/assets/toc.lua.
- README's unit table omits Fade Mixer 6, Fade Mixer 8, Expo D and Expo AD, all
  of which ship in this package as of v2.8.0.
- README describes Fade Mixer as "4-input crossfader with BranchMeter controls"
  and does not mention the smooth/snap menu option added in v2.8.0, nor that the
  unit sums its own chain input (In1) into the output alongside the crossfade
  result.
- README calls Quantoffset a "Quantizer with CV offset", which reads as if a
  patched-in pitch is quantized. The unit never connects its chain input; the
  only source is the Offset control and its branch.
- README describes Varishape Osc as "continuously variable sine/tri/saw/square/
  pulse, V/Oct, sync". There is no sine in the core (the morph is triangle → saw
  → square with variable pulse width), and sync is non-functional on both
  Varishape units: the branch exists but is not in any view and the DSP never
  reads the Sync inlet.
- README line 370 claims "dual DSP instances, shared params" for 94 Discont. In
  a stereo chain Mode and Amount are tied to both instances but Mix is tied only
  to the left one, so Mix affects the left channel only.
- README's Gated Slew line omits the Mode (up / both / down) control entirely.
- README's Integrator line omits the ±5 V output clip, which materially changes
  behaviour at high Rate.
- RELEASE-2.8.0.md line 107 refers to "the slew change above" for Constant
  Random, but the file has no breaking-changes section, so the reference
  dangles.
  The commit message for that release mentions two breaking changes; only the
  README records the Constant Random one.
- Neither the README nor any release note mentions that both Codescan units
  (Bletchley Park, Station X) auto-load testing/linux/libbiome.so as sample data
  on the emulator. On hardware they are silent / bypassed until a file is loaded
  from the menu.
- README calls Bletchley Park a "wavetable oscillator". There is no table
  interpolation or morphing; it plays one fixed 256-byte window as a single-cycle
  wave, and Scan moves the window's byte address.

Could not verify:

- The on-screen label text for Fade Mixer's input controls. FadeMuteMeter passes
  no description and inherits from the firmware's Unit.ViewControl.BranchMeter,
  which is not in this repo; documented as in1…inN from the button names.
- Exact numeric ranges of the firmware encoder maps (oscFreq, [-1,1], unit,
  ADSR, slewTimes). DSP-side clamps are documented where they exist.
- PSR's Quant Levels dial reaches 128 but the DSP only quantizes above 1, so 0
  and 1 are both "off". Undocumented anywhere else.
- Latch Filter hard-codes 48 kHz in its C++ rather than reading the global
  sample rate. Harmless on hardware, but the documented cutoff would be wrong at
  any other rate.
-->
