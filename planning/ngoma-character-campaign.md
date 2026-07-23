# Ngoma Character campaign - mechanism, evidence, plan

Ledger: `ngoma-character-campaign`. Started 2026-07-23. Parent: `ngoma-shape-fm-campaign`
(done, 2.8.3.77). Firmware notes and the capture harness live OUTSIDE this repo
(~/Downloads/Trinity2_0_Firmware, ~/repos/trinity-midi-harness) and are cited, not copied in.

## The ask

Pin down Character's true mechanism from the firmware, then replace the painted
Character machinery (kFoldRaise / kGateFit fold terms / sineDip / kAmpFit foldN column)
with the generative law. The Shape campaign flagged harmC residuals (+-9..14 dB) as
this exact Character-side over-painting, and the firmware Character read had been
REJECTED once (85ea7e8) because it contradicted a hardware sweep.

## P0 - instrument validation (2026-07-23)

- Rig alive (MIDI port + MOTU M4 via pw-record), descriptors2 self-test passes.
- Tuning: fresh char-0 captures at n48/60/72, transparent clipper. Empirical fc runs
  +71/+73/+71 cents above nominal 243.2*2^((n-60)/12), rep-spread <= 1.7 cents.
  The drift is real, in-session stable, and uniform across octaves. Every number in
  this campaign uses per-capture empirical fc (parabolic FFT peak, +-3 st window);
  nominal fc is never trusted.
- Repeatability at grit 0, transparent output: NCC 0.978-0.999 across 3 reps at
  char {32, 80, 120}; harmonic-ratio CV 1-8%. The instrument is good.

## P1 - the firmware Character mechanism (READ, instruction level)

Hand-disassembled FUN_2400b1d8 header (0x2400b1d8-0x2400b36a) and both fold sites
(0x2400b620-0x2400b68a osc B, 0x2400b820-0x2400b866 osc C), Thumb-2 via objdump.
Ghidra's flag-soup decomp had hidden two things: the vselge branch senses, and a
vrinta (round-to-nearest) it mis-rendered as a "fold center" subtraction.

**Character is s1** (the previously-undetermined 7th BLOCK argument), and it does TWO
things, in sequence across the throw:

```
blend = clamp(2*char, 0, 1)      # vselge 0x2400b2fa: sine->triangle waveform morph
w     = clamp(2*char, 1, 2)      # vselge 0x2400b268 + 0x2400b308: wavefold drive
mix_X  = sine_X + blend*(tri_X - sine_X)          # per oscillator, same phase
fold_X = triwave(w*mix_X*0.25 + 0.25)             # both oscillators, same law
  where triwave(u) = (|u - round(u)| - 0.25)*4    # periodic triangle, range [-1,1]
```

- CC 0-64: the oscillators morph sine -> triangle (blend). No folding (w = 1 keeps
  the argument inside the linear window; output = w*mix exactly).
- CC 64-127: the (now triangle) waveform is progressively WAVEFOLDED (w: 1 -> 2).
  The fold onset is geometrically soft - reflections only start once w*|mix| > 1 -
  which is why the empirical fit read "dead until ~CC78": the old firmware read
  ("blend saturates at CC64, flat above") had reported only the blend half and
  missed w entirely. Contradiction resolved; the rejected read was incomplete,
  not wrong about what it saw.
- The fold preserves odd symmetry (f(-x) = -f(x)), so it generates ONLY odd
  harmonics of each parent: h3/h5/h7/h9 of fc from osc C, 3fB/5fB from osc B
  ((3,3) and (5,5) in lattice terms). The "fold-only modes" were never intermods.
- The decomp's "fold center = p4*0.5+0.5" on osc B is actually POST-fold AM
  (0x2400b686) - the pitch-envelope scaling the Shape campaign already models as
  onset bloom. Nothing subtracts inside the fold; there is no per-sample center.
- The folded family rides the parent's envelope, which is why the measured
  tau-ratios for the "tone" modes are 0.89-1.0.

## P1 - hardware confirmation (fresh sweep, 162 captures)

