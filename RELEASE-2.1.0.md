# er-301-habitat v2.1.0

Release date: 2026-04-12

Requires firmware: v0.7.0-txo (er-301-stolmine)

17 commits since v2.0.1. Focused pass on Larets and Helicase; spreadsheet-only release.

## Larets (spreadsheet) - stepped multi-effect overhaul

### DSP

- **CPR single-band compressor** behind a single-knob macro. Giannoulis/Massberg/Reiss feedforward kernel adapted from Impasto. dB-domain gain computation, separate rise/fall coefficients. At max: -40 dB threshold, 20:1 ratio, 1 ms attack -- actual limiter territory.
- **Auto-makeup toggle** on an `od::Option`, exposed on the new Mix ply sub-display alongside output level and comp amount.
- **Grain pitch shift** replaces the old variable-read-rate pitch effect. Two-grain Dattorro-style overlap shifter, sin^2 windows, 180 deg phase offset. Adapted from the SDK's MonoGrainDelay idea.
- **Hard-clip distortion** replaces the tanh soft-knee. Drive range 1-20, hard-clip against a unit ceiling, makeup `1/drive^0.7` so the step sits slightly louder than bare input at max drive. Visibly squared-off waveform in the overview viz.
- **Clock-locked stutter**: snapshot of writePos at step start, loops a musical fraction (1/16, 1/8, 1/4, 1/2, 1 tick) of the clock period selected by param. Reads as beat repeat, not a buffer smear.
- **Fresh-per-loop shuffle**: same snapshot mechanism as stutter, but a fresh random start offset into a two-tick buffer window is chosen on every loop wrap. Successive repeats are different slices of audio rather than the same fixed permutation.
- **Inverted bitcrush direction**: param 0 is clean (12-bit, 4096 levels), param 1 is heavily crushed (~5.6 levels). Old 16-bit maximum was near-transparent; the new 12-bit ceiling is the actual clean state. Viz already agreed with the new direction.
- **Tape stop removed** -- deceleration across a short clock-synced step never felt musical. Slot reclaimed.
- **Gate removed** -- Larets steps are too short in samples for rhythmic gating to land. Attempted a 64-pattern clock-locked bank (duty gates, Euclidean, NR primes) with 1/16-tick base slot, but sub-tick slots slip into AM buzz well before anything musical emerges.

### Controls

- **Global param offset** -- new bipolar (-1..1) top-level fader positioned between overview and xform. Non-destructive: adds to every step's param when read at playback, clamped to [0, 1]. Applied per-sample so CV modulation tracks smoothly; pitch shift and filter moved off their step-transition caches to support this.
- **Loop length rework** -- range now 1-16 (default 16, clamped to step count). Minimum of 1 gives a momentary single-step hold for performance gestures. Old patches with `loopLength=0` migrate to 16 on deserialize.
- **Overview expansion row** gains dedicated GainBias faders for step count and loop length, matching the spreadsheet convention that every sub-display parameter gets an expansion fader.
- **Step-list type readout unified** via `addName` (the SDK's name-table feature). Name indexing uses the same `(int)(v + 0.5f)` rounding as C++ `storeStep`, so the displayed abbreviation always matches what's stored. `useHardSet` added to the step-list readouts so the parameter's target and value stay in sync -- the root cause of the previous off-by-one was `softSet`'s 50-step ramp, not rounding.
- **Parfait-style Mix ply** -- sub-display cycles output / comp / auto (BinaryIndicator toggle for auto-makeup).

### Visualizations

- **Auto-normalizing Y scale** -- per-frame peak tracking of the 128-sample ring, target gain 0.9 / peak, smoothed at 8%/frame, clamped at 10x. Waveform always fills the frame regardless of signal level.
- **Reverse viz** -- right-to-left highlight band sweeping at the playback rate, replacing the old static brightness fade. Reads as a lit playhead moving backward.
- **Bitcrush viz** -- hash-based particle fill between the waveform contour and center. Density and motion scale with crush. Waveform silhouette drawn on top in white; floor density so the viz never dies at max bit depth.
- **Comb viz** -- envelope fill bounded by per-column min/max plus concentric inner waveform copies shrinking toward center. Tight comb = many nested rings (up to 7), long comb = single contour. Envelope outlined in GRAY10 so the silhouette reads clearly.

## Helicase (spreadsheet) - cosmetic and expansion polish

- **Overview brightness unified** on a single measure: mean |Δ| / RMS of the decimated output ring, which grows with timbral richness and is approximately pitch-independent because the ring captures ~8 carrier cycles regardless of f0. Drops the ZCR path plus the dead centroid/shell-radius inversion. Floor raised 0.15 -> 0.3 so the viz stays readable at rest. Blob gray ceiling raised 13 -> 15, so peak blobs can hit pure white at maximum brightness and field density.
- **Sync phase-receptivity threshold** now CV-modulatable via a new expansion GainBias fader. Sync ply main view stays visually identical; the fader appears only when the sync ply is focused into its expansion row.

## Known tradeoffs

- Larets gate and tape-stop effects are gone; stored step types at indices 7 (gate) and 8-11 shift down by one on load. Patches that used them may need a touch-up.
- Helicase overview viz is visually workable but smooths over quick parameter wiggles. Candidate tweaks (faster slews, brightness-coupled LFOs) are logged for a future update.

## Install

Copy `.pkg` files from `testing/am335x/` to SD card at `/mnt/ER-301/packages/`.

Delete the installed spreadsheet lib from the rear card to force reinstall:

```
sudo rm -rf /mnt/v0.7/libs/spreadsheet/
```

## Package versions

- **spreadsheet**: 2.0.0 -> 2.1.0
- All other packages unchanged.

## Notes for contributors

Header changes to files `%include`d by SWIG don't trigger wrapper regen under the default Makefile rules. For ARM builds in particular: if only headers change, force-remove `testing/am335x/mods/<pkg>/*_swig.{cpp,o}` before `make` to avoid shipping a mismatched `.so` that crashes on reboot.
