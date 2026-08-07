# Anamnesis viz optimization - plan of record

## Round three (2026-08-06, pkg 0.2.0.88, uncommitted): strip raster + blit

Hardware split from the round-two gate: DSP alone (viz gated) = 30% mono,
ACCEPTED; on-screen still encoder-capture -> the draw path is ~70% and is the
entire remaining problem. Authorized: the 6-ply architecture rethink.

**Structure**: the whole 258x64 pond is now composited ONCE per UI frame into
`Anamnesis::mStripRaster` (a member byte plane + a written-pixel bitmask,
~18.5 KB) by `buildStripRaster()`; each ply's `drawImpl` is a masked blit of
its window (plus the front-most rain flecks, unchanged). Frame detection: a
draw whose ply index <= the previous draw's index starts a new frame (robust
to any subset of plies being visible). The masked blit writes ONLY pixels the
raster painted, so framework content under never-painted pixels (section
dividers in the 1px gaps) is treated exactly as the old per-ply renderer did.

What the structure removes on-device (invisible to the x86 bench, whose stub
framebuffer was never virtual): ALL od::FrameBuffer virtual readPixel calls
(raster blending reads its own bytes) and roughly half the virtual pixel
calls; per-ply z-sorts, drop caches, band control-grid overlap (~19%), and
the 6 overlapping marching-squares walks collapse to one each. Plus two
volume cuts the strip form made natural:
 - per-drop INNER skip radius (a mature ring is a thin annulus = crest span +
   4.5 sigma; points deep inside get h < 4e-5 -> bend < 1e-3 px): crestTrain
   evals -39% at worst case;
 - exact per-CELL classification in the metaball fill: all-below-threshold
   cells skip (bilinear <= max corner), solid-interior cells (min corner >=
   T + kEdgeSoft) write hard 0 with no per-pixel math.
Also strip-coherent by construction: the old per-ply draws could see the sim
mutate mid-frame (audio thread) -> potential seam tearing; the raster reads
once.

Proof: framebuffer A/B vs round two = 14 px of 17.5M changed (13 of them by
1 gray step; 10 of 1072 worst-case frames affected, <= 2 px each) -- that is
the bounded ripple inner-skip; the structural port itself is pixel-exact.
Cumulative vs the ORIGINAL pre-session renderer: 0.114% of pixels, 98% by 1
step. Audio: BIT-identical to round two. x86 O3 worst-case draw 694 -> 563
us/frame, default 354 us (x86 understates; see above). am335x hot loops are
call-free (objdump: only per-frame powf x2 / memset x2 + the inlined Perlin
bake cold path); drawImpl = 181 instructions. Both arches clean, all three
lints clean, emu insert smoke passes.

Honest device projection: raster math ~1.2-1.7M cycles/frame worst case +
~10-15k masked virtual pixel writes -> viz should drop from ~70% to roughly
15-30% of the core at 48k/55fps, i.e. total ~45-60% with the accepted 30%
DSP. If hardware still shows capture, the remaining levers are (a) halving
the strip rebuild rate (animation at ~27fps -- a RICHNESS change, needs user
sign-off), (b) single-pass multi-threshold marching squares (~0.5% est,
skipped as marginal), (c) direct MainFrameBuffer plane writes for the blit
(bypasses the virtual pixel API; layout-coupled, kept as a documented
last resort). A 6-ply all-over viz is NOT inherently over budget: the raster
is one 258x64 4-bit scene per frame; the framework's per-pixel virtual
interface is the floor.

## Round two (2026-08-06, pkg 0.2.0.87, committed c92f063)

Hardware after round one: unresponsive -> "just over 100% CPU", animation runs.
Target another ~2x. Changes, each A/B-proven:

- **Offscreen-viz gate** ([hab:viz-offscreen-gate-all], Fabula vizPing pattern):
  drawImpl pings a 256-block heartbeat; process() gates the DSP-side viz sim
  (flow phase, rain, bubble physics) on it. Audio never reads sim state ->
  zero audio effect. Offscreen, per-sample DSP libm calls measure ZERO and the
  sim's 5.7M expf + 1.6M sinf per 10 s (worst case) vanish. This also makes
  the device a measuring instrument: navigate away = DSP-only CPU%.
- **Draw-path fast transcendental kernels** (field::fastSin / fastExpNeg,
  templated twins flowFast/rippleEvalFast/crestTrainT so exact + fast share
  one formula source): the bands' libm storm is gone. Whole-run worst-case
  libm: 62.4M expf + 3.3M sinf per 10 s -> 5.8M + 1.6M, and the REMAINDER is
  the audio-thread bubble physics deliberately kept exact (coordinator call:
  ship slower and correct; its trajectories are bit-untouched, including
  noise::sample whose floorf->int-floor change is bit-exact). On A8 that is
  hundreds of M cycles/s off the UI thread. Errors: sin < 4e-6 (small args) /
  ~4e-4 rad at args ~6500 (0.006 px on a 100 px wave), exp < 3.1e-4 relative
  = milli-pixels. Framebuffer A/B (1072 worst-case frames, sim history
  matched via bench PING): 99.89% pixels identical, 98% of changes are 1 gray
  step, rest are 1-px boundary flips of moving lines (worst frame 38/16384).
