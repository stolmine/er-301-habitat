# Anamnesis viz optimization - plan of record

Status: **planning** (2026-07-11). Goal: take the all-over "Pond of Recollection"
viz from "crawls on CM4 / untested on am335x" to "comfortable on CM4, plausibly
am335x-tier" WITHOUT sacrificing the look, and (per the repo's own NEON doctrine)
WITHOUT reaching for graphics-path NEON until the architectural wins are spent.

The DSP is a separate question (see the end). The viz is the current blocker: on
the MBA it runs fine (brute force); on the CM4 the draw path starves the UI thread
before audio can even be auditioned.

Files: `mods/anamnesis/AnamFieldGraphic.h` (the per-ply draw), `AnamField.h`
(flow / ripple / metaball math + constants), `AnamNoise.h` (baked Perlin LUT).
Cross-refs: `planning/neon-opportunities.md` (the doctrine), the screensaver engine
`mods/spreadsheet/RaindropGraphic.h` (the architecture this was lifted from but
diverged from), `feedback_viz_encoder_capture_architectural`,
`feedback_no_out_of_line_virtuals`, `project_ngoma_codex`.

## 1. The makeup

`Anamnesis` = DSP (`atoms/Anamnesis.h`: looper + morphing field + cross-feedback +
granular Stretch) plus the viz. The viz renders one continuous pond-wide image
across the ply strip. The design intent (`planning/spatial-glitch-impl/07-allover-viz.md`)
is that every ply shows content-x `[index*kStride .. +width]` of the SAME field so
the strip reads as one flowing picture.

Crucial structural fact: **the viz is drawn by one `AnamFieldGraphic` object PER
PLY** (`kVizPlies = 6`, `kStride = 43`, strip width `kVizStripW = 258px`). The
framework calls each object's `draw()` independently. There is no shared per-frame
step.

## 2. Cost anatomy (per ply, per frame)

Per `AnamFieldGraphic::draw()`:

**a. Streamline bands** - `nBands = kStreamN/2 = 7`. Each band:
- control-point loop (`mctrl <= 40`): `field::flow()` twice (3x `sinf` + 1 noise
  each) + `rippleEval` per active drop per control point;
- per-pixel loop (`w + wext ~= 44`): 4x `catmull` + negative-space fill + 2x
  `drawLinePix` (2x `sqrtf` each).

**b. Bubble / metaball system** - the dominant cost:
- **Global point-drift layer**: `kNumPoints = 28` points, each 2x `noise::sample`.
- **Sub-bump cluster expansion**: for every pond-wide bubble, scan all 28 points
  (`dx,dy` distance + `noise::sample` affinity + `smooth01` + `sqrtf`), latch up
  to `kMaxLobes = 7` lobes. Produces the sub-bump list `sb*[]`.
- **`renderBubbleLevel` x `kBubLevels = 3`**, each level:
  - **field build**: grid up to `kMetaGW x kMetaGH = 24 x 24 = 576` cells; EACH
    cell loops over ALL `nsb` sub-bumps computing `expf(-(dx^2+dy^2)/(2s^2))`,
    THEN a 3-octave `fbm` (3x `noise::sample`) per cell. This is `O(cells x bumps)`
    + `O(cells)` transcendentals. (`AnamFieldGraphic.h:284-307`)
  - **fill**: `w x h ~= 44 x 64 ~= 2640` pixels, each a bilinear interp of the grid
    + `readPixel` + IGN dither + `floorf`. (`:308-337`)
  - **marching squares**: `(gw-1)(gh-1)` cells, AA line per crossing. (`:338-364`)

Per-ply persisted state: `mSlewGrid[kBubLevels * 24 * 24]` floats (~6.9 KB) for the
temporal slew, held per graphic object -> ~41 KB across 6 plies.

## 3. Root cause: per-ply recomputation of GLOBAL work

Everything in 2b marked "global" or "pond-wide" - the 28-point drift layer, the
sub-bump expansion over every bubble, and the field itself (indexed in CONTENT
space) - is **recomputed identically inside every ply's `draw()`**. With 6 visible
plies, the single most expensive work (sub-bump expansion + the `expf`/`fbm` field
build) runs up to **6x redundantly per frame**. Per
`feedback_viz_encoder_capture_architectural`, viz lag "is almost always draw-path
structure, not CPU" - this is textbook.

