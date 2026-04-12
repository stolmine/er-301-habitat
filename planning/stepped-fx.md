# Larets (Stepped Multi-Effect)

Clock-driven sequenced effect processor. Each step applies a different effect to the audio. Spreadsheet paradigm, dblue Glitch / Infiltrator inspired.

## Signal Flow

```
Audio In → Circular Buffer (continuous recording)
         → Active Effect (all primitives always running, crossfade on switch)
         → Wet/Dry Mix → Out
```

## Primitives

The entire effect engine is built from a small set of primitives:

1. **Circular buffer** (~1s at 48kHz = 48000 samples). Records continuously. Shared by all buffer-based effects.
2. **Variable-rate read head** — position, speed, direction, loop points. Covers stutter, reverse, tape stop, pitch shift.
3. **SVF filter** — single filter with sweepable cutoff. Covers filter sweep.
4. **Sample-and-hold** — covers downsample.
5. **Quantizer** — covers bitcrush. One line of math.
6. **Waveshaper/tanh** — covers distortion.
7. **VCA** — covers gate. Multiply by 0 or 1.

All primitives run continuously. On step transitions, crossfade between outgoing and incoming effect output to avoid clicks.

## Effect Types (10 + off)

| # | Effect | Param meaning |
|---|--------|---------------|
| 0 | Off | (ignored) |
| 1 | Stutter | Loop length within tick window |
| 2 | Reverse | Playback speed (0.5x–2x) |
| 3 | Bitcrush | Bit depth (1–16) |
| 4 | Downsample | Rate reduction factor |
| 5 | Filter sweep | Sweep range (cutoff start-to-end across step) |
| 6 | Pitch shift | Semitones |
| 7 | Tape stop | Deceleration curve |
| 8 | Gate | Gate shape (hard/fade) |
| 9 | Distortion | Drive amount |
| 10 | Buffer shuffle | Segment rearrangement density |

## Sub-Params (3 per step)

1. **Type** — effect type (0–10, off at default)
2. **Param** — meaning varies per type (see above)
3. **Ticks** — clock duration for this step (integer, >= 1)

## Plies (5)

1. **Clock** — input with sub-display: reset, division
2. **Step list** — navigator for up to 8 or 16 steps, sub-param editing on focus
3. **Overview** — sequence visualization (see below), sub-params: skew
4. **Xform** — gate-triggered transforms, typical spreadsheet xform menu
5. **Wet/dry** — MixControl with input level, output level, tanh saturation

## Overview Visualization

Two layers:

**Bottom: bar graph** — step sequence with bar widths proportional to tick count. Effect type shown as letter/icon per bar. Active step highlighted (bright), inactive steps dimmed.

**Top: morphing effect viz** — single animated visualization that changes form depending on the currently active effect and its param setting. Full width, sits above bar graph.

- Stutter: repeating waveform fragment
- Reverse: backwards waveform
- Bitcrush: staircase quantization
- Downsample: stepped/held waveform
- Filter sweep: moving resonant peak curve
- Pitch shift: stretched/compressed waveform
- Tape stop: decelerating waveform
- Gate: amplitude envelope
- Distortion: saturated waveform
- Buffer shuffle: rearranged segments
- Off: flat line or pass-through waveform

Viz morphs on step transitions. Only the active step's animation runs.

## Skew

Per-step clock multiplier curve. Each step's programmed tick count is multiplied by a weight derived from its position in the sequence and the skew value. At center: all weights = 1x (no change). Skewed left: early steps play slower (higher multiplier), late steps faster. Skewed right: opposite. Total sequence length changes, which is natural for a looping system. Non-destructive: programmed tick counts are always ground truth, skew is a multiplier on top. Lives on overview ply sub-display.

## Resolved

- Step count: 16 max
- Buffer length: 2s (96000 samples). Covers a quarter note down to 30 BPM.
- Crossfade duration: fixed 64 samples (~1.33ms at 48kHz)
- Filter: reuse DJ filter implementation from biome package
- Xform targets: effect type randomization, param randomization, tick randomization, rotate/reverse sequence
- Name: Larets