capture_character.py / analyze_character.py / fit_character.py in the harness repo.
Grid: char {0,16,...,127 x13} x note {48,60,72} x clipper {8 transparent, 48 corpus}
x shape {0, 55}, grit 0, every CC pinned, 3 reps at char {32,80,120}.

Transparent-clipper, shape 0 (the voice itself, no limiter):
- h2 = 0.000 at every point: odd symmetry confirmed.
- h3/h1: rises through the morph zone to ~0.09-0.10 at CC64, passes through a NULL
  at CC80 (0.025-0.030), then blooms to 0.51-0.61 at the top. The theory curve
  (zero free parameters) predicts the null at CC78-85 and the top at 0.65.
- h5/h1: peaks ~0.17-0.20 at CC112-120; h7/h1 peaks at CC88-96 then NULLS at
  CC112-120 (0.003-0.016 vs theory 0.000-0.004); h9 nulls near CC96. Every null
  and peak lands within a few CC of the zero-parameter prediction.
- Carrier absolute amplitude a1(char): measured non-monotonic dip-recover-fall
  (0.83 @CC64, 0.92 @CC80, 0.67 @top) matches theory within 1-2 percent at every
  point on all three notes - an independent observable the ratio fit never saw.
- oscB family at shape 55: a(fB)/a1 FLAT (0.62-0.63) across the entire Character
  throw - Character does not touch family balance. 3fB/fB and 5fB/fB track the
  same fold curves as h3/h1, h5/h1 (same null at 72-80, same bloom), offset
  exactly as predicted by the pitch droop below.

