# House ports: CPU optimization pass (8 port units)

Status: PROPOSED 2026-07-21. Analysis only; no DSP source touched.
Scope: the 8 port units (6 AW reverbs + 2 Console0/tape chains). RotCoat, Filament,
Carriage excluded (originals, being suppressed).

Evidence labels: **READ** = traced in the atom source in this pass. **INFERRED** =
deduced from structure or by analogy; not measured. CPU figures marked *(hw)* are
hardware-measured (ledger item ~300 / creamcoat-port-plan.md / todo-archive.md
"Retrofits" section); figures marked *(proj)* are plan-doc projections never
validated on hardware. **First execution step of this pass must be an on-device
CPU re-measurement of all 8 units at defaults** - every number below is either a
2026-06-04-era spot reading or a static estimate.

Build context (READ, mod.mk): `-O3 -ffast-math`, am335x appends `-fno-tree-vectorize`
last; NEON only via explicit intrinsics + class-member arrays. All state arrays in
these atoms are already class members (heap via addObject).

---

## 1. Per-unit findings

| Unit | CPU class | double in per-sample path? | Access pattern | libm/sample | Applicable levers |
|---|---|---|---|---|---|
| **kWoodRoom** | HEAVY: 29% stereo *(hw)* | YES - full double: 90 delay arrays + all trellis math + bez (READ) | Gather-ish: single-element reads across 90 separate arrays at per-line counters; NEON no-go (READ) | none (5x `pow` block-rate only) (READ) | **Hybrid float** (quantified 29% -> ~12-15%); optional Householder algebraic reduction |
| **WoodenBox** | MEDIUM: 14% stereo *(hw)* | YES - full double: 32 arrays + all FDN math + bez (READ) | Same gather-ish pattern, 32 arrays; NEON no-go (READ) | none (2x `pow` block-rate) (READ) | **Hybrid float** (quantified 14% -> ~6-8%) |
| **CreamCoat** | MEDIUM: 14% stereo *(hw, hybrid)* | Mostly no - float state AND float FDN math; only bez[] accumulator + reconstruction double, by design (READ) | Gather-ish; NEON no-go (READ) | none (READ) | **Done. Not worth touching.** |
| **BrightAmbience3** | HEAVY (inferred): 20-30% default, 50-70% Size=1 *(proj, never hw-recorded)* | PARTIAL - buffers float, but tap-sum accumulates `(double)pL[idx]` per tap (122..487 taps x 2ch, per sample at 48k), SVF + intermediates double (READ) | **Gather-bound**: prime-offset scattered reads over 256 KB (= entire L2); NEON no-go (READ) | **2x `sin`** per sample (cycleEnd=1 at 48k) + fabs (READ) | Float tap accumulators; sin -> polynomial (flagged, see §4); measure before/after |
| **Verbity** | MEDIUM-HEAVY: 15-20% stereo *(hw)* | YES in math - float *storage* but ALL FDN math double: 24x `(double)aXX[...]` cast-trap reads per cycle + double Householder + 8 double-mediated smoother updates (READ) | Gather-ish, 24 arrays; NEON no-go (READ) | none (fabs only; pow/sqrt block-rate) (READ) | **Float the per-cycle math** (CreamCoat pattern); iir/thunder stay double |
| **Galactic** | HEAVY: 25-30% stereo *(hw)* - heaviest port | YES in math - same double-math-on-float-storage FDN as Verbity, + double LFO/interp/IIR per sample (READ) | Gather-ish + 256-sample modulated predelay (data-dependent index, classic NEON-killer) (READ) | **2x `sin` + 2x `floor`** per sample (READ) | **sin -> polynomial** (flagged, §4; plan doc: ~10% CPU floor -> ~2%); **float the FDN math** |
| **TickerTape** (Console0Channel + ChromeOxide + Console0Buss + ChainMix) | LIGHT: ~1.7% *(proj)*, hw gate passed at <5% | YES - chain atoms compute per-sample in double (Console pair ~15 ops, ChromeOxide ~50 ops incl. poly saturator; ChainMix is float) (READ) | Fully sequential/contiguous; tiny state (READ) | none (ChromeOxide already swapped libm sin for spiralFastSaturate poly) (READ) | **Not worth touching** at current use |
| **Lacquer** | LIGHT: ~1.9-2.4% *(proj)*, hw gate <5% | YES for filters/Cojones (deliberate); TapeFat is int fixed-point + one reciprocal mul - already the third optimization iteration (READ) | Sequential; TapeFat 1 KB int ping-pong buffer, cache-resident (READ) | none (fabs only) (READ) | **Not worth touching** - a float retrofit was tried and REGRESSED (0.1.0.21 -> reverted 0.1.0.23, documented in the atom header) |

