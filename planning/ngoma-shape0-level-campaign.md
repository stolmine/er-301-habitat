# Ngoma shape-0 fundamental level campaign - mechanism, evidence, plan

Ledger: `ngoma-shape0-level-campaign`. Started 2026-07-23. Parent: `ngoma-character-campaign`
(done, 2.8.3.78). Firmware notes and the capture harness live OUTSIDE this repo
(~/Downloads/Trinity2_0_Firmware, ~/repos/trinity-midi-harness) and are cited, not copied in.

## The ask

At shape ~ 0 the two BLOCK parents collapse to the same frequency and the hardware sums
them COHERENTLY (measured 1.67x shape-0 fundamental vs the separated carrier, uniform
across Character). Our model's varied per-mode start phases (Tessera f774e02 anti-click
heritage) decorrelate the parents, attenuating the summed fundamental (0.44x) - shape-0
fundamental ~2.3x too quiet. The Character campaign patched the FOLD lanes with an
analytic coherence correction (crossfaded out by r = 0.15) but left the level structure
itself unmodeled. This campaign reads the real start-phase mechanism from the firmware
and replaces the patch.

## P0 - instrument validation (2026-07-23)

- Rig alive (Trinity MIDI + MOTU M4 via pw-record); descriptors2 self-test passes.
- Tuning re-pinned: empirical fc +67.0..+68.2 cents above nominal at n48/60/72
  (126.4 / 252.9 / 505.8 Hz), rep spread <= 1 cent, uniform across octaves. All numbers
  below use per-capture empirical fc.
- Repeatability at the collision zone itself (shape 0/6, char 0, transparent clipper):
  NCC 0.985-0.999 across 3 reps per cell; onset-referenced fundamental phase spread
  <= 0.08 rad. The voice is deterministic and the instrument is good.

## P1 - hardware measurements (fresh low-Shape grid, 42 captures)

captures/shape0/ + data/shape0_grid.jsonl in the harness repo. shape
{0,1,2,3,4,6,8,10,12,16,20,26,32,40,55} x char {0,80} at n60 + spot cells at n48/72,
clipper CC8 (transparent), grit 0, every CC pinned.

- Fundamental (5-120 ms window, empirical fc): a1 = 0.132-0.136 flat across shape 0-8
  (the r = 0 zone), dropping through s10-s12 to 0.080-0.081 flat for s16-55. Ratio
  shape-0 / shape-55 = 1.646-1.694 at char 0 AND 1.692 at char 80. The 1.67x is
  re-confirmed and character-independent.
- Separated family ratio aB/a1 = 0.63-0.69 (s16-55). Coherent aligned sum predicts
  1 + 0.63..0.69 = 1.63-1.69x; incoherent (power) sum predicts 1.19-1.21x; anti-phase
  0.31-0.37x. Only PHASE-ALIGNED start explains the measured 1.65-1.69x.
- Beat-phase proof at s12 (r*fc = 13.4 Hz): the fundamental's amplitude envelope has
  maxima at 70/150/225/300 ms and minima at 35/115/190/270 ms, with t = 0 in a maximum.
  Aligned start predicts max at 0 and first min at 37 ms. Measured. The parents reset
  with delta-phi ~ 0 every trigger.
- Onset (shape 0): waveform leaves zero smoothly (no step), reaches full amplitude
  within one cycle (3.4 ms to global peak at n60), HF(>4 kHz) energy fraction in the
  first 4 ms = 0.0000, identical to steady state. The hardware is click-free NOT because
  of phase variation or an attack ramp (firmware envelope jumps to 1.0 in one
  instruction, prior campaign) but because the oscillators start at a ZERO CROSSING:
  a sinusoid starting at its zero crossing has no value discontinuity, so an instant
  envelope produces no broadband splash. This is the mechanism that lets a model adopt
  aligned phases safely.

## P1 - firmware read (instruction level, cross-checked against the image bytes)

