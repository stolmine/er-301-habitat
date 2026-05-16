# Petrichor (MultitapDelay) CPU reduction — recon

User flagged Petrichor as a "massive CPU hog". This doc identifies
the per-sample work breakdown, classifies opportunities by ROI, and
flags the architectural reality that constrains the lift.

Code references: `mods/spreadsheet/MultitapDelay.cpp` (877 lines,
`process()` at line 488). Limits: `kMaxTaps=8`, `kGrainsPerTap=3`.

## Per-sample anatomy (8 taps active, grains on)

| Section | Per-sample cost (rough) | Notes |
|---|---|---|
| Input load + writeIndex update + bufWrite | ~5 cycles | one int16 store |
| **Smoothing + prefetch pre-pass** (×8 taps) | ~80 cycles | smooth + readPos calc + prefetch each tap |
| **Per-tap loop body** (×8 taps) | varies | see below |
| Feedback path (IIR LP + optional HP + soft clip) | ~15 cycles | scalar |
| Mix + output limiter (2× tanh stereo) | ~10 cycles | already polynomial fast_tanh |
| **Per-tap body breakdown — grains on**:  | | |
| ↳ grain spawn check + spawn (rare) | ~5 cycles avg | block-rate-ish |
| ↳ grain prefetch pre-pass (×3 grains) | ~10 cycles | one __builtin_prefetch each |
| ↳ grain compute loop (×3 grains) | ~60 cycles | hann LUT + bufRead×2 + interp + advance |
| ↳ filter switch dispatch (LP/BP/HP/Notch) | ~20 cycles | calls stmlib::Svf (TPT SVF, scalar) |
| ↳ level × energy follower × pan accumulate | ~10 cycles | |
| ↳ total per tap with grains | **~105 cycles** | |
| ↳ total per tap without grains (unity pitch) | **~45 cycles** | direct path skips grain machinery |

**Total per sample, 8 taps × grains on**: ~825-900 cycles
**Total per sample, 8 taps × no grains**: ~420 cycles

At 800 MHz × 48 kHz = ~5% CPU minimum (no grains, FLOPs only) up to
~5-7% (grains on, FLOPs only). But **measured CPU is much higher**.

## Why measured CPU >> FLOP estimate

**Petrichor is memory-bandwidth bound**, not FLOP bound. Per sample
with 8 taps + grains:
- 8 tap reads × 2 (interpolation) = 16 buffer reads from scattered locations
- 24 grain reads (8 taps × 3 grains) × 2 = 48 buffer reads
- **64 random int16 reads/sample** across a buffer up to ~2MB (20s @ 16-bit)

Cortex-A8 caches: 16KB L1 D-cache, 256KB L2. The buffer is far larger
than L2. With tap delays scattered across the buffer (user configures
masterTime × stack × grid), the working set far exceeds L1.

Per-miss penalties (Cortex-A8): L1 miss → L2 ~10 cycles, L2 miss → RAM
~100+ cycles. Even with the upfront prefetch pre-pass (which is good
engineering), the memory system can only service ~4 concurrent misses
on Cortex-A8.

**Memory-side estimate** (rough): if 50% of the 64 reads hit L1 and
50% miss to L2, that's 32 misses × 10 cycles = 320 extra cycles per
sample. At 48 kHz that's ~2% CPU just for L2 misses. If some miss to
RAM that escalates fast — RAM misses at 100+ cycles each take a
single sample's CPU into double digits.

**Implication**: pure FLOP optimization (NEON) helps the visible
compute, but the user-perceived CPU dominance is memory.

## Identified opportunities

### O1 — SoA NEON SVF filter bank for the 8 per-tap filters ⭐ BEST FIT

`mods/spreadsheet/MultitapDelay.cpp:796-818`. Currently each tap has
its own `stmlib::Svf filters[kMaxTaps]` instance, dispatched via
switch on `filterType[t]` per tap per sample. The TPT SVF math
(g/r/h/state_1/state_2) is **identical structure** to the Filterbank,
Rings modal, and Impasto SVF banks we already NEON'd.

Pattern (lifts directly from `mods/spreadsheet/Filterbank.cpp` /
`MultibandSaturator.cpp` Phase 3):
- Add SoA arrays to Internal: `svfG_[8]`, `svfR_[8]`, `svfH_[8]`,
  `svfS1_[8]`, `svfS2_[8]`, plus `lpGain_[8]`, `bpGain_[8]`,
  `hpGain_[8]`, `notchGain_[8]` baked at block-rate from `filterType[t]`.
