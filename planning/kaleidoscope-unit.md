# Kaleidoscope slicer - design

Status: **design** (2026-08-12). User idea, architecture settled the same day.

Working name: **Millefiori** (the glass technique where a cane is SLICED to
reveal a radially symmetric cross-section - the metaphor is exact, and it sits
with Vitrail in the stained-glass corner of the catalog). Alternative: Rosace
(rose window). Name is not locked.

Related: supersedes the direction of `spectral-sort-unit` (Sediment), which is
parked for sounding "quite dirty". Overlaps the open `units-buffer-shuffler`
(BBCut-style slicing) - this is that item with a symmetry aesthetic instead of
an algorithmic-cutting one.

## 1. The brief

A meld of Manual Loops and a slice-based rearranger, driven by three macros:
window/slice duration, rearrangement, and pitch/spectral shift. The target
character is **kaleidoscope: ordered, symmetric, clean** - explicitly NOT the
dirty spectral sound of Sediment.

Source is a **card-loaded sample** (user call), so file handling is the
now-established pattern: attach from card or pool, slicing view, waveform,
`SamplePool.serializeSample`. References: `ManualLoops.lua` (which is
`libcore.LoopHead` with CV-modulatable **Loop Start** and **Loop Length**) and
`VariSpeed.lua`. Sediment's Lua already implements this pattern and can be
lifted wholesale.

## 2. Why this will be clean where Sediment is dirty

Sediment permutes FFT bins, which destroys harmonic relationships and
inter-frame phase coherence. The boiling is intrinsic to the method, not a
polish problem. Here:
- each slice is an **intact stretch of the source waveform**, so timbre survives
  and only the ordering changes;
- transposition is by **resampling**, which preserves wave shape exactly and is
  artifact-free, unlike granular or phase-vocoder shifting.

Clean by construction rather than by tuning.

## 3. The central design point: a kaleidoscope is not a shuffler

A shuffler takes N slices and rearranges them into N slots. That is a glitch box
and the catalog already has one (Larets, dblue-Glitch style). **A kaleidoscope
takes ONE small shard and reflects it into a large symmetric pattern.** Small
source, many ordered copies, mirror and rotational symmetry, and the whole
pattern transforms continuously as one control moves.

So the middle macro is **reflection count, not shuffle order**:

| macro | what it is |
|---|---|
| **Window** | the shard: Loop Start + Loop Length, a few ms to the whole phrase |
| **Fold** | how many times the shard is reflected to fill the loop, and with what symmetry |
| **Prism** | transposition spread ACROSS the folds - fold k transposed by k intervals |

Turn Fold up and one shard becomes eight mirrored copies. Turn Prism and they
fan into a chord. That is kaleidoscopic in a way a shuffle never is.

### Fold semantics: the loop PERIOD stays constant

Two readings were considered:
- (a) fixed frame: raising Fold shrinks the source shard to Length/N and plays
  it N times inside the SAME loop duration;
- (b) fixed shard: raising Fold appends repetitions and extends the pattern.

**Take (a).** It matches a real kaleidoscope (more mirrors = more repetitions of
a smaller wedge), and critically it **keeps the loop period constant so the unit
stays in time** as the macro moves. (b) would make the macro a tempo control by
accident.

Mirror symmetry comes from alternating direction: fold f plays **forward when f
is even, reversed when f is odd**. That is the literal mirror and it is free.

### Sequential, not layered, for v1

A real kaleidoscope shows every facet at once, which argues for layering the
folds. But the brief describes rearranging slices in time, and sequential folds
give a rhythmic kaleidoscope for the cost of ONE interpolated read per sample.
Layering would make it N voices and turn it into a shimmer/chord machine - a
genuinely different instrument. Ship sequential; note layering as a mode worth
auditioning later.

## 4. Transposition, and the duration problem it creates

Resampling a fold changes its duration, but each fold has a fixed time slot
(Length/N). So:
- **transposed UP** - the shard finishes early and **repeats inside its slot**
  (a natural stutter, musically useful);
- **transposed DOWN** - the shard is truncated at the slot boundary, which is
  ordinary sampler behavior.

Both are clean. Do NOT time-scale to fit, which would reintroduce exactly the
artifacts this design exists to avoid.

**Prism law.** Fold k is transposed by `k * interval`, with interval swept by
the macro. At 0 the unit is a pure mirror kaleidoscope with no pitch movement.
Consider an option to quantize the interval to semitones or to a scale - equal
intervals in pitch space are geometric in frequency, which is what makes the
fan sound ordered rather than detuned.

