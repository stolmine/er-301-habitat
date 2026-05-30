# er-301-habitat v2.5.1

Release date: 2026-05-30

Package updates: **scope 1.1.0 -> 1.2.1**, **spreadsheet 2.7.0 -> 2.7.1**. biome, catchall, kryos, mi, peaks, porcelain unchanged.

## Highlights

Two upgrades targeting useful-on-bench daily-driver utility:

- **Scope** finally has a user-controllable timebase (1x-64x), Y-axis gain (0.25x-4x), and a built-in voltmeter readout. The voltmeter folds in what was originally scoped as a separate unit -- the probe + buffer infrastructure was already there.
- **Larets** now operates in true stereo. Internal-stereo rebuild rather than dual-instance mirroring, with linked CPR compression and a shared FX_SHUFFLE pick so stereo image stays coherent under heavy effect chains.

## Scope: timebase, gain, voltmeter (scope 1.2.1)

The three scope variants (Scope, Scope 2x, Scope Stereo) now expose three sub-display controls:

- **Time** (M1) -- 7 stepped choices, 1x..64x, mapping to FifoProbe decimation 1, 2, 4, 8, 16, 32, 64. Default 2x (matches firmware MiniScope default). Display window ranges 83 ms at 1x up to 5.3 s at 64x. On change, the probe buffer is reset and re-enters a brief warmup so the next display refresh draws from samples all captured at the new rate (no "spliced" half-old half-new buffer artifact).
- **Gain** (M2) -- 5 stepped choices, 0.25x..4x Y-axis multiplier. Applied at display time; doesn't affect the audio passthrough.
- **Volt** -- bare 3-decimal voltage readout in S3, no border (it's read-only). Rolling mean of the full probe buffer scaled to ER-301 ±10 V range. Integration window scales with timebase: stable for DC and V/Oct analysis, integrates to ~0 for AC content.

Sub-display now uses dotted-vs-solid borders to distinguish editable controls from read-only readouts, and unfocused-from-focused state. Boxes auto-resize to fit their content (so "0.25x" doesn't get clipped against a "2x"-sized box).

State persists across quicksave / reload.

No firmware changes -- this is all done in the scope package via a header-only custom `ScopeGraphic` aped from the firmware's MiniScope.

## Larets: true stereo (spreadsheet 2.7.1)

Larets previously processed mono only; on a stereo chain it would drop the R input and duplicate the mono output to both channels. Now it's a proper stereo unit, with three sources of cross-channel state correctly shared rather than duplicated:

- **Linked CPR compressor**: `max(|L|, |R|)` drives a single envelope follower, the same gain reduction is applied to both channels. No stereo-image smearing under heavy compression.
- **FX_SHUFFLE**: the per-loop random offset pick is lifted out of `processEffect` into the sequencer-advance section, fired once per beat. L and R always play matching slices of their respective buffers.
- **xform randomization**: step data is shared in one C++ instance, so transform-gate randomization keeps L and R in lockstep regardless of how many times it fires.

Per-channel state (audio buffer, SVF state for FX_FILTER, pitch phase for FX_PITCHSHIFT, output crossfade prevOutput) is correctly independent on L vs R.

This uses the **internal-stereo Object pattern** (single C++ Object with paired channel state) rather than the dual-instance Lua mirroring used by Pecto, Canals, Discont, etc. The pattern selection is now documented in memory: dual-instance is correct when the unit has no cross-channel state that must stay coherent; internal-stereo is correct when a shared sequencer, envelope, or RNG forces coherence.

Wire names changed (`In`/`Out` -> `In L`/`In R`/`Out L`/`Out R`). Patches inserted via the menu reconnect automatically; only custom patch scripts that reference Larets inlets by name will need updating.

## Known issues carried forward

- D8 highlight bug (from v2.4.0) still present.
- Pecto Doppler slew-time exposure still deferred.
- Alembic Phase 6 serialization still deferred.
- Plaits 6-op FM still grainy at native 48 kHz (still cooking).
- Biome is at internal version 2.2.0.1 with vendor-only CloudSeed DSP scaffolding (Phase A/B). No new biome units in this release; CloudSeed remains in progress.

## Plans

- `planning/scope-timebase-gain.md` -- the design walkthrough for the scope work
- `planning/larets-stereo.md` -- the design walkthrough for Larets stereo

## Compatibility

Built against firmware **v0.7.0-stolmine.9.2.0** (same as v2.5.0). No firmware changes required for this release.
