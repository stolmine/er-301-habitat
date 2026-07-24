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

## P1 - hardware depth vs CC (capture campaign, done)

Design: note 60 x grit CC {0,8,16,20,24,28,32,36,40,44,48,56,64,72,80,88,96,104,
112,120,127} x 8 repeats; notes 48/84 x {24,32,48,64,96} x 5 repeats. Shape 30,
Character 25, Decay CC100, Time CC40, Sweep 0, Hold 0, Clipper CC8 (transparent),
EQ 64 (dead zone), Vol 127, Cycle 0, pitch CC20=34. Model side: ngoma-mirror
(compiles the shipped DrumVoice.cpp) rendered at the per-cell empirical fc, same
repeat counts, same estimator, same floor subtraction.

### Findings (note 60, floor-subtracted in-band jitter Hz / repeat corr)

Shipped model (40 Hz one-pole, 2.8.3.80 state) vs re-pinned hardware:

| CC | hw jit | ship jit | ratio | hw corr | ship corr |
|---|---|---|---|---|---|
| 20 | dead | 0.99 | leak | 0.989 | 0.939 |
| 24 | 2.70 | 1.96 | 1.38 | 0.798 | 0.782 |
| 32 | 7.32 | 5.64 | 1.30 | 0.302 | 0.302 |
| 36 | 10.35 | 7.63 | 1.36 | 0.296 | 0.215 |
| 44 | 13.15 | 11.07 | 1.19 | 0.192 | 0.184 |
| 48 | 13.49 | 12.85 | 1.05 | 0.191 | 0.172 |
| 64 | 10.58 | 12.02 | 0.88 | 0.159 | 0.155 |
| 80 | 11.86 | 8.30 | 1.43 | 0.393 | 0.196 |
| 96 | 2.95 | 2.73 | 1.08 | 0.964 | 0.631 |
| 112 | 2.80 | 2.59 | 1.08 | 0.963 | 0.703 |
| 120 | 10.80 | 8.02 | 1.35 | 0.589 | 0.496 |

1. **The old "peaks at CC56-64, falls by CC72-88" shape was wrong in detail**:
   the plateau (~10-13.5 Hz at note 60) extends through CC80; the collapse
   happens between CC80 and CC88, and CC120 is DEEP again (10.8 Hz). The
   drift-era campaign put the fall-off a full zone early.
2. **Depth deficit concentrated at CC24-44 (1.2-1.4x) and CC80 (1.4x)** - the
   exact region of the user complaint - while decorrelation matched. That
   combination is impossible for a matched-spectrum modulator and pointed at
   the modulator bandwidth.
3. **Modulator spectrum measured (note 84 IF-PSD, +/-150 Hz observable): the
   hardware deviation PSD is flat-to-rising through 320 Hz; the model's 40 Hz
   one-pole rolls off.** Firmware agrees: the PRNG sample is added to the phase
   increment fresh every sample - white FM, no lowpass exists.
4. **Trigger-locked wobble discovered (IF-track cross-hit correlation)**: at
   CC88-112 the hardware's frequency modulation is DETERMINISTIC - IF tracks
   correlate 0.94-0.97 across repeat hits (vs ~0.0 at CC24-32, 0.06 at CC80,
   0.16-0.18 at CC120-127). The ~2.9 Hz "jitter" read there is a repeatable
   wobble pattern, not noise, so it must NOT be used as a noise-FM depth
   target. This resolves the depth/decorr inconsistency the white modulator
   still had in that zone.

### Why the old fit was so far off

The 40 Hz one-pole was fitted to hit-to-hit decorrelation at the drift-era
campaign's under-read depth. A slow modulator decorrelates ~1/fm^2 harder per
Hz of deviation than a fast one, so reproducing the measured decorrelation with
a 40 Hz modulator forced total depth ~30-45x below the hardware's actual white
modulation - which is precisely "grit inaudible until the noise bed at 0.8".
The tuning drift itself only cost 5-11% (P0); the modulator-spectrum proxy was
the real damage, and the drift-era instrument could not have caught it (the
depth and decorr numbers it fitted to were mutually consistent for the wrong
spectrum).

## P2 - re-fit (shipped in DrumVoice.cpp, 2.8.3.81)

