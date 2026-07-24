# Fabula: hoist into spreadsheet + merge back to main

Executes ledger items `fabula-promote-spreadsheet` and finishes the Fabula arc.
Current state: Fabula is feature-complete on branch `fabula-am335x` at habitat
`zaum` 0.2.0.63. The branch is **57 commits ahead of main, main 0 ahead** (main
is an ancestor) - so the eventual merge is a clean fast-forward, no conflicts.

Do the hoist on `fabula-am335x`, verify BOTH packages build, then FF-merge to
main and retire the branch. Two phases.

---

## Phase 1 - Hoist zaum -> spreadsheet (on `fabula-am335x`)

Fabula is currently one unit inside `zaum` (which also holds Sujet + STFTSpectral).
Move only Fabula's files; leave zaum intact and building.

### 1a. Move Fabula-specific files (use `git mv` to preserve history)
- `mods/zaum/atoms/APFTank.h`            -> `mods/spreadsheet/APFTank.h` (spreadsheet keeps atoms flat; match its layout - confirm where its .h atoms live)
- `mods/zaum/FabricGraphic.h`            -> `mods/spreadsheet/FabricGraphic.h`
- `mods/zaum/assets/Fabula.lua`          -> `mods/spreadsheet/assets/Fabula.lua`
- `mods/zaum/assets/FabulaOverviewControl.lua` -> `mods/spreadsheet/assets/`
- `mods/zaum/assets/MixHpfControl.lua`   -> `mods/spreadsheet/assets/`

### 1b. Reuse spreadsheet's existing shared assets - DELETE the zaum duplicates
- `mods/zaum/assets/ShiftHelpers.lua` -> already in spreadsheet; delete zaum copy.
- `mods/zaum/assets/TransformGateControl.lua` -> already in spreadsheet; delete zaum copy.
  (Diff them first - the zaum TransformGateControl only repointed the unused
  libspreadsheet require, so spreadsheet's original is the superset. Confirm no
  Fabula-specific edits were made to the zaum copy before deleting.)

### 1c. Repoint requires in the moved Lua
- `zaum.libzaum`               -> `spreadsheet.libspreadsheet`
- `zaum.ShiftHelpers`          -> `spreadsheet.ShiftHelpers`
- `zaum.FabulaOverviewControl` -> `spreadsheet.FabulaOverviewControl`
- `zaum.MixHpfControl`         -> `spreadsheet.MixHpfControl`
- `zaum.TransformGateControl`  -> `spreadsheet.TransformGateControl`
- In FabricGraphic.h: `#include "atoms/APFTank.h"` -> the new path.
- grep the moved files for any remaining `zaum`/`libzaum` reference.

### 1d. SWIG
- Move the `#include "FabricGraphic.h"` + APFTank/`%include` blocks out of
  `mods/zaum/zaum.cpp.swig` into the spreadsheet `.swig` (BOTH the C++ include
  and the `%include` block - keep FabricGraphic's `%include` AFTER APFTank's).
- Keep the SWIGLUA guard boundary correct: APFTank::setTopLevelBias +
  fireRandomize MUST remain above the `#ifndef SWIGLUA` guard so SWIG wraps them.
- **Force-clean the spreadsheet swig wrapper** after (sizeof/registry change) -
  see feedback_swig_header_dep. Verify `grep -c setTopLevelBias
  <spreadsheet>_swig.cpp` > 0.

### 1e. toc + version
- Add Fabula to `mods/spreadsheet/assets/toc.lua`; remove from zaum toc.lua.
- Bump `mods/spreadsheet/mod.mk` PKGVERSION (4th digit for dev iteration,
  feedback_package_version_bump); Fabula's private 0.2.0.x line ends - it now
  rides the spreadsheet version. Bump zaum too (its contents changed).

### 1f. Verify (both packages, both arches - feedback_always_build_both_arches)
- `make spreadsheet ARCH=linux && ARCH=am335x`; `make zaum ARCH=linux && ARCH=am335x`.
- Both clean; `check-neon-hints.sh` clean on both .so; FabricGraphic vtable weak
  (V) in the SPREADSHEET swig .o; `check-graphic-virtual-defs.sh` clean; luac-clean.
- Auto-install both linux pkgs to `~/.od/rear/` (feedback_linux_build_auto_install).
- Emu: Fabula loads via `loadUnit{libraryName="spreadsheet"}`; exercise viz,
  overview submenu + expand-to-fader, HPF knob, xform (default target 0 = all-
  but-freeze). Confirm zaum still loads Sujet.
- Commit the hoist with `[hab:fabula-promote-spreadsheet]` (mark the ledger item
  done on success).

### Risks
- Path layout: confirm where spreadsheet keeps atom headers (flat vs atoms/) and
  match it; fix the FabricGraphic include accordingly.
- Preset/serialization: units are addressed by `moduleName`+`libraryName`; moving
  packages changes `libraryName` (zaum -> spreadsheet), so existing patches that
  reference Fabula in zaum will not resolve. Acceptable pre-release (Fabula never
  shipped); note it if any test preset references it.

---

## Phase 2 - Merge `fabula-am335x` -> main

Preconditions: Phase 1 committed + pushed; both packages green; main still an
ancestor of the branch (re-check `git rev-list --count fabula-am335x..main` == 0
right before merging - if main moved, rebase the branch first).

Steps:
1. `git checkout main && git pull` (confirm up to date).
2. `git merge --ff-only fabula-am335x` - fast-forward, preserves the full
   `[hab:]`-tagged history (57 Fabula commits + the hoist). If FF is refused,
   main diverged: rebase `fabula-am335x` onto main, re-verify a build, retry.
   (Use `scripts/dev` for any commits; raw git push of main is the merge itself.)
3. Push main.
4. Retire the branch (delete local + remote), the way spatial-glitch-cm4 was
   folded in + retired (project_ledger_regime_habitat).
5. Post-merge bookkeeping:
   - Update `project_fabula_am335x` memory: new home = spreadsheet, libraryName
     spreadsheet, on main; branch retired.
   - Mark `fabula-promote-spreadsheet` ledger item done/attested.
   - Fabula is now a first-class spreadsheet unit; add it to the README
     "Original Units -> spreadsheet" table (row: algorithmic room reverb -
     SR/2 Dattorro tank, ER, Living Freeze, HPF knob, xform re-roll) as part of
     the next release notes, distinct from the resonant units
     (project_spreadsheet_effect_positioning).

History note: keep the honest history (Fabula developed in zaum, then moved). No
rebase-squash surgery - the ledger regime values the tagged commit trail.
