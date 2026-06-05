# Galactic port plan

Status: **PLANNED 2026-06-04**. Sixth AW atom in the house package. Per `planning/airwindows-reverb-research.md`: "The lush option. 3-stage cascaded 4×4 FDN with 12 fixed-tap delays + tiny 256-sample modulated pre-delay."

## Source

- Local: `~/repos/airwindows/plugins/MacVST/Galactic/source/{Galactic.h, Galactic.cpp, GalacticProc.cpp}`
- License: MIT. Naming: **Galactic** (faithful port).

## Topology (verified from source)

Same 3-stage cascaded 4×4 diff-Householder FDN as Verbity, with three signature additions:

1. **256-sample modulated predelay** (aML/aMR, 3111-sample buffers). LFO-driven vibrato applied here BEFORE the FDN cycle. Two sins per sample (offsetML uses `sin(vibM)`, offsetMR uses `sin(vibM + π/2)` for L/R decorrelation).
2. **LFO state**: `vibM` accumulates by `oldfpd * drift` each sample. When `vibM > 2π`, wrap to 0 AND re-seed `oldfpd = 0.4294967295 + (fpdL * 0.0000000000618)`. We don't have fpdL; substitute deterministic constant.
3. **Full L↔R cross-coupling at the feedback stage**: `aIL[countI] = inputSampleL + (feedbackAR * regen)` — L input receives R's feedback. Same for J/K/L. R input mirrors. This is the lush stereo wash signature.
4. **IIR lowpass at both input and output** (iirA, iirB), same as Verbity.
5. **Sum/8 combiner**, same as Verbity.
6. **Attenuate input by gain compensation** before predelay write: `aML[countM] = inputSampleL * attenuate` where `attenuate = (1 - regen/0.125) * 1.333`. Cuts input as feedback grows to prevent runaway.
7. **Standard crossfade wet/dry** (NOT submix — differs from Verbity and CreamCoat).

**No thunder, no previous-tap interpolation** (lighter feedback path than Verbity).

## State per instance

| Group | Lines / size | Bytes (double) | Bytes (hybrid float) |
|---|---|---|---|
| 12 FDN lines × 2 sides (same as Verbity) | 62920 samples | ~1006 KB | **~503 KB** |
| Predelay aML, aMR | 3111 × 2 samples | ~50 KB | **~25 KB** |
| iirAL/AR/BL/BR | 4 doubles | 32 B | 32 B (double) |
| feedbackA/B/C/D × 2 sides | 8 doubles | 64 B | 32 B (float) |
| lastRefL, lastRefR | 14 doubles | 112 B | 56 B (float) |
| vibM, oldfpd | 2 doubles | 16 B | 16 B (double) |
| 13 counters + 13 delay sizes | 26 ints | 104 B | 104 B |
| **Total per instance (hybrid)** | | | **~528 KB** |

Slightly heavier than Verbity (~25 KB more for predelay). Same L2-budget situation — exceeds 256 KB but per-sample working set is ~14 cache lines.

Vestigial fields in source header (NOT in process()): `vibML, vibMR, depthM, thunderL, thunderR` — **DO NOT include** in our port. Confirmed unused via grep.

## Public parameters

| AW name | Range | Default | Mapping | Effect |
|---|---|---|---|---|
| A "Replace" | 0..1 | **0.5** | regen = 0.0625 + (1-A)*0.0625 (0.0625..0.125, INVERTED) | A=0 → max regen 0.125 (infinite tail), A=1 → min regen 0.0625 (slap) |
| B "Brightness" | 0..1 | **0.5** | lowpass = (1.00001-(1-B))²/sqrt(scale) | LP filter, B=0 dark, B=1 bright |
| C "Detune" | 0..1 | **0.5** | drift = C³ * 0.001 | LFO speed for vibrato (very slow accumulator) |
| D "BigDim" | 0..1 | **1.0** | size = D*1.77+0.1 (0.1..1.87) | Room size / delay scale |
| E "Dry/Wet" | 0..1 | **1.0** | wet = 1 - (1-E)³ (cubic curve) | Standard crossfade mix |

