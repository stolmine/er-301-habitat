---
name: Alembic codex — architecture, signal chain, training pipeline
description: Canonical reference for Alembic (sample-trained 4-op phase-mod matrix synth voice in mods/spreadsheet/). Covers the analysis pipeline, signal chain from PMM through wavetable / filter pair / routing matrix / comb to output, the 49-float preset row layout, three independent scan positions, ply layout, and the Phase 5a→5d-4 progression. Read before touching AlembicVoice.cpp/.h or planning post-Phase-5 work.
type: project
originSessionId: 7888c904-72b3-4df4-8e74-cf6c4f80d5a9
---
# Alembic — what it is

Sample-driven 4-operator phase-modulation matrix synthesizer voice in the spreadsheet package. User loads any audio sample; Alembic analyzes it once at commit time and populates a 64-slot per-instance preset table from per-pick features. Three independent scan positions then traverse the trained dataset — main scan picks PMM voice (ratios/levels/detunes/matrix), reagent scan picks wavetable shaper, comb scan picks comb filter character. Plus a Ferment chaos scalar that collapses or amplifies trained chaos contributions.

State at v2.5.5.164 (2026-04-27): all DSP feature work shipped (Phase 5a-5d-4 complete). Phase 6 (serialization), Phase 7 (sub-params), Phase 8 (Order 2/3 + meta-mapping + sample-pointer excitation), Phase 9 (polish + naming) remain.

# Files

- `mods/spreadsheet/AlembicVoice.h` — class members, Parameters, preset table, scratch arrays
- `mods/spreadsheet/AlembicVoice.cpp` — DSP, analysis kernel, training mappings, helpers (lifted from Som + Pecto)
- `mods/spreadsheet/assets/AlembicVoice.lua` — control wiring, ply layout, sample-pool integration, serialize stubs
- `mods/spreadsheet/assets/AlembicScanControl.lua` — main scan ply (paramMode shift-toggle for K)
- `mods/spreadsheet/assets/AlembicReagentControl.lua` — reagent scan ply (shift-toggle for amount)
- `mods/spreadsheet/AlembicSphereGraphic.{h,cpp}` — sphere viz attached to main scan ply
- `mods/spreadsheet/pffft.{h,c}` — used by the analysis FFT pass
- `mods/catchall/Som.cpp` — TPT SVF + fastTan + fastExp lifted into Alembic's filter pair
- `mods/spreadsheet/Pecto.cpp` — comb engine helpers (bufRead/bufWrite, NEON 3-pass tap gather, 16 patterns, 4 resonator types) lifted verbatim
- `planning/alchemy-voice.md` — full design doc with phase table

# Signal chain

```
V/Oct + f0 → PMM (4-op matrix)
           → wavetable shaper (256-entry LUT, K=4 frame blend, fold-around-peak with per-node odd/even)
           → filter pair (2× TPT SVF + topoMix + bpLpBlend + drive)
           → comb (Pecto-clone: 24-tap multitap with 16 patterns × 4 slopes × 4 resonator types + Karplus/sitar feedback)
           → drive (hard clip at ±1 — preserves transient pop)
           → level
           → output limiter (soft-knee asymptote toward ±1)
```

PMM op outputs (sineBank[0..3]) feed into the routing matrix as sources alongside filter LP/BP/HP outputs (10 sources total). Routing destinations: F1/F2 cutoff verso/inverso pairs (filter-FM at audio rate via fastExp) + F1/F2 input addAdd (additive injection). 8 lanes per scan slot, hard-cut single-slot pick (no K-blend across routing layouts).

# Per-instance state (~80 KB)

- `mPresetTable[64][49]` — 12.5 KB (49 floats per slot, 64 slots)
- `mWavetableLUT[64][256]` — 64 KB (256-entry transfer-function LUTs)
- `mLaneSrc[64][8]` + `mLaneDst[64][8]` — 1 KB (uint8 routing src/dst)
- `mCombBuf[4096]` int16_t — 8 KB delay line
- Routing/comb scratch (idx0/1, frac, sA/SB, cached delay/weight) — ~400 B
- Filter SVF state, FFT scratch, etc — ~5 KB

# Preset row layout (49 floats per slot)

