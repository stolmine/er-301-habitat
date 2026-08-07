# er-301-habitat v2.8.1

Release date: 2026-08-06

Hotfix for two control bugs in v2.8.0. spreadsheet 2.8.4 -> 2.8.5, biome
2.2.2 -> 2.2.3. All other packages unchanged and republished as-is. Firmware
unchanged from v2.8.0.

## Fixes

- Fixed a bug where turning a mode selector's CV gain knob moved the selected
  mode instead of the gain. Affected Routing and Clock Src on Vitrail, and the
  selectors on Canals, Pecto, Tomograph, Etcher, Petrichor, Excel, Rauschen,
  Parfait, 94 Discont and Latch Filter.
- Fixed a bug where a partial encoder turn on one sub-display readout would
  carry over to the next readout you selected, making it step early.

The second was introduced in v2.8.0. The first is older: it has been present on
Parfait's saturation selector since v2.6.2, and v2.8.0 widened it from two
controls to eleven by making more selectors step discretely.

## Upgrading

Replace the spreadsheet and biome packages. Nothing else changed, and no patches
are affected.
