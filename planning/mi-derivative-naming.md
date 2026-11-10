# MI derivative-works naming compliance

Settled 2026-11-10 with the user.

## Why

The Mutable Instruments eurorack repo README, "Guidelines for derivative works":

> Mutable Instruments is a registered trademark. The name "Mutable Instruments"
> should not be used on any of the derivative works you create from these files.
> We do not recommend you to keep the original name of the Mutable Instruments
> module for your derivative works.

Source: https://github.com/pichenettes/eurorack#guidelines-for-derivative-works

## Scope: user-facing only

Display titles, categories and docs change. Internal identifiers stay: package
names `mi` / `peaks`, `moduleName`s, Lua filenames, C++ / SWIG class names. Saved
patches store `libraryName` + `moduleName`, so leaving those alone means zero
preset risk and no alias plumbing. (The firmware does support package and unit
aliases, `xroot/Unit/Factory/init.lua:164` and `Library.lua:76`, if a deeper
rename is ever wanted.)

## Names

| Old | New |
|---|---|
| Plaits | Pleats |
| Clouds | Cirrus |
| Rings | Rota |
| Grids | Gradus |
| Warps | Wring |
| Marbles T | Mica T |
| Marbles X | Mica X |
| Peaks (package title + category) | Hills |

Unchanged: Stratos, Commotio (already original names), the `mi` package title and
category "MI" (not the trademark), toc keywords (not user-facing), all Peaks unit
titles (already generic).

Width rule: the unit header (`xroot/Unit/ViewControl/Header.lua:98`) puts one word
per line and shortens words over 8 chars; consolas10 is 5 px/char, so 8 chars =
40 px in the 42 px ply. Every new word is <= 6 chars.

## Edits

- `mods/mi/assets/toc.lua`: unit titles.
- `mods/mi/assets/{Plaits,Clouds,Rings,Grids,Warps,MarblesT,MarblesX}.lua`:
  `args.title`; `Grids.lua` circle view `description`.
- `mods/peaks/assets/toc.lua`: package title + every unit category -> Hills.
- `README.md`, `docs/reference/{README,mi,peaks}.md`: new names; keep the Emilie
  Gillet / Tim Churches credits; drop "Mutable Instruments" as a descriptor of
  our packages. Historical RELEASE-*.md and planning docs are left as history.
- Version bumps: mi 1.0.4 -> 1.0.5.1, peaks 1.0.0 -> 1.0.1.1 (dev digit). These
  also carry the unreleased package-load fix from 282b410.

## Verify

Build both arches, install to emu, check the chooser shows the new names under
MI and Hills, and that a quicksave containing old-named units still loads. Saved
patches only restore a title when the user renamed the unit (`hasUserTitle`,
`xroot/Unit/init.lua:449`), so old patches show the new names on load.
Mnemonics also changed: Cl->Ci, Rn->Ro, Wp->Wr (mi 1.0.5.2).
