# er-301-habitat v2.8.2

Release date: 2026-11-13

Unit renames, no DSP changes. mi 1.0.4 -> 1.0.5, peaks 1.0.0 -> 1.0.1. All
other packages unchanged and republished as-is. Firmware unchanged from v2.8.0.

## Renames

The units derived from Émilie Gillet's open-source Eurorack firmware now carry
their own names, following the original author's guidelines for derivative
works. The DSP is the same code it always was, and the credit to Émilie Gillet
(and to Tim Churches for the Dead Man's Catch units) stays.

| Old | New | Mnemonic |
|---|---|---|
| Plaits | Pleats | Pl |
| Clouds | Cirrus | Ci |
| Rings | Rota | Ro |
| Grids | Gradus | Gr |
| Warps | Wring | Wr |
| Marbles T | Mica T | Mt |
| Marbles X | Mica X | Mx |

Stratos and Commotio keep their names. In the peaks package, the package title
and the unit-browser category change from Peaks to Hills. The unit names in it
(Bass Drum, Tap LFO and so on) and the Dead Man's Catch category are unchanged.

## Upgrading

Replace the mi and peaks packages. Saved patches keep loading: only the names
shown on screen changed, not the identifiers patches are saved against. A unit
in an old patch shows its new name when loaded, unless you had renamed it
yourself, in which case your name is kept.