The trigger/phase-init path is IN-LINE in FUN_2400b1d8, gated per sample by the trigger
flag (param_9 = register r8, the known trigger arg). There is no separate note-on
function touching the phase accumulators anywhere in the 503-function corpus (the other
0x1b8/0x1bc/0x1e0 hits are different structs or float-index aliases; catalogued).

- Osc C (+0x1bc) and osc B (+0x1e0): on a trigger sample the accumulator RELOAD is
  skipped and the phase restarts from the constant DAT_2400b740, then advances one
  increment. Verified in the raw Thumb-2 (osc B site): 0x2400b4be vldr s14,=DAT_2400b740;
  0x2400b508 cmp r8,#0; 0x2400b50c bne (skip reload); 0x2400b50e vldr s14,[r4,#0x1e0];
  0x2400b512 vfma (phase += incr); 0x2400b53a vstr. Same pattern osc C; channel 1/2
  clones identical at their own struct offsets.
- DAT_2400b740 = 0x00000000 = 0.0f, read straight from the image bytes (also used as
  the zero fallback in division guards in the same function). BOTH parents hard-reset
  to phase 0.0 on every trigger. No ramp, no offset, no variation.
- Osc A (+0x1b8, the grit ring-term triangle) is NOT gated - it free-runs across
  triggers.
- The +0.25 quarter turn is applied ONLY to the table index (0x2400b542 vadd s15,s14,
  0.25 after the phase store). A plain sin table would make the first sample jump to
  +-full scale (a step - refuted by the measured smooth onset); the measured onset
  departs from zero going negative, which is exactly cos(2*pi*(phase+0.25)) =
  -sin(2*pi*phase). The table is cos-form and the effective oscillator is a NEGATIVE
  SINE STARTING AT ITS ZERO CROSSING.

So the two campaign questions have one answer: the hardware resets both parents
phase-aligned at 0.0 (the coherent 1.67x sum), and it is click-free because phase 0.0
is a zero crossing of the effective waveform - no value discontinuity, so the instant
envelope (no attack ramp, prior campaign) produces no broadband splash. The global
-sin polarity is an unobservable overall sign: every fold harmonic is odd, so the whole
voice flips together.

## P2 - implementation (DrumVoice, spreadsheet 2.8.3.79)

- Start phases: ALL lanes now start at logical phase 0 (= sine zero crossing, rising;
  stored +0.25 P5 polySine convention unchanged) plus the existing half-turn for
  negative signed fold-series coefficients. This replaces the varied 0.25+0.37*m
  spread (Tessera f774e02 anti-click heritage) AND the fold-lane kH[m]*parent-phase
  locking (n*0 = 0 now). Parents aligned; fold harmonics coherent; silenced cross
  lanes moot; the drive+limiter stage sees hardware-true alignment when it generates
  intermods.
- REMOVED: the 2.8.3.78 analytic coherence correction entirely (corr3/corr5/corrSolo,
  the cos(2*pi*n*0.37) constants, the r=0.15 crossfade). The mechanism replaces the
  patch; nothing is fitted.
- Trigger-time bake only; the per-sample NEON kernel is untouched.

## P3 - validation (ngoma-mirror A/B vs hardware; base78 binary preserved)

Instrument note: the mirror must be driven at the hardware capture point (sweep 0,
sweepTime 10.7 ms, decay 242 ms) and a non-clipping level - the first measurement
pass hit the wav rail (peak 1.55) and a default-sweep chirp corrupted windowed probes.
Both re-pinned before any number below.

