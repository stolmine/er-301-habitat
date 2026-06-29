# ER-301 UI Aesthetics — Reference & Direction

Source: ~77 reference images in `~/Downloads/UI/` (filenames `7561122249870506XX.{jpg,png,gif}`),
surveyed 2026-06-28 for adaptability to the ER-301's displays. This doc is the durable takeaway —
the aesthetic vocabulary worth pulling into habitat units, the rendering techniques, and the
specific directions Bram flagged (animated/Muybridge controls, chrome/HUD, an icon-array steplist
control). Filenames below point back into the source folder.

## The canvas we design for
- Two MONOCHROME OLEDs: **MAIN 256×64**, **SUB 128×64** px. Small.
- **4-bit grayscale** (16 levels), single amber/white-on-black. No color/hue.
- ~55 fps, drawn **procedurally** in C++ via pixel / line / AA-line / circle primitives (not bitmaps).
- Detail must read at tiny scale — often a ~42px ply column, or the full 256-wide strip.
- **Fits:** bold line-art, dithered tone, generative flow/particles, op-art/moiré, dot/halftone,
  ASCII/dot-matrix, schematic/wireframe, scope/waveform, isolines/topographic, constructivist geometry.
- **Doesn't fit:** color/hue dependence, photographic texture, dense small text, subtle low-contrast.

## Aesthetic tiers (what transfers)

### Tier 1 — native, high payoff
1. **Halftone / dither / particle fields** — tone from dot size/density; figures dissolving into
   stipple. 4-bit mono's sweet spot (Anamnesis already does this: IGN dither, metaballs).
   *Technique:* per-pixel ordered/Bayer dither of a scalar field, or variable-radius `circle()` halftone.
   *Refs:* `…954` (figure→starfield dissolve), `…844`/`…688` (photo→block decay as a density param),
   `…855` set (negative-space form from a numeral cloud).
2. **Oscilloscope / waveform / spectral waterfall** — what a DSP scope does.
   *Technique:* per-frame polyline of sample/FFT data; **waterfall = stacked history lines, each older
   one dimmer + offset** → 3D-ish landscape from pure strokes.
   *Refs:* `…942`, `…940` (3D spectral waterfall — near drop-in for a spectral/decay visualizer).
3. **Schematic / wireframe / blueprint** — tunnels, rotating hulls, isolines, leader-line diagrams.
   All stroke, no fill, thrives at 1px. *Technique:* `line`/`aaline` + marching-squares contours.
   *Refs:* `…948` (vanishing-point tunnel = depth/feedback viz), `…971` (wireframe polyhedra),
   `…706` (PING signal-flow map).
4. **Telemetry / HUD / instrument chrome** — Soviet amber readouts, jet HUD tick-ladders, TX-6 icon
   sheet. Highest-value *layout/chrome* find; we have no consistent "instrument" skin yet.
   *Technique:* line-grid tables + bitmap font + segmented `rect` meters + polar radar (`circle` rings
   + swept `aaline`). *Refs:* `…622` (BRT satellite panel — near drop-in), `…670` (TX-6 vocabulary),
   `…839` (jet HUD tapes), `…613` (polar orbit-radar for an LFO/trajectory).

### Tier 2 — strong, with discipline
5. **Generative particle figures / chronophotography filmstrips** — square-particle figures that
   scatter/reassemble; motion decomposed into a left→right frame row. *Refs:* `…679` (particle
   dancers), `…884`/`…890`/`…939` (Muybridge horse/athletes/bird). *(See "Animated controls" below.)*
6. **Brutalist/Y2K vector glyphs + spec-sheet grid chrome** — corner-bracket framed cells, hazard
   hatching, barcodes, reticles, parametric dithered squares. Best as **borders, mode indicators,
   meters** — not whole canvases. *Refs:* `…633` (parametric squares), `…654` (Semiotic Standard
   glyphs), `…849` (corner-bracket cells).

### Tier 3 / skip
Network/node graphs (only if kept very sparse); color/photo/CJK-text pieces (`…620` A-Train,
`…701` slit-scan, `…616` color globe, `…953` citation graphs) — out of scope.

### Meta-idea worth stealing
**"Viz mode" parameter** (`…951` penguin-rendered-14-ways, glyph studies): one signal, a switchable
bank of render styles — stipple / pixel / slab / spray / wireframe. A reusable lever across units.

---

## Directions of particular interest (Bram)

