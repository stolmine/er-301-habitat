# Breccia effect expansion - what is missing, measured

Status: **shipped** in spreadsheet 2.8.5.26 (2026-08-13). Five of six built;
SLEW and TAPESTOP not taken (TAPESTOP still a user call, see below).

Originally research (2026-08-13). Follows the World control (2.8.5.23), which
removed the reason to keep the effect list short: with four weight vectors, a
new effect no longer dilutes the pool, it concentrates into whichever world
wants it.

## Method

Two passes, both against the same source (110 Hz harmonic tone plus transient
hits every 130 ms), each effect modelled offline to match the shipping C++.

- Spectral / dynamic: crest factor, envelope tilt (first third over last third),
  spectral centroid, spectral flatness, inharmonicity (share of energy off the
  110 Hz grid).
- Temporal: envelope correlation with the dry slice, best-alignment waveform
  correlation, onset count.

Two descriptor families were needed. The first pass is spectral-heavy and
literally cannot see a permutation: SCATTER measures within noise of dry on
every spectral axis while being obviously audible. Anything judged on one
family alone would have been judged wrong.

`/tmp/brx/space.py`, `/tmp/brx/temporal.py`.

## What the shipping nine cover

| effect | crest | tilt | centroid | flat | inharm | envCorr | waveCorr | onsets |
|---|---|---|---|---|---|---|---|---|
| *dry* | 7.10 | 1.03 | 10750 | 0.769 | 0.854 | 1.00 | 1.00 | 47 |
| stutter | 4.93 | 1.02 | 9525 | 0.075 | 0.819 | 0.02 | 0.20 | 82 |
| reverse | 5.62 | 0.98 | 9800 | 0.704 | 0.833 | 0.03 | 0.45 | 59 |
| pitch | 6.49 | - | 9550 | 0.713 | 0.892 | 0.01 | 0.11 | 40 |
| step | 7.49 | 1.90 | 7952 | 0.635 | 0.869 | 0.23 | 0.27 | 47 |
| scrub | 5.15 | 1.16 | 7152 | 0.603 | 0.908 | -0.03 | 0.05 | 149 |
| crush | 7.19 | 1.03 | 10707 | 0.768 | 0.847 | 1.00 | 0.99 | 148 |
| chop | 9.81 | 1.18 | 9860 | 0.736 | 0.896 | 0.43 | 0.72 | 155 |
| filter | 3.35 | 1.02 | 648 | 0.006 | 0.317 | 0.89 | 0.78 | 15 |
| comb | 6.05 | 1.03 | 10750 | 0.667 | 0.858 | 0.40 | 0.74 | 239 |

(pitch tilt omitted: the offline model clamps at the slice end where the C++
wraps, so that one cell is a model artifact, not unit behavior.)

**Three holes.**

1. **Dynamics is untouched.** Every slice plays flat-gain. Tilt spans 0.98 to
   1.90 across the whole set; nothing deliberately shapes the amplitude
   envelope. This is the largest empty region and the cheapest to fill.
2. **Nothing adds inharmonic content.** Inharmonicity runs 0.82 to 0.91 against
   a dry 0.854, so the set only ever removes (filter, 0.317) or leaves alone.
   No effect makes the material metallic or bell-like.
3. **Filter is static.** One cutoff held for the slice. A moving cutoff is a
   different effect perceptually and reuses all the existing state.

## Candidates, measured

