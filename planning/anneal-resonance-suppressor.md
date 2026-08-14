# Design note: Anneal - dynamic resonance suppression

Status: design note / not started. Ledger item `anneal-resonance-suppressor`.

User request 2026-08-14: "something like soothe2."

Named **Anneal**: heating a metal to relieve internal stress and remove
hardness, without changing what it is. That is the effect.

Package: **spreadsheet**, with the other spectral and multiband work (Impasto,
Spectral Freeze's sibling machinery, Sediment).

## What soothe does

Per-bin dynamic EQ keyed to **spectral prominence**. For each bin, compare its
magnitude against a local average of its neighbours; wherever a bin sticks out of
its own neighbourhood, duck it, with attack and release. Resonances - the
ringing, honking, sibilant peaks that a static EQ can only remove at the cost of
removing the note as well - stick out transiently, so they get ducked only while
they are ringing. Everything else passes.

The controls that matter are Depth (how much reduction), Sharpness (how narrow a
peak has to be to count), Selectivity/Bias (which part of the spectrum is
scrutinised), and speed. Plus a delta/difference listen, which on a unit like
this is not a luxury: you cannot tune it without hearing what is being removed.

## The detector already exists in this repo

This is the reason the unit is cheap.

`mods/zaum/atoms/STFTSpectral.h` (Sujet's Space) already computes, per bin:

```
localRef[k] = ±8-bin boxcar of the spectrum
ratio       = bloomState[k] / (localRef[k] + eps)
promW       = clamp(1 - (ratio - 1) * kPromSlope)
```

That is soothe's detector, written, debugged and shipping on hardware. Sujet
uses `promW` to decide how much decorrelation phase a bin receives - tonal peaks
stay frontal, noise spreads. **Point the same ratio at a gain reduction instead
of a phase offset and you have the effect.**

So the novel work is not the detector. It is:

- per-bin attack and release on the gain (Sujet's use is instantaneous),
- a Sharpness control over the boxcar width (Sujet's ±8 bins is fixed),
- resynthesis, which Spectral Freeze already does.

Verify the exact code before relying on these line-level details; the summary
above came from a survey pass, not from a line-by-line read.

## Design notes

**Sharpness is the boxcar width.** A narrow reference window makes only very
narrow peaks look prominent; a wide one flags broad emphases too. Exposing the
window width is both the cheapest and the most legible way to give the control
soothe calls Sharpness.

**Reduction must be smooth across bins.** Ducking single bins hard produces the
classic spectral-processing artefact - a warbling, phasey residue. Smooth the
gain curve across neighbouring bins before applying it. Sujet's own anti-metallic
trick is instructive: it smooths its phase table with 6 boxcar passes so adjacent
bins get *correlated* values. The same reasoning applies to gain.

**Delta listen is a first-class control, not a debug aid.** It is how the unit
gets tuned. Ship it as a Mode option (Process / Delta), not buried.

**Bias/tilt over frequency.** Resonance suppression is almost always wanted more
in the upper-mid than in the bass, where "prominent bin" just means "the bass
note." A tilt on the detector, not on the audio, is the fix. Sujet's `mFreqWeight`
BQI curve is a ready-made shape to start from even though its motivation is
different.

## Controls

| control | notes |
|---|---|
| **Depth** | maximum gain reduction, CV |
| **Sharp** | reference-window width in bins |
| **Tilt** | detector frequency bias, bipolar |
| **Speed** | per-bin attack/release, one knob, two coefficients |
| **Mode** | option: Process / Delta |
| **Mix** | linear |

## Cautions

- **Latency.** STFT hop latency is unavoidable and must be documented. Unlike
  Assay this is an insert effect, so the latency is in the audio path.
- **`log2_poly`/`exp2_poly` from `util/neon_math.h`** for any dB-domain work; no
  libm in the loop.
- **Depth = 0 must be a bit-identical bypass**, which for an STFT unit means the
  analysis/resynthesis round trip has to be transparent on its own. Test that
  first, before any detector exists - if the round trip is not clean, nothing
  built on top of it will be.
- **`feedback_runtime_branched_dsp_dispatch`** for the Mode option.
- Relationship to `units-spectral-processing` (Spectral Mask + Spectral Gate):
  those are **sidechain-keyed or crossover-keyed** per-band processors. Anneal is
  **self-keyed** against a local spectral average. Genuinely different effects
  that happen to share a front end - which is the argument for
  `stft-frontend-atom`.

## Phases

1. **Round-trip transparency.** STFT in, STFT out, no processing. Prove
   bit-transparency (or measure and document the floor) before anything else.
2. **Detector.** Port Sujet's `localRef`/ratio, expose Sharp, and ship Delta mode
   immediately so the detector can be *heard* while it is being tuned.
3. **Gain reduction** with per-bin attack/release and cross-bin smoothing.
4. **Tilt, Depth, Mix.**
5. **Hardware.** A8 CPU - an STFT insert is the most expensive thing proposed in
   this batch, and it needs a real number early.