## 4. The screensaver architecture we diverged from

`AnamNoise.h` states the engine was lifted from the stolmine RaindropGraphic
contour engine. Reading `mods/spreadsheet/RaindropGraphic.h`, that engine:
- renders **one full-frame grid** (`kRainGridW = 42 x kRainGridH = 64`);
- uses a **baked Perlin LUT** ("Runtime LUT sampling replaces per-frame Perlin");
- traces contours with a **single-pass multi-threshold marching squares** that
  "reads each cell once" (`drawContoursMulti`).

Anamnesis kept the LUT noise (`AnamNoise`) but threw away the single-full-frame
architecture: it went per-ply and re-derives the global field per object. The fix
is to restore the screensaver's shape - **compute the field once for the whole
strip, then blit per-ply windows.**

## 5. Doctrine: architectural before NEON (and no graphics NEON yet)

From `planning/neon-opportunities.md`:
- **Principle 1**: on the Visadhara Corona Fold contour field, NEON of the per-pixel
  math would have given ~4x; the frame-invariant **precompute cache gave ~50x**.
  "NEON of a redundant computation is still redundant."
- **Principle 5**: **no habitat graphic uses NEON.** Graphic headers compile into
  `<pkg>_swig.o` at `-Os` (not the DSP `-O3 -ffast-math`), so the `:64`
  alignment-trap behavior there is UNPROVEN, and this codebase has a documented
  history of hardware-only NEON crashes (`feedback_neon_intrinsics_drumvoice`,
  `feedback_neon_hint_surfaces`).

Conclusion: NEON is the LAST lever, and likely unnecessary once the redundancy is
gone. Do the architecture first.

## 6. Prioritized plan

### Item 1 - shared per-frame field cache (the 50x lever)

Compute the global field ONCE per frame; each ply only rasters its window.

**Where the cache lives.** The op (`Anamnesis*`, `mpOp`) is already shared by every
ply graphic (the "Helicase pattern": graphics hold + read the one op). Add an
`AnamFieldCache` owned by the op (or a file-static keyed on the op pointer). The op
does NOT render - it only holds the cache struct; the field-build code stays in a
shared header (`AnamField.h`), operating on the cache.

**What the cache holds** (all content-space, whole strip):
- the drifting points `ptX/ptY[kNumPoints]`;
- the sub-bump list `sbX/sbY/sbR/sbAmp/sbLvl[]`;
- the per-level metaball grid over the WHOLE strip: `stripCols = kVizStripW/kMetaCell
  ~= 86` by `gh ~= 22` by `kBubLevels = 3` ~= 5,700 floats (~23 KB) - ONE grid,
  computed once, replacing the current 6x-redundant per-ply grids. `mSlewGrid`
  moves here and becomes a single strip-wide slew grid.

**Frame detection without a frame hook.** Plies draw in sequence each frame and all
share `vizPhase`. Cache stores `lastPhase`; on `draw()`, if `phase != lastPhase`
the (first) ply rebuilds the shared field and sets `lastPhase = phase`; the other 5
plies find it fresh and skip straight to rastering their window. O(1) guard, no
framework change.

**Per-ply work after the cache**: sample the shared grid over the ply's `w`
columns (fill + contour) + the streamline bands (bands are cheap and already
per-ply-local; can stay, or also hoist their control points to the cache since the
control grid is global too - secondary).

Expected win: removes ~5/6 of the field-build + sub-bump cost. This is the item
that most likely fixes the crawl on its own.

### Item 2 - bound the field build to bump neighborhoods

Today the build is `cells -> all bumps` (`AnamFieldGraphic.h:291-297`): every cell
tests every sub-bump. A Gaussian bump is negligible beyond ~3 sigma. Invert to
`bumps -> splat into a local cell bbox` (`ceil(3*sigma/kMetaCell)` cells around the
bump center). Turns `O(cells x bumps)` into `O(bumps x smallbox)`. Compounds with
Item 1 (build the shared grid this way).