| candidate | crest | tilt | centroid | flat | inharm | envCorr | waveCorr | verdict |
|---|---|---|---|---|---|---|---|---|
| ENVELOPE perc | **19.19** | **798** | 10245 | 0.732 | 0.806 | 0.20 | 0.44 | **take** |
| ENVELOPE swell | 6.91 | **0.06** | 10703 | 0.766 | 0.842 | 0.41 | 0.74 | **take** |
| RINGMOD | 9.26 | 1.03 | 10090 | 0.748 | **0.949** | 0.97 | **0.02** | **take** |
| SCATTER | 7.10 | 1.06 | 9618 | 0.703 | 0.815 | -0.07 | 0.36 | **take** |
| FREEZE 20ms | 4.06 | 1.01 | 10465 | **0.0001** | 0.909 | -0.02 | 0.06 | **take** |
| FILTER SWEEP | 4.12 | 1.47 | 1632 | 0.036 | 0.526 | 0.74 | 0.77 | **take** |
| SLEW | 2.84 | 1.00 | 5325 | 0.472 | 0.633 | 0.95 | 0.95 | maybe |
| WAVEFOLD | 3.31 | 1.03 | 10777 | 0.758 | 0.844 | **1.00** | **0.99** | **reject** |
| SMEAR allpass | 4.70 | 0.93 | 10753 | 0.768 | 0.853 | 0.47 | 0.74 | **reject** |

### The two rejections are the useful part

**WAVEFOLD measures as almost exactly dry.** Centroid 10777 against 10750,
flatness 0.758 against 0.769, inharmonicity 0.844 against 0.854, waveform
correlation 0.99. At the drive where it stays musical it is doing nothing but
limiting, and at the drive where it is audible it lands on top of CRUSH. This
was near the top of the list on intuition and the measurement killed it.

**SMEAR (allpass diffusion) changes no measured spectral quantity at all** -
centroid, flatness and inharmonicity all match dry to three decimals, which is
what "phase scrambled, magnitude unchanged" means. It is not inaudible, but it
would cost a 430-sample delay line per layer (12 KB across 7 layers) to buy a
change nothing can see. Rejected on value, not on feasibility. Note that
`networkAllpassStep` already exists in Network.h if this is ever revisited.

### Why each acceptance earns its place

- **ENVELOPE** fills hole 1 outright: crest 19.19 and tilt 798 for percussive,
  tilt 0.06 for swell, against a shipping range of 0.98 to 1.90. One effect with
  a shape parameter covers swell through flat through percussive.
- **RINGMOD** fills hole 2 and has a signature nothing else in the set has:
  **envCorr 0.97 with waveCorr 0.02**. It destroys the waveform completely while
  preserving the amplitude envelope exactly, so it recolours without disturbing
  rhythm. Highest inharmonicity of all eighteen things measured.
- **SCATTER** is the unit's own premise applied one level down, and permutation
  is the one operation guaranteed to preserve the material. envCorr -0.07 with
  onsets unchanged (49 against a dry 47) is exactly the signature of a
  rearrangement.
- **FREEZE** is not a variant of STUTTER despite sharing its machinery: at a
  20 ms absolute window the flatness is **0.0001 against stutter's 0.075**, a
  factor of 750. Short enough and a repeat stops being a repeat and becomes a
  pitched drone at 1/window. Distinct effect, existing code path.
- **FILTER SWEEP** separates cleanly from static FILTER (centroid 1632 against
  648, tilt 1.47 against 1.02, envCorr 0.74 against 0.89) and needs no new
  state, only two precomputed `g` endpoints in `SliceFx` and a lerp. Recomputing
  `tan` per sample would be unaffordable; the lerp is the whole trick.

## External survey

The canonical slice-effect list is dblue Glitch's nine: TapeStop, Modulator,
Retrigger, Shuffler, Reverser, Crusher, Gater, Delay, Stretcher. Mapping:

| Glitch | Breccia |
|---|---|
| Retrigger / Reverser / Crusher / Gater | stutter / reverse / crush / chop |
| **Modulator** (FM-based) | **missing** - this is RINGMOD |
| **Shuffler** | **missing** - this is SCATTER |
| **TapeStop** | missing, see below |
| Delay | COMB is a short feedforward delay already |
| Stretcher | granular time-stretch, expensive; PITCH plus STUTTER approximates |

Two independent confirmations of candidates the measurement had already picked.
Worth noting the Shuffler is the effect users most often single out as a loss in
Glitch 2, which dropped it.

