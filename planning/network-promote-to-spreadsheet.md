# Network unit — promote from catchall to spreadsheet

## Context

Network at catchall 0.3.59 is feature-complete and well-optimized
(~62% CPU at full stereo settings). Catchall is the experimental
package; spreadsheet is the production-quality package. Network has
matured past the experimental tier — it's a polished multi-tap
delay/reverb with a substantial feature set (S1/S2/L3 lush polish,
G1–G8 glitch primitives, motion-cycle reseed, mutex modes,
listener-relative stereo, sign-change ZC stutter alignment).

This plan moves Network from `mods/catchall/` to
`mods/spreadsheet/`, leaving catchall lighter and grouping Network
with its musical-quality peers (Petrichor, Larets, Colmatage, Pecto,
Ngoma, JF, Visadhara, etc.).

UI rework (overview ply, glitch fader to far left) is a separate
follow-up plan — this commit just relocates the unit.

## Files to move (`git mv`)

1. `mods/catchall/Network.h` → `mods/spreadsheet/Network.h`
2. `mods/catchall/network/` → `mods/spreadsheet/network/`
   (subdirectory containing `trig_lut.h` and `geometry.h`)
3. `mods/catchall/assets/Network.lua` → `mods/spreadsheet/assets/Network.lua`

Network.h's includes already use a relative path
(`#include "network/trig_lut.h"`), so the subdirectory move is
self-contained — no path edits needed in the header.

## Files to update

### Lua require change (after move)

In `mods/spreadsheet/assets/Network.lua`:

```lua
-- before:
local libcatchall = require "catchall.libcatchall"
-- ...
local op = self:addObject("op", libcatchall.Network())

-- after:
local libspreadsheet = require "spreadsheet.libspreadsheet"
-- ...
local op = self:addObject("op", libspreadsheet.Network())
```

### SWIG registration

`mods/catchall/catchall.cpp.swig`: remove the two Network lines.
```cpp
// remove:
#include "Network.h"
%include "Network.h"
```

