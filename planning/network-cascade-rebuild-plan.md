# Network — serial cascade rebuild (plexus topology)

## Context

`mods/spreadsheet/Network.h` currently implements a **star multitap**: one shared int16 delay buffer, all 64 taps read in parallel at their own delays, single feedback sum back to one write head. The visual metaphor (`NetworkOverviewGraphic`) reads as a plexus, but the audio engine is parallel multitap with selectable feedback. The original brief (`planning/spatial-effect-hybrid.md`) framed this as an intentional hybrid; recent listening sessions concluded the audio doesn't read as a plexus — just as a lush multitap. The addendum at the bottom of `planning/network-design-notes.md` sketched three architectural shapes for evolving toward true routed-through-network audio; this plan executes **Option 1 — serial cascade by distance** with the user's specific framing:

- Taps live in **groups of 4** (NEON-native width).
- Signal flows cascade-style: group 0 → group 1 → … → group N. Group I+1's input = group I's mono output.
- `size` parameter expands/contracts the whole network — at size=1 the last group is at maximum distance from listener.
- `connectivity` controls BOTH (a) local feedback depth inside each group AND (b) each group's contribution to a global feedback pool.
- Tap-to-group assignment + intra-group placement is driven by a per-instance stable seed (the `mPerturbSeed` idiom from `NetworkOverviewGraphic`); **static after construction** — motion knob does not audibly re-route audio (no Doppler from rerouting).
- Per-tap glitch modes (MUTE/STUTTER/CRUSH/SCRUB/REVERSE) survive almost verbatim; they were already per-tap and become per-tap-in-group with minimal code change.

The visual (`NetworkOverviewGraphic`) reads per-tap state via accessors that continue to work unchanged; visualization is decoupled from audio topology.

Intended outcome: audio that audibly propagates through stages of the tap network rather than firing all reflectors simultaneously. Connectivity becomes a single perceptual axis from "open delay-line cascade" (conn=0) to "diffuse network reverb" (conn=1).

## TL;DR decisions

| Decision | Choice | Why |
|---|---|---|
| Group size | **4 taps** (NEON 4-wide native; 16 groups at full density) | One NEON iteration per group, no tail loop, tight local feedback round-trip. 8/16 give too few groups for serial-cascade illusion. |
| Memory layout | **One shared int16 buffer with per-group sub-windows** | Keeps `allocateTimeUpTo` API; 96 kB total in one TLB region; per-group ring is just origin + length + write index. |
| Group I+1 input | **Group I's mono output** (no dry input bypass to groups ≥ 1) | True cascade. Input bypass dilutes the topology. |
| Wet bus | **Sum of every group's pan-multiplied output** | Every group is audible; size controls spatial extent, not which group reaches the listener. |
| Local feedback | **One float per group**: `groupLocalFbState[g] = group mono out`, gain `conn × 0.6`, fed back into group input on next sample | Cheap (one float per group), uses the group's own taps for coloration via subsequent reads. No per-group allpass. |
| Global pool | **Sum of last 1/3 of active groups** → existing 4-stage Schroeder → mixed by `soften` (= conn) → injected only into group 0's input | Avoids correlation/runaway. Group 0 is the natural injection point ("recycle back to head"). |
| `size` curve | **Quadratic**: `delayTarget = sizeNorm² × ((g+1)/numActiveGroups) × intraGroupOffset × groupLen` | Linear feels weighted heavy; geometric blows the buffer; quadratic gives perceptual resolution at the short end. |
| Group base spacing | **Linear in group index** | Matches "group N is N steps further than group N-1." Log/geometric compresses or overflows. |
| Intra-group offsets | Hashed from `mCascadeSeed ^ (group*4 + slot)` to `[0.1, 0.9]` × groupLen | Margins avoid 0-delay click and inter-group buffer collision. Per-instance stable. |
| Tap-to-group | Sort reflectors by distance from listener centerline, take groups of 4 by sorted rank, then shuffle intra-group order via `mCascadeSeed` | Sorted-by-distance gives the cascade structure; per-instance shuffle gives the wobble. |
| Pan / stereo | **Keep listener-azimuth pan per tap** (unchanged from current `recomputeTaps`) | Cascade-position pan would force the field to feel like a left-to-right line. |
| NEON preservation | **Yes everywhere except inter-group cascade dependency** | Pass A/B/C per group = exactly one NEON iteration. Inter-group sequencing is the only new serial constraint. |
| CPU estimate | **~7% stereo on AM335x** (current is ~6%) | Same total tap math; +16 horizontal sums per sample (~96 cyc), +16 group writes (~64 cyc), +16 local fb reads (~48 cyc). |
| Migration | **Replace Network.h entirely**; git history is the fallback | Two engines in one file = SWIG/graphic surface doubled, maintenance trap. |
| Visual changes | **None required** | Every accessor (`getTapMode`, `getFbWeight`, `getReflectorX/Y`, etc.) still works. Optional follow-up: color taps by group index. |