| Index | Slot | Source feature mapping |
|---|---|---|
| 0..3 | ratios | 1.0 carrier + brightness/pitched/noisy/runLen for ops B/C/D |
| 4..7 | levels | rms-driven, op A always 0.5+rms*0.5 |
| 8..11 | detunes | op A: flatness*4 + flux*2 (5d-1.6); B/C/D: noisy/flux/entropy with runLen pull-back |
| 12..27 | matrix 4×4 | feature-driven strengths gated by 1-of-6 chaosScore-picked topology mask |
| 28 | wavetable blend | (1-runLen) * (entropy + flatness) * 0.5 |
| 29..34 | filter base | cutoff1=brightness, cutoff2=(entropy+flux)/2, Q=1-flatness, topoMix=runLen, bpLpBlend=pitched, drive=noisy*0.6+flux*0.4 |
| 35..42 | routing lane attens | feature-driven (src,dst,atten) selection per pick (top 8 by laneAffinity score + per-pick hash) |
| 43 | comb density | entropy*0.6 + flux*0.4 → 1..24 active taps |
| 44 | comb pitch | pitched*0.7 + (1-flatness)*0.3 → exp-mapped delay length |
| 45 | comb pattern | chaosScore-like signature → 1-of-16 categorical (hard-cut) |
| 46 | comb resType | tonal+pitched signature → 1-of-4 categorical (raw / Guitar LP / Clarinet clip / Sitar) |
| 47 | comb feedback | runLen*0.6 + pitched*0.4 → 0..0.95 |
| 48 | comb slope | entropy + brightness + flux signature → 1-of-4 (flat / rise / fall / hump) |

Plus per-node `mLaneSrc[n][8]` / `mLaneDst[n][8]` uint8 indices (NOT in preset row — categorical routing topology, hard-cut at scan boundaries).

# Coarse-pass features (7 dimensions)

Computed at ~20 Hz hop in `extractCoarseFeatures`, min/max normalized across the buffer:

| Index | Feature | Source |
|---|---|---|
| 0 | RMS | sum-of-squares of mono'd window |
| 1 | ZCR | zero-crossing rate (drives pitched = clamp(1-2*zcr)) |
| 2 | brightness | spectral centroid via 256-pt FFT (Hann window, normalized magnitude spectrum) |
| 3 | flux | L2 distance to previous frame's normalized spectrum |
| 4 | entropy | Shannon over 16-bucket [-1,+1] amplitude histogram, normalized by log2(16) |
| 5 | runLen | mean run length in bucket histogram (high = stationary content) |
| 6 | flatness | spectral flatness (geometric mean / arithmetic mean of magnitude spectrum, bins 4..109) |

Picks: greedy farthest-point sampling on 7-dim normalized space, time-sorted ascending so K-blend stays musically smooth.

# Scan / Reagent / Comb — three independent traversals

- `mScanPos` — main PMM voice + filter base + lane attens (K-blend on continuous slots; hard-cut on 5c topology mask + 5d-3 routing src/dst)
- `mReagentScan` — wavetable LUT + row[28] blend amount; multiplied by `mReagent` amount
- `mCombScan` — comb density/pitch/feedback (K-blend) + pattern/resType/slope (hard-cut). Single fader collapses dry/wet AND scan position onto one axis: fader=0 = bypass, fader>0 = scan = fader*63 with wet = fader.

# Visible plies (8 total)

`{"tune", "freq", "sync", "scan", "reagent", "comb", "ferment", "level"}`

- tune, freq, sync, level: stock controls
- scan: AlembicScanControl with K shift-toggle and sphere viz
- reagent: AlembicReagentControl with amount shift-toggle (no sphere — sphere belongs to main scan)
- comb: GainBias single fader (collapses dry/wet + comb-scan)
- ferment: GainBias single fader (chaos scalar, default 1.0, range [0, 1.5])

# Macro: Ferment

Single-axis chaos scalar. Multiplies trained matrix (all 16 entries — both off-diag cross-mod and diagonal self-fb) AND routing lane attens. Tonal regions (ratios, levels, detunes, filter base, wavetable, drive, comb) NOT scaled. At Ferment=0: matrix and routing collapse → 4 independent sines + filter pair + wavetable shaper + comb (all configurable still). At Ferment=1: full trained chaos. At Ferment=1.5: boost beyond trained.

