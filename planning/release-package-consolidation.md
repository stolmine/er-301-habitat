# Release Package Consolidation Plan

Status: **planning**, persisted 2026-04-29 before any code edits.

Two pre-release scoping moves:

1. **Alembic** moves from `spreadsheet` to `catchall`
2. **8 Mutable Instruments ports** (clouds / commotio / grids / marbles
   / plaits / rings / stratos / warps) consolidate into **one package**

Both touch package shape, not unit DSP. Sequence them: do Alembic
first (simpler, ~30 file moves) to validate the move-pattern, then
tackle Mutables consolidation (much larger, 8 mod dirs → 1).

---

## Move 1 — Alembic → catchall

### What moves

**C++ sources (4 files):**
- `mods/spreadsheet/AlembicVoice.cpp`
- `mods/spreadsheet/AlembicVoice.h`
- `mods/spreadsheet/AlembicSphereGraphic.cpp`
- `mods/spreadsheet/AlembicSphereGraphic.h`

**Lua assets (4 files):**
- `mods/spreadsheet/assets/AlembicVoice.lua`
- `mods/spreadsheet/assets/AlembicScanControl.lua`
- `mods/spreadsheet/assets/AlembicReagentControl.lua`
- `mods/spreadsheet/assets/AlembicRef.lua` (Phase 1 reference; still
  registered in toc.lua. Decision: ship it in catchall too as the
  listening reference, or retire? Codex notes it's "retired after
  Phase 2 A/B-verifies" — but still registered. **Decision: drop
  AlembicRef** since AlembicVoice is the production form and it was
  always meant to be temporary.)

### Cross-deps to resolve

**1. `pffft` is shared.** AlembicVoice uses `#include "pffft.h"` for
its FFT analysis kernel. **`pffft` is also used by spreadsheet's
MultibandCompressor + MultibandSaturator.** Cannot move pffft out
of spreadsheet entirely.

Options:
- **A)** Copy `pffft.{c,h,_stubs.c}` into `mods/catchall/`. Pragmatic,
  duplicates 3 small files (~50 KB total). Both packages compile their
  own copy. Clean separation. **Pick this.**
- B) Move pffft into `eurorack/` (shared tree). More work, more
  invasive, no concrete benefit for this scope.

**2. `ShiftHelpers.lua`** — both AlembicScanControl + AlembicReagent-
Control `require "spreadsheet.ShiftHelpers"`. Must change to
`require "catchall.ShiftHelpers"` (or equivalent). **Copy
`ShiftHelpers.lua`** from `mods/spreadsheet/assets/` to
`mods/catchall/assets/`. Functional duplicate; keep both copies in
sync if anything ever updates ShiftHelpers (low-rate concern; it's
been stable).

**3. Lua `require "spreadsheet.libspreadsheet"` references** — these
need to become `require "catchall.libcatchall"` in:
- AlembicVoice.lua: `local libspreadsheet = require
  "spreadsheet.libspreadsheet"` → `local libcatchall = require
  "catchall.libcatchall"` (and rename the local var or update its
  use sites — used as `libspreadsheet.AlembicVoice()` and
  `libspreadsheet.AlembicSphereGraphic(...)`).
- AlembicScanControl.lua: same — `libspreadsheet` → `libcatchall`.
- AlembicReagentControl.lua: doesn't actually require
  `libspreadsheet` per the grep, but verify before move.

### `.cpp.swig` edits

**Remove from `mods/spreadsheet/spreadsheet.cpp.swig`:**
```
#include "AlembicVoice.h"
#include "AlembicSphereGraphic.h"
%include "AlembicVoice.h"
%include "AlembicSphereGraphic.h"
```

**Add to `mods/catchall/catchall.cpp.swig`:**
```cpp
%{
#undef SWIGLUA
#include "AlembicVoice.h"
#include "AlembicSphereGraphic.h"
#define SWIGLUA
%}

%include "AlembicVoice.h"
%include "AlembicSphereGraphic.h"
```

(Slot in alphabetically; Sfera/Lambda/Flakes/Som already there.)

