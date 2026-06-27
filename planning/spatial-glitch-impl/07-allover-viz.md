# 07 — All-over viz: "Pond of Recollection" (Phase 5b)

Status: **LOCKED 2026-06-27.** Supersedes the per-ply viz idea in `06-ui-surface.md`.
One continuous flowing image painted across Anamnesis's whole main-display ply
strip; each ply is a disturbance in a single shared field. Resolves the open
Looper-ply blocker (every ply now has a field-slice main; Looper's subs = Speed/Length).

## The concept
A continuous **streamline flow-field** — a sea of memory — spanning all 6 plies,
read left→right as signal/time flow. Each ply *is* a feature in that one field.
Bold black/white line-art (refs in `~/Downloads/vizref/anamnesis`: flow streamlines
#1, pond ripples #5, field-warped-by-objects #7).

| Ply (target order) | Field feature | Driven by |
|---|---|---|
| **1 Clock** (Overview) | global current: streamline velocity + spacing; **grit shatters lines into dashes** | Clock R (`mRcurZ`), Grit |
| **2 Looper** | **raindrop ripples** from the playhead, expanding + drifting downstream; buffer wave bends the rings | playhead, loop fill, Speed dir, mode |
| **3 Freeze** | the flowing field **crystallizes to ice** (a still-point) | `effFreeze` |
| **4 Size** | a **vortex/eddy** whose radius = room size | Size amt |
| **5 Density** | sparse discrete ripple-centers → dense **interfering moiré wash** | Density amt |
| **6 Mix** | field **fades (dry)** or **floods the frame (wet)** | Mix amt |

The **active ply's feature brightens** while you edit it (the reborn
`project_bias_indication` — region lights up instead of a dotted line).

## Why it's feasible (architecture facts, verified 2026-06-27)
- Main display **256×64**; ViewControls laid as a horizontal `SpottedStrip` of
  **42px columns + 1px gap**; focused ply snaps to a detent, strip pans at **55 FPS**.
- **Seams align by construction:** each ply gets a fixed `canvasIndex 0..N-1` and
  renders content-X ∈ `[i*43, i*43+42]` of one shared field f(X,y,t,state). Slice i's
  right edge == slice i+1's left edge because both evaluate the same f at the same X.
- Graphics are **not clipped** to their column → strokes bleed across the 1px gap;
  organic flow swallows the seam.
- **Panning is free:** field is painted into each graphic's local pixels, so it slides
  with the strip when the focused ply centers ("camera across a mural").
- **Cheap:** line-art (≈10–30 strokes/ply, ~6 plies visible) at 55fps is trivial on the A72.

## Scope / honest limits
- **All 6 plies share one field-graphic base** (incl. Freeze gate + Mix) — required for
  an unbroken image. One reusable renderer + `canvasIndex`, folded into the existing
  `AnamSubControl` interaction model.
- **"All-over" = the main 256-wide strip only.** The right **sub-display (128×64)** still
  holds Speed/Length readouts + sub-buttons (separate screen). Optional later: echo the
  field faintly behind the readouts.
- Mural spans Anamnesis's own contiguous plies; begins/ends at the unit boundary (framed).

## Architecture
- **Atom getters (C++-only, inside `#ifndef SWIGLUA`):** add public inline getters to
  `Anamnesis.h` reading the smoothed state — `getVizPhase()` (monotonic phase advanced in
  process(), sample-clock based → shared anim sync, flow speed = clock), `getPlayheadNorm()`,
  `getLoopFill()`, `getFreezeAmt()` (store `effFreeze`), `getClockR()`, `getSizeAmt()`,
  `getDensityAmt()`, `getEnv()`, `getModeV()`, `getBufferSample(int)`. No od::Output needed —
  the C++ graphic holds `Anamnesis*` and calls getters directly (Helicase pattern).
- **`AnamFieldGraphic.h`** (new, subclasses `od::Graphic`, SWIG-exposed like
  HelicasePhaseGraphic): ctor `(left,bottom,w,h)` + `follow(Anamnesis*)` + `setCanvas(index,count)`
  + `setFeature(kind)`. `draw()` (SWIGLUA-guarded) renders this slice of the shared streamline
  field + its feature. Field math in a small header `AnamField.h` (shared, header-only) so the
  graphic stays readable.
- **`AnamFieldControl.lua`** (new): GainBias subclass = AnamSubControl's interaction model with
  the main graphic swapped to `libanamnesis.AnamFieldGraphic` (setControlGraphic + setMainCursorController);
  carries `canvasIndex`/`feature`/`subs`. A **Gate variant** for Freeze. Migrate all plies to it.

## Build phases (each → make + manual pkg copy to front/ER-301/packages + audition + commit)
- **A — Foundation:** atom getters + `mVizPhase`; `AnamFieldGraphic` + `AnamField` skeleton;
  SWIG-expose; build. **Milestone: a static flowing streamline field rendering continuously
  across all plies, seams aligned** (no per-ply features yet). Proves the mechanism.
- **B — Consolidate to 6 plies on the field base:** Clock→ply1; new Looper ply (field main +
  Speed/Length subs); drop standalone Length/Speed plies; Freeze as field-Gate. Whole surface
  on `AnamFieldControl`, continuous field everywhere.
- **C — Per-ply features:** ripples (Looper, buffer-warped), vortex (Size), crystal (Freeze),
  moiré (Density), flow-rate/grit-dashes (Clock), fade/flood (Mix). Wire to live getters.
- **D — Polish:** active-ply brightening, animation tuning, CPU profile on CM4 (Phase 6 overlap).

## Open / deferred
- Exact streamline count, baseline spacing, ripple ring count — tune in Phase A/C.
- Whether `getVizPhase` is sample-accurate global or per-block; start per-block.
- Sub-display field echo — deferred (optional polish).
