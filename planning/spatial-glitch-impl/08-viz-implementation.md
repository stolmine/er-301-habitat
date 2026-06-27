# 08 — All-over viz: IMPLEMENTATION (Phase 5b)

How the "Pond of Recollection" (design in `07-allover-viz.md`) is built. Files:
- `mods/anamnesis/AnamField.h` — shared math + ALL tuning constants (header-only).
- `mods/anamnesis/AnamFieldGraphic.h` — the per-ply `od::Graphic` (renders a slice).
- `mods/anamnesis/atoms/Anamnesis.h` — viz getters + droplet pool (DSP-side state).
- `mods/anamnesis/assets/AnamFieldControl.lua` / `AnamFieldGate.lua` — the ViewControls.

## Rendering pipeline (AnamFieldGraphic::draw)
Each ply renders content-X `[x0 .. x0+w]` where `x0 = canvasIndex * kStride` (43).
1. **Cache active droplets** once from the atom into locals (x,y,age,speed,amp).
2. **Per streamline** s of `kStreamlines`: baseline `yb = (s+0.5)*h/n`.
   - **Control points** on a GLOBAL grid (multiples of `kCtrlStep`=4px, shared by
     all plies → seams align). At each control point: `y0 = yb + flow(cx) + Σ_d
     amp_d·bend_d`; `glow = Σ_d amp_d·glow_d`. Store `ctrlY[i]`, `ctrlB[i]`.
   - **Per pixel**: Catmull-Rom interpolate y AND glow from the control points.
     Brightness `= clamp(baseB + glow·kGlowGain·Mix, 0, 15)`, base `= kBaseDim +
     (kBaseBright-kBaseDim)·Mix`.
   - **Render**: sqrt-coverage anti-alias (crisp bright core + smooth sub-pixel
     edge); solid-fill steep gaps to the previous column so the line is unbroken.
3. **Gap bridge**: every ply but the last draws ONE extra column (px=left+w) into
   the 1px SpottedStrip gap, on the same global-grid curve → no hairline seam.
4. **Impact splash** (Looper feature): a bright fleck at each young drop in-ply.

`flow(cx,yb,phase)` = sum of 3 travelling sines (the braided current). `phase`
(= `vizPhase`) advances per block ∝ 1/ClockR, and is **frozen when Freeze is on**.

## Ripple model (the rain) — AnamField::rippleEval → {bend, glow}
Researched drop-impact physics (IMA splashing-drop, capillary-gravity dispersion,
Worthington jet). A drop's surface height `h(r,age)` = impact + dispersive train +
knock-on; **bend** = `kRippleD·h·(dy/r)` (radial Y push → geometry), **glow** =
positive train height (→ illumination). Superpose drops (linear → interference is
free). KEY RULE: **no temporal carrier** — crests are smooth Gaussians that travel
outward so a point feels each crest PASS ONCE (a standing `sin(kr-ωt)` carrier read
as a vibrating guitar string; that was the bug we removed).
- **Impact transient** (`age < kImpactT`): central crater(−) releasing into a
  rebound jet(+) — "the plop", seeds the first ring.
- **Dispersive fanning train** (`crestTrain`): crest COUNT grows with age
  (`2 + kRippleFan·age`, capped `kRippleMaxCrests`) and spacing widens outward
  (`kRippleSpread`) → one impact spreads into a family of rings (fakes the
  `Δr ∝ r²/t²` dispersion). Amplitude `(A0/√r)·exp(-age/τ)`.
- **Knock-on**: one delayed (`kSecDelay`), weaker (`kSecAmp`), finer (`kSecLam`)
  secondary train (the jet-rebound droplet's second splash).

## Droplet pool (Anamnesis.h, DSP-side, all `#ifndef SWIGLUA`)
- Pool of `kVizMaxDrops` (16): `mDropX/Y` (content-px), `mDropAge` (s, <0=inactive),
  `mDropSpeed` (px/s ring speed), `mDropAmp` (= loudness of that capture), `mDropPhase`.
- **Spawn one per loop cycle**: a read-head wrap (jump > ½ loop) → `spawnDrop()`,
  random epicenter in the Looper ply's content-x (0..42), per-drop speed/amp jitter
  (amp from `mEnvFast`). Works any mode/direction (Tape/Env use `mLoopReadPos`,
  Stretch the source head). Retire at `age > kRippleLife` (= 3·τ).
- Advanced per block; ages keep advancing even when Freeze halts the flow.
- **Getters** (C++ graphic calls directly, Helicase pattern): `vizPhase`,
  `vizDropX/Y/Age/Speed/Amp(i)`, `vizMix`, `vizFreeze`, `vizClockR`, `vizSize`,
  `vizDensity`, `vizEnv`, `vizMode`, `vizGrit`, `vizPlayhead`, `vizBuffer`, `vizLoopLen`.

## Tuning constants (all in AnamField.h)
- **Flow lines**: `kStreamlines` (12, THE density lever), `kCtrlStep` (4).
- **Brightness**: `kBaseBright` (12), `kBaseDim` (4), `kGlowGain` (1.0).
- **Ripple train**: `kRippleLambda` (11), `kRippleSigma` (3.2), `kRippleSpread`
  (0.16), `kRippleFan` (4), `kRippleMaxCrests` (6), `kRippleTrail` (0.66),
  `kRippleTau` (1.2), `kRippleA0` (8), `kRippleD` (2.4), `kRippleEps` (1).
- **Impact**: `kImpactT` (0.12s), `kImpactA` (6), `kImpactCraterW` (6), `kImpactJetW` (3).
- **Knock-on**: `kSecDelay` (0.18s), `kSecAmp` (0.4), `kSecLam` (0.7).
- **Drop spawn** (Anamnesis.h `spawnDrop`): speed `22 + 20·rand` px/s, amp
  `0.5 + 0.5·clamp(mEnvFast·4)`. Pool `kVizMaxDrops` (16, in Anamnesis.h).

## CPU note
Cost ≈ `lines × controlPts × activeDrops × (ripple evals)`. With 12 lines, ~15
control pts, up to 16 drops, ~6 plies @55fps it is fine on the dev Mac; **profile
on CM4** (Phase 6) — the front-reject + age-scaled crest count keep it bounded, but
longer lifetimes raise concurrent-drop count. Lever down via `kStreamlines`,
`kVizMaxDrops`, or `kRippleTau` if needed.

## PENDING — Density → branch/absorb (next)
Smoothly vary line count 7 (min) → 20 (max) with fluid branch/absorb:
- Up to 20 target baselines + an **activation priority** by *largest-gap bisection*
  (first 7 evenly spread, each later line bisects the widest current gap → fills
  evenly). Precompute the order + each line's PARENT (an adjacent line).
- Continuous Density → active count `k = 7 + density·13`. Lines below `floor(k)`
  full; the `floor(k)`-th branches in with progress `frac(k)`.
- **Fluidity**: a branching line's baseline lerps OUT from its parent to its slot as
  it fades in (split off); reverse Density → slides back and merges (absorb).
- All plies use the identical density-driven layout → seams stay aligned.
- `kStreamlines` becomes the *max* (20); `vizDensity` drives active count.
