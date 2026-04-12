# Clock Ply Pattern

Custom clock control with division + reset on sub-display, replacing the default Gate's voltage threshold.

## Pattern

- **Main graphic:** ComparatorView (same as Gate — shows input signal, edge detection)
- **Sub-display:** scope (sub1/input), division readout (sub2/div), reset fire (sub3/reset)
- **Expansion view:** clock → reset (Gate), clockDiv (GainBias with CV branch)

Dual access: sub-display for quick tweaks without leaving the ply, expansion view for full-size controls with CV modulation.

## Current Implementation

`mods/spreadsheet/assets/LaretClockControl.lua` — built for Larets, could be generalized.

## Adoption Candidates

Any clock-driven unit currently using a plain Gate for clock input:

- Ballot (GateSeq)
- Excel (TrackerSeq)
- Transport
- Future Control Forge-alike
- Future clock-driven units

## To Generalize

- Rename to `ClockControl` and move to a shared location (or keep in spreadsheet if all consumers are there)
- Division range may vary per unit (some want 1-4, others 1-16) — pass as arg
- Some units may not need reset on the sub-display — make reset comparator optional
- Consider adding multiplication as well as division (x2, x4 etc.)