Note: A is INVERTED vs Verbity's B. A=0 = max feedback. AW chose this so "Replace" reads as "replace dry with verb" — high A means dry-dominant.

Defaults D=1.0 and E=1.0 are extreme — AW ships Galactic at max size and full wet. Preserve faithfully.

## CPU projection

Per-sample work is heavier than any prior atom due to LFO + predelay:
- **2 sin() calls per sample per channel** = 4 sin/sample total. ~50ns each on Cortex-A8 scalar → ~200ns/sample = ~10% CPU floor just for sins at 48k stereo.
- Predelay write + 2-tap fractional interp read (one floor() call per side) per sample
- LFO accumulate + wrap branch (cheap but branchy)
- Same per-sample IIRs as Verbity (4)
- FDN cycle work (same as Verbity)

**Projected: ~25-30% stereo on Cortex-A8** — heaviest house atom yet.

Mitigation: at lower Detune (C=0), drift = 0; vibM never advances. But sin still gets computed. Could short-circuit sin computation when drift=0 (block-rate gate), but messes with the "is Detune actually disabled" semantics. Defer that optimization.

## CloudSeed-trap audit

- **No `firstFrame` guards.** Counters init to 1 (per source), vibM=3.0, oldfpd=429496.7295.
- **No allocations after constructor.**
- **No host APIs** beyond `getSampleRate()`.
- **No `std::vector`.**
- **Modulated read** on predelay: tap = countM + floor(offsetML). offsetML range [0, 254]. delayM = 256. So workingML max = countM + 254, wraps with the same `(workingML > delayM)?delayM+1:0` formula. **Safe** — single-step wrap, all offsets bounded by buffer size with margin.
- **2 sin per sample per channel** — Cortex-A8 scalar sin is the libm version. Per `feedback_disable_tree_vectorize_am335x` we have `-fno-tree-vectorize` package-wide; that prevents sin auto-vectorization that would crash. Scalar sin should be fine.
- **Per-sample dither dropped** per template.

**Verdict: clean** despite higher CPU. The 2-sin/sample is the only concern, and it's the AW design — character cost.

## LOAD-BEARING preservation

- Counters init to 1, including countM (per source)
- cycle = 0
- **vibM = 3.0** (initial value, drives first-frame LFO phase)
- **oldfpd = 429496.7295** (HUGE initial value — produces first-frame vibM burst that immediately wraps, then resets to ~0.43 for normal operation. AW design.)
- Predelay reset path: `oldfpd = 0.4294967295` (substituting for `0.4294967295 + (fpdL * 0.0000000000618)` since we have no fpdL; the fpd term is at most ~0.265, so dropping it deterministic-shifts the LFO seed slightly. Acceptable for non-RNG-based port.)
- attenuate gain compensation BEFORE predelay write
- Cross-coupled feedback at I/J/K/L lines (L gets R's feedback, R gets L's)
- Cycle reset re-uses `vibM=0` on overflow (NOT vibM = vibM - 2π — full reset)

## Phasing

Skip Phase 0 Smoketest. Phase 1 is full port.

1. `mods/house/atoms/Galactic.h` (header-only, hybrid float)
2. `mods/house/assets/Galactic.lua` (5 plies)
3. SWIG + toc + version bump (joint with Verbity → 0.1.0.8)
4. Both-arch build + lints + install linux
5. Audition. Watch CPU at default (D=E=1.0) — could be high.

## Phase 2+ (deferred)

- NEON Householder reduction (same as Verbity)
- LFO sin polynomial approximation (replace libm sin with min-max poly, ~5 ops, drops the ~10% sin floor to ~2%)
- Predelay interp NEON (2-tap, simple)

## Open decisions

- Default D=1.0, E=1.0 are AW defaults but make the unit "max big, full wet" on insert. Surprising. **Decision: keep AW defaults per faithful-port discipline.**
- A is inverted (high A = low feedback). Could rename to "Decay" with reversed mapping but user can dial. **Decision: keep "Replace" label per AW.**