From the granular side, Nebulae v2 is noted for exposing grain window shape
(Gaussian, Blackman-Harris, Sawtooth, Bartlett) as a user parameter, which
sources call unusual because grain envelope is "very often missing as a
user-definable parameter". That is independent support for ENVELOPE being a real
and underexposed axis rather than a detail. Morphagene's Organize (cut a reel
into up to 99 pieces and rearrange) is Breccia's core premise, and its Morph
macro bundles density, spatialisation and random octaves-and-fifths, which is
close to what Glitch plus World now do together.

## TAPESTOP: flagged, not recommended

Deliberate rate ramp to zero. It is the one item on the canonical list with no
Breccia equivalent and it is cheap. **But** it directly contradicts earlier
direction: slewing pitch was rejected once already ("can we ensure this is
discrete?") and RAMP was replaced by STEP for that reason. A tape stop is a
recognisable gesture rather than an accidental portamento, so it may be
acceptable where RAMP was not. User call, not an agent call.

## Cost

All six acceptances stay inside the existing structure. Position-domain effects
(SCATTER, FREEZE, ENVELOPE) are pure arithmetic on `u` or on the output gain and
cost nothing measurable. FILTER SWEEP adds two floats to `SliceFx` and a lerp.

RINGMOD is the only one needing new per-layer state (one phase accumulator, 7
floats) and the only one needing a periodic function. It must **not** call
`sinf`: `feedback_package_trig_lut` records that runtime single-precision trig
from a package .so miscomputes on am335x, and `feedback_am335x_libm_sin_cost`
puts double `sin` at 300 to 500 ns. Use a polynomial approximation or a
construction-time LUT, as the crossfade ramps already do.

Two constraints carry forward from the current implementation:
- Every new effect needs its identity value set in `identityFx`. The `decim`
  bug came from exactly this gap, and the invariant harness exists to catch it.
- Each world row grows from 9 numbers to 15, and every row must still sum to
  0.60 or World starts changing density.

## Built (2.8.5.25)

Nine effects to fourteen. `kNumFx` 9 -> 14, world rows re-voiced, all four still
summing to 0.60, and the ordering re-solved: RHYTHMIC / DEGRADED / DIFFUSE /
TONAL remains the best monotone traversal with the endpoints the max-distance
pair (0.62).

Two implementation notes worth keeping.

**Sub-segment length became `sliceIn * segFrac + segAbs`,** replacing the old
`sliceIn / stutter`. Exactly one term is ever live: relative for STUTTER and
REVERSE, absolute for FREEZE. Summing rather than branching keeps identity
exact, since `sliceIn * 1.0 + 0.0 == sliceIn` bit-for-bit.

**FILTER SWEEP lerps the TPT coefficients rather than recomputing them.** An
exact sweep needs a divide per sample per layer (7 divides a sample at ~20
cycles on A8). Measured against exact recomputation across Q 1.2 to 8.2 and
four sweep shapes including full-range up and down: the lerped path is
**stable everywhere and consistently lower-peaking than exact** (worst 2.041
against 2.235 at Q 8.2). Lerping is the conservative choice here, not the risky
one. Static FILTER shares the path with zero deltas, so it costs three
multiply-adds instead of a divide.

**The envelope mapping had to be geometric.** A linear one shipped first and its
midpoint was not flat: it put the centre decay at 1.2 s, which is still -3.6 dB
across a 0.5 s slice, so "no envelope" was quietly a fade. Geometric puts the
centre at a 2 s decay. Known edge: at full swell against a 60 ms slice the
attack only reaches 0.20, because a 300 ms swell cannot complete in 60 ms. That
degrades to a ghost slice, which is acceptable, but it is the reason the attack
maximum is 300 ms and not the 1.2 s first tried.

Verification: `identityFx` covers all 39 `SliceFx` fields (mechanical check, the
gap that caused the `decim` bug), invariant harness still bit-identical at
Glitch 0, NEON hints clean, audio stack under 512 B, per-sample loop libm-free
(the `pow`/`sin`/`cos` in `process()` are the Size map at lines 539/546 and
`rebuildFade`, all ahead of the sample loop at 553).

## Not taken

- **SLEW** - measured usable (centroid 5325, flatness 0.47) but overlaps FILTER.
- **WAVEFOLD**, **SMEAR** - rejected on measurement, see above.
- **TAPESTOP** - still a user call.

## Next

A fifth world is now cheap and newly justified: dynamics (ENVELOPE) and
inharmonic content (RINGMOD) exist as families for the first time, so a
"CEREMONIAL" or "HAUNTED" row leaning on env swells plus ring plus sweep would
land somewhere none of the four currently reach.

## Sources

- https://illformed.com/ (dblue Glitch effect list)
- https://www.audiopluginsforfree.com/glitch/
- https://www.perfectcircuit.com/signal/microsound
- https://www.perfectcircuit.com/make-noise-morphagene.html
- https://equipboard.com/items/qu-bit-electronix-nebulae-v-2
- https://ok-instruments.com/ (Spill, six-effect slice vocabulary)


## Offset recalibration (2.8.5.26)

Reported symptom: negative Offset produces no glitch. The guessed cause was
negative constants leaking into 0..1 parameters. **That is not it** - `anchored()`
reflects at zero, so `p` is never negative and never outside 0..1, which was
verified when it was built (0% of the population pinned at either endpoint).

The actual cause. At Offset -1 the anchor is 0, so `p` lands in **[0, 0.3] with
mean 0.15**, and for several effects the low end of the parameter range WAS the
effect's null point. Offset was therefore acting as a stealth intensity control
on exactly the effects it is supposed to leave alone. Worst case: ENVELOPE
measured 0.071 spectrogram distance at **Offset 0**, i.e. dead at the default
knob position, because its flat point sat in the middle of the travel.

Seven ranges re-cut so both ends are audible and differ in CHARACTER:

| effect | was | now |
|---|---|---|
| ENVELOPE | flat at the midpoint | attack and decay move together: swell / hump / percussive, no null anywhere |
| STUTTER | floor 2 repeats | floor 3 |
| SCRUB | floor 0.35 cycles/slice | floor 1.5 (below 1 the playhead barely moves) |
| FILTER | ceiling 12 kHz | ceiling 5 kHz (a 12 kHz lowpass is bypass) |
| SWEEP | base to 3.6 kHz | base to 1.1 kHz |
| COMB | 20 samples at the low end | 1 ms to 25 ms, logarithmic, gain floor 0.6 |
| RINGMOD | floor 18 Hz, mix floor 0.45 | floor 80 Hz (below ~50 Hz it is tremolo, not sidebands), mix floor 0.7 |

Measured result: ENVELOPE departure from dry at Offset 0 went 0.182 -> 0.735,
and its weakest point anywhere on the knob 0.096 -> 0.326. STUTTER at -1 rose
0.83 -> 1.46, SWEEP at +1 0.44 -> 0.68, FILTER at +1 0.44 -> 0.55.

### Three metric failures worth remembering

This took four metrics because the first three each had a blind spot, and each
one would have produced a wrong decision on its own.

1. **Waveform correlation is normalized**, so it cannot see an amplitude swell
   at all, and a reversed PERIODIC tone still correlates with itself. It scored
   REVERSE at 12.6% departure and CRUSH as the worst offender. Both wrong.
2. **Spectrogram distance over-weights time structure.** Position-domain effects
   score 1.0 to 2.9, colouring effects 0.1 to 0.7, so it flagged COMB and
   RINGMOD as broken. A level-normalized long-term spectrum shows them at 4.35
   and 4.70 dB RMS against a reference of **4.51 dB for an entirely different
   slice of the source** - they were fine all along.
3. **Envelope tilt measures asymmetry**, so a symmetric hump reads as identical
   to flat. It put a false null at Offset -0.5. Departure-from-constant is the
   right axis for an amplitude effect.

General lesson: pick the metric from the effect's DOMAIN. Position effects want
time-structure metrics, colouring effects want level-normalized spectra,
amplitude effects want envelope-shape metrics. A single number across all three
families will always condemn two of them.