`mods/spreadsheet/spreadsheet.cpp.swig`: add the two Network lines
in matching positions in the `%{ ... %}` block and the `%include`
list (alphabetical or end-of-list — match existing convention; the
existing list isn't strictly alphabetical so end-of-list is fine).

### Package TOC (unit registry)

`mods/catchall/assets/toc.lua`: remove the Network row.

`mods/spreadsheet/assets/toc.lua`: add a Network row matching the
existing pattern. Use `category = "Effect"` (current spreadsheet
units use "Spreadsheet" but Effect is more accurate for Network).
Or keep `"Spreadsheet"` for consistency — pick one in plan.
**Recommendation: `"Spreadsheet"`** to match the package convention.

```lua
{ title = "Network", moduleName = "Network", category = "Spreadsheet",
  keywords = "reverb, multitap, delay, spatial, network, geometry, glitch" }
```

### PKGVERSION bumps

Per `feedback_package_version_bump` — the ER-301 only re-extracts
to rear SD when PKGVERSION changes. Both packages need bumps so
both reinstall.

- `mods/catchall/mod.mk`: `PKGVERSION ?= 0.3.59` → `0.3.60`
- `mods/spreadsheet/mod.mk`: `PKGVERSION ?= 2.6.1.17` → `2.6.1.18`

## Class name and namespace

Network is `stolmine::Network` — the namespace is shared between
catchall and spreadsheet (both use `namespace stolmine`), so the
class name doesn't conflict. SWIG module names differ
(`catchall_libcatchall` vs `spreadsheet_libspreadsheet`) so the
Lua-side reference is unique per package.

No naming conflict with existing spreadsheet units.

## Memory rules to observe

- `feedback_swig_header_dep` — moving Network.h is effectively a
  header edit from SWIG's perspective. **Force-clean both SWIG
  wrappers before build:**
  ```
  rm -f testing/am335x/mods/catchall/catchall_swig.cpp \
        testing/linux/mods/catchall/catchall_swig.cpp \
        testing/am335x/mods/spreadsheet/spreadsheet_swig.cpp \
        testing/linux/mods/spreadsheet/spreadsheet_swig.cpp
  ```
- `feedback_package_version_bump` — both PKGVERSIONs bumped.
- `feedback_persist_plans_to_repo` — this plan goes to
  `planning/network-promote-to-spreadsheet.md` before first edit.
- `feedback_linux_build_auto_install` — install BOTH linux packages
  to `~/.od/rear/` after build:
  ```
  cp testing/linux/catchall-0.3.60.pkg ~/.od/rear/
  cp testing/linux/spreadsheet-2.6.1.18.pkg ~/.od/rear/
  ```

## Implementation order

1. Persist plan to `planning/network-promote-to-spreadsheet.md`,
   commit (separate plan-persist commit, before code edits).
2. `git mv` Network.h, network/, assets/Network.lua to spreadsheet.
3. Edit Network.lua require to `libspreadsheet`.
4. Edit catchall.cpp.swig (remove) and spreadsheet.cpp.swig (add).
5. Edit catchall toc.lua (remove) and spreadsheet toc.lua (add).
6. Bump PKGVERSIONs in both mod.mk.
7. Force-clean both SWIG wrappers.
8. Build both packages: linux + am335x.
9. Verify NEON hints (0), vtable (V), graphic-virtuals lint (clean).
10. Install both linux packages to `~/.od/rear/`.
11. Single commit.

Single commit covers the move — the changes are tightly coupled.

## Verification

1. **Build clean**:
   - `make ARCH=linux PKGNAME=catchall` succeeds.
   - `make ARCH=linux PKGNAME=spreadsheet` succeeds.
   - `make ARCH=am335x PKGNAME=catchall` succeeds.
   - `make ARCH=am335x PKGNAME=spreadsheet` succeeds.
2. **NEON hint check** (memory rule):
   - `arm-none-eabi-objdump -d testing/am335x/mods/spreadsheet/spreadsheet_swig.o | grep -cE '\.32.*:(64|128)'` returns 0.
   - Same for catchall_swig.o.
3. **Lint**: `tools/check-graphic-virtual-defs.sh` exits clean
   (Network has no graphics; should pass trivially).
4. **vtable check**:
   `arm-none-eabi-nm -C testing/am335x/libspreadsheet.so | grep 'vtable for stolmine::Network'`
   shows `V` (COMDAT vague-linkage).
5. **Hardware audition**:
   - Catchall reloads, no Network entry visible in unit picker.
   - Spreadsheet reloads, Network appears in unit picker.
   - Insert Network from spreadsheet — sound engine identical to
     0.3.59 catchall version (same code, same character).
   - Other catchall units (Sfera, Lambda, Flakes, Som, Alembic)
     still load and work — confirms catchall.cpp.swig still
     compiles.
   - Other spreadsheet units (Petrichor, Pecto, etc.) still load.

## Critical files

**To modify:**
- `mods/catchall/Network.h` → moved to spreadsheet (no edit beyond move)
- `mods/catchall/network/{trig_lut,geometry}.h` → moved to spreadsheet
- `mods/catchall/assets/Network.lua` → moved + require edit
- `mods/catchall/catchall.cpp.swig` — remove two lines
- `mods/spreadsheet/spreadsheet.cpp.swig` — add two lines
- `mods/catchall/assets/toc.lua` — remove Network row
- `mods/spreadsheet/assets/toc.lua` — add Network row
- `mods/catchall/mod.mk` — PKGVERSION bump
- `mods/spreadsheet/mod.mk` — PKGVERSION bump

**To reference (read-only):**
- `mods/spreadsheet/assets/Pecto.lua`, `JF.lua` — reference for
  spreadsheet-side require pattern.
- `mods/spreadsheet/assets/toc.lua` — reference for toc row format.
- `er-301/xroot/boot/globals-setup.lua:23-28` — confirms
  unitOutputNames table covers Out1+Out2 (Network's needs).