- Shape-0 fundamental ratio (s0/s55, 5-120 ms window, transparent clipper):
  hardware 1.646 (char 0) / 1.692 (char 0.63); model BEFORE 0.729 (uniform);
  model AFTER 1.618 (uniform across char, matching the hardware's uniformity).
  Error -55% -> -2/-4%, zero fitted parameters. Cross-octave: n48 hw 1.667 /
  model 1.644; n72 hw 1.717 / model 1.592 (the n72 residual tracks the m1 family
  amp fit, not phases: like-for-like separated aB/a1 = model 0.615-0.626 vs hw
  0.590-0.679 across the throw - pre-existing fitted structure, untouched).
- Beat-phase (hardware, s12, r*fc = 13.4 Hz): fundamental envelope maxima at
  70/150/225/300 ms, minima at 35/115/190/270 ms, t=0 at a max - aligned-start
  prediction confirmed at the signal level, independent of the binary read.
- Onset: transparent-clipper onset HF(>4k) fraction 0.0000 in the first 4 ms for
  hardware AND model, before and after, at shape {0, .43, 1} x char {0, .63, 1};
  model waveform leaves zero smoothly (first sample ~0.04 of peak). The zero-crossing
  start IS the click-tamer; no attack ramp needed (default attack stays 0).
- Regression grid (16 cells: grit/decay/char/shape/clipper throws, new vs base78):
  band-fraction deltas <= 0.0084 everywhere (mostly <= 0.002). Waveform NCC vs base78
  is low/negative BY DESIGN - the inter-lane phase relationships are the change.
  Transparent-clipper fundamental tau byte-identical A/B (283 ms both).
- Aliasing/NaN spot check voct 4/5/6 at shape 1 char 1: clean, sane peaks.
- Static gates: both arches build, DrumVoice.o am335x 0 NEON suspect hints, no
  out-of-line graphic virtuals, linux pkg 2.8.3.79 installed to emu.

### Known aggravation (documented, out of scope, belongs to the drive calibration)

The model's clipper at high settings over-pins the shape-0 fundamental: fund tau at
shape 0 measures hw 446 ms (CC48) vs model base78 4.7 s / new 17 s at clipper=1
(565 ms -> 1.4 s at clipper 0.378). The voice level entering the stage is now
hardware-true (1.6x hotter coherent fundamental); the over-pinning is the documented
drive/threshold calibration open item (since Tessera 2.8.3.56, "drive 12 stretches
decay") that the hotter fundamental now aggravates at shape-0 x high-clipper. Fix
belongs in the limiter stage, not in faking the voice level back down. Clean default
(clipper 0) is unaffected. Similarly the clipper=1 onset HF (0.008 vs hw 0.000) is
the known generated-product overshoot, not a click (onset ~ body, <1% energy).

## Hardware checklist (needs the device + ears)

1. Insert Ngoma 2.8.3.79 on ER-301 hardware (am335x): no crash on insert, audio on
   trigger (trigger-time bake change only; kernel untouched - low risk, but verify).
2. EAR: Shape at 0, Clipper 0: the hit should now have the hardware's full-bodied
   fundamental (was ~2.3x quiet). Sweep Shape slowly from 0: level steps down
   ~4-5 dB through the bottom of the throw as the families separate - the hardware
   does exactly this (measured 0.132 -> 0.080 across s8-s16).
3. EAR: onset at Shape 0 / low Character, Clipper 0: must stay click-free (attack 0).
   Compare a fast retrigger too (phase reset mid-ring is a value step on BOTH
   hardware and model - should sound identical in kind).
4. EAR: high Clipper at Shape 0: expect a longer-sustaining, more compressed
   fundamental than 2.8.3.78 (the documented aggravation). Judge whether it reads
   as "more hardware" or "too pinned" - feeds the drive-calibration follow-up.
5. Regression spots: grit throw, decay throw, Character throw, sweep, EQ/comp/level,
   serialization round-trip.

## Open items

- Drive/threshold calibration of the clipper stage vs the now-hardware-true voice
  levels (fund tau over-pinning above; its own campaign).
- m1 (oscB) family amp fit reads 0.62 flat vs hardware 0.59-0.68 sloping down in r;
  worth folding into any future kAmpFit refit (would close most of the n72 -7%
  residual and the last ~2% at n60).
- Osc A free-run (grit ring term) is not modeled per-trigger (our noise machinery
  differs mechanically); no measured consequence at grit 0.
