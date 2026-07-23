# Ngoma grit drama: loudness + persistence campaign (ngoma-grit-noise-persistence)

Started 2026-07-23. Complaint: grit still feels much more dramatic on the Trinity
than on Ngoma even after the depth re-fit (2.8.3.81). Diagnosis (two unvalidated
axes; the FM DEPTH axis is fitted 0.99-1.14 and is NOT touched here):

1. **Persistence**: the noise bed's own envelope `noiseTau` is capped at 150 ms
   and hard-fixed at 60 ms above CC110, decoupled from Decay - grit dies fast.
2. **Loudness-vs-control**: the `noiseMix` law (DrumVoice.cpp) was never measured
   against hardware; the depth re-fit measured Hz deviation, not audible level.

Harness-side artifacts (trinity-midi-harness repo): `gritlvl.py` (descriptors +
known-truth self-test), `capture_gritlvl.py` (p0 gate + campaign),
`analyze_gritlvl.py` (mirror-vs-hardware comparison). Captures/data stay there.

## Method

Mechanism-agnostic ENERGY descriptors, applied identically to hardware captures
and ngoma-mirror renders (bias cancels in the comparison):

- comb split: lattice-lane comb mask (+/-32 Hz around every (h,k) lane at the
  per-capture empirical fc/r) -> tonal vs residual ("noise") energy, 60 Hz-16 kHz.
- level law: baseline-subtracted added noise energy, each rig normalized to its
  own grit-0 total energy (capture gain / mirror level cancel), in dB.
- persistence: hi-band (3-10 kHz) frame energy envelope -> amplitude tau via
  dB-slope fit between peak-2dB and floor+8dB.

Self-test on synthetics with known truth (P0, PASS): tau recovery +6.9/+1.5/
+0.4/+0.0% at 60/150/400/1000 ms; baseline-subtracted energy scales x3.79/x3.99
for a known x4; comb leak -29 dB; no fake bed tau on modal-only signal.

## P0 rig gate (2026-07-23, PASS)

- chain floor -91 dBFS idle; hi-band frame floor recorded and used in tau fits.
- tuning: +45.5 cents sharp uniform (n48/n60), every CC pinned incl. pitch CC20;
  handled by per-capture empirical fc (frozen to the note's grit-0 tuning above
  CC 92 where mix-out can bury the carrier).
- repeatability: e_resid cv 3-4%, tau_hi spread ~1% (4 reps, grit 0 and 112).
- early tell: hardware n60 g112 d100 tau_hi 318 ms where the model pins the bed
  at 60 ms.

## Campaign design

- LEVEL: note 60, decay CC100, grit {0,8,...,88,92,96,100,104,108,112,116,120,
  124,127} x4 reps (dense above 92 where the noiseMix law lives).
- PERSISTENCE: note 60, grit {96,104,112,120,127} x decay CC {40,72,100,116}
  x3 reps, tails 1.6-4.6 s by decay; plus grit 48 x decay {40,100} as the
  mid-throw FM-smear control.
- PITCH: note 48, grit {0,48,96,112,120,127} x3.

Decay CC -> tau via the Tessera law (96/402/1160/1963 ms at CC 40/72/100/116);
model side rendered by ngoma-mirror at matched dials, anchor fc, same hit counts.

## P1 findings (53 cells, 2026-07-23)

### Persistence: the bed envelope tracks Decay ONLY
hi-band amp tau vs decay CC (tauC 96/402/1160/1963 ms): **37/105/252/412 ms**,
IDENTICAL across grit CC 96/104/112/120/127 (spread < 5%). No 150 ms ceiling,
no 60 ms fix, no grit shortening - sublinear in tauC (~tauC^0.8). The manual's
"envelope gets even shorter" is the VOICE gate, not the bed.

### Loudness: bed is a single flat plateau; the "step" is the voice dying
- hi-band bed energy: flat **-26.5 dB** re grit-0 total from CC88 through 127
  (ramping in from ~CC64). No bed step at CC114-116.
- tonal energy: two flat plateaus - **-7.25 dB across CC92-112** and **-20.5 dB
  across CC116-127** (the audible "just noise" step = voice kill).
- carrier-band decay: gate engages ~CC72 (441 ms), saturates ~70 ms by CC88;
  head shape = cubed linear ramp, duration ~400 ms (matches the firmware's
  20..400 ms clamp).
- pitch: bed level scales mildly with fc (n48 sits ~-1 to -3 dB relative);
  persistence pitch-independent.
- mid-throw (CC24-48): added noise energy matches the model within 1.3 dB -
  the FM depth re-fit carries the loudness there. CC56-72 hardware has a
  smear-shape residual the white modulator cannot express (documented below).

### Firmware mechanism (FUN_2400b1d8 decoded at instruction level)
The mix-out is a dedicated GATE env on the oscillator term: init 1.0/trigger,
linear decrement rate/(dt*T), sustain floor, env^3 in audio; rate = 4*(g-0.5)
clamp [0,1], floor = 1-rate, T = decay clamped [20,400] ms. The "bed" is the
FM'd oscillator mixed in at a STATIC grit gain ((g-0.5)*3.25 saturating 0.8125
at g=0.75) with NO envelope of its own - it dies under the main decay VCA.
Both measured laws (decay-only persistence, flat bed level) drop out of this.
The old tauEff collapse misattributed the gate's T-ramp to per-mode decay.

