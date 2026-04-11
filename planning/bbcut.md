# BBCut: Breakbeat Cutting Unit for ER-301

## Research Summary

Based on Nick Collins' BBCut/BBCut2 library and the Livecut VST C++ port. The core idea: a phrase/block/cut state machine that algorithmically slices and rearranges audio in real time, producing DnB-style breakbeat manipulation, stutter fills, and rhythmic fragmentation.

## Architecture

Three-level temporal hierarchy: **Phrase > Block > Cut**.

- **Phrase**: N bars (1-8). At phrase end, a new phrase length is chosen and the cycle restarts.
- **Block**: A contiguous group of cuts all reading from the same slice point. Block size is chosen by the active cut procedure. When a block is exhausted, the next block is scheduled.
- **Cut**: A single playback event within a block. Each cut has its own amplitude, pan, pitch shift, and duty cycle. Cuts within a block replay the same source window.

### Object Decomposition (from Livecut C++ port)

- **BBCutter**: Master scheduler. Owns phrase/block/unit counters, delegates to the active CutProc, drives the player.
- **CutProc** (abstract): Defines `ChooseCuts()` and `ChoosePhraseLength()`. Three concrete subclasses.
- **LivePlayer**: Per-sample audio engine. Holds recording buffer and cut playback state.
- **CutInfo struct**: Per-cut data (size, length, amp, pan, cents).

### Signal Flow

```
Audio In -> [circular recording buffer, always writing]
                |
                v
         [BBCutter state machine]
                |
          on each block: snapshot write position as slice origin
          on each cut: read from slice origin with offset
                |
                v
         [per-cut effects: amp, pan, pitch, fade envelope]
                |
                v
         [dry/wet mix] -> Audio Out
```

## Input Modes

**Live audio**: The Livecut model. A circular buffer records incoming audio continuously. When a new block starts, the current write position is snapshotted as the slice origin. First pass is always live passthrough; repeats replay the recorded window. This is the target for the ER-301 unit.

**Buffer playback**: BBCut2 in SuperCollider also supports pre-loaded buffers with beat-count metadata. Not needed for the ER-301 unit -- live input is the priority.

## Clock and Timing

Fundamental timing conversion:
```
SamplesPerBeat = (60.0 / tempo) * sampleRate
SamplesPerBar  = SamplesPerBeat * beatsPerBar
SamplesPerUnit = SamplesPerBar / subdiv
```

At 48kHz, 120 BPM, 4/4, subdiv=8: SamplesPerUnit = 12000 (one eighth note = 250ms).

**For the ER-301**: a sample counter advances in process(). When it crosses a unit boundary, call Unit(). External clock input (gate) can reset the counter to realign phase. Tempo changes and subdiv changes should only take effect at phrase boundaries to avoid mid-phrase incoherence.

## Three Cut Procedures

### CutProc11 (Classic DnB)

The original algorithm. Fills a phrase one block at a time.

**Normal path**: Pick an odd-numbered cut size (1, 3, 5... units via `2*floor(rand(0, subdiv/4) + 0.5) + 1`), pick random repeats (minrepeats to maxrepeats). Constraint loop ensures block fits remaining phrase.

**Stutter path**: Triggered when `unitsleft/subdiv < stutterarea` AND `rand() < stutterchance`. Picks a multiplier from `[1,2,3,4,6,8]` with weights `[0.4,0.3,0.1,0.1,0.05,0.05]`, creates rapid repeats filling the phrase tail. This is the "roll" or stutter fill.

Why odd-only cut sizes: cuts land on downbeats and off-beats in a way that is rhythmically coherent with typical 4/4 drum patterns.

### WarpCut (Warp Records Style)

Extremely fast iterated repeats with accelerating/decelerating timing. Three stochastic modes:

1. **Straight**: Single cut, no repetition.
2. **Regular**: Multiple repeats at equal size, linear interpolation of pan/amp/detune across repeats.
3. **Accel/Ritard**: Geometric shrinkage. Each repeat's size is `size_i = spu * T * accel^i` where `T = totalUnits * (1-accel) / (1-accel^repeats)`. Ritardchance reverses the array for deceleration.

Block sizes biased toward 1-2 units for very rapid stutter.

### SQPusher (Squarepusher Style)