## Architecture

### Memory layout — shared int16 buffer with sub-windows

```
[0 ........................ maxDelay-1]
 │←─G0─→│←─G1─→│ ... │←─G15─→│
 W0     W1            W15
```

- `mBuffer` = single int16 ring, allocated via existing `allocateTimeUpTo()` (no API change).
- `groupOrigin[NETWORK_NUM_GROUPS_MAX=16]` — start sample index in the ring (block-rate constant).
- `groupLen[16]` — length of each sub-window. Default split: `maxDelay / NUM_GROUPS_MAX` for each group.
- `groupWriteIndex[16]` — per-sample write head, wraps within `[0, groupLen[g])`.
- Per-tap `mTapOldReadIdx[t]` and `mTapNewReadIdx[t]` are **group-relative** offsets in `[0, groupLen[g])`. Pass A wraps modulo `groupLen[g]`. Pass B adds `groupOrigin[g]` before indexing `buf`. This is the only invasive change to existing Pass A/B/C arithmetic.

### Per-sample serial loop

```
x_in = (DC-blocked, scaled input)
g_prev_out = 0
wetL = wetR = fb_pool_in = 0

for (g = 0; g < numActiveGroups; g++):
    group_in = (g == 0) ? x_in : g_prev_out
    group_in += groupLocalFbState[g] * (conn × 0.6)
    if g == 0:  group_in += diffusedGlobalPool   // from previous sample

    bufWrite(buf, groupOrigin[g] + groupWriteIndex[g], tanh(group_in))

    // Pass A (NEON 4-wide advance read indices, wrap mod groupLen[g])
    // Pass B (scalar gather sA, sB from buf[groupOrigin[g] + idx])
    // Pass C (NEON 4-wide interp + LP + crush + triple FMA into:
    //         wetLg / wetRg / groupMonoAcc — local vector accumulators)
    // Horizontal-sum NEON accumulators into scalars (per-group, once)

    group_out_mono = groupMonoAcc + groupMonoStutterAcc[g]
    wetL += wetLg + groupStutterWetL[g]
    wetR += wetRg + groupStutterWetR[g]

    if g >= numActiveGroups - max(1, numActiveGroups/3):
        fb_pool_in += group_out_mono

    groupLocalFbState[g] = group_out_mono
    g_prev_out = group_out_mono

    groupWriteIndex[g]++
    if groupWriteIndex[g] >= groupLen[g]: groupWriteIndex[g] = 0

// Global pool (computed this sample, used NEXT sample at group 0)
fb_pool_tanh = networkFastTanh(fb_pool_in × conn × decay / sqrt(numFbGroups))
fb_pool_dc   = DCblock(fb_pool_tanh)
diffused     = allpass4Chain(fb_pool_dc)                  // existing chain
diffusedGlobalPool = fb_pool_dc + soften × (diffused - fb_pool_dc)

// Dry/wet mix and output DC block — unchanged
mixedL = x_in × (1-wet) + wetL × wet  → output DC blocker → outL[i]
mixedR = x_in × (1-wet) + wetR × wet  → output DC blocker → outR[i]
```

### Block-rate setup

1. **Tap-to-group assignment** (re-run only on `mSeed` change or first process): sort reflector indices by distance from listener centerline, take groups of 4 by sorted rank. Stash sorted-order in `mTapGroupMap[t] → g` and `mTapGroupSlot[t] → 0..3`.
2. **Intra-group offsets** (re-run on `mSeed` change): hash `mCascadeSeed ^ (g*4 + slot) × 2654435761u` to a [0.1, 0.9] float in `mTapIntraGroupOffset[t]`.
3. **Per-tap delay target** (every block, depends on `size`): `delayTarget[t] = sizeNorm² × ((g+1)/numActiveGroups) × mTapIntraGroupOffset[t] × groupLen[g]`. Done in a new `network_geom::recomputeCascadeTaps()` helper (see Critical Files).
4. **Pan/gain** (every block): unchanged. `recomputeTaps` continues to write `mTapGainL/R[t]` using listener-azimuth pan.
5. **Mode mutex, glitch state, G4 ricochet, G7 respawn** — unchanged. They operate on per-tap arrays.
6. **Stutter trigger setup** — gain extra field `mStutGroup[s]` to record which group each active stutter tap belongs to (so the scalar stutter pass can route into the right per-group accumulator).

### Mode mapping onto tap-in-group

- **MUTE / CRUSH / SCRUB / REVERSE**: per-tap in Pass C — *zero code change*. They operate on per-tap arrays that the cascade still maintains.
- **STUTTER**: scalar pass survives; its sample contributions accumulate into `groupMonoStutterAcc[mStutGroup[s]]` instead of one shared `wetL/wetR/fbSum`. Each group's serial-loop body adds its `groupMonoStutterAcc[g]` into its mono output → stutter content flows through the rest of the cascade and gets local-feedbacked just like normal tap energy.
- **G7 respawn**: reflectors mutate; group assignments do **not** re-sort. A respawned reflector keeps its slot with new (x,y). Documented as intentional ("respawn breaks sorted order audibly").