- **NEON 3-pass tap + FDN gather** (Pecto pattern): Pass A computes 12 tap /
  8 FDN modulated delays 4-wide (vectorized polySin2PiQ, bit-identical
  quadrant fold), prefetch pre-pass, scalar gather, NEON interp/gain/pan MAC
  (taps) and mixed/guard/clamp/write (FDN; feedback + output sums kept scalar
  in the old order = bit-identical). Class-member SoA, NO aligned() claims
  (plain new is 8-aligned; unhinted vld1 cannot trap). First build emitted 2
  quad [sp :64] spills (the AlembicVoice trap, feedback_neon_hint_surfaces);
  fixed by constructing vdupq constants inside the short q-loops. Hint scan
  now 0 safe / 0 suspect across the whole .so.
- **Latent OOB fixed** (feedback_multitap_idx_wrap_ulp, present in the
  SHIPPED readTap/readLine): wr - d + len can round to exactly len -> one
  past the buffer. Guarded in all paths. Audio A/B vs round one: bit-identical
  for 5.8 s until the (deterministic) event fires, then sparse -68..-93 dBr
  diffs = the corrected sample recirculating; corr 0.9999999+; clock case 0
  diffs in 30 s. A bug fix, not a character change.
- x86 O3 worst-case: field build 112 -> 99 us, 6-ply draw 864 -> 694 us
  (x86 undervalues both -- its libm is nearly free; the call-count drop is
  the device-transferable number).
- Verify: both arches clean, all three lints clean (audio stack now 0
  warnings), emu insert smoke test passes on 0.2.0.87.
- OWED: hardware CPU% on-screen vs off-screen (the gate gives the split).
  Left alone: bubble-physics exact libm on the audio thread (~0.5-8% A8
  depending on Density/drops -- the biggest remaining known cost, waiting on
  the device split), Item 5 single-pass marching squares, band control-grid
  hoist (~19% dedupe).

## Round one

Status: **implemented through Item 4-lite** (2026-08-06, pkg 0.2.0.86, committed eb85049).
Item 1 (shared per-frame cache) had already shipped; this pass added the pieces
below plus the DSP question at the bottom (which turned out to be the meter the
user was seeing). Measured on the offline bench (`tools/house-bench/anam_bench.cpp`,
x86 proxy; worst case Density=1/Diffusion=1):

- ROOT CAUSE FOUND FIRST: the package was SINGLE-TU - all DSP + draw code was
  emitted in the SWIG wrapper TU at CFLAGS.size (-Os, no -ffast-math), not the
  speed profile. New `mods/anamnesis/Anamnesis.cpp` carries `process()`,
  `spawnDrop()`, `buildFieldFrame()` and `AnamFieldGraphic::drawImpl()` at
  -O3 -ffast-math -fno-tree-vectorize (the DrumVoice pattern; `draw()` itself
  stays inline per the graphic-virtual rule - it delegates to the non-virtual
  drawImpl, which creates no key function).
- DSP: 21.5 per-sample libm sinf (12 tap-LFO + 8 FDN-LFO + spiralSat) + 2
  floorf are GONE from the sample loop (dynamic count 5.16M -> 0.12M per 5 s,
  remainder is block-rate bubble physics). LFO phase accumulators kept
  bit-identical; only the sine EVALUATION is poly (max err 3.7e-6; a more
  accurate rotator was tried and REJECTED because it diverged from the shipped
  phase trajectories). Audio A/B over 4 setting sets: corr 1.000000000,
  diff floor -84..-96 dBr, 88% of samples bit-identical.
- Viz: buildFieldFrame 849 -> 112 us (7.6x: bump SPLAT with 4.5-sigma cutoff +
  fbm evaluated only where bumps != 0, which is EXACT since f = bumps*(1+g*nz);
  decayed cells snap to 0); 6-ply draw 3342 -> 864 us (3.9x, adds the
  zero-cell fill skip - exact because bloomLo >= 0). Framebuffer A/B over 1072
  worst-case frames: 20 pixels of 17.5M changed (0.0001%), max delta 2/15.
- Also fixed en route: field::hash01 signed-overflow UB (gcc -O3 provably
  exploited it), isfinitef made bit-based (the old v==v form is folded away
  under -ffast-math -> NaN firewall would have died in the new TU), linux arch
  gets -fno-tree-vectorize (libmvec _ZGVbN4v_expf dlopen failure, same fix as
  spreadsheet). Emu insert smoke test: er-301/tests/emu/90-anamnesis-insert-smoke.test.
- NOT done (in order of likely next value): NEON 3-pass tap gather for the 12
  sparse taps + 8 FDN reads (measure on hardware first); Item 5 single-pass
  marching squares (contour loop is small); band control-grid hoist (~18%
  dedupe only); bubble-physics block-rate expf/sinf on the audio thread (~1%
  est, exact-output preserved, moving it changes sim timing).
- OWED: hardware CPU% before/after on am335x (the bench is an x86 proxy; the
  eliminated per-sample libm calls are worth far more on Cortex-A8 than the
  x86 numbers show, per feedback_f64_count_poor_cpu_proxy in reverse).

Original plan follows.

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