- 8 SVFs = 2 NEON quads. Inner loop runs 4 filters in parallel,
  twice. No padding needed (kMaxTaps=8 is already multiple of 4).
- Branchless mode dispatch via per-tap-baked gain coefficients
  (zero for non-selected modes); replaces the switch.
- Apply: `tapOut = lp*lpGain + bp*bpGain + hp*hpGain + (lp+hp)*notchGain`.

**Estimated win: 2-3pp CPU** at full tilt. Filter math is ~120 FLOPs
(8 SVFs × 15 FLOPs scalar); NEON does ~30 effective FLOPs in 2 quads.
Save ~90 FLOPs/sample = ~12-15 cycles = ~2-3% CPU at 48 kHz.

**Risk**: low. Same proven pattern used in 4 other units now.
`util/neon_math.h` foundation already in place.

**Effort**: medium. ~150 lines of code (Internal additions + block-rate
bake loop + NEON inner kernel + scalar fallback).

### O2 — Pass C consolidation (filter + level + pan + energy in NEON) ⭐ MED FIT

After O1 produces 8 tap outputs in NEON quads, we can keep going:

- Level multiply: `filteredOut = tapOut * tapLevel` — 8 lanes in 2 quads
- Energy follower: `tapEnergy[t] += (e - tapEnergy[t]) * 0.001f` — 8 lanes
  - Note: could also be hoisted to block-rate (only used for viz)
- Pan accumulate: `wetL += filteredOut * panL[t]`, `wetR += ...` —
  vectorize as 2 separate quads (panL bank, panR bank), horizontal
  sum at end

Combined Pass C in NEON saves ~30-40 cycles/sample on top of O1.
**Estimated additional win: 1-2pp**.

### O3 — Block-rate filter mode bake (branchless dispatch) — BUNDLE WITH O1

Replace the per-sample `switch (filterType[t])` with block-rate-baked
`lpGain[t]`, `bpGain[t]`, `hpGain[t]`, `notchGain[t]`. Same pattern
as Parfait's SVF morph mix.

Pre-bake table:
- TAP_FILTER_OFF: bypass — handle outside SVF entirely with a "use
  raw tapOut" mask (analogous to `useSvfMask` in Parfait)
- TAP_FILTER_LP: lpGain=1, others=0
- TAP_FILTER_BP: bpGain=1, others=0
- TAP_FILTER_HP: hpGain=1, others=0
- TAP_FILTER_NOTCH: lp+hp combo (lpGain=1, hpGain=1, others=0)

**Estimated win: 1-2pp** when combined with O1 (eliminates the switch
branch mispredict cost).

### O4 — NEON grain phase + envelope update ⭐ LOW FIT