Implementation: fused into the matrix K-blend (avoids gcc auto-vec'ing a standalone post-blend multiply into trap-prone `:64` quad-D hints).

Crucible was originally planned but dropped in 5d-4.

# Phase progression (status as of 2.5.5.164)

| Phase | Sub | Status | Version |
|---|---|---|---|
| 1 | AlembicRef Lua reference | done, retired | (pre-2.5.5.123) |
| 2 | Native PMM C++ | done | 2.5.5.123 |
| 3 | Preset crossfader + scan ply | done | 2.5.5.128-129 |
| 4 | Sphere viz | done | 2.5.5.134-135 |
| 5a | Sample slot plumbing | done | 2.5.5.136 |
| 5b | Analysis kernel | done | 2.5.5.150 |
| 5b sample-load gating | — | done | 2.5.5.151 |
| 5c | Topology selection + flatness + detune[8] | done | 2.5.5.152 |
| 5d-1 | Wavetable reagent | done | 2.5.5.153 |
| 5d-1.5 | Independent reagent scan | done | 2.5.5.154 |
| 5d-1 normalize fix | — | done | 2.5.5.155 |
| 5d-1.6 | 256-entry LUT + multi-cycle + peak fold | done | 2.5.5.156 |
| 5d-1.7 | Wavefolder replaces soft-clip | done | 2.5.5.157 |
| 5d-2 | Filter pair (no routing) | done | 2.5.5.158 |
| 5d-2.1 | Filter mapping fixes + hard clip drive | done | 2.5.5.159 |
| 5d-3 | Routing matrix | done | 2.5.5.160 |
| 5d-3 feedback limiting | — | done | 2.5.5.161 |
| 5d-3 resonance fix | — | done | 2.5.5.162 |
| 5d output limiter | — | done | 2.5.5.163 |
| 5d-4 | Comb (Pecto clone) + Ferment | done | 2.5.5.164 |
| **6** | Serialization | TODO | — |
| **7** | Sub-params (per-region user-bias fades) | TODO | — |
| **8** | Order 2/3 + meta-mapping + sample-pointer excitation | TODO | — |
| **9** | Polish + naming | TODO | — |

# Known issues / TODO highlights

- **Sample swap bug**: direct sample swap without detach doesn't retrain reliably. Detach-then-attach via menu works. Suspected analyzeSample race against partial Sample.Pool async load. Tracked in todo.md Alembic section.
- **Wooliness at low f0**: filter resonance peak lands in low-mid where PMM harmonics pile up. Acceptable trade.
- **Wavetable viz**: planned stacked-waveforms viz of the 64 trained LUT frames hasn't shipped yet (todo).
- **Reagent / Crucible globals**: original plan had Crucible too; dropped in 5d-4. Reagent global = `mReagent` already wired as wavetable amount multiplier.

# NEON / am335x discipline

Lessons from this voice (live in `feedback_neon_intrinsics_drumvoice` + `feedback_neon_hint_surfaces`):

- Class-member arrays for all NEON-touched scratch (mRoutingSources, mRoutingDst, mCombIdx0/1, mCombFrac, mCombSA/B, cached delay/weight, etc.). Stack-locals trap.
- Init loops + post-blend multiply loops on class-member arrays must be wrapped in `noinline + no-tree-vectorize` helpers (`initCombTapDefaults`, `cacheCombTapDelays`). Otherwise gcc auto-vecs to quad-D `:64` hints.
- Ferment scaling fused into K-blend (not a standalone post-blend multiply) for the same reason.
- 9 NEON `:64` hints in current AlembicVoice.o: 5 are pre-existing fastExp register spills around routing matrix; 4 are intentional NEON intrinsics in comb 3-pass (mirrors Pecto which ships with these).
- Force-clean SWIG wrapper before each header-changing build (`feedback_swig_header_dep`).

# Sound-design knobs for future tuning

- Per-feature mapping coefficients in `derivePresetRow` are the easiest dials.
- Topology selection thresholds (5c chaosScore buckets) and routing affinity rules (`laneAffinity`) shape the categorical character.
- Comb pattern/resType/slope feature signatures are independent — can rebalance without affecting other regions.
- Wavetable wavefolder drive scales with entropy + flatness (5d-1.7); user explicitly liked the result.
- Filter cutoff orthogonalization (cutoff1=brightness, cutoff2=entropy+flux) is critical for variation across nodes.