**Click control is mandatory for "clean":** a short equal-power crossfade (a few
ms) at every fold boundary, and the same at the loop wrap. Sediment's loop-wrap
gap is the cautionary tale - it was a measured 13 ms fade to -39 dB and it was
only found by measuring, not by listening.

## 5. Controls

| control | notes |
|---|---|
| Window (Loop Length) | the shard. CV. Use an octave/time map, not wide-linear (`feedback_rate_time_control_octave_maps`) |
| Start | loop start position. CV |
| Fold | 1..16 reflections. Discrete: adopt the discrete-stepping standard (`feedback_parammode_convention`) |
| Prism | interval per fold, 0 to +/-24 semitones. CV |
| Pattern (option) | mirror / rotate / palindrome - which symmetry the folds use |
| Trigger, Level | standard |

Mod gain defaults to 0 on everything (`feedback_mod_gain_default_zero`).

## 6. Feasibility

Trivial next to Sediment. Sequential folds mean **one interpolated read plus a
crossfade per output sample**, no FFT, no transcendentals on the hot path, and
state is a handful of scalars. This is a compute-light unit that should land in
low single-digit CPU without any optimization work.

Note it inherits none of Sediment's risk surface: no pffft, no resynthesis, and
therefore no exposure to whatever is behind the unexplained `kryos-load-hang`.

## 7. Build order

0. **Offline prototype in Python against a WAV.** Fold semantics, mirror
   alternation and the Prism law are SOUND questions; settle them by ear in an
   offline loop measured in minutes, not build cycles. This is the step that was
   right for Sediment too.
1. **Lift Sediment's Lua file handling verbatim** - attach/detach/pool/serialize
   is already written and emu-proven; only the controls differ.
2. C++ head: loop region, fold scheduler, resampled reads, crossfades.
3. Prism + Pattern.
4. Hardware CPU and the listening pass.
5. Optional: layered-folds mode.

## 8. Open

- Name (Millefiori vs Rosace vs something else).
- Whether Prism quantizes to a scale by default.
- Whether Fold should also offer non-integer values (crossfading between fold
  counts) or stay discrete. Discrete is simpler and probably more musical.

## 9. OFFLINE PROTOTYPE (2026-08-12) - the model is verified

`tools/kaleidoscope-proto/kaleido.py`. Renders WAVs to listen to and measures
the structure rather than assuming it. All MEASURED.

| claim | result |
|---|---|
| loop period stays constant as Fold rises | 4.000 s at folds = 1/2/3/4/6/8; only the shard shrinks (1000 ms down to 125 ms) |
| odd folds are the time-reverse of even (mirror) | `corr(fold0, reversed fold1) = +1.0000`, vs -0.85 unreversed |
| Prism law: fold k transposed by k * interval | at 7 st: measured +0.0 / +6.9 / +14.0 / +20.9 st. At 12 st: +0.0 / +12.0 / +24.2 / +36.2. Within 0.2 st |
| no clicks at fold boundaries | boundary step 0.41 against a 0.73 typical step, i.e. BELOW ordinary signal motion |

**Two findings worth carrying into the C++.**

1. **Mirror folds are inherently click-free at the seam.** Reflecting at the
   boundary means fold f ends on the same sample fold f+1 starts on, so a
   palindrome is continuous by construction. The crossfade is insurance for the
   transposed case, not the thing preventing clicks. Pleasant consequence: the
   pure-mirror sound (Prism = 0) is seamless for free.
2. **Crossfade default: 3 ms.** Equal-power level through the crossfade measures
   -0.24 dB at 3 ms and -0.30 dB at 10 ms, but **-1.05 dB at 1 ms** - too short
   to average against a low-frequency waveform period, so it dips. 3 ms is the
   knee.

**Measurement caution recorded because it nearly fooled me:** the first click
test used the decaying test phrase, where every fold boundary happens to land in
near-silence, so it reported "clean" at a 1-sample crossfade and the crossfade
length appeared to make no difference at all. Only re-running on SUSTAINED loud
material put real content at every boundary and made the test meaningful. When
testing seams, the material has to be loud exactly where the seam is.

Listening set at `/tmp/kal_*.wav`: dry window, fold 2/4/8 pure mirror, fold 4
with Prism 7 and 12, fold 8 with Prism 5, fold 6 with negative Prism, a short
shard, and a long phrase.
