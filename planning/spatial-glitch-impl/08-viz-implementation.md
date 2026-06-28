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

## Density → bubbles + z-depth weave (DONE @ 0.2.0.48)
Fixed **14 streamlines** (`kStreamN`) grouped into **7 pair-BANDS** (band b = lines
2b, 2b+1). Density spawns **bubbles** woven through the bands by z-depth.
- **Bubble pool** (atom, `kVizMaxBubbles`=12, all `#ifndef SWIGLUA`): `mBubX`
  (content-x), `mBubY` (column-y), `mBubZ`, `mBubR` (≤0 inactive), `mBubVx`. Per
  block: rise (`kBubRise`) + slight drift; spawn toward target `= density·max` on a
  timer; retire off the top. Getters `vizBub{X,Y,Z,R}(i)`, `vizMaxBubbles`.
- **Band z permutation** randomized per unit insertion (Fisher-Yates in the atom
  ctor → `mBandZ[7]`, getter `vizBandZ(b)`). Shared by all plies → consistent depth.
- **Render = z-ordered painter's composite** (AnamFieldGraphic): build a list of
  bands (z = `vizBandZ`) + active bubbles (own z), insertion-sort ascending z, draw
  back→front. `renderBand(b)` draws both lines AND **fills the negative space
  between the pair with background (0)** → occludes lower-z material. `drawBubble`
  **fills its disk with background then draws a bright outline** (2 brighter than the
  lines, `bubB = baseB+2`) → occludes lower-z; both **clipped to the ply window**
  (`plotClip`) so strip-spanning shapes draw in pieces with correct per-ply z (no
  cross-ply clobber). `drawLinePix` = the shared sqrt-AA line-pixel helper.
- Constants: `kStreamN`(14), `kVizColH`(64), `kBubRise`(14), `kVizMaxBubbles`(12),
  spawn radius 2..6 / drift ±3 / interval 0.25s (in atom `spawnDrop`-adjacent block).

## NEXT — organic bubble shapes (Perlin contour)
Replace circle outlines with organic blobs: lift the Perlin LUT + marching-squares
iso-contour engine from `mods/spreadsheet/RaindropGraphic.h` (LUT-baked `sampleNoise`
+ `kSegTable` marching squares). Each bubble = a contour blob sampled from a noise
field (animated/scrolled), still z-ordered + occluding as now. Then: **wave/droplet
carry** (nudge bubble x by local flow + ripple as it rises).
