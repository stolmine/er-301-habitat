# ER-301 Habitat: Unit Reference

Complete control reference for every publicly shipping unit, as of **v2.8.1**
(2026-08-06). 87 units across 7 packages.

Each page lists units in `toc.lua` order with their exact on-screen control
labels, types, ranges, defaults, sub-displays, menus and I/O. Everything is
derived from the shipped Lua and C++. Where prose in `README.md` or the release
notes disagreed with the code, the code won, and the disagreement is recorded in
a verification block at the foot of each page.

| Page | Package | Version | Units |
|------|---------|---------|-------|
| [mi.md](mi.md) | `mi` | 1.0.4 | 9 (Mutable Instruments ports) |
| [peaks.md](peaks.md) | `peaks` | 1.0.0 | 14 (Peaks / Dead Man's Catch ports) |
| [spreadsheet-generators.md](spreadsheet-generators.md) | `spreadsheet` | 2.8.5.1 | 9 (sources, voices, sequencers) |
| [spreadsheet-effects.md](spreadsheet-effects.md) | `spreadsheet` | 2.8.5.1 | 12 (effects) |
| [biome.md](biome.md) | `biome` | 2.2.3 | 23 (utilities, sequencers, small voices) |
| [scope.md](scope.md) | `scope` | 1.2.7 | 7 (inline visualization) |
| [house.md](house.md) | `house` | 0.1.1 | 8 (Airwindows reverbs + character) |
| [catchall.md](catchall.md) | `catchall` | 0.4.1 | 5 (**experimental**) |

## Not covered

Packages that exist in the tree but do not ship publicly: `kryos` (WIP),
`porcelain`, `anamnesis`, `zaum`, `stolmine`. Also excluded: `Plenum`
(suppressed in spreadsheet's `toc.lua`) and `XYZ` (parked in house).

## Known defects

The audit that produced these pages turned up 18 suspected defects, filed in
`planning/ledger.toml` under the `2026-08-10 reference audit` block. **None has
been confirmed on device**; each was found by reading source. The ones that
change a control's meaning are summarized per-package under "Known issues" in
the top-level `README.md`.

The most consequential:

- **`catchall.Flakes` cannot load**: it requires a module that does not exist.
- **`mi.Grids` is display-only**: its control view is never switched to.
- **`peaks` control labels**: Tap LFO, FM LFO, WSM LFO and ByteBeats have
  labels that do not match what the parameter drives.
- **`spreadsheet.Rauschen` V/Oct** is used by the DSP but bound to no control.
- **`biome.Quantoffset`** never connects its chain input.
