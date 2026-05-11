---
name: Use LUT for trig in habitat package graphics on am335x
description: Runtime sinf/cosf from a package .so miscompute on am335x — replace with a precomputed cos/sin LUT in any circular/rotational graphic.
type: feedback
originSessionId: 6486de73-452f-466c-9c43-03152c66fce2
---
In habitat package `.so` files built for am335x with the TI 4.9.3 toolchain, runtime `sinf`/`cosf` calls from `Graphic::draw()` produce bad output — a symmetric circle built from `cosf(a)*r, sinf(a)*r` renders with a distended bottom lobe and extra line-like artifacts, while the exact same math with precomputed `static const float` LUT entries renders a perfect circle. Diagnosed in Tomograph's `FilterResponseGraphic.h` 2026-04-13.

Firmware-side graphics (e.g. ScaleQuantizer's PitchCircle, compiled into the main firmware binary) are NOT affected — same `sinf`/`cosf` calls there render correctly. The bug lives specifically at the package→firmware call boundary.

**Why:** Not fully root-caused. Confirmed unrelated to SWIG version (persists on 4.4.0 and 4.4.1), unrelated to habitat source (`v2.0.0` rebuild reproduces), unrelated to the machine (reproduces on two different dev hosts). Smell is package-to-libm symbol resolution drifting, or a calling-convention / FPU ABI mismatch surfacing only when `sinf`/`cosf` are invoked from a dlopen'd `.so` on this Cortex-A8 target. The fix is robust either way.

**How to apply:** When a package graphic needs trig for circular layouts:

- Precompute a 72-entry (or similar) `static const float kLutCos[N]` and `kLutSin[N]` at `a = 2*pi*i/N - pi/2` so step 0 is top and indexing matches a natural `for (int step = 0; step < N; step++)` loop.
- For perimeter/step-aligned draws, index the LUT directly — no interpolation needed, and no `floorf` (also libm).
- For arbitrary-radian angles (e.g. spoke directions from frequency-mapped bands), wrap with a large positive bias (`72.0f * 1000.0f` in the index calculation) to avoid negative-truncation issues, cast to `int`, modulo 72, linear-interpolate between neighbours. Inline helpers `lutCos(float rad)` and `lutSin(float rad)` keep call sites readable.
- Keep `logf` / `expf` for now — only `sinf`/`cosf` are confirmed bad. If a future graphic shows Gaussian/log-scale artifacts (wrong band positions, malformed bumps), treat the same way.
- Reference implementation: `mods/spreadsheet/FilterResponseGraphic.h` — `kLutCos`/`kLutSin`/`lutCos`/`lutSin` at top of file.

If we ever track down the root cause (toolchain patch, firmware libm re-export, linker flag), we can revert to direct `sinf`/`cosf` and drop the LUTs. Until then, default to LUT for every new circular/rotational graphic in a habitat package.
