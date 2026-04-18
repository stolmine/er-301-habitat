# er-301-habitat v2.3.0

Release date: 2026-04-17

Requires firmware: v0.7.0-txo (er-301-stolmine)

Package updates: **spreadsheet 2.2.0 -> 2.3.1**, **biome 2.0.0 -> 2.1.0**. All other packages unchanged.

## Highlights

New unit: Colmatage (breakbeat cutter). Blanda (three-input scan mixer) ships as a complete unit after extensive viz and control work. Major audio quality improvements on Helicase and Petrichor. Pecto gets a substantial CPU reduction via NEON optimization.

## New unit: Colmatage

Clock-driven breakbeat cutter in the spreadsheet package. Algorithm lineage: Nick Collins' BBCut library via Remy Muller's Livecut, refined into a single WarpCut-derived engine with parameterized internals.

Core mechanic: records incoming audio into a circular buffer, then algorithmically slices and rearranges it in real time using geometric repeat series. Accelerating repeats create rhythmic momentum (like a bouncing ball); decelerating repeats create settling patterns (like a spinning coin). The interplay between block size, repeat count, and acceleration ratio produces everything from subtle double-hits to granular IDM territory.

6 plies:
- **clock** -- external clock input with subdivision control, genuine phrase reset, CV-automatable subdiv on expansion
- **block** -- block size weight (small choppy to big sweeping), BSP mosaic overview graphic with Perlin noise cloud modulation and connected cut-activation walk. Shift sub: phrase min / phrase max / block max (in beats)
- **density** -- how often cutting happens vs clean passthrough (0 = all straight, 1 = all warped)
- **repeats** -- target repeat count per block (2-64). Shift sub: ritard bias (accel vs decel), blend (even vs geometric), accel (geometric ratio)
- **texture** -- bipolar duty cycle (-1 = full reverse, 0 = gated, +1 = full forward). Shift sub: amp min / amp max / fade
- **mix** -- dry/wet. Shift sub: input level / output level / tanh saturation

## Blanda -- three-input scan mixer

Blanda ships complete with per-input bell shape controls, territory-based ghost occlusion for clean butt-joint visualization, global Skew macro, expansion views for all custom controls, and a continuous polyline bell landscape with per-segment shading.

## Helicase -- audio quality

Anti-alias discontinuity shaping with polyBLEP for value jumps (shapes 7, 12), smoothed-abs for V-corners (shape 15), and full 2x oversampling of the hi-fi inner loop (mod + feedback + FM + carrier + shaper) with 2-tap halfband decimation. Breakpoint-gated 20 Hz DC blocker on output (bypassed below 1 Hz for LFO use).

## Petrichor -- granular improvements

Grain system rebuilt: Hann envelope replaces rectangular window, 50% overlap between grains, anchored grain spawn at tap read positions, max grain size capped at 300ms, stack cap at 8. Upfront parallel prefetch of all tap read positions. Grain loop micro-optimizations (dropped modulo/floorf, added read prefetch). Time fader throw restricted to the buffer-size menu setting.

## Pecto -- CPU optimization

Three-pass tap loop restructuring (compute/gather/combine) with 8-ahead prefetch and explicit NEON intrinsics on passes A and C. Stereo CPU usage at density 24 dropped from ~50% to ~6%. Fixed 20 Hz DC blocker at output. Bipolar feedback (-1 to +1).

## Other fixes

- Spreadsheet dual-mode GainBias controls guarded against nil focusedReadout (crash fix on mode transitions)
