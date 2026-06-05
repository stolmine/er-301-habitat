# Verbity port plan

Status: **PLANNED 2026-06-04**. Fifth AW atom in the house package. Per `planning/airwindows-reverb-research.md` addendum: "Feedforward-with-one-feedback topology — single instance spans zero-feedback slapback through infinite tail."

## Source

- Local: `~/repos/airwindows/plugins/MacVST/Verbity/source/{Verbity.h, Verbity.cpp, VerbityProc.cpp}`
- License: MIT. Naming: **Verbity** (faithful port).

## Topology (verified from source)

3-stage cascaded 4×4 diff-Householder FDN per side, same canonical shape as kWoodRoom inner FDN / WoodenBox / CreamCoat. **Two distinctions** from prior atoms:

1. **IIR lowpass at BOTH input and output** (iirA before cycle gate, iirB after). Symmetric darkness filter.
2. **Feedback smoothing via previous taps**: `feedback*L = feedback*L * (1 - interpolate) + previous*L * interpolate; previous*L = feedback*L`. Per-feedback-tap IIR-like smoother. Eight `previous` doubles (A/B/C/D × L/R).
3. **Thunder sub-low**: `thunderL = thunderL * 0.99 - feedbackAL * thunderAmount`. 0.99 IIR leak with feedback-A as input. Added to feedbackA's contribution to the I-line. Drives a sub-frequency rumble.
4. **Sum/8 combiner**: final output is `(outE + outF + outG + outH) / 8.0` — four sums divided by 8 (not 4). Intentional gain reduction per AW.
5. **Submix-style wet/dry**: `wet = D*2`, `dry = 2 - wet`, both clamped to [0,1]. Wetness=0.5 → wet=1.0 AND dry=1.0 (both full, summed). Same pattern as CreamCoat.
6. **No predelay, no LFO, no vibrato, no cross-coupling** — L→L, R→R throughout.

## State per instance

| Group | Lines / size | Bytes (double) | Bytes (hybrid float) |
|---|---|---|---|
| Stage 1 lines (I/J/K/L) × 2 sides | 12540 samples | ~200 KB | **~100 KB** |
| Stage 2 lines (A/B/C/D) × 2 sides | 18960 samples | ~303 KB | **~152 KB** |
| Stage 3 lines (E/F/G/H) × 2 sides | 31420 samples | ~503 KB | **~251 KB** |
| iirA, iirB × 2 sides | 4 doubles | 32 B | 32 B (stays double) |
| feedbackA/B/C/D × 2 sides | 8 doubles | 64 B | 32 B (float) |
| previousA/B/C/D × 2 sides | 8 doubles | 64 B | 32 B (float) |
| thunderL, thunderR | 2 doubles | 16 B | 16 B (stays double) |
| lastRefL, lastRefR | 14 doubles | 112 B | 56 B (float) |
| 12 counters + 12 delay sizes | 24 ints | 96 B | 96 B |
| **Total per instance (hybrid)** | | | **~503 KB** |

L2 budget on Cortex-A8 is 256 KB; total state exceeds by ~2x. But per-cycle access pattern is **12 reads + 12 writes** — 24 cache lines, ~768 B working set per sample. Cache miss only when crossing into a region not recently touched. This is the standard FDN access pattern and was validated as LOW risk in the research doc.

Full double would be ~1 MB stereo. **Hybrid float is mandatory.**

## Public parameters

| AW name | Range | Default | Mapping | Effect |
|---|---|---|---|---|
| A "Bigness" | 0..1 | **0.25** | size = A*1.77+0.1 → all 12 delay sizes scale | Room size / decay length |
| B "Longness" | 0..1 | **0.0** | regen = 0.0625+B*0.03125 (0.0625..0.09375); also thunderAmount damping | Feedback amount + sub-rumble level |
| C "Darkness" | 0..1 | **0.25** | lowpass coefficient = (1-C²)/sqrt(scale); also interpolate = C²*0.618; also thunderAmount = (0.3-B*0.22)*C*0.1 | Treble cut + feedback smoothing + thunder activation |
| D "Wetness" | 0..1 | **0.25** | submix wet/dry (0.5 = full+full) | Mix |

Note Darkness drives three things: the LP filter, the feedback-smoother rate, AND the thunder amount. Single control, three behaviors.

## CPU projection

Per-cycle work (same as WoodenBox + CreamCoat, ~14% baseline hybrid float):
- 12 reads + 12 writes across FDN lines
- 12 Householder ops (3 stages × 4 lines, but cheap)
- 8 previous-tap interpolation IIRs
- 2 thunder IIR steps
- 4 IIR lowpass steps (iirA × 2 + iirB × 2; iirB happens per-sample, iirA happens per-sample too)

Per-sample (always-on):
- 2 IIR lowpass steps (iirA L+R) BEFORE the cycle gate — per sample
- 2 IIR lowpass steps (iirB L+R) AFTER the cycle gate — per sample
- multi-pole averaging switch fallthrough
- wet/dry submix sum

At 48k cycleEnd=1, all per-cycle work runs every sample.

**Projected: ~15-20% stereo on Cortex-A8** — slightly heavier than WoodenBox/CreamCoat due to extra previous-tap interpolation + thunder + 4 per-sample IIRs, but no NEON-hostile ops.

## CloudSeed-trap audit

- **No `firstFrame` guards.** Counters init to 1 (per source).
- **No allocations after constructor.** Fixed sizes.
- **No host APIs** beyond `getSampleRate()` (read top-of-block).
- **No `std::vector`.**
- **No modulated reads.** Each delay line tap is at the count head; the delay size is block-rate.
- **No runtime-branched DSP dispatch in per-sample loop.** Only the cycleEnd-fallthrough switch (block-constant value).
- **No transcendentals per sample.** No sin/cos/pow/cbrt in the per-sample path.
- **Per-sample dither dropped** per template.

**Verdict: clean.** Lightest trap profile of any house atom so far.

## LOAD-BEARING preservation

- Counters init to 1 (countA..H, countI..L all = 1), cycle = 0
- thunderL/R init to 0
- All state arrays memset to 0
- Sum/8 combiner — divide-by-8 not divide-by-4, despite 4 outputs
- Submix wet/dry semantics: Wetness=0.5 = full wet AND full dry summed (NOT crossfaded)
- thunder term: `thunderL = thunderL * 0.99 - feedbackAL * thunderAmount` — preserve the SIGN (minus, not plus)
- previous-tap update order: feedback first via interpolate, THEN assign previous = updated feedback

## Phasing

Skip Phase 0 Smoketest per template (four consecutive first-try hardware successes).

### Phase 1 — atom + unit

1. `mods/house/atoms/Verbity.h` (header-only, hybrid float).
2. `mods/house/assets/Verbity.lua` (4 plies).
3. SWIG + toc + version bump.
4. Both-arch build + lints + install linux.
5. Audition.

## Phase 2+ (deferred)

- NEON Householder reduction (3 stages × 4 lines = 12 reductions per cycle, NEON-friendly)
- Possible thunder + previous interp NEON pack-up
