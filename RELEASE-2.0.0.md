# er-301-habitat v2.0.0

Release date: 2026-04-09

Requires firmware: v0.7.0-txo (er-301-stolmine)

## New Units

### Impasto (spreadsheet)

3-band multiband compressor with per-band FFT spectrum display.

- LR4 crossover (24dB/oct, shared engine with Parfait)
- Per-band feedforward compression with threshold, ratio, and speed controls
- Speed follows SSL G-Bus attack/release breakpoints (0.1ms-30ms attack, 0.1s-1.2s release)
- Threshold uses cubic fader scaling for usable range across the full throw
- Sidechain input split through the same crossover for frequency-aware detection
- Per-band spectrum with Catmull-Rom gain reduction ceiling contour
- Band level control via GainBias with dotted level indicator on spectrum
- Auto makeup gain, drive with tone EQ, skew, dry/wet mix
- Dual-instance stereo

### Lambda (catchall, experimental)

Seeded procedural synthesizer. PRNG generates wavetable frames and SVF filter bank from a seed number. Scan parameter morphs through generated timbres.

## Major Updates

### Rauschen (spreadsheet)

Now 11 algorithms (was 10). All chaos/noise algorithms retuned for better parameter response:

- Pink: fixed silence bug (unstable feedback loop removed), Y is now spectral thinning
- Crackle: proper SC-style abs() fold for real chaos (was a clamped oscillator)
- Logistic: X focused on chaotic region (r=3.45-4.0), Y controls iteration rate
- Henon: wider parameter ranges, fold on divergence
- Gendy: 3x amplitude perturbation, proportional duration, Levy-like large jumps
- Lorenz (new): 3D chaotic attractor with sub-stepping for accurate integration

### Etcher (spreadsheet)

- 8 depth-controlled transforms: random, rotate, invert, reverse, smooth, quantize, spread, fold
- Transform gate with CV input (same TransformGateControl UI as Excel and Ballot)
- Symmetric skew fix (linear shift replacing asymmetric pow() curve)

### Bletchley Park (biome)

- Scan restricted to random 4096-byte region per insert for finer timbral control
- Each instance explores a different neighborhood of the binary data

## Bug Fixes

- VarishapeOsc: crash on delete (SWIG class size mismatch)
- VarishapeVoice: same crash fix
- Etcher: crash on delete (13 private members hidden from SWIG)
- DriveControl: sub-display label centering (also affects Parfait)

## Package Changes

- New **catchall** package (v0.1.0) for experimental/WIP units
  - Sfera (z-plane filter, moved from spreadsheet)
  - Lambda (seeded procedural synth)
  - Flakes (granular shimmer, moved from biome)
- **biome** v2.0.0 (20 units)
- **spreadsheet** v2.0.0 (8 units)
- All 14 packages rebuilt against current firmware

## Install

Copy `.pkg` files from `testing/am335x/` to SD card at `/mnt/ER-301/packages/`.

Remove old v1.0.x packages before installing to avoid conflicts:

```
sudo rm /mnt/ER-301/packages/biome-1.0.*-stolmine.pkg
sudo rm /mnt/ER-301/packages/spreadsheet-1.0.*-stolmine.pkg
```

If upgrading from v1.x, also delete installed libs from the rear card to force reinstall:

```
sudo rm -rf /mnt/v0.7/libs/biome/
sudo rm -rf /mnt/v0.7/libs/spreadsheet/
```