### A. Animated controls — Muybridge / chronophotography / decomposition over time
Treat a control's display as **time decomposed across space or as a looping animation**:
- **Filmstrip mode:** render N evenly-spaced frames of a parameter's state across the 256-wide strip
  (a waveform snapshot, particle pose, level). Current frame brightest; history dims via the 4-bit
  ramp. The 256×64 strip *is* a frame-row. *Refs:* `…884`, `…890`, `…939`.
- **Loop mode:** a single locomotion-cycle animation (a walking figure, a turning shape) whose
  **playback rate / phase is driven by the parameter** (e.g. clock, rate, depth). Reads as a living
  indicator, not a static readout.
- **Decompose-over-time:** a still that progressively shatters into particles / dither / numerals as a
  param rises (`…954`, `…855`, `…844`) — a "resolution / density / decay" lever made visible.
- *Technique:* a small particle/pose system sampled from a target shape (env, waveform, silhouette),
  density + jitter signal-driven; or a keyframe-interpolated stroke figure cycled by phase.

### B. Chrome / HUD / telemetry language
A consistent **instrument skin** habitat lacks: framed panels with corner-tick brackets, segmented
bar meters, tick-ladder tapes (vertical level/param scales), crosshair/reticle, polar radar (rings +
swept line + scatter). Pure line + `rect` + `circle` + bitmap font. Use as the *frame* around a
unit's live visualizer, and as mode/state indication. *Refs:* `…622`, `…670`, `…839`, `…613`,
`…633`, `…849`, `…654`.

### C. NEW CONTROL — Icon-array steplist (scroll-through signs)
A control derived from habitat's existing **steplist** pattern, but the ply presents an **array of
icons / signs** the user scrolls through (rather than numeric step values).
- **Model on the existing pattern:** `mods/stolmine/StepListGraphic.h` + `assets/StepListControl.lua`
  (and the `mods/spreadsheet` equivalents `StepListGraphic.h` / `LaretStepListGraphic.h`). Fork the
  list/scroll/cursor machinery; swap the per-cell renderer from step values to **icon glyphs**.
- **Render:** each cell = one bold procedural glyph (drawn with line/poly/circle primitives, ~1-bit
  shapes from the brutalist/Semiotic vocabulary — `…654`, `…633`, `…849`). Selected cell highlighted
  (bright/inverted/corner-bracketed); neighbors dimmer; horizontal scroll like a `SpottedStrip`.
- **Interaction:** encoder scrolls the array; press selects. The icon set is the option vocabulary for
  whatever the control picks — **mode / algorithm / waveform / routing / preset** — i.e. an
  `od::Option` whose choices are *pictograms* instead of words.
- **Why it fits:** a single bold glyph per cell reads perfectly at this scale; turns abstract mode
  enums into a legible, scrollable visual menu. Pairs naturally with the "viz mode" meta-idea (B/§meta).
- **Build path:** new `IconListGraphic.h` (cell layout + glyph dispatch by index) + `IconListControl.lua`
  (Option-backed, scroll/select), plus a small library of procedural icon-draw functions. Reusable
  across units.

---

## Reusable toolkit (proposed)
Rather than per-unit one-offs, build shared, composable graphics components in the habitat tree (the
way `AnamField`/`AnamFieldGraphic` are shared) — modular files, each small enough to read whole:
- **Halftone/dither engine** — scalar field → dithered tone (Tier-1 #1).
- **Scope / waterfall widget** — waveform + stacked-dimming-history (Tier-1 #2).
- **HUD chrome** — frames, corner brackets, segmented meters, tick-tapes, reticle, polar radar (#4/§B).
- **Particle/film system** — pose sampling, filmstrip + loop playback (Tier-2 #5 / §A).
- **Icon-array control** — `IconListGraphic` + `IconListControl` + procedural icon set (§C).

## Pointers
- Source images: `~/Downloads/UI/` (standouts above by suffix). Two `.mp4` clips also present (not surveyed).
- Existing list/steplist controls to model: `mods/stolmine/{StepList,SegmentList,TapList,BandList}*`,
  `mods/spreadsheet/{StepList,LaretStepList,SegmentList,Band,Tap}List*`.
- Anamnesis (branch `planning/spatial-glitch-cm4`) already exercises Tier-1 #1–3 in its all-over viz —
  see `planning/spatial-glitch-impl/07-allover-viz.md` + `08-viz-implementation.md` for working techniques.
