# er-301-habitat v2.4.1

Release date: 2026-05-02 (hotfix)

Single package update: **spreadsheet 2.6.0 → 2.6.1**. All other packages unchanged from v2.4.0.

## Fix

**Ngoma insert hard-fault on hardware.** `DrumCubeGraphic` had its `draw()` defined out-of-line in `DrumCubeGraphic.cpp`. Under GCC's "key function" rule, a class with one or more out-of-line virtuals emits its vtable in a single TU instead of as COMDAT. Any ABI drift in the `od::Graphic` vtable layout between firmware and the package's compile-time view then drifts the vtable offsets the package was compiled against — and inserting the cube hit a bad slot, hard-faulting on Cortex-A8.

**Fix:** full `DrumCubeGraphic` implementation moved inline into the header so the vtable is COMDAT-linked and immune to firmware-vs-package vtable drift. `DrumCubeGraphic.cpp` deleted.

`DrumVoice` ctor `sinf` initialization was also swapped to a precomputed 257-entry LUT (`DrumVoiceSineLUT.h`) per the known am335x package-trig miscompute, eliminating one more potential failure mode in the same insert path.

## Bundled prevention

- **`docs/graphics-authoring-guide.md`** — working patterns and anti-patterns for custom `od::Graphic` subclasses. The TL;DR rule: **all virtual overrides defined inline in the header**, no out-of-line virtuals in package `.cpp` files.
- **`tools/check-graphic-virtual-defs.sh`** — lint script flagging `<type> Class::virtualMethod(` patterns in package `.cpp` files. Run pre-release.

## What's not in this release

- No new units.
- No UI changes.
- No API changes.
- No other package changes (catchall, biome, mi, peaks, scope all unchanged from v2.4.0).

## Migration

None. Drop-in replacement for v2.4.0's `spreadsheet-2.6.0.pkg`.

## Known issues

Unchanged from v2.4.0 (D8 highlight bug, Pecto Doppler slew-time exposure deferred, Alembic Phase 6 serialization, Alembic sample-swap-without-detach race).
