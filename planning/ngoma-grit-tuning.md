# Ngoma grit depth re-fit (ngoma-grit-tuning)

Started 2026-07-23. Complaint (user, direct hardware A/B): Grit "has very little
effect until we pass 0.8 and the noise bed picks up; we don't get the noise
phase-mod up to 50% travel that occurs on the hardware." The mechanism (common-mode
noise FM, one shared lowpassed source, depth = kappa(grit)*fc) is settled - measured
in the 155-capture phase-domain campaign and confirmed at instruction level in the
firmware walk. This campaign re-fits the DEPTH: `kGritKappa[16]` /
`kGritDepthTrim` in mods/spreadsheet/DrumVoice.cpp.

Why a re-fit is warranted: the original kappa table was calibrated on captures made
BEFORE the rig tuning drift was discovered (pitch CC20 unsent, vol/eq/cycle unpinned),
with analysis bands placed at nominal fc. The shape campaign later measured that era
of the rig ~+0.75 st sharp - off-lobe for upper partials. Harness-side artifacts:
`capture_grit2.py`, `analyze_grit2.py`, `grit2lib.py`, `selftest_grit2.py`
(trinity-midi-harness repo; captures/data stay out of this repo).

## P0 - instrument validation (done, 2026-07-23)

Estimator self-test on synthetics with known ground truth (`selftest_grit2.py`),
per `feedback_validate_the_instrument_first`:

- **Recovery**: with correct band placement the per-partial IF estimator reads
  0.59-0.64 of a known 40 Hz-lowpassed common-mode deviation (the +/-bw analysis
  band low-passes the modulator; expected and repeatable). Floor 0.003 Hz.
  Consequence: absolute jitter numbers from this estimator are ~40% low ACROSS THE
  BOARD - so the fit compares hardware and model THROUGH THE SAME ESTIMATOR and
  drives kappa by the hw/model ratio, which cancels the bias.
- **Drift replay** (signal +0.75 st sharp, bands at nominal - how the original
  campaign measured): only 0.89-0.95 of the on-lobe estimate. So the tuning drift
  alone cost ~5-11% on the strong partials, NOT a large depth error by itself.
- **Suspicious constant**: shipped `kGritDepthTrim = 1.85` is close to 1/0.59.
  If the original calibration compared estimator-read hardware numbers against
  ANALYTIC model depth (not estimator-read model renders), it would have absorbed
  the estimator bandwidth bias into the trim - a candidate root cause to test
  against the new data. (Resolved in P1: it did not - see below.)
- **Common-mode check**: at dev 13 Hz the estimator reads 7.2-8.8 Hz uniformly
  across partials 55-1705 Hz. Matches the common-mode mechanism.

Hardware gate (`capture_grit2.py p0`, every CC pinned incl. pitch CC20=34):

| note | empirical fc | vs corpus nominal | grit-0 repeat corr | jitter floor |
|---|---|---|---|---|
| 48 | 126.26 | +41.8 cents | 0.996 | 0.37 Hz |
| 60 | 252.46 | +41.4 cents | 0.988 | 0.59 Hz |
| 84 | 1010.04 | +41.7 cents | 0.822 | 1.94 Hz |

- The rig sits a UNIFORM +41.5 cents sharp of the corpus reference even with CC20
  pinned (CC20 steps ~1 st, so 34 stays the nearest value). Handled by per-capture
  empirical fc on the hardware side and rendering the model AT that empirical fc.
- Note 84 is not repeatable at grit 0 (0.82; trigger-timing artifact growing with
  fc). Notes 48/60 carry the decorrelation constraint; note 84 depth-only, and its
  1.94 Hz floor is subtracted in quadrature (as are all per-note floors).
- Grit 0-16 sits at the floor and fully repeatable: dead zone confirmed (firmware
  threshold grit>0.1 = CC 12.7).

## P1 - hardware depth vs CC (capture campaign)

Design: note 60 x grit CC {0,8,16,20,24,28,32,36,40,44,48,56,64,72,80,88,96,104,
112,120,127} x 8 repeats; notes 48/84 x {24,32,48,64,96} x 5 repeats. Shape 30,
Character 25, Decay CC100, Time CC40, Sweep 0, Hold 0, Clipper CC8 (transparent),
EQ 64 (dead zone), Vol 127, Cycle 0, pitch CC20=34. Model side: ngoma-mirror
(compiles the shipped DrumVoice.cpp) rendered at the per-cell empirical fc, same
repeat counts, same estimator, same floor subtraction.

(Results below - filled in as the campaign lands.)
