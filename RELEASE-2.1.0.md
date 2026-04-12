# er-301-habitat v2.1.0

Release date: 2026-04-12

Requires firmware: v0.7.0-txo (er-301-stolmine)

Two new units added to the spreadsheet package.

## New Units

### Larets (spreadsheet) - Stepped Multi-Effect

Clock-driven 16-step sequencer where each step applies one of 10 audio effects to the incoming signal.

**Effects per step**

- **Stutter** - beat repeat from a step-start buffer snapshot, loop length snapped to a musical fraction (1/16, 1/8, 1/4, 1/2, 1 tick) via the per-step param.
- **Reverse** - playback rate 0.5x..2.0x.
- **Bitcrush** - 12-bit ceiling down to ~2.5-bit crush.
- **Downsample** - variable sample-rate reduction.
- **Filter** - SVF sweep with param-controlled cutoff.
- **Pitch shift** - two-grain Dattorro-style overlap shifter (sin^2 windows, 180 deg phase offset).
- **Distortion** - hard-clip against a unit-ceiling limiter, drive 1..20 with 1/drive^0.7 makeup.
- **Shuffle** - chunk-quantized beat repeat that picks a fresh random source slice on every loop wrap.
- **Delay** - clock-synced tap up to 0.5s.
- **Comb** - short-delay resonant comb.

**Global controls**

- **CPR single-band compressor** behind a one-knob macro (threshold + ratio scaled together). Auto-makeup toggle on the Mix ply sub-display.
- **Global param offset** - bipolar (-1..1) top-level fader between overview and xform. Non-destructive, applied per-sample so CV tracks smoothly.
- **Loop length** - 1..16 with default 16 (clamped to step count). Minimum of 1 gives a momentary single-step hold for performance gestures.
- **Clock division**, **skew** (per-step tick stretching), **mix** (dry/wet), **output level**.
- **Transform gate** - CV-triggered randomization across types, params, or ticks.

**Visualizations**

Custom overview viz renders per-effect visual feedback against the live waveform: concentric rings for comb, R->L highlight sweep for reverse, particle fill for bitcrush, jittered Y-fill for distortion, layered ghost traces for delay, segment swaps for shuffle. Auto-normalizing Y axis so the waveform always fills the frame regardless of signal level.

### Helicase (spreadsheet) - 2-Op FM Oscillator

Two-operator FM synthesis voice with discontinuity waveshaping and phase-latched sync.

**Signal path**

- **Carrier + modulator**, each offering 8 OPL3-inspired waveforms (sine, half-sine, full-wave, quarter-sine, alternating sine, camel sine, square, logarithmic saw).
- **Modulator feedback** with tanh soft clip.
- **16 discontinuity fold shapes** applied post-FM: tanh, sqrt-abs, soft clip, hard clip, sin-squared, asymmetric soft, bit crush, ring mod, triangle fold, sine fold, hard fold, staircase, wrap, asymmetric fold, Chebyshev T3, ring fold.
- **Lin/expo FM toggle** and **lo-fi/hi-fi quality option** via menu.

**Sync**

JF-inspired phase-receptivity sync: pending sync edges are latched and fire only when the modulator's phase crosses a programmable threshold. 0.0 = hard sync, 0.5 = soft sync, 1.0 = subharmonic lock. Threshold CV-modulatable via the sync ply's expansion GainBias.

**Controls (7 plies)**

V/Oct, fundamental, overview (modMix main fader, carrier shape + FM mode on expansion), shaping (mod index + discontinuity index + type), modulator (ratio + feedback + mod shape + fine tune), sync, level.

**Visualizations**

Three custom graphics:

- **Overview** - 3D k-means clustered phase-space metaballs with Voronoi edges, spectral-brightness modulation (mean |dx| / RMS of decimated output ring, approximately pitch-independent).
- **Shaping** - live transfer curve showing the active discontinuity fold.
- **Modulator** - circular modulator ribbon with Catmull-Rom interpolation.

## Install

Copy `.pkg` files from `testing/am335x/` to SD card at `/mnt/ER-301/packages/`.

Delete the installed spreadsheet lib from the rear card to force reinstall:

```
sudo rm -rf /mnt/v0.7/libs/spreadsheet/
```

## Package versions

- **spreadsheet**: 2.0.0 -> 2.1.0 (adds Larets and Helicase)
- All other packages unchanged.

## Notes for contributors

Header changes to files `%include`d by SWIG don't trigger wrapper regen under the default Makefile rules. For ARM builds in particular: if only headers change, force-remove `testing/am335x/mods/<pkg>/*_swig.{cpp,o}` before `make` to avoid shipping a mismatched `.so` that crashes on reboot.