- **Modulator: white** (fresh draw per sample, no filter), matching the
  firmware injection. `kGritLpHz` deleted; `kGritDepthTrim` = 1.0 (semantics:
  kappa*fc IS the total deviation std in Hz).
- **kGritKappa forward-model fitted per node** (fit_grit2.py): hardware and
  model measured through the SAME estimator so its band-limit bias cancels;
  iterated to convergence because the estimator saturates near band-width
  deviations (a one-shot ratio scale is ill-conditioned there).
  - Nodes CC24-72: est-fit (random-FM zone). New values 0.33-1.55 (was
    0.0072-0.055 - the depth is ~25x the old table in total-std terms, which
    is the bandwidth trade above, NOT a 25x audible-loudness change).
  - CC80: corr-fit 0.58 (mixed zone; est target contaminated by partial lock).
  - CC88-112: corr-fit 0.10-0.17 (deterministic zone: random component set by
    measured decorrelation; the ~2 Hz trigger-locked wobble is not faked).
  - CC120: corr-fit 0.59.
- Per-sample kernel untouched (jitDev stays one block-common scalar broadcast).

## P3 - validation

Final model vs hardware (note 60): depth ratio 0.99-1.14 across the whole
random-FM zone (CC24-72, exact at 24/48/56/64/72); corr tracks throughout
(0.67/0.80 at 24, 0.23/0.30 at 32, 0.16-0.18 vs 0.15-0.21 at 44-72); corr
matched by construction at 80-120 (0.37/0.39, 0.90/0.92, 0.955/0.964,
0.55/0.59). Notes 48/84 (depth ∝ fc + estimator-bw cross-check): ratios
0.71-1.26 in the random zone, corr tracks (n84 g32: 0.115 vs 0.120). Band
energies across the throw track hardware with no noise-skirt overshoot (model
slightly conservative at g64).

Regression: A/B vs the shipped 2.8.3.80 mirror is **bit-identical at grit 0**
across character/shape/sweep/clipper/decay/hold/eq/comp/punch/attack points;
diverges only where grit engages. (The frozen-Tessera parity point set is no
longer a valid gate for Ngoma - stale since the shape/character/clipper
campaigns legitimately diverged the two units; noted 2026-07-23.)

Builds: linux + am335x clean, DrumVoice.o 0 suspect NEON hints, linux pkg
2.8.3.81 installed to ~/.od/rear/.

### Known residuals (documented, deliberate)

- **CC20 leak**: model reads 1.4 Hz jitter where hardware is dead - the 8-CC
  table grid cannot express the hardware's ~CC21 threshold. Corr 0.90 vs 0.99;
  audibly a whisper of texture at 16% travel.
- **CC88-112 deterministic wobble** (~2-3 Hz, trigger-locked) not modelled;
  the model is correspondingly ~2 Hz "cleaner" in-band there at matched
  decorrelation.
- **CC127**: hardware repeat corr 0.60 vs model 0.11 - the hardware's
  top-regime noise burst is partly repeatable. Pre-existing (shipped model
  identical); belongs to the >0.8 noise-bed regime kept as-is per scope.
- **n84 deterministic zone** (g96): model corr 0.66 vs hw 0.93 - one kappa
  cannot corr-match every fc in that zone since deviation ∝ fc but the
  hardware's random component there does not scale up with fc. Fitted at
  note 60 (drum-range primary).
- Hardware modulator PSD shows a mild RISING tilt vs our flat white; est+corr
  endpoints match, tilt not modelled.

### Hardware (device) checklist - needs the rig/user

1. Install spreadsheet-2.8.3.81 on the ER-301 (front SD install; version bump
   forces re-extract).
2. Insert Ngoma, trigger at default pitch: sweep Grit 0 -> 1 slowly. Expect:
   silent to ~0.15, audible noise phase-mod building from ~0.2, strong
   stochastic roughness through ~0.35-0.65 (peak "alive" zone), drier and
   more repeatable 0.7-0.9, noise bed + shortening tail above ~0.9. Compare
   against the Trinity side-by-side at 50% travel - this is the complaint
   under test.
3. CPU check on device (kernel unchanged; expect no delta).
4. Ear-check low pitches (voct -1/-2): deviation scales with fc by mechanism;
   confirm grit still reads at low tunings.
