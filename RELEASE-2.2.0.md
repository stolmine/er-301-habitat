# er-301-habitat v2.2.0

Release date: 2026-04-14

Requires firmware: v0.7.0-txo (er-301-stolmine)

Package updates: **spreadsheet 2.1.0 -> 2.2.0**. All other packages unchanged.

## Highlights

Comprehensive save/load reliability pass across every unit in the spreadsheet package, a critical hardware-only visualization fix on Tomograph, plus a new pitch macro for Petrichor.

## Tomograph -- hardware viz fix

Restored the "ferrofluid ring" overview visualization on ER-301 hardware. The radial polyline was rendering with a distended bottom lobe and spoke lines landing in wrong positions for every reload since the v2.0.0 release window. Root cause was that runtime `sinf` / `cosf` calls from inside a package `.so` miscompute on am335x with the TI 4.9.3 toolchain; firmware-compiled graphics (ScaleQuantizer's PitchCircle, etc.) are unaffected because they link directly.

Fixed by rebuilding `FilterResponseGraphic` around a 72-entry precomputed cos/sin lookup table. Identical render math, values never touch libm at runtime. Emulator behavior unchanged.

## New feature: Petrichor pitch macro

Sixth MacroControl on Petrichor alongside vol / pan / cutoff / Q / type. 14 presets that snap all active taps to musical pitch patterns in semitones:

- `0`, `+12`, `-12`, `+7`, `-7` -- unison at each consonant interval
- `asc`, `desc` -- ascending / descending semitones per tap
- `asc8`, `ds8` -- octave spread from -12 to +12 across the tap count
- `e12`, `e-12`, `e+7` -- even / odd splits at octave and fifth intervals
- `maj`, `min` -- cycling major and minor chord tones (0 4 7 12, 0 3 7 12)

Lives in the taps expansion row, button label `pitch`. Sets per-tap pitch non-destructively -- further per-tap edits work over the macro baseline.

## Serialization overhaul

Every unit in the spreadsheet package now round-trips its full user-facing state across quicksave / reload. Previous behavior was that most top-level faders reverted to their insert-time defaults on patch reload, and some toggle state and preset labels stayed out of sync with the underlying value.

Concrete fixes per unit:

- **Excel** -- offset range switch now scales all 64 stored offsets proportionally (x5 on 2V->10V, x0.2 on 10V->2V) and reloads the edit buffer so readouts update live. Playhead modulo-wraps into the new range on stepCount reduction (was stuck showing "14/1").
- **Ballot** -- RatchetLen / RatchetVel options rebased to the 1/2 value convention (the previous 0/1 convention placed "off" on the CHOICE_UNKNOWN sentinel and silently failed to persist); `enableSerialization()` moved to the C++ constructor. RatchetMult fader persists. Toggle badges (`len:ON`, `vel:ON`) now refresh post-reload to match restored options. Playhead wrap matches Excel.
- **Larets** -- AutoMakeup option persists. Playhead wrap matches Excel / Ballot.
- **Etcher** -- Skew default corrected from 1.0 (extreme) to 0.0 (neutral); `initialBias` on the GainBias had been overriding the intended neutral `hardSet`. Etcher's `mActiveSegment` is recomputed from input CV each sample so no playhead wrap is needed.
- **Tomograph** -- `scale` ParameterAdapter Bias now round-trips and the scale-name fader label refreshes on load.
- **Petrichor** -- every top-level knob (23 ParameterAdapters covering masterTime, feedback, mix, tapCount, feedbackTone, V/Oct, all 5 macro selectors, xform target/depth/spread, grainSize, skew, drift, reverse, stack, grid, input/output level, tanh) plus tune ConstantOffset and xform-gate Comparator threshold now persists. Macro fader labels refresh on load.
- **Parfait** -- 33 ParameterAdapter Biases (9 global + 24 per-band) plus BandMute parameters now persist.
- **Rauschen** -- 7 ParameterAdapter Biases persist. Algorithm fader label refreshes on load.
- **Impasto** -- 28 ParameterAdapter Biases plus both options now persist. opR option sync retained for legacy patches so L/R stay coherent on stereo chains after save/load.
- **Helicase** -- 12 ParameterAdapter Biases plus LinExpo and HiFi options plus tune Offset plus syncComparator Threshold now persist. lin/exp label refreshes on load.

## Step-list UX

Fix applied to every unit with a step list (Excel, Ballot, Larets, Etcher). When the step count is reduced below the currently selected step, the graphic now clamps the selection to the new last entry and the viewport follows, instead of scrolling past the end and leaving the list rendering as a blank region. On re-entering the ply, the edit buffer reconciles to the clamped step so readouts reflect the right step, not the stale selection.

## Install

Copy `.pkg` files from `testing/am335x/` to SD card at `/mnt/ER-301/packages/`.

Delete the installed spreadsheet lib from the rear card to force reinstall:

```
sudo rm -rf /mnt/v0.7/libs/spreadsheet/
```

## Notes for contributors

Serialization pattern has been standardized. Every unit's `MyUnit:serialize()` / `MyUnit:deserialize()` follows the same shape:

1. Loop over `adapterBiases` list calling `target()` / `hardSet("Bias", v)` for every ParameterAdapter.
2. Special cases for `ConstantOffset` (param name `Offset`) and `Comparator` (`Threshold`).
3. `od::Option` toggles get `enableSerialization()` in the C++ constructor (not in Lua control init) with values 1 / 2 (never 0, which is `CHOICE_UNKNOWN`).
4. Any label derived from a restored value calls its refresh method (`updateLabel()` on ModeSelector / MacroControl, custom methods on other controls) at the end of deserialize.

Header changes to files `%include`d by SWIG still don't trigger wrapper regen under default Makefile rules -- for ARM builds, force-remove `testing/am335x/mods/<pkg>/*_swig.{cpp,o}` before `make` if only headers changed.