### `mod.mk` edits

**`mods/catchall/mod.mk`:**
- Add `MOD_C` declaration if not already present (for pffft.c +
  pffft_stubs.c). Look at spreadsheet's pattern.
- Add `EURORACK = eurorack` line if not present (catchall doesn't
  currently use it but Alembic's pffft does inline).
- Bump PKGVERSION 0.2.0 → 0.3.0.

**`mods/spreadsheet/mod.mk`:**
- No structural changes needed (pffft stays for Multiband units).
- Bump PKGVERSION to next spreadsheet release number once Alembic
  is gone.

### `toc.lua` edits

**Remove from `mods/spreadsheet/assets/toc.lua`:**
```lua
{ title = "AlembicRef",   moduleName = "AlembicRef",   category = "Spreadsheet", keywords = "..." },
{ title = "Alembic",      moduleName = "AlembicVoice", category = "Spreadsheet", keywords = "..." },
```

**Add to `mods/catchall/assets/toc.lua`:**
```lua
{ title = "Alembic", moduleName = "AlembicVoice", category = "Experimental", keywords = "phase, mod, matrix, fm, synthesis, voice, alembic" },
```

Drop AlembicRef from the catchall toc per the decision above.

### SWIG-build force-clean

Per `feedback_swig_header_dep`: removing/adding `%include` headers
needs the swig wrapper regenerated. Build flow:
```
rm -f testing/{linux,am335x}/mods/{spreadsheet,catchall}/*_swig.{cpp,o}
make spreadsheet ARCH=am335x
make catchall   ARCH=am335x
make spreadsheet ARCH=linux
make catchall   ARCH=linux
```

### Test matrix

After move, hardware-test:
- catchall installs cleanly; Alembic appears under "Experimental"
  category in the unit picker.
- Insert Alembic; default voice loads (no sample, placeholder
  preset). Confirm sphere viz renders.
- Sample-load flow: load any .wav → analyzeSample runs → sphere
  re-renders at scan position 0.
- Scan ply (paramMode shift-toggle for K) works; depth fader
  on the shift sub-display works (Phase 8e).
- Save/load round-trip: serialization NOT YET landed, so save
  won't preserve trained state. Sample re-analyses on load.
- spreadsheet still builds, installs, and all non-Alembic units
  function.
- Confirm pffft is built into BOTH spreadsheet (for Multiband) AND
  catchall (for Alembic).

### Open question on Alembic comb retrofit

The `feedback_doppler_basedelay_smoother.md` + `feedback_multitap_idx
_wrap_ulp.md` retrofit listed in `todo.md` was scoped against
spreadsheet's Pecto. With Alembic moving to catchall, the Alembic
comb retrofit becomes a **catchall** task. Update the todo location
accordingly post-move.

### Files this move touches

**Move (delete from spreadsheet, create in catchall):**
- mods/spreadsheet/AlembicVoice.{cpp,h} → mods/catchall/
- mods/spreadsheet/AlembicSphereGraphic.{cpp,h} → mods/catchall/
- mods/spreadsheet/assets/AlembicVoice.lua → mods/catchall/assets/
- mods/spreadsheet/assets/AlembicScanControl.lua → mods/catchall/assets/
- mods/spreadsheet/assets/AlembicReagentControl.lua → mods/catchall/assets/

**Drop:**
- mods/spreadsheet/assets/AlembicRef.lua (retire)

**Copy (duplicate):**
- mods/spreadsheet/pffft.{c,h} + pffft_stubs.c → mods/catchall/
- mods/spreadsheet/assets/ShiftHelpers.lua → mods/catchall/assets/

**Edit in place:**
- mods/spreadsheet/spreadsheet.cpp.swig (remove Alembic includes)
- mods/spreadsheet/assets/toc.lua (remove AlembicRef + AlembicVoice)
- mods/spreadsheet/mod.mk (PKGVERSION bump)
- mods/catchall/catchall.cpp.swig (add Alembic includes)
- mods/catchall/assets/toc.lua (add AlembicVoice)
- mods/catchall/mod.mk (PKGVERSION bump, MOD_C if needed,
  EURORACK if needed)
- mods/catchall/assets/AlembicVoice.lua: require strings
- mods/catchall/assets/AlembicScanControl.lua: require strings
- mods/catchall/assets/AlembicReagentControl.lua: require strings

---

## Move 2 — Mutable Instruments ports → single package

### Source-of-truth inventory (8 packages)

| pkg | units | C++ unit files | eurorack/ subset | own DSP under mod dir |
|---|---|---|---|---|
| clouds   | Clouds (granular)  | Clouds.{cpp,h} + pffft.{c,h} + pffft_stubs.c | clouds/dsp/granular_processor.cc + correlator.cc + mu_law.cc + pvoc/{phase_vocoder,stft,frame_transformation}.cc + clouds/resources.cc | none |
| commotio | Commotio (Elements exciter) | Commotio.{cpp,h} | elements/resources.cc + stmlib/{dsp/units.cc,dsp/atan.cc,utils/random.cc} | elements/ subtree (custom modified version) |
| grids    | Grids (pattern)    | Grids.{cpp,h} + grids_resources.h | (none — uses local grids_resources.h only) | none |
| marbles  | MarblesT, MarblesX | MarblesT.{cpp,h} + MarblesX.{cpp,h} + resources.cc | (uses MOD_DIR/random/, MOD_DIR/ramp/, MOD_DIR/stmlib/ — local copies) | random/, ramp/, stmlib/ subtrees |
| plaits   | Plaits (macro)     | PlaitsVoice.cpp + dsp/ | (uses MOD_DIR/dsp/, MOD_DIR/stmlib/ — local copies) | dsp/, stmlib/ subtrees |
| rings    | Rings (resonator)  | RingsVoice.cpp + part.cc + resonator.cc | (uses MOD_DIR/dsp/, MOD_DIR/stmlib/ — local copies) | dsp/, stmlib/ subtrees |
| stratos  | Stratos (reverb extracted from Clouds) | Stratos.{cpp,h} | (none direct; **#includes "clouds/dsp/frame.h" + "clouds/dsp/fx/reverb.h"** -- expects mods/clouds in include path or eurorack/clouds) | none |
| warps    | Warps (xmod)       | WarpsModulator.cpp + dsp/ | (uses MOD_DIR/dsp/, MOD_DIR/stmlib/ — local copies) | dsp/, stmlib/ subtrees |

**Cross-package source deps detected:**
1. **Stratos** uses `clouds/dsp/frame.h` + `clouds/dsp/fx/reverb.h`.
   Currently builds because… likely a hidden include dep on
   `mods/clouds/clouds/...` or `eurorack/clouds/...`. **This is the
   exact "cross package deps" the user flagged.**
2. **Rings (Lua)** does `require "plaits.EngineSelector"`. Lua-level
   cross-package dep. Without plaits installed, Rings's selector
   widget breaks.

### Symbol collision audit (the consolidation risk)

Each MI port has its own copy of `stmlib/dsp/units.cc`,
`stmlib/utils/random.cc`, etc. inside `mods/<pkg>/stmlib/` (per the
mod.mk listings). Some pull from `$(EURORACK)/stmlib/...` (commotio
specifically uses EURORACK-rooted paths).

Within a single package, building both `marbles/stmlib/dsp/units.cc`
AND `plaits/stmlib/dsp/units.cc` would cause **duplicate symbol
errors at link time** if they define the same exterior symbols.

Mitigation in the consolidated package:
- **Pick one copy** of stmlib (the one in `eurorack/stmlib/` — the
  shared canonical — or the `commotio` flavor since it already uses
  the EURORACK path).
- Drop the local stmlib copies from marbles, plaits, rings, warps.
  All units link against the single shared stmlib build.
- Verify by diffing `mods/<pkg>/stmlib/dsp/units.cc` against
  `eurorack/stmlib/dsp/units.cc` for each pkg — if they differ
  (likely yes, MI ports vendor-fork these), pick the most recent /
  most-functional version. Worth a careful merge if functions
  diverged.

Same potential issue for:
- `clouds/`, `plaits/`, `rings/`, `warps/`, `marbles/` DSP source
  trees: they're vendored MI source trees. Each has its own
  namespace (`namespace clouds {`, etc.) so symbol-level collisions
  are unlikely between *different* vendor trees, but **same-vendor
  duplicates are a risk** (e.g. if Stratos pulls clouds source AND
  Clouds also pulls clouds source via the same paths, units.o
  builds twice → duplicate definitions).
- `pffft.c` is in BOTH clouds/ AND spreadsheet/ AND will need to be
  in catchall/ (post-Move-1). The consolidated MI package only
  needs ONE copy via clouds (since other MI units don't use it).

### New package name decision

Per `feedback_no_third_party_branding`, can't ship as `mutables` /
`mi` / etc. publicly (and arguably not internally either since the
todo / planning notes go in repo).

Candidates discussed in todo (all generic-functional):
- `ports` — anchors at "this is a port of external sources"
- `library` — anchors at "shared library of voices/effects"
- `synthesis` — too narrow (excludes Marbles/Grids which are CV)
- `studio` — vague but OK

Recommendation: **`ports`** — clear meaning ("collection of ported
units"), short, no branding, fits with the existing "spreadsheet"
/ "catchall" / "biome" idiom of single-word descriptive package
names. Final name to be locked before implementation; the rest of
this plan uses `<pkg>` as a placeholder.

### Consolidated package layout

```
mods/<pkg>/
├── <pkg>.cpp.swig                 # %includes Plaits/Clouds/Rings/Grids/Warps/Stratos/Commotio/MarblesT/MarblesX
├── mod.mk                         # PKGVERSION 1.0.0; combined OBJECTS list
├── pffft.{c,h} + pffft_stubs.c    # for Clouds (only MI port using it)
├── grids_resources.h
├── PlaitsVoice.{cpp,h}
├── Clouds.{cpp,h}
├── RingsVoice.{cpp,h}
├── Grids.{cpp,h}
├── WarpsModulator.{cpp,h}
├── Stratos.{cpp,h}
├── Commotio.{cpp,h}
├── MarblesT.{cpp,h} + MarblesX.{cpp,h}
├── (per-vendor source subtrees only where the unit needs source.cc
│    files compiled — clouds/, plaits/, rings/, warps/, marbles/,
│    elements/. NO duplicate stmlib/ subtree -- single shared
│    eurorack/stmlib build.)
└── assets/
    ├── init.lua                  # single Library binding
    ├── toc.lua                   # all 9 units registered
    ├── Plaits.lua, EngineSelector.lua
    ├── Clouds.lua, ModeSelector.lua
    ├── Rings.lua, MixControl.lua
    ├── Grids.lua, GridsCircle.lua
    ├── Warps.lua, AlgoSelector.lua
    ├── Stratos.lua
    ├── Commotio.lua
    ├── MarblesT.lua, MarblesX.lua
    └── (any other shared controls — verify no duplicates by name
         e.g. EngineSelector reused between Plaits and Rings)
```

### Lua require migration

All 8 packages currently have `local lib<pkg> = require
"<pkg>.lib<pkg>"`. Migration: globally replace each per-pkg require
with `require "<pkg>.lib<pkg>"` (where `<pkg>` is the new
consolidated name).

Cross-package Lua requires that need updating:
- `mods/rings/assets/Rings.lua`: `require "plaits.EngineSelector"`
  → `require "<pkg>.EngineSelector"` (after consolidation, both
  Plaits and Rings live in the same package, single
  EngineSelector.lua copy works for both).

### `.cpp.swig` consolidated file

```cpp
%module <pkg>_lib<pkg>
%include <od/glue/mod.cpp.swig>

%{
#undef SWIGLUA
#include "PlaitsVoice.h"
#include "Clouds.h"
#include "RingsVoice.h"
#include "Grids.h"
#include "WarpsModulator.h"
#include "Stratos.h"
#include "Commotio.h"
#include "MarblesT.h"
#include "MarblesX.h"
#define SWIGLUA
%}

%include "PlaitsVoice.h"
%include "Clouds.h"
%include "RingsVoice.h"
%include "Grids.h"
%include "WarpsModulator.h"
%include "Stratos.h"
%include "Commotio.h"
%include "MarblesT.h"
%include "MarblesX.h"
```

### `mod.mk` consolidated OBJECTS

```make
PKGNAME ?= <pkg>
PKGVERSION ?= 1.0.0
include scripts/env.mk
EURORACK = eurorack

# ... boilerplate ...
MOD_DIR = mods/$(PKGNAME)
MOD_CPP = $(wildcard $(MOD_DIR)/*.cpp)
MOD_C   = $(wildcard $(MOD_DIR)/*.c)             # for pffft.c

# Vendor source trees (carefully de-duplicated)
CLOUDS_CC    = $(EURORACK)/clouds/dsp/granular_processor.cc \
               $(EURORACK)/clouds/dsp/correlator.cc \
               $(EURORACK)/clouds/dsp/mu_law.cc \
               $(EURORACK)/clouds/dsp/pvoc/phase_vocoder.cc \
               $(EURORACK)/clouds/dsp/pvoc/stft.cc \
               $(EURORACK)/clouds/dsp/pvoc/frame_transformation.cc
CLOUDS_RES   = $(EURORACK)/clouds/resources.cc

# Note: Stratos uses clouds/dsp/fx/reverb only -- already pulled in
# above? No, reverb.h is header-only or its .cc must be added:
STRATOS_CC   = $(EURORACK)/clouds/dsp/fx/reverb.cc      # (verify presence)

PLAITS_CC    = $(shell find -L $(EURORACK)/plaits/dsp -name '*.cc')
PLAITS_RES   = $(EURORACK)/plaits/resources.cc

RINGS_CC     = $(shell find -L $(EURORACK)/rings/dsp -name '*.cc')
RINGS_RES    = $(EURORACK)/rings/resources.cc

WARPS_CC     = $(shell find -L $(EURORACK)/warps/dsp -name '*.cc')
WARPS_RES    = $(EURORACK)/warps/resources.cc

ELEMENTS_CC  = $(MOD_DIR)/elements/dsp/exciter.cc       # commotio's
                                                        # custom-mod'd
                                                        # elements
ELEMENTS_RES = $(EURORACK)/elements/resources.cc

MARBLES_CC   = $(shell find -L $(EURORACK)/marbles/random -name '*.cc')
MARBLES_CC  += $(shell find -L $(EURORACK)/marbles/ramp -name '*.cc')
MARBLES_RES  = $(EURORACK)/marbles/resources.cc

# Single shared stmlib build (de-duplicated)
STMLIB_CC    = $(EURORACK)/stmlib/dsp/units.cc \
               $(EURORACK)/stmlib/dsp/atan.cc \
               $(EURORACK)/stmlib/utils/random.cc

OBJECTS  = $(addprefix $(OUT_DIR)/,$(MOD_CPP:%.cpp=%.o))
OBJECTS += $(addprefix $(OUT_DIR)/,$(MOD_C:%.c=%.o))
OBJECTS += $(addprefix $(OUT_DIR)/,$(CLOUDS_CC:%.cc=%.o))
OBJECTS += $(addprefix $(OUT_DIR)/,$(CLOUDS_RES:%.cc=%.o))
OBJECTS += $(addprefix $(OUT_DIR)/,$(STRATOS_CC:%.cc=%.o))
OBJECTS += $(addprefix $(OUT_DIR)/,$(PLAITS_CC:%.cc=%.o))
OBJECTS += $(addprefix $(OUT_DIR)/,$(PLAITS_RES:%.cc=%.o))
OBJECTS += $(addprefix $(OUT_DIR)/,$(RINGS_CC:%.cc=%.o))
OBJECTS += $(addprefix $(OUT_DIR)/,$(RINGS_RES:%.cc=%.o))
OBJECTS += $(addprefix $(OUT_DIR)/,$(WARPS_CC:%.cc=%.o))
OBJECTS += $(addprefix $(OUT_DIR)/,$(WARPS_RES:%.cc=%.o))
OBJECTS += $(addprefix $(OUT_DIR)/,$(ELEMENTS_CC:%.cc=%.o))
OBJECTS += $(addprefix $(OUT_DIR)/,$(ELEMENTS_RES:%.cc=%.o))
OBJECTS += $(addprefix $(OUT_DIR)/,$(MARBLES_CC:%.cc=%.o))
OBJECTS += $(addprefix $(OUT_DIR)/,$(MARBLES_RES:%.cc=%.o))
OBJECTS += $(addprefix $(OUT_DIR)/,$(STMLIB_CC:%.cc=%.o))
OBJECTS += $(SWIG_OBJECT)

INCLUDES = $(MOD_DIR) $(MOD_DIR)/elements/dsp mods $(SDKPATH) \
           $(SDKPATH)/arch/$(ARCH) $(SDKPATH)/emu $(EURORACK)
```

Watch out for:
- `find` returning files that overlap (e.g. if any of the per-vendor
  CCs include stmlib paths). Sanity-check with `make -n` once
  written.
- The current Marbles / Plaits / Rings / Warps mod.mk's use
  `$(MOD_DIR)/<vendor>/...` because they vendor-fork the source.
  After consolidation, the vendor sources need to live somewhere —
  ideally `eurorack/<vendor>/` if they match upstream, or we keep
  the forks in `mods/<pkg>/<vendor>/` and pull from there. **Need to
  diff `mods/<pkg>/<vendor>/` against `eurorack/<vendor>/`** for
  each. If they're identical, use eurorack. If different, copy the
  fork into `mods/<pkg>/<vendor>/` and pull from there.

### `init.lua` (Library binding)

Each package's `init.lua` defines a Library subclass. Consolidated
package needs ONE init.lua with all the units registered. Pattern:

```lua
local Class = require "Base.Class"
local Library = require "Package.Library"

local Ports = Class {}    -- (or whatever the new name is)
Ports:include(Library)

function Ports:init(args)
  Library.init(self, args)
end

return Ports
```

The actual unit registration happens in `toc.lua`.

### `toc.lua` consolidated

Single toc registering all 9 units. Per the no-branding memory the
unit names stay generic-functional (already the case — we ship
"Plaits", "Clouds", "Rings" but those are descriptive of what they
DO; if the brand-name policy requires them to change, that's a
separate decision out of this scope).

```lua
return {
  title    = "Ports",     -- (placeholder name)
  author   = "stolmine",
  name     = "<pkg>",
  keyword  = "ports, mutables, instruments",
  units = {
    { title = "Plaits",    moduleName = "Plaits",    category = "Synthesis", keywords = "..." },
    { title = "Clouds",    moduleName = "Clouds",    category = "Effect",    keywords = "..." },
    { title = "Rings",     moduleName = "Rings",     category = "Synthesis", keywords = "..." },
    { title = "Grids",     moduleName = "Grids",     category = "Modulation",keywords = "..." },
    { title = "Warps",     moduleName = "Warps",     category = "Effect",    keywords = "..." },
    { title = "Stratos",   moduleName = "Stratos",   category = "Effect",    keywords = "..." },
    { title = "Commotio",  moduleName = "Commotio",  category = "Synthesis", keywords = "..." },
    { title = "MarblesT",  moduleName = "MarblesT",  category = "Modulation",keywords = "..." },
    { title = "MarblesX",  moduleName = "MarblesX",  category = "Modulation",keywords = "..." }
  }
}
```

Need to harvest existing categories + keywords from each
package's current toc.lua during the merge.

### Sub-task ordering for Move 2

1. **Lock the consolidated package name.** Choose between `ports` /
   `library` / etc. Update plan + scripts to use the chosen name.
2. **Diff vendor source trees**: for each pkg, compare
   `mods/<pkg>/<vendor>/` to `eurorack/<vendor>/`. Identify forks
   vs identical copies. Decide per-pkg whether to use eurorack or
   carry the fork into the consolidated package.
3. **Author the new mod.mk** with the de-duplicated OBJECTS list.
   Build with `make -n` to verify file resolution before any actual
   compilation.
4. **Author the new `.cpp.swig`** %include'ing all 9 unit headers.
5. **Author the new `assets/init.lua` + `toc.lua`**.
6. **Move source files** (cpp, h, .c) into `mods/<pkg>/`. NOT
   "git mv" since they may need slight edits; can do as
   git-rename + edit in subsequent commits.
7. **Move asset files** into `mods/<pkg>/assets/` with require-string
   migrations done in the same commit.
8. **Build clean** — `make clean; rm -f testing/*/mods/<pkg>/*_swig.{cpp,o};
   make <pkg> ARCH=am335x; make <pkg> ARCH=linux`. Watch for
   duplicate-symbol errors at link time.
9. **Retire the 8 individual mod dirs** (or `git mv` them under
   `_archive/` for one release cycle if we want to keep history
   visible, then drop later).
10. **Hardware test matrix**: insert each of 9 units, verify
    function. Special-attention tests:
    - Stratos: should still produce Clouds-style reverb (cross-dep
      now resolved internally).
    - Rings: EngineSelector should work (now sourced from the same
      package as Plaits's).
    - Audio quality of CV-rate units (Marbles, Grids) unchanged.
    - Save/load any patches that referenced the OLD package names.
      **Quicksave compatibility may break** if patches saved the
      pre-consolidation package paths. Mitigation: load-time
      migration in firmware? Or accept that v1.0.0 of new package
      means old patches don't migrate. **Decision required**.

### Patch migration concern

Existing user quicksaves reference units by `"<pkg>.<UnitName>"`.
After consolidation, all units move to `<newpkg>.<UnitName>`. Old
quicksaves won't resolve.

Mitigations (none ideal):
- **A)** Leave the old packages in place ALSO (ship both old
  individual + new consolidated). Doubles the package count at
  install time and confuses the picker. Reject.
- **B)** Add load-time string substitution in firmware that maps
  old `<pkg>.X` references to `<newpkg>.X` for the 8 retired
  packages. Cleanest UX but requires firmware patching, which is
  out of scope for habitat.
- **C)** Document the breaking change in release notes; users
  re-load patches and re-bind unit references manually. Pragmatic
  but disruptive.

**Decision required before locking the move.** Recommendation: C
with a clear release-notes section ("Mutable Instruments ports
consolidated; saved patches need re-load").

---

## Order of operations

1. **Move 1 (Alembic → catchall)** — smaller, simpler, lower-risk.
   Validate the move-pattern.
2. **Pause**: hardware-test catchall + spreadsheet builds.
3. **Move 2 (Mutables consolidation)** — larger, requires the
   package-name decision + the vendor-source diff work + the patch
   migration policy decision before starting code edits.
4. **Pause**: hardware-test the new consolidated package.
5. **Cut release**.

## Decisions needed before implementation

- [ ] Alembic comb retrofit todo location: still in spreadsheet
  todo or moved with Alembic to catchall? (Plan assumes catchall.)
- [ ] Drop AlembicRef.lua entirely or carry into catchall?
  (Plan recommends drop.)
- [ ] Consolidated MI package name. Plan placeholder uses `<pkg>`.
- [ ] Vendor source resolution: per-pkg fork vs eurorack canonical.
  Need diff-pass before locking.
- [ ] Saved-patch migration policy: A / B / C above.

## Cross-references

- `feedback_swig_header_dep` — force-clean SWIG on header changes
- `feedback_no_third_party_branding` — package + unit name policy
- `feedback_package_version_bump` — version bumps to force re-extract
- `feedback_doppler_basedelay_smoother` + `feedback_multitap_idx_wrap_ulp`
  — apply to Alembic comb (post-move retrofit)
- `project_alembic_codex` — full Alembic architecture reference
