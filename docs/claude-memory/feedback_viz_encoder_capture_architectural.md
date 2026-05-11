---
name: Encoder capture in viz is usually architectural, not CPU
description: When a graphic causes encoder capture / input lag, the fix is almost always draw-path structure (granularity, state caching, time slicing) — not micro-optimization or NEON. Colmatage screensaver is the working reference for the right shape.
type: feedback
originSessionId: ccd87a27-34a8-447f-8c81-f2d3ae1ad5b2
---
When a custom `od::Graphic` causes encoder capture or visible UI lag, the instinct is to blame CPU cycles. Usually wrong. The fix is almost always **architectural** — the draw call structure is starving the display/input thread's event loop, and no amount of micro-optimization inside the hot inner loop changes that.

**Why:** draw and encoder dispatch share the same thread. A single long draw() call blocks input events until it returns. The UI thread wants to return to the event pump 30–60 times per second. If one frame's draw takes multiple frames' worth of time, encoders queue and feel captured, regardless of total MHz consumed.

**How to apply:** check these four architectural moves before reaching for NEON:

1. **Tile granularity.** Per-pixel iteration over a 128×64 viewport = 8192 ops/frame. Go to 2×2 tiles (672 ops) or 4×4 (512 ops) — 12–16× fewer per-frame ops for imperceptible visual loss. `ColmatageOverviewGraphic.h` is the reference: 2×2 tiles, full-screen Perlin field, zero lag even with 1344 `sinf`/`cosf` calls inside the tile loop.
2. **State cache keyed on quantized state.** Cache the expensive computation (Voronoi partition, BSP walk, whatever) keyed on a coarse-quantized state tuple. Small integrator slews must not invalidate the cache. `SomSphereGraphic.h`'s half-frame pixel cache is the anti-pattern — it invalidates every frame because rotation advances every frame.
3. **Time slicing.** When the cache does invalidate, split the recompute across 4–8 frames. Each frame updates 1/N of the tiles; stale regions keep their previous assignment until their slice comes up. No single frame runs the full recompute synchronously.
4. **State-machine evolution, not continuous recompute.** Expensive structure changes happen on discrete triggers (phrase boundaries, scan-node crossings), not every frame. Most frames just slew existing state. Colmatage's BSP restructures only on phrase boundaries; most frames are pure tile-state slewing.

NEON vectorization of the inner loop (e.g., 4-wide dot-product search for nearest-seed) is a **secondary optimization** worth ~4× scalar speedup. Apply only if the architectural fixes aren't enough. Reaching for it first is a trap — it doesn't fix the event-loop starvation pattern even if it cuts cycles.

**Cost budget heuristic:** if per-frame op count is in the same order as Colmatage (~35K–130K cycles with tile granularity + caching + time slicing) the UI will feel smooth. Per-pixel + per-frame full-recompute patterns at 500K+ cycles will starve input even if the arithmetic is nominally cheap.

**References:**
- Working pattern: `mods/spreadsheet/ColmatageOverviewGraphic.h` — BSP tile field, Perlin LUT baked at init, state-machine evolution on phrase boundaries.
- Anti-pattern: `mods/catchall/SomSphereGraphic.h` — per-pixel Voronoi search, half-frame pixel cache (weak), no partition cache, no time slicing. Causes encoder capture.
- Related memory: `feedback_package_trig_lut.md` — LUT trig for per-tile/per-pixel draws regardless (package `sinf`/`cosf` miscomputes on am335x).
