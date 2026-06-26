# 03 — Global CLOCK axis (variable sample-rate + aliasing-grit)

Combines online research (dsp-research-expert, 2026-06-25) with our codebase primitives.
The CLOCK is the unit's defining idea: ONE control sets an internal sample rate Fc that
warps the looper's length+resolution AND the field's time+quality together, in harmonized
steps, with a continuous "smooth" mode.

## The mechanical model (why one knob = time + pitch + length + quality)

Run the sub-engine (looper + field) at Fc = Fs/R while the codec stays at Fs = 48 kHz.
Then for the SAME memory buffers:
- **Length**: N samples covers N·R/Fs seconds → loop & reverb times stretch ∝ R.
- **Pitch**: record/play at Fc but D/A at Fs → pitch drops by log2(R) octaves.
- **Reverb time**: fixed delay-line taps represent R× more real seconds → RT60 ∝ R.
- **Quality**: sub-engine Nyquist = Fc/2 → bandwidth ceiling falls with R.

Coherent triple scaling (time × pitch × bandwidth) from one parameter is the whole point.
MOOD's steps approximate musical intervals (48k→32k = 3:2 = down a fifth, 32k→24k = 4:3 =
down a fourth, …). Implement as a LUT of `(label, ratio N:M, Fc)`; quantized mode snaps,
smooth mode interpolates ratios.

## Recommended implementation

**Architecture = reduced-rate sub-engine harness** (research Strategy 2), which is exactly
what **RotCoat already does** (`mods/house/atoms/RotCoat.h`): a per-line phase accumulator
fires the engine every R-th step; we generalize it to clock the WHOLE looper+field block.
The sub-engine runs at 1/R the rate → 1/R its full-rate CPU. This is the cheapest path and
the one most native to our code.

Pipeline:
1. **Downsample in**: phase accumulator `phase += Fc/Fs`; capture a sub-engine input sample
   when it overflows 1.0. Clean mode: precede with a polyphase FIR anti-alias (≈64 taps,
   linear phase, NEON 4-wide). Broken mode: skip the FIR → raw sample-and-hold decimation.
2. **Run looper + field at Fc** (the reduced-rate block).
3. **Upsample out**: linear interpolation between sub-engine output samples (1 mul + 1 add/
   sample) for clean; ZOH (sample repeat, free) for grittier. Clean mode adds a post
   reconstruction LP; broken mode bypasses it.

**Phase 1 = quantized integer steps.** Polyphase FIR (clean) / S&H decimation (broken) +
linear-interp or ZOH up. CPU: at R=4 the sub-engine costs ¼; the FIR pair ≈128 MACs/block
(negligible). Well within a single A72 core.

**Phase 2 = smooth glide.** Cubic **Farrow** resampler for continuous R (≈16–20 MACs/sample,
~3–4× the integer path, only active in smooth mode). Crossfade quantized↔smooth at the
boundary. Defer until Phase 1 is auditioned.

## Aliasing-as-grit (the CLASSIC clean↔broken axis)

- **Clean** = anti-alias FIR before decimate + reconstruction LP after interp → bandwidth-
  limited "tape" lo-fi, no alias distortion.
- **Broken** = bypass both filters → content >Fc/2 folds back as inharmonic metallic grit
  (SP-1200 / early-Akai character). This is **Mirror's paradigm** already in-tree
  (`mods/spreadsheet/Mirror.cpp`): tanh pre-sat → divider S&H without anti-alias →
  bit-quantize (mid-riser) → Nyquist-polarity-flip reconstruction. Lift Mirror's stages for
  the broken end.
- **Bit reduction** as a secondary grit param: `floor(x·2^(B-1))/2^(B-1)` (8-bit ≈ −48 dB
  floor, 4-bit ≈ −24 dB). Combine with rate reduction for compound lo-fi.
- Make clean↔broken a crossfade, not a hard switch (per-block crossfade avoids clicks).

## Hazards / discipline
- **FPCR FZ (flush-to-zero) bit must be set in every audio thread** on ARM64 (per-thread,
  NOT inherited): `fpcr |= (1<<24)`. NEON is always FTZ; scalar paths are not. Skipping it
  → 10–100× denormal stalls (see `04-fusion-governor.md`).
- Quantized step changes: crossfade params over 1–2 blocks to avoid zipper.
- RotCoat's `while(cyclePhase ≥ 1)` multi-fire at high rate is itself an intentional
  rate-mismatch alias source — keep it bounded with a per-line LP + Spiral (RotCoat already
  does fc≈500 Hz·worldRate LP to restore bass and stop HF runaway).

## Well-established vs exotic
- Integer polyphase decimation / ZOH / linear interp up: WELL-ESTABLISHED.
- Farrow cubic SRC for smooth glide: WELL-ESTABLISHED, heavier, Phase 2.
- Deliberate-alias broken mode + bit-crush: WELL-ESTABLISHED (and we own Mirror).
- Nothing here is exotic. Risk is integration, not algorithm.

## Codebase tie-ins
- `mods/house/atoms/RotCoat.h` — reduced-rate harness (the CLOCK skeleton).
- `mods/spreadsheet/Mirror.{h,cpp}` — alias-as-synthesis broken mode + bit-crush.
- Spiral (`mods/house/atoms/Spiral.h`) + per-line LP — bound the aliased feedback.
