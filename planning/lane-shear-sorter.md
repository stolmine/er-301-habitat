# Lane-shear sorter - filterbank pixel sorting for audio

Status: **design + falsification run** (2026-08-13). Supersedes the direction of
both earlier attempts. Design is the user's; the measurements are mine.

## 1. What the first two attempts got wrong

Pixel sorting's characteristic offset comes from **independently permuting many
parallel, highly correlated lanes**. Adjacent rows are nearly identical, so
differential displacement shears a formerly-vertical edge into a ragged one.
That is the entire visual signature.

- **Sediment** (spectral bin sort) permuted lanes AGAINST EACH OTHER at
  boundaries aligned across the whole spectrum. Rhythmic windowing aligns
  interval boundaries across every lane, so everything displaces together and
  there is no tear.
- **Millefiori** (slice / repitch / repeat) **violates the permutation
  invariant**. Sorting never alters pixel values; the multiset is preserved
  exactly. Repitching makes it a different image.

Worth recording: the spectral-sort design doc DID contain the right shape as its
"time axis" option - each band's envelope reordering independently. The MVP
shipped the frequency axis because it needed no analysis matrix, i.e. the wrong
half of a document that already knew better.

## 2. The faithful mapping

- **Lanes = frequency bands (8-24). Filterbank, NOT FFT.** Within a band the
  signal is narrowband, so splices land at band-local zero crossings and the
  click problem mostly evaporates. FFT drags in phase management for no gain.
- **Sort axis = time**, over segments of ~10-30 ms.
- **Key = segment RMS** (the brightness analog). Ascending sort turns an
  interval into a monotone swell: that is the smear.
- **Intervals are content-derived and PER BAND**: threshold on band RMS or flux,
  runs bounded by segments that fail it. Sub-threshold regions pass through
  completely untouched. **The alternation between melted and intact is what
  makes pixel sorting legible**; a uniform grid gives none of it.
- **Permutation only.** No gain, no pitch, no repetition. Per-band long-term
  spectrum invariant across the interval; only arrival time changes. Audibly: a
  transient's attack lands at a different moment in each band, and the source
  snaps back into focus in the intact regions.
- **Latency** is bounded by max interval length - you cannot sort until the run
  closes. Fixed lookahead, cap intervals to it.

Granular is the right substrate, but **the scheduler has to be a sort, not a
cloud**. Stochastic scattering is the opposite of what makes this read.

## 3. Two variants worth having

- **Streaming**: fixed per-band delay of L ms, emit in rank order from a running
  window. No interval detection, always on, lands squarely on the
  sorting-network work.
- **Imprinting**: derive the mask and rank order from input B, apply the
  resulting permutation to A. Order transfer rather than spectral transfer -
  much closer to "sort image X with mask from image Y" than anything spectral.

## 4. Falsification result (2026-08-13)

Per the brief: skip sorting entirely, circularly rotate each band by an amount
proportional to band index. Pure shear. `tools/shear-proto/shear.py`.

**Confirmed by measurement:**
- The operation is a **true permutation per lane** - deviation from an exact
  circular rotation is 0.00e+00, by construction. No gain, pitch or repetition.
- The filterbank **reconstructs exactly** at zero shift: **-329 dB**. This
  matters rather than being a nicety - sub-threshold regions cannot pass through
  "untouched" if the bank colors them. Independent Butterworth bandpasses do NOT
  sum flat (measured -27 dB, ~4% error); building bands as DIFFERENCES OF
  LOWPASSES telescopes and sums exactly.

**The real finding - crossover phase interaction sets a band-count budget.**
When bands are differentially shifted, their overlapping skirts recombine at new
relative phases and comb. It scales with the number of crossovers, and steeper
filters cure it:

