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

(Findings, fit, and validation appended below as the campaign completes.)