### Item 3 - LUT the `expf`, thin the `fbm`

- Gaussian falloff `expf(-d^2/2s^2)` -> a normalized-`d^2` LUT (same idea `AnamNoise`
  already uses for Perlin). The splat (Item 2) makes the index range small + bounded.
- `fbm` is 3x `noise::sample` per cell; sample at grid nodes only (the grid is
  already coarse at `kMetaCell = 3`) and/or drop to 2 octaves. Audition the look.

### Item 4 - fill only each level's active bbox

The fill loop runs `w x h` per level even where the field is 0
(`AnamFieldGraphic.h:308-337`). Track each level's active bbox (union of its bump
splat boxes from Item 2) and fill only inside it.

### Item 5 - single-pass multi-threshold marching squares

Port `RaindropGraphic::drawContoursMulti`: walk the grid once, resolve all levels /
thresholds per cell, instead of re-walking `(gw-1)(gh-1)` per level.

### Item 6 - NEON, only if still needed (and it may not be)

After 1-5, re-measure. If a hotspot remains it is the splat (Item 2) or the bilinear
fill (Item 4) inner loops. If pursued, per Principle 5 and the Ngoma rules:
- **class-member storage only** for any NEON scratch (never stack-local NEON arrays;
  `feedback_neon_intrinsics_drumvoice`). `mSlewGrid`/the shared grid already qualify.
- scan the WHOLE `.o` with `tools/check-neon-hints.sh` before install
  (`feedback_neon_hint_surfaces` - ctor / auto-vec init can trap too);
- hardware-test - the emu (x86_64 / aarch64) cannot reproduce am335x codegen traps.

## 7. Crash-avoidance rails (Ngoma lessons)

- **Every virtual stays inline in the header.** `DrumCubeGraphic` insert-crashed
  purely from an out-of-line `draw()` after firmware rebuilds
  (`feedback_no_out_of_line_virtuals`). `AnamFieldGraphic::draw()` is already inline
  - keep it; run `tools/check-graphic-virtual-defs.sh` after the refactor.
- Moving state to one shared strip-wide grid REDUCES surface (less per-object state,
  one grid to reason about) - a simplification, not new risk.
- SWIG: the anamnesis headers compile into `anamnesis_swig.o`; force-clean the
  wrapper on any header-with-struct-size edit (`feedback_swig_header_dep`) or the
  stale `sizeof` corrupts the heap.

## 8. Verification

- **Emu**: subjective frame-rate / no encoder-capture with ALL plies on screen +
  bubbles active + Diffusion high (the worst case). Before/after on the MBA emu.
- **CM4**: the real target for "responsive"; test on the uConsole once reachable.
- **am335x**: build clean (`ARCH=am335x`), run `tools/check-graphic-virtual-defs.sh`
  and, if any NEON lands, `tools/check-neon-hints.sh` on the `.o` before install.
  Hardware audition gates "responsive on am335x".

## 9. Build order

1. Item 1 (shared per-frame cache) - the big architectural cut. Ship + measure
   before anything else; it may be sufficient.
2. Items 2-4 (bounded splat + exp LUT + bbox fill) - stack if Item 1 leaves headroom
   to close, or if am335x is the target.
3. Item 5 (single-pass MS) - cleanup / marginal.
4. Item 6 (NEON) - only on a measured remaining hotspot, with the full rail set.

## 10. The DSP question (separate, later)

Once the viz no longer starves the UI thread and the sound can actually be
auditioned on CM4, revisit am335x DSP viability. The memory pegs it ~40-60% stereo
with granular **Stretch** as the killer + L2 pressure; an am335x profile would drop
/ lighten Stretch, float-retarget, and shrink the field. Out of scope for this doc.

## Open questions

- Cache ownership: struct on the op vs file-static keyed on the op pointer. Lean
  op-owned (lifetime already managed via attach/release).
- Do the streamline bands also move to the cache (global control grid) or stay
  per-ply? They are cheaper than the metaball field; defer unless measurement says.
- Strip-wide grid memory (~23 KB) vs current ~41 KB across objects: net win, but
  confirm it is not a problem inside the SWIG `-Os` TU.
