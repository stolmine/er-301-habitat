# Mirror — phase-space phosphor viz (Y-axis reflected)

Planning doc for the Mirror unit's overview ply custom graphic.
Builds on the architecture landed at spreadsheet `2.7.1.35`.

**Goal:** Replace the fader area on the Shape (overview) ply with a
phosphor phase-space scope of the unit's L output, reflected
across the Y axis so the image is left-right symmetric. The
reflection IS the unit's identity — paradigm-coherent visualization
of a unit named Mirror.

---

## Architectural framing

- Modeled directly on `mods/spreadsheet/Rauschen.h:43` —
  `PhaseSpaceGraphic` — proven inline-header pattern with phosphor
  decay, auto-scaling, ring-buffer access.
- Stripped of Rauschen's 3D tumble — static 2D phase portrait
  keeps the mirror identity clean (rotation would scramble the
  symmetry).
- Y-axis reflection at plot time: for each sample pair `(x, y)`
  plotted at pixel `(px, py)`, also plot at `((w-1)-px, py)`.
  Image is symmetric about the vertical centerline.
- Source signal: the unit's existing `outputRing[256]` (decimated
  L output, post-Mirror crusher, post-DC, post-soft-clip). Already
  exposed via `Mirror::getOutputSample(int idx)`.

## Phase-space delay choice

Two-sample lag: `x[n]` vs `x[n+Δ]` with Δ = 1 in the ring-buffer
(decimated already). This gives a tight phase portrait — closer to
the signal's local derivative than a wide-Δ smear. For wavetable
envelope content this produces clean closed curves at lock zones
and jagged trajectories under Mirror crusher engagement.

## Phosphor decay parameters (Rauschen-aligned)

- 64 × 64 internal buffer (uint8 brightness)
- Fade `−1` per draw call (~40 fps → 200 ms full-decay)
- Increment `+4` per hit, capped at 12
- 4-bit framebuffer: 12 / 16 = bright peak, 0 = invisible

## Auto-scaling

Same fast-expand / slow-contract pattern (smoothing 0.5 / 0.02
per draw call). Clamps min/max range to ≥ 0.01, NaN/Inf guards.

## Mounting in MirrorOverviewControl

Pattern matches `HelicaseOverviewControl` — fader area replaced by
custom graphic, paramMode grandfathered ON so the custom view is
the headline. Shift-toggle reveals the standard normalSubGraphic
(level/fader) for precise Shape value reads.

```lua
local phosphor = libspreadsheet.MirrorPhosphorGraphic(0, 0, ply, 64)
phosphor:follow(args.mirror)  -- args.mirror = the C++ Mirror op
local container = app.Graphic(0, 0, ply, 64)
container:addChild(phosphor)
self:setMainCursorController(phosphor)
self:setControlGraphic(container)
self.paramMode = true  -- grandfather: custom view is headline
```

The `args.mirror` parameter is the C++ op (`libspreadsheet.Mirror()`)
passed through from `Mirror.lua`.

## Implementation

### `mods/spreadsheet/MirrorPhosphorGraphic.h` (new, header-only)

- `class MirrorPhosphorGraphic : public od::Graphic` — all virtuals
  inline per `feedback_no_out_of_line_virtuals`
- `follow(Mirror *p)` — store pointer, attach/release lifetime
- `draw(od::FrameBuffer &fb)` — fade buffer, sample 254 pairs from
  ring, plot to `(px, py)` AND `(w-1-px, py)` for Y-reflection,
  render

### `mods/spreadsheet/spreadsheet.cpp.swig`

- Add `#include "MirrorPhosphorGraphic.h"` to the `%{ %}` block
- Add `%include "MirrorPhosphorGraphic.h"` to the `%include` list
- Both placed alongside the existing Mirror.h entries

### `mods/spreadsheet/assets/MirrorOverviewControl.lua`

- Add `args.mirror` to the expected args list
- Construct `MirrorPhosphorGraphic`, call `follow(args.mirror)`
- Wrap in container, attach via `setControlGraphic`
- Grandfather `self.paramMode = true`
- Preserve `levelSubGraphic = self.subGraphic` reference for the
  toggle-off path

### `mods/spreadsheet/assets/Mirror.lua`

- Pass `mirror = self.objects.op` (the C++ op) to
  `MirrorOverviewControl` args

### Version

PKGVERSION 2.7.1.35 → 2.7.1.36

## Audition expectations

- Mirror = 0, Sync lock, Shape = triangle: smooth ellipse/closed curve
  in the right half, mirror-image in the left half. Calm Rorschach.
- Mirror = 0.5, Sync chaos: jagged, multi-loop figure, precessing.
  Each frame shows phosphor decay trails of recent trajectory.
- Mirror = 0.85+: stair-step quantization corners, sharp angles,
  buffer pixels light up across the plot rapidly. Mirror reflection
  makes the chaos read as a symmetric "shattered glass" pattern.
- Shape sweeps: closed curve continuously deforms across the
  wavetable inventory. Each frame's distinctive shape appears in
  phase space.
- Sync Threshold sweep: at lock zones the figure stabilizes
  (closed curve), between locks the figure precesses (open curve
  with no return path).

## Risks

1. **Symmetry feels gimmicky** — if every frame is always perfectly
   left-right symmetric, the eye might tune it out. Mitigation:
   the asymmetry of the underlying signal IS the content; the
   symmetry just emphasizes the "this is Mirror" identity.
2. **Phase space too jittery at high Mirror knob** — the bit
   reduction creates discontinuous trajectories that may appear
   noisy rather than musical. Mitigation: phosphor decay smooths
   visually; reduce update rate / increase decay if too jumpy.
3. **CPU on am335x** — ~250 sample-pair plot operations per frame,
   nothing intensive. Should be well under budget.
4. **Auto-scale chasing transients** — bit-crushed Out has fast
   amplitude changes. Slow contract (0.02) handles this; fast
   expand catches new peaks instantly. Same balance Rauschen uses
   successfully.
5. **Lua-side: `args.mirror` not in scope** — need to pass the C++
   op pointer through from `Mirror.lua`'s `onLoadViews` into
   MirrorOverviewControl args.

## Out of scope

- Stereo-aware variant (L top, R reflected to bottom) — defer to v1.5
  if the v1 reflection reads as too "always symmetric"
- 3D tumble — explicitly omitted to preserve the mirror identity
- Color / tint variation — 4-bit grayscale phosphor only
- Configurable axis (X vs Y reflection) — Y locked for v1; X
  available later if requested
- Mirror knob's divisor / bit-depth indicator overlays — keep
  the viz pure