### Gain normalization scopes

The legacy star multitap has two normalization scopes; the cascade needs a third the original plan missed:

| Scope | Where | Math | Purpose |
|---|---|---|---|
| Per-tap wet bus energy | `network_geom::recomputeTaps` via `densityCompGain = 2.5 × N^-0.4` | density-compensating gain factor | Keeps wet bus RMS stable across density sweep without coupling density to amplitude. |
| Feedback bus | Existing `fbWeightUnit = 1/sqrt(kRecycle)` | Statistical sqrt-norm | Keeps fb pool RMS stable for incoherent tap sum. |
| **Inter-stage cascade (NEW)** | `g_prev_out = groupMono × (1/kNetworkGroupSize)` | `1/N` average instead of sum | **Prevents inter-stage gain runaway.** Without this, each group's bufWrite tanh-saturates the prior stage's amplified signal, producing square-wave harshness by group 5–10. The 1/N (not 1/√N) factor handles the coherent-worst-case (intra-group reads are clustered and partially correlated). Discovered during Phase A audit (2.6.1.64 sounded harsh) and patched in 2.6.1.65. |
| **Wet bus compensation (NEW)** | `wetL/R *= (1.0 + activeGroups / 16.0)` after cascade group loop, before mix | Linear-in-aG | **Compensates for per-stage tanh attenuation in cascade buffers.** Cascade flow is itself correctly normalized, but each group's `bufWrite(tanh(group_in))` attenuates buffer contents geometrically — by group 16, buffer levels are ~0.5x of input. Wet bus reads sample those attenuated buffers, so summed wet output undervolumes without compensation. Coupled to aG so shorter cascades (less cumulative atten) get proportionally less boost. Empirically calibrated from .65 audit (user reported 0.5x at full wet). Patched in 2.6.1.66. May need recalibration at Phase B when feedback enters and changes per-stage levels. |

### NEON preservation map

| Loop | Stays NEON? | Notes |
|---|---|---|
| Per-sample smoother (gainL/gainR/fbWeight) | Yes (4-wide over kMaxNetworkTaps) | Unchanged from current. |
| Pass A read-idx advance | Yes (one 4-wide iter per group) | Wraps mod groupLen[g] instead of maxDelay. |
| Pass B scalar gather | Scalar (as today) | Adds groupOrigin[g] offset. |
| Pass C interp + LP + crush + FMA | Yes (one 4-wide iter per group) | Per-group horizontal-sum via `vpadd_f32` adds ~6 cyc/group. |
| Inter-group sequencing | **Serial** | Price of cascade. ~16 iter at full density. |
| Stutter scalar pass | NEON 4-wide as today | Now uses `mStutGroup[s]` to route into per-group accumulators. |
| Pool diffusion | Scalar (as today) | Single serial chain. |

## Critical files

- **`mods/spreadsheet/Network.h`** — Replace entirely. New state members (group origins/lengths/write-indices, local fb state, cascade seed, intra-group offsets, tap-group map, per-group stutter accumulators). New per-sample serial cascade loop body. Existing `networkFastTanh`, DC blocker pattern, 4-stage Schroeder, mode-mutex code, G4/G7 logic, stutter scalar pass — all reused with minor adjustments.
- **`mods/spreadsheet/network/geometry.h`** — Add new `recomputeCascadeTaps()` helper that produces per-tap group-relative `delayTarget` using `sizeNorm² × groupBaseFrac × intraGroupOffset`. Existing `recomputeTaps` is **kept** because pan/gain math is unchanged and still used. New helper composes alongside it.
- **`mods/spreadsheet/NetworkOverviewGraphic.h`** — No mandatory changes. Every accessor still works. Optional follow-up: add a group-coloring layer.
- **`planning/network-design-notes.md`** — Add a "Cascade shipped" section after Phase E lands.