`mods/spreadsheet/MultitapDelay.cpp:758-780`. Per tap per sample,
up to 3 active grains each get:
- Hann LUT lookup (env)
- Buffer read × 2 (already scalar gather, can't NEON)
- Multiply by env, sum
- readPos advance + wrap
- Phase advance + active check

8 taps × 3 grains = 24 grain updates per sample. The arithmetic
parts vectorize (phase advance, env multiply, sum); the buffer reads
stay scalar (gather constraint).

**Estimated win: 0.5-1pp.** **Below 2pp threshold** — not worth the
effort given the constraint that the buffer reads still dominate.

### O5 — NEON smoothing + prefetch pre-pass ⭐ TINY

`MultitapDelay.cpp:697-706`. The per-tap smoothing update is 8 × 3
FLOPs = 24 FLOPs. NEON 4-lane: 2 quads = ~8 cycles vs scalar ~24
cycles. Save ~16 cycles/sample.

**Estimated win: ~0.3pp.** Below threshold.

### O6 — Block-rate energy follower ⭐ TINY

`MultitapDelay.cpp:823-824`. Update once per block from per-block RMS
instead of per-sample IIR. Save 24 FLOPs per sample × 128 = 3K FLOPs
per block. **~0.3pp.** Below threshold.

### O7 — Architectural: reduce buffer access count ⚠️ MEMORY-BOUND

The 64 reads per sample is the real cost. Options:

**O7a — Shared filter bank across taps** (BAD IDEA): the user wants
each tap to have its OWN filter (Rainmaker-style). Sharing breaks
the unit.

**O7b — Lower-quality interpolation modes** (USER CHOICE):
- Drop linear interp → nearest neighbor. Saves 8 reads/sample (50%
  fewer for non-grain path). Quality hit: audible HF artifacts.
- Could expose as user option "Quality: Hi / Lo" if CPU is critical.

**O7c — Pack tap reads spatially**: when taps are configured with
small spread (small masterTime), reads cluster naturally. When
spread is large, can't help. User-dependent.

**O7d — Grain reduction**: drop `kGrainsPerTap` from 3 to 2. Halves
grain memory accesses. Quality hit: COLA spec wants 3 for clean
50% overlap. **Probably off the table.**

**O7e — Smaller delay buffer**: dynamic allocation based on
`maxBufferTime` already in place. No further gain.

**Verdict on architectural changes**: largely off the table without
sacrificing the unit's character.

### O8 — Stagger prefetch (advance to next-sample) ⚠️ NEEDS BENCH

Current prefetch fires for current-sample reads. Could fire prefetches
for NEXT sample's reads (one sample ahead in tap loop) so memory has
even more time to land the lines.

Risk: on tight scheduling, advance prefetch may evict THIS sample's
hot data before it's fully consumed. Hardware-dependent.

**Estimated win: 1-3pp if it works, 0pp if it doesn't, potential
regression if prefetch evictions hurt.** Benchmark required.

### O9 — Hann LUT lane gather replacement ⚠️ TINY

The `lookupSine` LUT call per grain. Could replace with a polynomial
Hann approximation: `0.5 * (1 - cos(2π*phase))`. With NEON
`sine_poly_4lane` from `util/neon_math.h`, could compute 4 grain
envelopes in parallel. But each grain has its own phase, so it's
4-lane within a single tap's 3 grains (with 1 lane wasted).

**Estimated win: 0.3-0.5pp.** Below threshold.

### O10 — Feedback path NEON ⚠️ TINY

Feedback path is ~15 cycles scalar, single-stream. Not parallel
across anything. NEON not applicable. Skip.

## Ranked plan

| # | Change | Est. CPU drop | Actual | Effort | Status |
|---|---|---|---|---|---|
| 1 | **O1+O3+O2 bundled**: SoA NEON SVF bank + branchless mode dispatch + Pass C level/pan accumulate | 3-5pp | **~25pp** at baseline (8 taps + filters: 55%→30%) | Medium | **SHIPPED 2.6.2.47** |
| 2 | O8: advance prefetch experiment | 1-3pp or regression | — | Small + bench | conditional (Phase 2) |
| 3 | NEON grain phase + env update (cross-tap) | 0.5-1pp estimated, but ?? after Phase 1 result | — | Medium | reconsider for grain-heavy configs |
| 4 | O7b: optional Lo-Q interp mode (nearest-neighbor switch) | situational | — | Small | user-feature, not pure opt |
| ✗ | O5: smoothing pre-pass NEON | 0.3pp | — | Small | sub-threshold |
| ✗ | O6: block-rate energy | 0.3pp | — | Trivial | sub-threshold |
| ✗ | O7a: shared filter bank | breaks unit | — | — | reject |
| ✗ | O7d: kGrainsPerTap reduction | quality hit | — | — | reject |

## Realistic combined ceiling

Bundling #1 + #2 (the only above-threshold pure-NEON wins): **~4-7pp
total CPU drop** at full tilt.

If Petrichor measures ~30-40% baseline at 8 taps with grains, this
takes it to ~25-35%. Real improvement but not dramatic — Petrichor
remains memory-bound at the architectural level.

For a bigger win, would need architectural change:
- Reduce kMaxTaps (no — changes character)
- Reduce kGrainsPerTap (probably no — quality hit)
- Reduce delay buffer size (already dynamic)
- Lo-Q interp mode (user-facing feature, separate decision)

## 2.6.2.47 ship — actual hardware result

Baseline (8 taps + all filters engaged): **55% → ~30%** (~25pp drop,
~45% relative reduction). Far better than my 4-7pp estimate.

Top-end with grains/pitch/drift/reverse heavy config: still climbs to
~60%. Filter NEON didn't help that case much — confirms grain
machinery (still scalar Pass B) is the dominant cost when pitch is
active.

The estimate was off because:
1. **stmlib::Svf had significant per-tap function-call + template
   instantiation overhead** that scalar costing didn't capture
2. **Switch dispatch + 4 different template paths** had branch
   mispredict cost
3. **TPT SVF math is dependency-heavy on Cortex-A8** — NEON
   parallelizing across 4 lanes hides latency in ways that scalar
   pipelining can't

Lesson: the scalar baseline for stateful filter banks called via
template-dispatch is materially worse than the FLOP count suggests.
Same pattern likely explains the bigger-than-expected wins on
Filterbank and Impasto SVF banks earlier in this codebase.

## What's left (revised after Phase 1 measurement)

The remaining gap (grain-heavy top-end at 60%) is where any next
work would land. Three options ranked by likely effort/payoff:

### Phase 2 — Advance prefetch experiment (small, conditional)

Fire `__builtin_prefetch` for next-sample tap reads (one sample
ahead) during the pre-pass, instead of current-sample. Memory has
more cycles to land the lines.

Risk: advance prefetch may evict THIS sample's hot data before it's
consumed. Hardware-dependent and reversible.

**Expected: 1-3pp on the grain-heavy 60% case. Small code change.**

### Phase 3 — Per-tap grain NEON (medium, speculative)

3 grains per tap could pack into a NEON quad (1 lane wasted). The
arithmetic (phase, env, advance, multiply) vectorizes; the 2 buffer
reads per grain stay scalar gathers.

Per-tap grain loop currently ~60 cycles scalar. NEON might cut to
~30. × 8 taps = ~240 cycles/sample saved. At 800 MHz × 48 kHz =
**~1.5pp**. Below threshold even with the Phase 1 surprise factor
applied (which was a SVF-specific effect from template/function-call
overhead, doesn't translate to the grain loop which is already inline).

**Expected: 1-3pp. Below the 2pp threshold realistically.**

### Phase 4 — Lo-Q interp user option (feature, not opt)

Add a "Quality" parameter: Hi (current linear interp) vs Lo (nearest
neighbor). Lo halves buffer reads in both grain and direct paths.
**~5-10pp drop on Lo mode, with audible HF artifacts.**

This is a UX decision, not pure optimization. Would need user-facing
discussion before pursuing.

---

# Petrichor feedback rework (separate workstream)

User complaint: Petrichor "really does not produce much space" — at
typical 8-tap configs, feedback feels thin/monoacoustic instead of
filling out the delay network.

## Diagnosis

Current feedback path (`MultitapDelay.cpp:851-857` after Phase 1):
```cpp
lastTapOut = tapOutScratch_[lastActiveTapIdx]
           / (1.0f + cachedBandQ[lastActiveTapIdx] * 0.1f);
float fb = lastTapOut * fbNorm;  // fbNorm = feedback / (1 + 0.15 * sqrt(tapCount))
```

Two problems compound:
1. **Single-tap-source feedback** — only the *last* active tap
   contributes to the feedback loop. That makes the recirculation
   a single-echo comb, not a delay network. Pecto deliberately
   uses this shape (it's a Karplus-Strong / resonator unit); for a
   Rainmaker-style multi-tap delay it's the wrong default.
2. **Tap-count attenuation in `fbNorm`** — `feedback / (1 + 0.15 *
   sqrt(N))`. At N=8 this is feedback × 0.70. Combined with the
   single-tap source, the recirculation at full taps is ~70% of
   what a 1-tap config would produce, from one tap.

Together: at 8 taps the user sees a single echo loop attenuated to
~70%. That's the "no space" symptom.

## Lessons from prior feedback designs

### From Pecto (works as designed)
Single-tap feedback is **correct** for a comb/resonator (Pecto's
goal). The unit's character IS the single-tap recirculation.

### From Network Phase 2 (shipped, working)
Multi-tap weighted-sum feedback with `1/sqrt(N)` normalization +
per-tap weights. Each tap contributes a portion; summed total
energy stays roughly constant regardless of N (for uncorrelated
audio). User dials the feedback amount; weights take care of
stability.

### From Network cascade FDN postmortem (failed, reverted)
DO NOT retrofit FDN matrix math (Hadamard cross-feed, Jot decay
calibration, per-line state vector) onto a multi-tap structure.
The math fights itself:
- Per-sample vs per-round-trip matrix application disagreement
- Multi-tap |H(jω)| comb peaks vs unitary-matrix requirement
- Sparse vs dense state regime ambiguity
- Density × Hadamard normalization × T60 cancellation

20 commits over 5 days produced an unstable design that was
reverted to pre-FDN state. The takeaway: **stay with the simpler
weighted-sum, don't reach for FDN**.

### From Network's structural separation principle
- **Multi-tap = early reflections** (coloration mechanism)
- **Diffusion/space = SEPARATE late-tail mechanism** (FxEngine
  allpass loop, if added)

If multi-tap weighted feedback alone doesn't give enough space,
the answer is to ADD a parallel diffusion stage (Phase B) — not
to try to make the multi-tap path do double duty.

## Phase A design — Multi-tap weighted feedback

Replace `lastTapOut` single-tap extraction with weighted-sum dot
product over all active taps. Energy normalized by `1/sqrt(N)`;
per-tap Q-compensation preserved.

### Code changes

**1. Internal addition** (`MultitapDelay.cpp:62`):
```cpp
// Per-tap baked feedback contribution weight. Folds:
//   - feedback amount
//   - 1/sqrt(activeTapCount) energy normalization
//   - per-tap Q compensation: 1 / (1 + bandQ * 0.1) for filtered taps,
//     1.0 for OFF-mode taps
//   - tap-active mask: 0 for muted (tapLevel < 0.001) and unused lanes
// NEON-loaded for per-sample weighted-sum dot product.
float fbWeightSum_[kMaxTaps] __attribute__((aligned(16)));
```

**2. Init()** — zero `fbWeightSum_[i]` alongside existing inits.

**3. Block-rate setup** — replace the existing single line:
```cpp
float fbNorm = feedback / (1.0f + 0.15f * sqrtf((float)tapCount));
```
with a per-tap weight bake (placed AFTER the filter coefficient bake
so `cachedBandQ_[t]` is current):
```cpp
// Count active taps for 1/sqrt(N) normalization
int activeTapCount = 0;
for (int t = 0; t < tapCount; t++)
  if (s.tapLevel[t] >= 0.001f) activeTapCount++;
float fbNormFactor = (activeTapCount > 0)
    ? feedback / sqrtf((float)activeTapCount)
    : 0.0f;

for (int t = 0; t < tapCount; t++) {
  if (s.tapLevel[t] < 0.001f) {
    s.fbWeightSum_[t] = 0.0f;
  } else {
    // Q-comp only for filtered taps; OFF mode taps get unity comp
    float qComp = (s.filterType[t] != TAP_FILTER_OFF)
        ? (1.0f / (1.0f + s.cachedBandQ[t] * 0.1f))
        : 1.0f;
    s.fbWeightSum_[t] = fbNormFactor * qComp;
  }
}
for (int t = tapCount; t < kMaxTaps; t++)
  s.fbWeightSum_[t] = 0.0f;
```

**4. Remove `lastActiveTapIdx` tracking** — no longer needed.

**5. Per-sample inner loop** — replace:
```cpp
lastTapOut = s.tapOutScratch_[lastActiveTapIdx]
           / (1.0f + s.cachedBandQ[lastActiveTapIdx] * 0.1f);
...
float fb = lastTapOut * fbNorm;
```
with NEON dot product (2 quads):
```cpp
// Multi-tap weighted-sum feedback: sum(tapOutScratch_[t] * fbWeightSum_[t])
// across 8 lanes. Energy normalized by 1/sqrt(N) at block rate so
// total feedback contribution stays bounded as N varies.
float fb;
#if defined(__ARM_NEON__) || defined(__ARM_NEON)
  {
    float32x4_t tap0 = vld1q_f32(&s.tapOutScratch_[0]);
    float32x4_t tap1 = vld1q_f32(&s.tapOutScratch_[4]);
    float32x4_t w0   = vld1q_f32(&s.fbWeightSum_[0]);
    float32x4_t w1   = vld1q_f32(&s.fbWeightSum_[4]);
    float32x4_t fbQ  = vmlaq_f32(vmulq_f32(tap0, w0), tap1, w1);
    fb = vgetq_lane_f32(fbQ, 0) + vgetq_lane_f32(fbQ, 1)
       + vgetq_lane_f32(fbQ, 2) + vgetq_lane_f32(fbQ, 3);
  }
#else
  fb = 0.0f;
  for (int t = 0; t < kMaxTaps; t++)
    fb += s.tapOutScratch_[t] * s.fbWeightSum_[t];
#endif
```

**6. Existing tone shaping stays intact** — `fbFilterState` LP IIR,
optional `fbHpState` HP path, `if (|fb| > 1.5) fast_tanh()` soft clip,
`bufWrite()` injection. All downstream of the new fb computation,
unaffected.

### Stability analysis

Per-tap fb contribution: `tapOutScratch_[t] × (feedback / sqrt(N) / qComp[t])`.

**Uncorrelated audio** (typical case): sum RMS ≈ sqrt(N) × per-tap-RMS
× (feedback / sqrt(N)) = feedback × per-tap-RMS. Same as single-tap
case — designed-in by sqrt(N) normalization.

**Correlated worst case** (taps share filter freq, resonant peaks
align): peak ≤ N × per-tap-peak × (feedback / sqrt(N)) = sqrt(N) ×
per-tap-peak × feedback. At N=8, feedback=0.95: peak ≤ 2.69 ×
per-tap-peak. The existing `|fb| > 1.5 → fast_tanh` soft clip catches
this. **No new stability rule needed** — Phase 1's soft clip handles
the worst case.

Compared to current design: at N=8, current peak feedback contribution
is `tapMax × 0.70` (single tap, attenuated). New design peak is
`tapMax × 2.69` worst case (8 correlated taps), or `tapMax × 0.95`
typical (uncorrelated). The dynamic range of "spatial feel" expands
in both directions: more headroom for rich pattern AND more potential
for hot peaks (clipped by the existing tanh).

### CPU impact

- Adds: 4 NEON loads + 1 mul + 1 mla + horizontal sum ≈ 8 cycles/sample
- Removes: scalar lastTapOut compute ≈ 3 cycles/sample
- Net: ~5 cycles/sample added = **~0.06% CPU**. Negligible.

The CPU win from Phase 1 (25pp drop) easily absorbs this.

### Tone-shaping audition concerns

The existing `fbFilterState` LP IIR was tuned for single-tap
feedback. With multi-tap weighted-sum:
- LP IIR sees richer broadband content → tone control still works
  (it's just a filter) but the perceived "darkness" curve may feel
  different
- HP path likewise
- Soft clip threshold may engage more often on dense audio

These are audition-gated. Tuning may want a follow-up if defaults
feel wrong.

## Out of scope (Phase A specifically)

- Per-tap user-configurable feedback contribution (Network's
  "connectivity" / fb selection policy). Default all-active-equal
  weight is the cleanest first cut.
- Cross-tap diffusion / Hadamard matrix. Explicitly avoided per
  Network postmortem.
- FxEngine diffusion stage (Phase B). Separate UX decision.

## Verification

1. `make spreadsheet ARCH=linux && cp testing/linux/*.pkg ~/.od/rear/`
2. `make spreadsheet ARCH=am335x`
3. `tools/check-neon-hints.sh
   testing/am335x/mods/spreadsheet/MultitapDelay.o` — 0 SUSPECT
4. Hardware audition (the load-bearing test):
   - **Confirm space**: at 8 taps, feedback ~0.5-0.7, listen for
     pattern recirculation (not single-echo). Should feel
     spatial / diffuse rather than thin / single-bounce.
   - **Stability**: push feedback to ceiling (0.95), various tap
     configs, filter settings. No runaway, no harsh resonance
     buildup. Soft clip should engage cleanly when triggered.
   - **No regression on single-tap feel**: at tapCount=1, weighted
     sum = single tap × (feedback / 1) = feedback × tap (same as
     pre-rework). Single-tap configs should sound identical to
     before within float precision.
5. Emu A/B at tapCount=1: RMS within float-precision error.
   At tapCount=8: WILL differ (designed behavior change).
6. PKGVERSION bump 2.6.2.47 → 2.6.2.48.

## Implementation phases

### Phase 1 — SoA NEON SVF bank + branchless dispatch (O1 + O3)

**Files**: `mods/spreadsheet/MultitapDelay.cpp` only.

1. Add to `Internal` (heap-allocated, NEON-safe):
   ```cpp
   float svfG_[8] __attribute__((aligned(16)));
   float svfR_[8] __attribute__((aligned(16)));
   float svfH_[8] __attribute__((aligned(16)));
   float svfS1_[8] __attribute__((aligned(16)));
   float svfS2_[8] __attribute__((aligned(16)));
   float lpGain_[8] __attribute__((aligned(16)));
   float bpGain_[8] __attribute__((aligned(16)));
   float hpGain_[8] __attribute__((aligned(16)));
   float notchLpGain_[8] __attribute__((aligned(16))); // for notch: lp+hp combo
   float notchHpGain_[8] __attribute__((aligned(16)));
   float useFilterMask_[8] __attribute__((aligned(16))); // 0=bypass, 1=active
   float tapOutScratch_[8] __attribute__((aligned(16))); // pass-B gather output
   ```

2. Block-rate setup loop (replaces existing `filters[t].set_f_q` block):
   - Compute g/r/h directly into `svfG_/R_/H_` (mirror Filterbank).
   - Bake `lpGain_/bpGain_/hpGain_/notchLpGain_/notchHpGain_` from
     `filterType[t]`. Set `useFilterMask_[t] = (filterType != OFF) ? 1 : 0`.

3. Per-sample inner loop restructure to 3-pass:
   - **Pass A**: existing pre-pass (smoothing + prefetch).
   - **Per-tap scalar pass**: grain machinery + buffer reads + interp.
     Write each tap's pre-filter output into `tapOutScratch_[t]`.
   - **Pass C (NEON)**: load `tapOutScratch_` into 2 quads, run 4-lane
     SVF kernel × 2, blend lp/bp/hp/notch per per-tap-baked gains,
     branchless mask between filter output and raw tap (for OFF mode),
     multiply by `tapLevel`, pan accumulate to `wetL` + `wetR`,
     horizontal sum at end.

4. File-level `#pragma GCC optimize("no-tree-vectorize")` per
   `feedback_neon_hint_surfaces`.

5. Hint audit (`tools/check-neon-hints.sh`) — expect 0 SUSPECT.

6. Hardware audition: drive Petrichor at 8 taps with filters on every
   tap (LP/BP/HP/Notch mix). Compare CPU vs current baseline; verify
   audio fidelity (RMS A/B in emu within 1e-4).

7. PKGVERSION bump.

### Phase 2 — Advance prefetch experiment (O8)

Conditional on Phase 1 success. Modify the prefetch pre-pass to fire
for NEXT sample's reads. Measure on hardware: CPU drop vs Phase 1
baseline. If positive: keep. If neutral or negative: revert.

## Verification gates

Standard per `feedback_linux_build_auto_install`,
`feedback_package_version_bump`:
1. `make spreadsheet ARCH=linux && cp testing/linux/*.pkg ~/.od/rear/`
2. `make spreadsheet ARCH=am335x`
3. `tools/check-neon-hints.sh testing/am335x/mods/spreadsheet/MultitapDelay.o`
   — 0 SUSPECT hints
4. Emu A/B: 8-tap config with filters, RMS within 1e-4
5. Hardware audition: tap+filter+grain config at user's typical use
6. CPU drop measured ≥ 60% of estimate (≥ 2.5pp on a 4pp estimate)

## Cross-references

- `mods/spreadsheet/Filterbank.cpp` — canonical SoA SVF bank pattern;
  Phase 1 SVF kernel lifts directly
- `mods/spreadsheet/MultibandSaturator.cpp` Phase 3 — branchless
  morph dispatch via `useSvfMask`; Phase 1 mode dispatch mirrors it
- `mods/spreadsheet/util/neon_math.h` — foundation; not directly
  needed here (SVF is pure FLOPs, no transcendentals)
- `feedback_neon_soa_svf_bank` — SoA filter bank template
- `feedback_neon_delay_gather` — 3-pass pattern (Pecto). Petrichor's
  pattern is similar but with grain machinery insertion between
  Pass B and Pass C.
- `feedback_neon_no_gather_lut_dsp` — explains why grain reads
  (different addresses per grain) can't NEON
- `feedback_neon_intrinsics_drumvoice` — class-member scratch arrays
  rule (Pass B output goes in `tapOutScratch_` in Internal, not stack)
- `feedback_neon_hint_surfaces` — file-level `no-tree-vectorize`
- `planning/neon-opportunities.md` — overall audit; Petrichor entry
  to be updated post-Phase-1
- `planning/multiband-units-cpu-reduction.md` — reference for the
  recent Phase 3 SVF bank work in MultibandSaturator