13 pre-defined rhythmic templates with position-dependent probability. At certain quaver positions (weighted by `sqweights = {0.0, 0.3, 0.0, 0.5, 0.7, 0.8, 0.9, 0.6}`), scaled by activity parameter, decides whether to subdivide a beat into semiquavers or keep quavers. Fill triggered in final bar of phrase.

## Per-Cut Effects

| Effect | Range | Notes |
|--------|-------|-------|
| Amplitude | 0.0-1.0 | Random within min/max per cut |
| Pan | -1.0 to 1.0 | Constant-power stereo panning matrix |
| Pitch shift | -2400 to +2400 cents | Linear interpolation resampling, computed once per block |
| Duty cycle | 0.0-1.0 | Ratio of playback to cut duration (gate width) |
| Fade | 0-100ms | Exponential envelope on each cut for anti-click |

Stutter fills interpolate pan/amp/detune linearly across repeats within a block.

## Full Parameter Set

### Clock/Timing
| Parameter | Range | Default | Notes |
|-----------|-------|---------|-------|
| Tempo | BPM (or clock input) | 120 | External clock gate preferred |
| Subdiv | 6/8/12/16/24/32 | 8 | Grid resolution (8 = eighth notes) |
| Phrase length | 1-8 bars | 2 | Min/max for random selection |
| Time sig | 3/4, 4/4, 5/4, 7/8 | 4/4 | Numerator/denominator |

### Algorithm
| Parameter | Range | Default | Notes |
|-----------|-------|---------|-------|
| Cut procedure | 0-2 | 0 | CutProc11 / WarpCut / SQPusher |
| Min repeats | 0-4 | 0 | CutProc11: normal block repeat range |
| Max repeats | 0-4 | 2 | |
| Stutter chance | 0.0-1.0 | 0.5 | Probability of stutter at phrase tail |
| Stutter area | 0.0-1.0 | 0.5 | Fraction of phrase that enables stutter |
| Activity | 0.0-1.0 | 0.5 | SQPusher: density of semiquaver fills |
| Straight chance | 0.0-1.0 | 0.3 | WarpCut: prob of plain single cut |
| Regular chance | 0.0-1.0 | 0.3 | WarpCut: prob of equal-size repeats |
| Ritard chance | 0.0-1.0 | 0.5 | WarpCut: prob of reversing accel |
| Accel | 0.5-0.999 | 0.9 | WarpCut: geometric ratio |

### Per-Cut Effects
| Parameter | Range | Default | Notes |
|-----------|-------|---------|-------|
| Duty cycle | 0.0-1.0 | 1.0 | Gate width of each cut |
| Min/max amp | 0.0-1.0 | 0.8/1.0 | Amplitude range |
| Min/max pan | -1.0 to 1.0 | 0.0/0.0 | Pan range (0 = center) |
| Min/max pitch | -2400 to 2400 cents | 0/0 | Pitch shift range |
| Fade | 0-100ms | 5 | Anti-click envelope |

### Mix
| Parameter | Range | Default | Notes |
|-----------|-------|---------|-------|
| Mix | 0.0-1.0 | 1.0 | Dry/wet blend |

## CPU and Memory on Cortex-A8

**Very cheap.** Per-sample work: one buffer write + one buffer read + panning matrix (4 muls + 2 adds) + fade envelope. Cut decisions happen once per block boundary (integer math + a few random draws).

Pitch shift resampling is linear interpolation computed once per block transition, amortized across the block.

**Memory**: ~192KB mono for 2-bar buffer at 120 BPM/48kHz as float. Could use int16 to halve. Even float is fine at these buffer sizes.

No per-sample `pow()` or `exp()` calls needed on the hot path. Envelope can use one-pole IIR approximation.

## Key References

- [The BBCut Library (ICMC 2002)](https://composerprogrammer.com/research/bbcutlib.pdf) -- Nick Collins
- [Further Automatic Breakbeat Cutting Methods](https://papers.cumincad.org/data/works/att/ga0121.content.pdf) -- Collins
- [Livecut VST source (C++)](https://github.com/mdsp/Livecut) -- reference implementation
- [LiveCut DISTRHO port](https://github.com/eventual-recluse/LiveCut) -- modern build
- [BBCut2 SuperCollider quark](https://github.com/nhthn/BBCut)
- [bbcut2 homepage](https://composerprogrammer.com/bbcut2.html)