Reference-only (read, don't edit):
- `mods/spreadsheet/Pecto.cpp` — NEON 3-pass template, lines 576–707.
- `planning/spatial-effect-hybrid.md` — Original brief.

### Per-group LP damping on local feedback (added in Phase B)

Phase B audition (2.6.1.68) revealed a second stability issue separate from DC drift: at moderate settings (density 0.5, size 0.35, conn 0.75+), audible mid-range rumble accumulated. Mechanism: short per-group delays at low size (~7ms @ size=0.35, ~140 Hz fundamental) behave as undamped Karplus-Strong loops when fed back unfiltered. The legacy star multitap had L3 per-tap LP for this damping; the cascade rebuild deferred L3 to Phase C, leaving local fb loops undamped.

Fix landed in 2.6.1.69: per-group one-pole LP filter on the local fb signal *before* injection into `group_in`. Same one-pole topology as the DC blocker, just LP. State: `mGroupFbLpState[16]`. Coefficient `kFbLpAlpha = 0.5 − 0.35 × decay` — light damping at low decay (crisp echoes preserved), heavier at high decay (long sustain stays stable). Matches the legacy L3 design intent of decay-coupled damping.

### Per-group DC blocker (added in Phase B)

Phase B audition (2.6.1.67) confirmed the plan's "rumble accrues at high conn" risk — the per-group local recirculation accumulates DC from asymmetric tanh saturation, then amplifies through subsequent samples. Mitigation landed in 2.6.1.68 as a per-group one-pole DC blocker.

State: `mGroupDcX1[16]`, `mGroupDcY1[16]`. Same one-pole topology and R coefficient (`kNetworkDcR`) as the existing input / output / global-pool DC blockers. Applied to `g_prev_out` immediately after the cascade-flow normalization, *before* `g_prev_out` is used as (a) next group's cascade input, (b) current group's `mGroupLocalFbState` write, or (c) pool contribution. One blocker per group catches DC for all three downstream consumers.

### Rank-based uniform tap distribution (added in Phase B)

Phase B audition (2.6.1.74) reported "closer to reverb but still slapback / resonator-y; tap staggering across size should produce density naturally." Velvet-Noise FDN literature (DAFx 2020) and Schlecht's "FDN Echo Density and Mixing Time" gave the structural diagnosis:

**Echo density = N delay lines × M taps per line, but only when delays are well-distributed.** Our prior `delayTarget = sizeNorm × ((g+1)/aG) × intraOffset × Ns` with `intraOffset ∈ [0.1, 0.9]` gave:
- Group 0: delays in `[0.006, 0.056] × Ns × sizeNorm`
- Group 7: delays in `[0.05, 0.45] × Ns × sizeNorm`
- Group 15: delays in `[0.1, 0.9] × Ns × sizeNorm`

Adjacent groups' ranges overlapped massively — group 15's range alone covered every other group's range. We had 64 tap reads but effectively ~16 distinct echo-time clusters because most taps clumped into similar bands. Combined with Jot per-group attenuation (correct for T60 but it means short-delay groups dominate early density), the system produced slapback-like character at high density.

Fix landed in 2.6.1.75:

- **`recomputeCascadeAssignment` populates `mTapIntraGroupOffset[t]` with rank-based delay fractions.** `rank[t] = g × kNetworkGroupSize + slot` (already determined by distance sort). `frac = (rank + 0.5 + jitter) / N` where `jitter ±0.4/N` is a small mCascadeSeed-derived perturbation. Result: 64 unique delay fractions in `[0, 1]` covering the full range with no inter-group overlap. Each group's 4 taps occupy a contiguous 1/16 slice.
- **`geometry.h::recomputeCascadeTaps` reads the rank-fraction directly**: `delayTarget = sizeNorm × tapDelayFrac × 0.9 × groupLen[g]`. The `((g+1)/aG)` factor is GONE — group structure for cascade routing is preserved via the sorted `tapGroupMap`, but the delay range no longer compresses based on group index.

User's "tap staggering across size should produce density naturally" maps directly: at small size, all 64 delays compress proportionally (tight slapback cluster); at large size, they spread uniformly across nearly the whole buffer (dense reverb). Group 0 still hosts the 4 shortest delays (closest reflectors), group 15 the 4 longest (farthest reflectors) — cascade routing intact.

### Jot per-group T60 attenuation + linear size mapping (added in Phase B)

Phase B audition (2.6.1.73) reported "sounds like FDN now (good), but decay produces negligible effect compared to OG, and delay times feel short off the taps." Both diagnoses required deeper FDN literature:

**Why decay was still nearly dead even with Hadamard cross-feed.** Per Jot 1991 (canonical FDN paper, summarized in Stanford CCRMA's PASP and the 2024 differentiable FDN paper): T60 in an FDN is controlled by **per-delay-line attenuation filters**, not by scaling the matrix gain. Each delay line gets `g_g = 10^(-3 × D_g / (T60 × Fs))` so all delay lines decay at the same rate per frequency, regardless of their length. Uniform matrix gain (what we had as `decay × conn × 1/√16`) gives short-delay groups (~2-5ms) and long-delay groups (~500-900ms) the same per-round-trip gain — but they go around the loop at vastly different rates per second, so the actual T60 is wildly different across groups. The structurally-correct Hadamard cross-feed was in place, but the decay knob was scaling the wrong parameter.

**Why delay times felt short off the taps.** The original cascade plan specified `delayTarget = sizeNorm² × groupBaseFrac × intraOffset × groupLen` (quadratic size mapping), chosen for "perceptual resolution at the short end." But the squaring squishes moderate sizes severely — at size=0.5, sizeSq=0.25, giving 1/4 the OG linear delay range. Legacy uses linear `sizeNorm × distNorm × maxDelay`. Switching to linear restores OG-like delay range at moderate sizes where most musical use lives.

Fix landed in 2.6.1.74:

- **`mGroupDecayCoef[16]`** — block-rate computed per-group attenuation coefficient. Average tap delay `D_g` accumulated alongside `mTapNewReadIdx` setup. T60 from `decay`: `T60 = 0.05 + decay² × 5` seconds (50 ms at decay=0, 5.05 s at decay=1). Coefficient `expf(-3 × ln(10) × D_g / (T60 × Fs))`. Floored at 0, capped at 0.999 to prevent unconditional losslessness.
- **`kCrossFeedScale = connectivity × (1/√16)`** — Hadamard normalization × cross-feed enable. T60 control is now ALL in `mGroupDecayCoef[g]`. Matrix gain is just "is FDN cross-feed on at all."
- **Per-sample injection**: `group_in += mHadamardScratch[g] × kCrossFeedScale × mGroupDecayCoef[g]`. Each group's feedback now decays at the user's chosen T60 regardless of its delay length.
- **`geometry.h::recomputeCascadeTaps`** switched from `sizeSq` to `sizeNorm`. Per-tap delay range now matches OG at every size setting.

Result: decay knob now directly controls reverb tail length (T60), as in legacy. Delay times at moderate size match OG's linear scaling.

### Hadamard 16×16 FDN cross-feed (added in Phase B, replaces pool path)

Phase B audition (2.6.1.72) reported "still sounds like a resonator more than a reverb, much more space at lower densities than higher." Research against the established FDN reverb literature (CCRMA / Stanford, DAFx FDN Toolbox, Signalsmith, Valhalla DSP writings) identified the architectural cause:

**The structural problem: rank-1 cross-feed.** The defining property of a proper FDN reverb is a **full-rank cross-feed matrix** between delay lines — Hadamard or Stautner-Puckette matrices are the canonical choice because they give "the maximum amount of inter-channel mixing" (CCRMA). Every delay line's output feeds back into every delay line's input with orthogonal (sign-varied) weights, producing N mutually orthogonal modes.

Our prior cascade had **rank-1 cross-feed**: one scalar (`mDiffusedGlobalPool`) was computed from the tail-third sum and injected into all 16 groups uniformly. The effective cross-feed matrix was constant across rows i → rank-1 → exactly one dominant feedback mode → all 16 groups collapse into one coupled resonator. This matched all three audition symptoms:
- "Resonator more than reverb": rank-1 is mathematically a resonator (one dominant eigenvalue).
- "More space at lower densities": at low density, fewer groups active, the rank-1 coupling degenerates into a sparse multitap with distinct echoes. At high density, all 16 modes collapse into the rank-1 coupling.
- "Smear above density 0.1": 64 simultaneous tap reads × rank-1 cross-feed = harmonic mode collapse → wash.

Fix landed in 2.6.1.73:

- **Replaced pool path entirely with Hadamard FDN cross-feed.** Per sample, before the per-group loop: collect previous-sample per-group outputs `G[16] = mGroupLocalFbState[0..15]` into `mHadamardScratch[16]`, apply 16-point Fast Walsh-Hadamard Transform (FWHT) via butterflies (4 stages × 8 add/sub = 64 ops total, no multiplies). Result `M[g]` is each group's cross-feed contribution from all 15 other groups with orthogonal ±1 weighting.
- **Per-group injection**: `group_in += mHadamardScratch[g] * kCrossFeedScale` where `kCrossFeedScale = connectivity × decay × (1/√16)`. The 1/√16 = 0.25 normalizes the Hadamard matrix to unitary (operator norm 1), so decay × connectivity is the actual spectral radius of the feedback loop. Decay=conn=1 = marginally stable infinite tail (clamped by bufWrite tanh + per-group LP+HPF). Decay=0 = no cross-feed → cascade reads as 16 independent damped delay lines.
- **Pool source/diffusion/HPF removed** from the per-sample cascade path. Hadamard alone provides decorrelation/mixing (FDN literature: proper mixing matrix doesn't need additional allpass diffusion). State members `mDcFbX1/Y1`, `mGroupPoolSign`, `mPoolHpX1/Y1`, `mDiffusedGlobalPool`, `mApBuf{1..4}` retained as declared (still used by the legacy non-cascade path) but unused in cascade.

CPU cost of Hadamard cross-feed: 64 add/sub + 16 multiplies per sample = ~80 ops × 48 kHz = 3.8 M ops/sec. Trivial on Cortex-A8 (~1% CPU). The pool path it replaced was 4-stage allpass + DC + HPF + tanh = significantly more expensive; net CPU is lower.

### Multi-group pool injection + widened decay→local-fb range (added in Phase B)

Phase B audition (2.6.1.71) reported "still smears at any non-low density, decay is still dead, OG produced much more ring-out." Diagnosis required tracing the legacy feedback path:

**How legacy decay actually worked** (Network.h:1066-1094, 1974-2343):
1. `fbWeightUnit = decay / sqrt(kRecycle)` — decay scales per-tap fb weight directly.
2. `mFbWeight[t] = sign × fbWeightUnit` for `kRecycle = conn × activeTaps` selected taps.
3. Per sample: `fbSum = Σ tap[t] × fbWeightSmoothed[t]` summed across all 64 taps.
4. `fbTanh → DC block → 4-stage allpass → fb`.
5. `bufWrite(buf, writeIdx, tanh(x + fb))` — **fb added to the SINGLE write head that ALL 64 taps read from**.

The critical pattern: legacy decay controls feedback into the *one* write head, and **every tap re-echoes it** after its delay. That's what produces the tails — every tap participates in the long ring.

**Why cascade decay was dead:** the pool (decay-controlled path) injected ONLY into group 0's write head (`if (g == 0) group_in += mDiffusedGlobalPool`). Only group 0's 4 taps saw the recirculated tail — 1/16th of the system. Groups 1-15 only saw cascade-flow propagation (attenuated × 0.25/stage) + their own local fb. Decay's main lever reached almost none of the network. Decay also coupled to the per-group LP cutoff, but stronger damping = darker tail, not longer.

**Why "smear at density > 0.1":** with 64 simultaneous tap reads driving 16 group writes and only group 0 receiving fb, no coherent ring-back from the long delays could form. A pluck → 64 simultaneous spatial reads → diffuse wash with no recirculation reinforcement. Legacy at the same density rang because the feedback bus re-injected into all 64 reads, building constructive recirculation at the delay set.

Fix landed in 2.6.1.72:

- **`mDiffusedGlobalPool` injects into every group's write head**, not just group 0. Pool now drives the full cascade like legacy's global fb drove all taps. The pool's source (tail-third with per-group sign decorrelation, added in 2.6.1.70) is preserved.
- **Per-group pool attenuation `1/sqrt(aG)`**: pool is pre-attenuated when stored so each group's injection is `pool/sqrt(aG)`. Total injected energy = aG × (pool/√aG)² = pool², matching legacy's single-write injection energy budget. Pre-attenuating at the storage site saves the per-group multiply in the inner loop.
- **Widened `kLocalFbScale` range** from `0.35 + 0.4×decay` (0.35-0.75, "always somewhat ringing") to `0.1 + 0.7×decay` (0.1-0.8, near-off → strong ring). Decay now has clean on/off perceptual range on the short-time character.

The plan's original "inject pool to group 0 only" choice was a stability hedge documented as "avoids correlation/runaway." With sign decorrelation on the pool source (2.6.1.70) and the pool HPF in place, multi-group injection is safe.

### Per-group full-Ns sub-windows + decay→local-fb coupling + local-fb HPF (added in Phase B)

Phase B audition (2.6.1.70) reported "virtually no echo at all, mostly very subtle feedback until low size which produces rumble." Three coupled architectural choices were limiting the cascade:

1. **Per-group sub-window too small.** The plan's "one shared int16 buffer with per-group sub-windows" sized each group at `Ns / 16` (~3000 samples at 1s buffer). That capped any tap's max delay at `sizeSq × groupBaseFrac × 0.9 × Ns/16` ≈ **56 ms at size=1, group 15** — chorus territory, not echo. Legacy maxed at `sizeNorm × Ns` ≈ 1s. The cascade had 18× shorter max delay than legacy, so `size` had no echo range and the listener heard a smeared continuous wash instead of cascade routing.
2. **Local fb gain decoupled from decay.** `kLocalFbScale = connectivity × 0.6` was a fixed constant. Decay only fed (a) the pool path (heavily attenuated by sqrt(fbCount) × HPF × sign-decorrelation) and (b) the per-group LP cutoff (which *damps* the loop — works against perceived decay extension). Net: decay knob was nearly inert.
3. **No HPF on local fb path.** At low size, per-group delays fall to 1-5 ms (loop fundamentals 200-1000 Hz) and the LP filter's cutoff (~1-5 kHz) sits inside the loop bandwidth, effectively integrating sub-150 Hz content. Per-group DC blocker at ~50 Hz doesn't catch this; pool HPF only fixes pool-path; local fb path leaked 60-150 Hz buildup → audible rumble at low size.

Fix landed in 2.6.1.71:

- **`allocate(Ns)`** now allocates `Ns × kNetworkNumGroupsMax × 2 bytes` (~1.5 MB at 1s @ 48 kHz; well within am335x 64 MB DDR budget). Each group gets `groupLen[g] = Ns`, `groupOrigin[g] = g × Ns`. Max per-tap delay now `sizeSq × groupBaseFrac × 0.9 × Ns` → up to ~0.9s at size=1, group 15. Legacy delay range restored, cascade routing now operates on echo timescales.
- **`kLocalFbScale = connectivity × (0.35 + 0.4 × decay)`**. At decay=0 → 0.35×conn (short echoes); at decay=1 → 0.75×conn (extended ringing tails). Stays sub-unity at all settings; band-pass below keeps the loop stable. Decay now has its primary perceptual axis on short-time character.
- **`mGroupFbHpX1[16]`, `mGroupFbHpY1[16]`** with `kFbHpR = 0.987` (~100 Hz cutoff). Applied after the LP on the local fb signal → per-group loops are now band-passed instead of low-passed. Catches the 60-150 Hz buildup that the LP otherwise accumulates at low size.

### Pool sign randomization + pool HPF (added in Phase B)

Phase B audition (2.6.1.69) reported "beautiful cascading feedback but serious low-end buildup at low size settings." At low size, per-group fundamentals are *high* (~140 Hz @ size=0.35), so the buildup isn't from per-group resonance — it's from two specific cascade-vs-legacy gaps:

1. **No sign randomization in the pool sum.** Legacy star multitap multiplied each tap's pool contribution by a hash-derived ±1 (`mFbWeight[t]`) so coherent frequency components didn't accumulate constructively across taps. The cascade rebuild summed tail-third group outputs into `fb_pool_in` with all-same-sign — the per-group LP filter biases each group's local fb toward low-mid content, and same-sign summing across tail groups makes that bias accumulate.
2. **Allpass chain has its own low-end modes.** The 4-stage Schroeder allpass (lengths 167/263/419/677 samples @ 48 kHz) has fundamental modes at 71/115/183/286 Hz. Any input excites these modes; with sustained pool recirculation, they ring. The pool's `mDcFb` blocker is at ~50 Hz (`kNetworkDcR = 0.9935`) — below the allpass modal band — so it doesn't catch this. Legacy didn't need a higher-cutoff HPF because the feedback bus didn't recirculate enough times to ring the allpass modes audibly; the cascade's many-pass topology does.

Fix landed in 2.6.1.70 as two complementary mitigations:

- **`mGroupPoolSign[16]`**: per-group ±1 multiplier for the pool contribution, hashed from `mCascadeSeed` at construction / seed change. Populated alongside `mTapIntraGroupOffset` in `recomputeCascadeAssignment()`. Stable per instance.
- **`mPoolHpX1`, `mPoolHpY1`**: second one-pole HPF applied to the pool output *after* the soften blend, *before* storing to `mDiffusedGlobalPool`. Coefficient `kPoolHpR = 0.987` → ~100 Hz cutoff, sitting just below the lowest allpass mode (71 Hz) so it catches the modal buildup without thinning useful low-mid content.

## Phases

### Phase A — Scaffolding (1–2 days)
Compiles and renders a clean cascade with feedback OFF, modes OFF. Verify topology is right.

A1. Add `NETWORK_GROUP_SIZE = 4`, `NETWORK_NUM_GROUPS_MAX = 16` constants in Network.h.
A2. Add new state: `mGroupOrigin`, `mGroupLen`, `mGroupWriteIndex`, `mGroupLocalFbState`, `mTapIntraGroupOffset`, `mTapGroupMap`, `mTapGroupSlot`, `mCascadeSeed`, `mStutGroup`, `mGroupMonoStutterAcc`, `mGroupStutterWetL/R`.
A3. Constructor: seed `mCascadeSeed = (uint32_t)((uintptr_t)this * 2654435761u) ^ 0xCA5CADE1u`. Init all group state to zero.
A4. `allocate(Ns)`: divide `Ns` evenly into `groupLen[g]` and `groupOrigin[g]`.
A5. Add `recomputeCascadeTaps()` to `geometry.h` (group-relative delay targets).
A6. Sort taps by reflector distance once at construction + on `mSeed` change → `mTapGroupMap` / `mTapGroupSlot`. Hash intra-group offsets.
A7. Per-sample loop: cascade with no local fb, no global pool. Pass A/B/C per group with group-relative wraps.
A8. Wet bus sums every group's pan-multiplied output. Output DC + dry mix unchanged.

**Verify**: short transient pluck — hear 16 distinct echoes spaced by group delays at low size, smearing toward continuum at high size.

### Phase B — Local + global feedback (2–3 days)

B1. `groupLocalFbState[g] = group_mono_out` at end of each group.
B2. Feed `groupLocalFbState[g] × (conn × 0.6)` into group input on next sample.
B3. `fbStartGroup = activeGroups - max(1, activeGroups / 3)`; sum `group_mono_out` from `[fbStartGroup, activeGroups)` into `fb_pool_in`.
B4. `fb_pool_in × (conn × decay / sqrt(numFbGroups))` → `networkFastTanh` → DC → 4-stage Schroeder → blend by `soften`. Existing pipeline reused verbatim.
B5. `diffusedGlobalPool` injects only into group 0's input (previous sample's pool result).

**Verify**:
- conn=0: short impulse, tail dies in one cascade traversal.
- conn=1, decay=0.95: short impulse, sustained cloud comparable to current 0.3.21 character.
- Stress: density=1, conn=1, decay=0.95, glitch=1 — no NaN, no clip, no DC drift after 5 min.

### Phase C — Modes (1–2 days)

C1. Confirm MUTE/CRUSH/SCRUB/REVERSE work per-tap in the new group loop with **no code change** (they operate on per-tap arrays the cascade still maintains).
C2. Restructure stutter scalar pass: add `mStutGroup[s]` field, populate at block-rate trigger. Stutter sample contributions accumulate into `mGroupMonoStutterAcc[mStutGroup[s]]` (and per-group wet L/R if pan applies). Per-group serial-loop body adds its accumulator into mono out.
C3. G7 respawn: document that respawned reflectors keep their group slot (don't re-sort).

**Verify**: glitch=1, motion=0.5. Hear all five mode characters cleanly. Stutter taps clearly inside the cascade (later groups' delays *on* stuttered content, not stutter tagged on after).

### Phase D — Pan + listener tracking (1 day)

D1. `recomputeTaps` continues to write `mTapGainL/R[t]` via listener-azimuth pan (unchanged).
D2. Verify pan tracks motion phase via listener orbit. Confirm cascade arrangement does NOT rotate with motion.

### Phase E — Tuning / audition (3–5 days)

E1. Sweep size 0→1 — confirm quadratic curve feels musical.
E2. Sweep conn 0→1 — confirm "open delay-line → coupled stages → diffuse reverb" morph (not just "more wet").
E3. Sweep motion 0→1 — listener pan tracks; cascade structure stable.
E4. **Side-by-side vs `git checkout HEAD~1 -- Network.h` build** — must sound clearly different ("propagation through stages" vs "many parallel echoes").
E5. CPU profile on AM335x. Target ≤8% stereo; budget ceiling 10%.

## Verification

Audible tests at minimum:

1. **Stage isolation** — conn=0, density max, transient pluck: hear N distinct echoes in groupLen bands.
2. **Local-loop coloration** — conn=0.5, decay=0.7, sustained input: each group imparts modal color; size sweep shifts loop pitch.
3. **Cascade propagation** — conn=1, decay=0.5, hard transient: transient arrives at "different distances" sequentially (vs current 0.3.21 firing simultaneously).
4. **Size-as-endpoint** — conn=0.7, size sweep: size=1 adds ~1s additional decay vs size=0.5; "field expanding around listener."
5. **Connectivity as networkedness** — conn=0/0.25/0.5/0.75/1.0 holding decay=0.7: clear morph from delay-line → coupled → reverb.
6. **Motion preserves cascade** — sweep motion: listener azimuth moves; cascade structure does NOT rotate/rearrange.

Build/lint verification (each phase):
- `make ARCH=am335x PKGNAME=spreadsheet` clean.
- NEON `:64/:128` hint count = 0 (`arm-none-eabi-objdump -d ... | grep -cE '\.32.*:(64|128)'`).
- vtable for `stolmine::Network` shows `V` (COMDAT).
- `tools/check-graphic-virtual-defs.sh` clean.
- PKGVERSION bumped each rebuild iteration (4th digit) per `feedback_package_version_bump`.
- Force-clean `spreadsheet_swig.cpp` on header edits per `feedback_swig_header_dep`.

Hardware soak (Phase E gate): 30 min continuous modulation at full settings, no clicks, no NaN-poisoning, no CPU drift.

## Risks

| Risk | P | Mitigation |
|---|---|---|
| Local feedback instability at conn=1 / decay=0.95 | Medium-high | `conn × 0.6` ceiling; add per-group DC blocker if rumble accrues; worst case drop to `conn × 0.4`. |
| Cascade sounds like a fancy delay line, not plexus | Medium | Audition at Phase A end; if topology isn't carrying character, broaden pool to all groups (accepting stability cost) before continuing. |
| Stutter routing per-group introduces bugs | Medium | Unit-test: trigger single stutter, verify it appears in correct group's output only. |
| CPU overrun on AM335x | Low-medium | Per-group horizontal sums are the main new cost; budget ceiling 10% stereo. Profile early. |
| "Different, not better" — user audition fails | Medium-high | `git revert` is one command. Real risk. Mitigation: audition early at Phase A end. |
| Calendar overrun | Medium | 1-week buffer in Phase E for topology adjustments. |

## Calendar

| Phase | Time | Critical path |
|---|---|---|
| A — Scaffolding | 1–2 days | Group-relative wraps in Pass A. |
| B — Local + global feedback | 2–3 days | Stability tuning at high conn × decay. |
| C — Modes | 1–2 days | Stutter per-group accumulator routing. |
| D — Pan + listener tracking | 1 day | Mostly verification. |
| E — Audition + tuning + CPU profile | 3–5 days | Multiple listen-revise cycles. |
| **Total focused work** | **8–13 days** | |
| Part-time elapsed (~50% utilization) | **~3 weeks calendar** | |
| Plan budget with 1 week buffer | **4 weeks** | |
