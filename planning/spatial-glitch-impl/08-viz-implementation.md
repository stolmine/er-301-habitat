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

## Density → lava-lamp metaball bubbles, woven by z-depth (DONE @ 0.2.0.61)
Fixed **14 streamlines** (`kStreamN`) grouped into **7 pair-BANDS** (band b = lines
2b, 2b+1). Density spawns **metaball bubbles** (marching-squares iso-contours) woven
through the bands by z-depth. Long iterative journey (circles → Perlin radial blobs →
metaball field → point-attractor lobes); the LANDED design:

**Bubble pool (atom, `kVizMaxBubbles`=12, all `#ifndef SWIGLUA`).** `mBubX` (content-x),
`mBubY` (column-y), `mBubZ` (= metaball z-LEVEL 0..`kBubLevels`-1), `mBubR` (≤0 inactive),
`mBubVx`, `mBubSeed`. Per block (in the `spawnDrop`-adjacent block in process()):
- speed via a **clock-scaled step `vdt = FRAMELENGTH/fs/mRcurZ`** (whole bubble system
  tracks the Clock). Rise `kBubRise`; **Freeze** (`vizFreeze`) blends rise → per-bubble
  RANDOM direction (`kFreezeDrift`, angle from seed); retire off any edge.
- spawn toward target **`= density·max`** (count = Density) on `kBubSpawnInt` timer; a
  new bubble has chance `kCalveProb` to **CALVE** off a mature parent (radius>5): born
  beside it, drifting away, parent ×0.82 mass → split-offs persist as real bubbles.
- radius **3..15**, level random. Getters `vizBub{X,Y,Z,R,Seed}(i)`, `vizMaxBubbles`.

**Band z permutation** randomized per unit insertion (Fisher-Yates, atom ctor →
`mBandZ[7]`, getter `vizBandZ(b)`); shared by all plies → consistent depth.

**Render (AnamFieldGraphic, z-ordered painter's composite).** Items = 7 bands
(z=`vizBandZ`) + up to `kBubLevels`(3) bubble-LEVELS (z interleaved); insertion-sort
ascending z, draw back→front. `renderBand` fills the negative space between its pair
with background (occludes lower-z). **`renderBubbleLevel(L)`** = the metaball engine:
- **Sub-bump list** per level: each bubble = a **CORE** bump + **lobes LATCHED onto a
  drifting POINT layer**. Points: `kNumPoints`(28) content-space points, base from
  `hash01`, drift by noise (`kPointDrift`/`kPointDriftRate`). A bubble latches each
  point within **reach** (`= R·kLatchK + kLatchBase`, which **breathes** with noise
  `kReachVar` → occasionally grabs a far point = big separation), weighted by distance
  (full to `reach·kLatchFull`, then smooth fade → lobes grow & shed → pinch/split).
  Per-(bubble,point) **affinity** (from `mBubSeed`, below `kAffBias` ignored) +
  per-point **strength** (`hash01`→lobe size). Capped `kMaxLobes`(7).
- **Field grid** (cell `kMetaCell`=3, GLOBAL content-aligned → seams) = Σ sub-bumps on
  level (Gaussian, sigma `R·kMetaSigmaK`) × multiplicative noise edge-wobble
  (`kMetaNoiseGain`); **temporally SLEWED** into `mSlewGrid` (`kMetaSlew`) for gentle
  morph. Threshold `kMetaThresh`.
- **Marching squares** (`kSeg` table — re-derived for OUR corner-bit convention; the
  lifted screensaver table caused spurious spikes) → **AA contour** (`drawAALineClip`,
  Wu) so the edge glides sub-pixel. **Per-pixel fill** where field>T occludes lower-z.
  All **clipped to the ply window** (strip-spanning shapes draw in pieces, correct
  per-ply z). Brightness `bubB = baseB+2` (2 over the lines).
- Perlin LUT + `fbm` in **`AnamNoise.h`** (lifted from spreadsheet RaindropGraphic).

**ALL bubble/metaball constants live in `AnamField.h`** (kBub*, kMeta*, kNumPoints,
kLatch*, kReach*, kAff*, kSub*/point, kCalve*, kFreezeDrift, etc.).

## NEXT — richer bubble physics (the flow + droplet carry)
The weighted point/affinity field is the groundwork: have the **flow streamlines + the
droplet ripple field actually shove the bubbles around** (nudge `mBubVx`/position by the
local `flow()` slope + nearby `rippleEval` bend as they move) so shapes get thrown off
course over the weighted field. Then revisit Field subsystem channels still pending
(Diffusion fuzz, Mod wander, Decay persistence, Grit dashes, Clock-tempo polish), the
active-ply emphasis, Clock→ply-1 reorder, and CM4 CPU profiling (the metaball field +
per-pixel fill is the heaviest part — profile before shipping).