## P2 fit (DrumVoice.cpp, 2.8.3.82)

- **Voice gate** (replaces the (1-noiseMix*0.5) trim AND the tauEff modal
  collapse): per-sample gateEnv linear decrement toward floor, cubed into the
  audio path. rate/floor per firmware; T = min(tauC, 400 ms), stepping to
  kGateTopMs = 20 ms at kGateTopCC = 114 (the measured voice-kill step; the
  burst threshold moves from 115 to the same constant).
- **Bed**: bedAmp = kBedFlat 0.447 flat from CC88 (linear ramp from CC64),
  pitch factor (fc/246.5)^0.30 baked per trigger; blend becomes
  `y = y*gate^3 + noiseLp*(bedAmp*bedPitch*noiseEnv + burst*0.12)`.
- **noiseTau = 0.88 * tauC^0.809 ms** - decay-tracked, grit-independent
  (deletes the 150 ms cap and the 60 ms fix).
- **kGritKappa nodes 10-15 re-converged** under the new structure: the CC80-120
  nodes were CORR-fitted against the old no-gate/no-bed model, which is a
  structural mis-calibration once the gate truncates the voice and the bed
  decorrelates hits. Node CC80 corr-fit 0.86 (model 0.44 vs hw 0.39); CC88-112
  fit to the bed's corr ceiling 0.94 (hw 0.90-0.96); node CC120 = 0.156
  (est/corr both bed-saturated there; value pinned for a sane CC127
  extrapolation, jit 4.3 vs hw 5.2). The est-fitted random zone CC24-72 is
  UNTOUCHED (verified ratio 0.91-1.00 post-change).

## P3 validation (model vs hardware, same estimator both sides)

- Persistence: tau ratio **0.92-1.11** across the full grit x decay grid
  (30 cells; was 0.15-2.6 before the fix).
- Bed level: plateau within **+-0.9 dB** of hardware across CC88-112; top
  regime (116+) resid flat -25.3 vs hw bed flat (by hi-band) ~1 dB high.
- Carrier-band head envelope overlays hardware at g104
  (-2.2/-6.6/-18.1 vs -2.4/-7.8/-20.2 dB at 50/100/200 ms).
- Pitch check (n48): bed level within +-1 dB, tau 264 vs 250-256 ms.
- FM depth: est-fitted zone ratio 1.00/1.00/1.00/0.98/1.00/0.91 at CC
  24/32/48/56/64/72; corr tracks (0.72/0.24/0.16 vs hw 0.80/0.30/0.19).
- Grit-0 A/B vs 2.8.3.81 mirror: **bit-identical** across 8 cases spanning
  character/shape/decay/clipper/sweep/punch/eq/comp/hold/voct AND the whole
  sub-onset grit throw (dial 0.1/0.25/0.4/0.5). New behavior begins only above
  dial ~0.5 (CC64), where the measured gate/bed live.
- Builds: linux + am335x clean, DrumVoice.o 0 suspect NEON hints,
  spreadsheet-2.8.3.82 linux pkg installed to ~/.od/rear/.

### Known residuals (documented, deliberate)

- **CC56-72 smear shape**: hardware shows more broadband smear at 56-64
  (-5.4/-7.2 vs model -10.4) and a sharp smear collapse at 72 (-24.3 vs -15.1)
  - the modulator's spectral tilt / partial trigger-lock in the transition
  zone, invisible to the in-band depth estimator that kappa is fitted through.
  Matching it would mean touching the depth fit; out of scope.
- **Tonal plateau ratios** read -11.0 vs hw -7.25 (92-112) and -23.5 vs -20.5
  (116+), but this is a normalization artifact: each rig is normalized to its
  own grit-0 tonal energy and the model's grit-0 carrier decays ~1.5x slower
  through this window estimator (pre-existing decay-shape difference, cubed
  ramp vs the hardware's straighter dB slope). The ABSOLUTE gated heads
  overlay (see P3); the gate is calibrated to the absolute shape, not the
  polluted ratio.
- **Top-regime corr**: model 0.40 vs hw 0.59-0.60 at CC120-127 - the
  hardware's partly-deterministic top-regime hits (trigger-locked component)
  are not modelled; the random bed caps model corr at ~0.40. Improved from
  the 0.11 shipped in 2.8.3.81.
- CC20 leak and CC88-112 deterministic wobble: unchanged from the depth
  campaign (see planning/ngoma-grit-tuning.md).

### Hardware (device) checklist - needs the rig/user

1. Install spreadsheet-2.8.3.82 on the ER-301 (front SD; version bump forces
   re-extract).
2. Insert Ngoma, default pitch, sweep Grit slowly 0 -> 1 against the Trinity:
   - 0.5-0.7: voice starts dying faster + a bright noise bed fades in under it
     (both new; the old model had silence here beyond the FM roughness).
   - 0.7-0.9: short gated thump + sustained bed; bed tail now TRACKS the Decay
     knob (turn Decay while hammering hits - the grit tail must follow).
   - above ~0.9: voice kill step to "just noise" + punch burst; the noise tail
     still tracks Decay (the old model pinned it at 60 ms).
3. Low pitches (voct -1/-2): bed level drops mildly with pitch (measured law);
   confirm grit still reads.
4. CPU check: one extra per-sample multiply-add pair in the scalar path
   (gate + bed add); expect no measurable delta. Kernel untouched.