| bands | order 2 | order 4 | order 8 | order 12 |
|---|---|---|---|---|
| 8  | -1.64 / 11.8 dB | -0.75 / 7.3 dB | -0.30 / 5.1 dB | -0.19 / 4.2 dB |
| 16 | -5.31 / 31.3 dB | -2.25 / 13.0 dB | -0.88 / 8.0 dB | -0.47 / 6.3 dB |
| 24 | -8.99 / 34.2 dB | -4.25 / 20.5 dB | -1.70 / 10.5 dB | -0.94 / 8.4 dB |

(median / 95th-percentile spectral deviation, 60 ms max shift.)

So **order 8 is the knee**. Habitat's existing LR4 crossover (Impasto) is 4th
order and would be marginal at 16+ bands. Note the comb only appears where bands
are differentially displaced, so with content-derived per-band intervals it is a
dynamic, partial coloration of the MELTED regions - the intact regions are
genuinely intact.

**What I could NOT establish numerically:** whether this produces the perceptual
dislocation. A transient-smearing metric (80%-energy width) came out at
0.97-1.07x source and is useless - the window spans the whole decay, so a 60 ms
differential barely moves it. The verdict is a listening one, which is what the
renders are for.

Renders: `/tmp/shear_00_source.wav`, `/tmp/shear_n{8,16,24}_60ms.wav`,
`/tmp/shear_n16_60ms_steep.wav` (order 12), plus a 12-variant sweep over band
count / shift / linear-vs-alternating.

## 5. If the shear reads

Build order: filterbank (order 8+) -> per-band segmentation at ~10-30 ms ->
per-band RMS key -> content-derived interval detection -> rank-order emission
with bounded lookahead. Streaming variant first; it needs no interval detection
and is the smaller thing to get right.

## 6. If it does not

Then the problem is elsewhere and the sort will not rescue it. Do not build the
sorter.

## 7. DENSE COLLAGE is the target material, and it changes the mask (2026-08-13)

User correction: the goal is **a dense collage, not hits**. The first
falsification used 12 discrete percussive hits with silence between them, which
is the wrong regime - a pixel-sorted image is dense (photographs, not dots on
black), and the characteristic smear comes from CONTINUOUSLY PRESENT correlated
content. Re-run on synthesized dense collage (harmonic bed + noise beds with
wandering formants + a 900-grain cloud + slow swells): **band occupancy is 100%
of the time in all 16 bands**.

**The load-bearing finding: an ABSOLUTE RMS threshold does not work on dense
material.** Runs never close, so every interval hits the lookahead cap and the
structure degenerates into the uniform grid that the design explicitly rejects.
Measured per band over 20 ms segments, against a 500 ms cap (25 segments):

| mask | melted | median run | max run | verdict |
|---|---|---|---|---|
| absolute RMS > 20% of peak | 89% | 190 seg (3.8 s) | 213 | GRID - never closes |
| absolute RMS > 50% of peak | 53% | 17 seg | 109 (2.2 s) | still blows the cap |
| RMS > 1.0x local mean (1 s) | 51% | 2.2 seg | 20 | bounded, runs too short to sort |
| **RMS > 1.2x local mean (1 s)** | **20%** | **5.8 seg** | **12.3** | **use this** |
| spectral flux > median | 50% | 1.8 seg | 9.1 | bounded, but ~2 elements per run |

**Recommendation: a RELATIVE mask - band RMS against a ~1 s local moving average,
threshold ~1.2x.** That gives ~20% melted, runs of 6-12 segments (120-250 ms):
content-derived, comfortably inside any sane lookahead, and long enough that the
sort has something to permute. Flux bounds well too but leaves ~2 elements per
run, which is not enough for a rank ordering to read.

Coloration on dense material matches the sparse case: 16 bands at order 4 gives
-2.54 dB median, order 8 gives -0.79 dB. Order 8 remains the knee.

Renders: `/tmp/coll_00_source.wav`, `/tmp/coll_n{8,16,24}_60ms.wav`,
`/tmp/coll_n16_25ms.wav`, `/tmp/coll_n16_60ms_steep.wav`.