### NEON verdict (applies to all 6 reverbs) - READ, consistent with the Fabula finding

Every reverb hot loop is dominated by single-element loads/stores scattered across
many separate delay arrays at per-line counter indices (kWoodRoom 90 arrays,
WoodenBox 32, CreamCoat 32, Verbity/Galactic 24, BA3 prime-offset taps over 256 KB).
Cortex-A8 NEON has no gather load, so building a 4-lane vector requires 4 scalar
loads + lane inserts; the arithmetic per load (one Householder add/sub) is too small
to amortize that. Galactic's modulated predelay read is additionally data-dependent
per sample. **No NEON work is proposed for any unit in this pass.** The old
"NEON Householder reduction" note in WoodenBox's header should be considered
retired by the Fabula experience.

### Wrap math - READ

No modulo anywhere; all lines use increment + compare-reset with exact (non-pow2)
lengths. The pow2-mask lever is **inapplicable**: forcing pow2 lengths would change
the delay tuning = tone change. BA3's 32768 buffer already wraps via conditional
subtract. Do not touch.

---

## 2. Ranked execution order

Ordering rule: quantified hybrid-float wins on the heaviest all-double units first;
then the two float-storage/double-math units; then the approximation-gated items.

| # | Task | Expected win | Effort | Risk |
|---|---|---|---|---|
| 0 | **On-device profiling baseline.** Re-measure all 8 at defaults + BA3 at Size=1.0 + kWoodRoom at Time extremes. | evidence | S | none |
| 1 | **kWoodRoom hybrid float** (state arrays + trellis math + feedback taps -> float; bez[]/bezF[] accumulators + block scalars stay double; 3x3 ER trellis may stay double if the A/B says so). | 29% -> ~12-15% *(quantified projection, todo-archive; pattern hw-proven on CreamCoat)* | M (large file, 90 arrays, mechanical) | LOW - pattern shipped twice; keep denormal flush; A/B vs current |
| 2 | **WoodenBox hybrid float** (same pattern; bez[] stays double). | 14% -> ~6-8% *(quantified projection)* | S-M | LOW |
| 3 | **Verbity: float the per-cycle FDN math.** Storage is already float; change `double outXX = (double)aXX[..]` to float and do the Householder/feedback-smoother arithmetic in float (exactly CreamCoat's shipped pattern). Keep iirA/B, thunder double. | 15-20% -> ~10-13% **INFERRED** by analogy to CreamCoat (same FDN in float = 14% doing more work) | S-M | LOW |
| 4 | **Galactic part A: float the FDN math** (same as #3; keep iir, vibM/oldfpd double). | removes the same class of cost as #3 | S-M | LOW |
| 5 | **Galactic part B: LFO sin -> polynomial** (2x sin/sample; Helicase poly-sine or spiralFastSaturate-class approx). Plan doc estimates the sin floor at ~10% CPU, poly ~2%. | ~-8% on the heaviest unit *(proj)* | S | MEDIUM - approximation; see §4 flag. Combined A+B target: 25-30% -> low-to-mid teens (INFERRED) |
| 6 | **BrightAmbience3: float tap accumulators + float per-sample intermediates.** Kills the per-tap float->double convert + scalar-VFP double add (244..974 per sample). Caveat (INFERRED): the loop may be L2-miss-bound at high Size, in which case the FP win is partially hidden by load latency - hence step 0 before/after matters most here. | unknown until measured; helps most at high Size | S-M | LOW-MEDIUM - up-to-487-term float summation, A/B for noise floor |
| 7 | **BrightAmbience3: SVF-feedback sin -> polynomial** (2x sin/sample). | small-medium | S | MEDIUM - approximation flag, §4 |

Optional micro-lever to fold into #1/#2 (not standalone): the Householder algebraic
reduction - compute `total = hA+..+hF` once, then `out_i = 3*h_i - total` (6-lane:
~36 ops -> ~13 per layer; 4-lane similar). Bit-inexact reassociation, but the same
tolerance class as hybrid float itself (and `-ffast-math` may already reassociate);
acceptable only inside a retrofit that is being A/B'd anyway.

Process requirements for every step: bump PKGVERSION 4th digit; build BOTH arches;
re-read edited files after big cross-file edits (silent-edit-failure memory); expect
SWIG re-run on header edits (mod.mk tracks headers, but force-clean if in doubt);
A/B against the current build for tone identity; hardware-validate before moving on.

---

## 3. Not worth touching

- **CreamCoat** - already the reference hybrid-float implementation; remaining
  doubles are the precision-scoped bezier accumulator (~20 ops/sample). Floatizing
  the reconstruction would risk the documented low-DeRez precision case for a
  marginal win. Leave.
- **TickerTape** - ~1.7% projected, <5% hardware-gated. A hybrid-float pass on
  Console0Channel/Buss + ChromeOxide would roughly halve a tiny number. Only
  revisit if these component atoms get reused inside something hot.
- **Lacquer** - ~2% and it is already on its third precision iteration; the atom
  header documents that the 0.1.0.21 float conversion made it SLOWER (float->double
  cast tax) and was reverted. Firsthand evidence says leave it alone.

## 4. Character-changing options - user decision required

Everything in §2 except these is in the "A/B-identical in tone" class (hybrid float
/ float math / algebraic reassociation: LSB-level differences only). The following
are lossy approximations or structural changes; do not execute without sign-off:

1. **Galactic LFO sin -> polynomial (#5).** Approximates the vibrato LFO shape.
   Precedent exists (ChromeOxide shipped spiralFastSaturate for libm sin, 0.45%
   error called inaudible), and an LFO position error of ~1e-4 of 127 samples is
   far below a sample - but it is still not bit-faithful. Alternative: incremental
   rotation recurrence (near-exact, slightly fiddlier).
2. **BrightAmbience3 SVF sin -> polynomial (#7).** sin() acts as a soft-limiter in
   the feedback path; a poly changes the curve at the extremes where feedback is
   hottest. Same precedent, same caveat.
3. **BrightAmbience3 structural relief** (only real lever if #6 measures poor):
   Size soft-cap, tap decimation, or an undersample shell. All change the sound of
   the top of the Size throw. The plan doc's shipped decision was "leave at full,
   document" - reopening it is a product call, not an optimization.
4. **NOT proposed even as an option:** pow2 delay lengths (tone change), tank
   downsampling of the FDNs (these ports' undersample behavior is already part of
   their character via derez/cycleEnd).

## 5. Expected shape of the win (INFERRED, static estimates)

Steps 1+2 alone remove ~25 CPU points across the two heaviest all-double units and
are the only steps with prior quantification. Steps 3-5 plausibly take Verbity and
Galactic from ~45% combined to ~25% combined. If everything lands, the six reverbs
go from roughly 115-135% summed (can't run 4 together today) to roughly 60-75%
summed. Treat these as direction, not commitments; step 0 and per-step re-measure
are the ground truth.

---

## Reverb conversion — the exact hybrid boundary (analysed 2026-07-22)

TickerTape (Console0Channel/Buss, ChromeOxide) + Lacquer shipped hybrid-float, tone-identical
(1 LSB, corr 1.0), 0.1.0.38/39. The reverbs follow, mirroring the CreamCoat reference boundary.

**The boundary, confirmed from CreamCoat (its loop is 32 double / 54 float — a genuine hybrid):**
- **Delay-line arrays -> float** (the memory bulk + per-sample reads/writes). This is the win.
- **The `bez[]` / interpolation-undersample network -> KEEP double.** In kWoodRoom `bez[bez_cycle]`
  is the undersample TIMING accumulator (fires the trellis at `> 1.0`) - the exact mCyclePhase
  lesson from Lacquer: floating a timing accumulator shifts event-sample-positions and
  decorrelates the output. bez also carries the bezier reconstruction + the output IIR; keep all
  of it double.
- **Block-rate coefficient bakes -> keep double, but bake a float copy of any that MULTIPLY a
  float per-sample** (e.g. kWoodRoom `reg6n` scales the float feedback `f6* * reg6n` in the
  trellis -> needs `reg6nF`, else every feedback tap is a float*double->double cast trap).
  `derez` stays double (used only in the double bez context).
- **Trellis math locals -> float** (inputSample*, drySample*, hA-hF, earlyReflection*, f6*
  feedback state), with **all trellis literals f-suffixed** (-2.0f Householder, -0.0625f, etc) -
  a bare double literal in a float expression silently promotes the whole op to double.
- **Boundary casts are fine** where the float trellis meets the double bez (inputSample crosses
  contexts a few times/sample) - that is the deliberate precision boundary, not the per-op trap.

**Verification gate (per unit, non-negotiable):** the `tools/house-bench` A/B harness across the
FULL param space (each unit's corner settings) - must be max diff 1 LSB (3.05e-5) + corr
1.0000000 at every point, like the shipped four. The harness already caught the Lacquer
phase-accumulator decorrelation that a single-operating-point check would have missed.

**Per-unit specifics:**
- kWoodRoom (798 lines): a3AL..a3IR + a6AL..a6ZKR delay arrays -> float; f6* feedback -> float;
  reg6n -> reg6nF; bez[]/bezF[] double. Full-double baseline, biggest win (~29%->12-15%).
- WoodenBox: same full-double CreamCoat pattern.
- Verbity / Galactic: storage already float - fix is REMOVING the `(double)` cast-traps in the
  FDN math (convert the math to float), NOT re-typing storage. Simpler than kWoodRoom.
- BrightAmbience3: float the tap accumulators; CAVEAT - gather-bound over a 256KB buffer (~L2
  size), may be load-latency-bound not FLOP-bound, so measure the delta specifically.
- SKIP the character-changing sin->poly swaps (Galactic/BA3) per user: "no character change".

---

## Pass status (2026-07-22) + remaining two

**Shipped, dual-gated (tone 1 LSB + corr 1.0 across full param space, AND f64-op drop confirming
a real full conversion not a silent half):**
- Console0Channel 60->16, Console0Buss 63->16, ChromeOxide 368->89 (TickerTape's chain) - 0.1.0.38
- Lacquer (per-sample float; TapeFat int-core + mCyclePhase-double kept) - 0.1.0.39
- kWoodRoom 1087->270 (delay float / bez double) - 0.1.0.40
- WoodenBox 369->149 - 0.1.0.41
- Verbity 649->123 (cast-trap removal; no timing accumulator) - 0.1.0.42
Whole-package f64 static 2757 -> 1694.

**Remaining - each needs care, NOT a blanket pass:**
- **Galactic** - SAME cast-trap removal as Verbity for the FDN, BUT the vibrato is a modulated
  predelay: `vibM` is a phase ACCUMULATOR and `offsetML/MR = (sin(vibM)+1)*127`, `fracML/MR`
  drive fractional delay-tap reads. The header explicitly says "precision matters for stable
  vibrato". So keep `vibM`, the `sin()` (also: SKIP the sin->poly swap per no-character-change),
  and the offset/frac computation DOUBLE; float only the FDN delay/feedback math. Draw the
  boundary at the interpolated delay read (double frac x float taps -> boundary cast). Dual-gate
  including a LONG render (vibrato drift only shows over time) at several Detune/Bigness settings.
- **BrightAmbience3** - gather-bound over a ~256 KB buffer (~L2 size). MEASURE ON HARDWARE FIRST:
  if it is load-latency-bound rather than FLOP-bound, the hybrid-float delta may be small and not
  worth the risk. Defer until the on-device CPU read is available. (Also skip its sin->poly.)

---

## OUTCOME (2026-07-22): hybrid-float is LOW-VALUE on these reverbs - stop here

Hardware measurement: kWoodRoom dropped only **~1% CPU mono** (settings-dependent) despite the
f64-op static count dropping 1087->270. The static f64-op count MASSIVELY oversold it: these AW
reverbs are memory/latency-bound (the gather-bound scattered delay reads dominate the loop), not
FLOP-bound, so removing the scalar-double math barely helps. Same root cause as the Fabula
NEON-no-go. LESSON: do not chase f64-op counts on gather-bound delay/reverb DSP - profile on
hardware first; the FLOP reduction is real but irrelevant when the loop is load-bound.

Decision: ship the 7 shipped conversions as-is (tone-identical, harmless, minor win), do NOT
convert Galactic/BA3 (not worth the effort/risk for ~1%). Release house as-is soon.
