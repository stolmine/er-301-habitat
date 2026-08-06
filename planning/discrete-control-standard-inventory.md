# Discrete-control stepping: standard + inventory

Survey 2026-08-05. Scope: every control that addresses an **integer or a discrete
state**, regardless of whether its parameter is typed as a float or normalised
0-1. Goal is one uniform feel for stepping through discrete options.

## The problem, stated once

Scaling the dial-map step is the obvious approach and it is **wrong**: it fights
the encoder's acceleration, so a fast turn still jumps several options. The
firmware's own discrete navigation instead **accumulates raw encoder change** and
steps once per threshold, which is acceleration-independent.

The catalogue has almost entirely not adopted this:

| Surface | Count | On the discrete model |
|---|---|---|
| `ModeSelector` instances | ~26 across 22 files | **2** (Parfait, Vitrail) |
| Files defining their own `encoder()` | 89 | **3** (2 ModeSelector copies + SpectrogramFactory) |
| Files with a paramMode sub-display | 41 | — routes straight to a Readout |
| Files with an integer dial map | 83 | — |

So ~24 of 26 selectors and effectively every sub-display readout currently step
by dial-map value and inherit acceleration.

## The standard (from Vitrail routing, `spreadsheet/assets/ModeSelector.lua`)

A discrete list has no fractional positions, so the encoder's four resolutions
cannot be four step *values* the way a continuous dial's are. They collapse to
**two axes**: how many entries a step moves, and how much travel one step costs.

| Resolution | Step | Travel |
|---|---|---|
| **coarse** (default) | `discreteCoarseStep`, normally **1 entry** | `discreteThreshold` = **8** (~2 detents) |
| **fine** | same 1 entry | **2 × threshold** = 16 (~4 detents), deliberate, cannot overshoot |
| **shift** | `discreteJumpStep`, a whole group | threshold |

Rules:
1. **Coarse moves one entry.** This is the default encoder state, so it must be
   the useful resolution. Vitrail originally shipped coarse=5 and it was
   unusable — you could not land on a specific option.
2. **Fine does not subdivide, it costs more travel.** There is nothing between
   two adjacent options, so "finer" can only mean harder to move past.
3. **Shift jumps a group** only where the list has real internal structure
   (Vitrail routing: 5 filter-A families × 2 halves). Omit otherwise.
4. **Threshold 8** as the base. The older habitat note proposed 16; 8 (~2
   detents) reads better and is what shipped. Longer lists keep 8 and lean on
   the shift-jump rather than lowering it further.
5. Every step is an **exact multiple of the index**, so no resolution can land
   between entries or skip one.

## Worklist

### Tier 1 — `ModeSelector` consumers (share the patched class per package)

One line each: add `discrete = true, discreteThreshold = 8`. The class already
supports it; these simply never opted in.

| Package | File | Selectors | Notes |
|---|---|---|---|
| spreadsheet | Pecto | 3 | pattern(16) / resonator(4) / slope(4) — pattern wants shift-jump |
| spreadsheet | MultitapDelay | 2 | vol/pan macros, grid/stack |
| spreadsheet | Canals | 2 | |
| spreadsheet | Rauschen | 1 | 11 algorithms |
| spreadsheet | Filterbank | 1 | scale selector |
| spreadsheet | Etcher | 1 | |
| spreadsheet | TrackerSeq | 1 | |
| spreadsheet | MultibandSaturator | 1 | **already discrete** — the reference |
| spreadsheet | Vitrail | 2 | **already discrete** — the standard |
| biome | Discont, LatchFilter | 2 | |
| catchall | Lambda, Sfera | 2 | Lambda seed, Sfera config |
| mi | Clouds | 1 | mode selector |
| porcelain | Chime | 1 | not shipping |
| stolmine | (7 files) | 8 | legacy package, skip |

### Tier 2 — discrete readouts inside paramMode sub-displays

These are the "sub-display options" and they are the largest block. Each routes
the encoder to `paramFocusedReadout:encoder(...)`, so an integer readout steps by
map value and accelerates. They need the accumulate model in the host control's
`encoder()`, the way `SpectrogramFactory` open-coded it.

| File | Integer readouts |
|---|---|
| spreadsheet/DensityControl | 5 |
| spreadsheet/LaretOverviewControl | 4 |
| spreadsheet/ColmatageBlockControl | 3 |
| spreadsheet/TransformGateControl | 3 |
| spreadsheet/DrumVoicePitchControl | 2 |
| spreadsheet/HelicaseOverviewControl | 2 |
| spreadsheet/RatchetControl | 2 |
| spreadsheet/TimeControl | 2 |
| spreadsheet/VisadharaPitchControl | 2 |
| catchall/AlembicScanControl | 2 |

`TransformGateControl` is shared by Larets, Pecto, TrackerSeq and MultitapDelay,
so fixing it once covers four units.

### Tier 3 — binary toggles

Already effectively discrete (a press, not a turn): Fade Mixer smooth/snap,
Larets random/sequential, Larets auto-makeup, every menu `OptionControl`. No
encoder involved, nothing to standardise. Listed so the audit is complete.

### Out of scope

- `stolmine` — legacy pre-split package, not in the release set.
- `porcelain`, `kryos`, `anamnesis`, `zaum` — not shipping.
- Integer maps on controls that are **not** user-stepped (internal state,
  serialisation-only parameters).

## Risk

`ModeSelector.lua` is duplicated in biome and spreadsheet and is consumed by ~20
units. Any change there is a catalogue-wide regression surface — the same reason
tier 2 of the v2.8.0 test matrix exists. Do this **after** v2.8.0 ships, not
inside it.