Calibration (fit over all notes and harmonics, log-domain, factor 1.42 typical
residual including the nulls):
- char_eff = min(cc/124, 0.970); blend = clamp(2*ce,0,1); w = 1 + (clamp(2*ce,1,2)-1)*0.95
- triangle softness beta = 0.85 (the 512-entry table's upper harmonics run ~85%
  of an ideal triangle's)
- pitch droop: fold ratios scale as (f_parent/253.6)^-0.128. This single exponent
  explains BOTH the per-note trend (h3@127: 0.603/0.548/0.505 at n48/60/72) AND
  the carrier-vs-oscB family offset at shape 55 (predicted 0.506, measured 0.503).
- top-of-throw saturation: CC120 and CC127 measure identical (char_eff ceiling).

## P2 - implementation (DrumVoice, spreadsheet 2.8.3.78)

Replace the painted Character machinery with the generative law, evaluated exactly
offline and baked as five 33-point signed LUTs over the knob throw (blend and w are
functions of ONE knob, so the harmonic curves are 1-D):

- kCharA1: parent-lane amplitude a1(char)/a1(corpus char 48) - applied to the
  carrier and oscB lanes (normalized at the corpus point so every fitted intercept
  keeps its operating point).
- kCharR3/R5/R7/R9: signed harmonic ratios a_n/a1 at the same char. Lane amp =
  |R_n| * parent amp * droop; sign < 0 becomes a half-turn phase offset. Phases are
  harmonic-locked to the parent lane (p_n = n*p_parent + sign offset) - the fold's
  products are coherent, not arbitrary.
- Lanes: m8 (3,0), m9 (5,0), m10 (7,0) from the carrier; m12 (3,3) from oscB; NEW
  m14 (9,0) and m15 (5,5) fill the two former padding lanes (NM 14 -> 16, the
  kernel's 4 quads exactly). 5fB measures -15.6 dB rel carrier at the top - louder
  than h7 - and h9 peaks audibly at the h9 bump near CC80.
- REMOVED: kFoldRaise, kFoldKill (all its targets were already silenced by
  kCrossPaint - dead code), sineDip, the kAmpFit foldN column (c2), the presence
  gates on m8/9/10 (kGated now true only for m11, itself a kCrossPaint-dead cross
  lane), and the fold/foldN block-rate laws.
- The voice now carries TRANSPARENT-output harmonic levels (e.g. h3 = 0.065 at the
  corpus char, not the post-limiter 0.25); the drive+limiter stage regenerates the
  rest at high Clipper exactly as the hardware's does - same generate-dont-paint
  architecture as the Shape campaign's cross lattice.

Deliberately NOT ported: per-sample folding of the lanes (the additive
representation of the folded waveform is exact for a per-trigger-constant char;
folding per-sample would buy only mid-hit knob response at real NEON cost).

## P3 - validation (measured, ngoma-mirror vs hardware + prev-source A/B)

- Hardware parity (model clipper 0 vs hardware CC8, n60 shape 0, 13 chars x 4
  harmonics): median +1.0 dB, mean |err| 1.8 dB above floor - from the painted
  table's +13 dB class errors to sub-2 dB generative. h3 exact at the corpus
  char (0.065/0.065), null tracked at CC80 (0.027/0.025), top 0.548/0.560.
  Residual: h7 reads ~2x hot at the very top (0.014 hw vs 0.027 model - small
  absolute, the h7-null region).
- oscB family (shape CC55): 3fB/fB 0.059/0.060, 0.194/0.188, 0.507/0.516 at
  cc 48/96/127; 5fB/fB 0.019/0.021, 0.114/0.132, 0.167/0.163. Essentially exact.
- COLLISION-ZONE FINDING: at shape ~ 0 both parents share fc, and the model's
  varied parent start phases (0.37-turn spread, anti-click, level-calibrated)
  attenuate the summed fundamental (factor 0.44) while fold harmonics sum near
  1.0 - inflating audible ratios 2.1x (+6.4 dB measured before the fix).
  Hardware's oscillators reset PHASE-ALIGNED: its shape-0 fundamental measures
  1.67x the separated carrier, uniform across the whole Character throw (4
  chars). Fixed with an analytic coherence correction on the fold lanes
  (computed from the model's own phases, crossfaded out by r = 0.15; NOT a
  fit). OPEN ITEM: the hardware's 1.67x shape-0 level structure itself remains
  unmodeled (the model's shape-0 fundamental is relatively too quiet by ~2.3x)
  - that is a Shape-side level-structure follow-up, separable from Character.
- Regression (new vs 2.8.3.77 source, mirror A/B, char at the corpus point):
  grit throw {0,0.35,0.65,0.85,0.95}, decay {0.05,0.5,1.5}, shape
  {0.2,0.6,1.0}, clipper {0.5,1.0}: NCC 0.976-0.999, band-fraction deltas
  <= 0.005 everywhere except the shape-1.0 cell (e_fund -0.125 / e_up +0.125),
  which is an IMPROVEMENT: hardware at that cell reads e_fund 0.65/e_up 0.29;
  prev model 0.93/0.07; new 0.84/0.15-0.17. decay_fund ratios 0.998-1.002.
- Headroom: default patch peak 0.989 (prev 0.926); worst corner over
  char x shape at level 0.8 dropped 1.96 -> 1.28 (the painted raises used to
  pile up at char 1.0). No level-policy change needed.
- Aliasing spot check at voct 4/5/6 (fc up to 7 kHz), shape 1 char 1: no NaN,
  sane peaks (lanes are pure sines; the range gate zeroes out-of-band lanes,
  m15 at 15x fc included).
- Static gates: both arches build, DrumVoice.o am335x 0 NEON suspect hints
  (kernel untouched - all changes are trigger-time bake), graphic virtuals OK,
  linux pkg 2.8.3.78 installed to emu.

## Hardware checklist (needs the device + ears)

1. Insert Ngoma 2.8.3.78+ on ER-301 hardware: no crash on insert, audio on
   trigger (bake-time-only changes + 2 new active lanes; kernel unchanged).
2. EAR: Character throw at Clipper 0, Shape 0: sine -> triangle warmth over the
   bottom half, then the fold blooms in with the h3 null "dip" near 60 percent
   and the octave-ish shimmer at the top. No static painted brightness.
3. EAR: Character x Clipper: at high Clipper the limiter should add its own
   harmonic heft on top of the fold family (hardware h3 reads 0.25 at CC48
   clipper even at char 0).
4. EAR: Character x Shape: at mid shape the oscB family folds identically
   ((3,3)/(5,5) shimmer); no h3 pile-up at high shape.
5. Regression spots: grit throw, decay throw, sweep, EQ/comp/level,
   serialization round-trip.
