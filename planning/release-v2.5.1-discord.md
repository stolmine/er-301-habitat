**er-301-habitat v2.5.1** - 2026-05-30

Two daily-driver upgrades: **Scope** with user-controllable timebase / Y-axis gain / built-in voltmeter, and **Larets** rebuilt as a proper stereo unit with linked CPR compression.

**Scope timebase / gain / voltmeter** (scope 1.1.0 -> 1.2.1) - All three scope variants (Scope, Scope 2x, Scope Stereo) now expose three sub-display controls: TIME (7 stops 1x..64x via FifoProbe decimation, display window 83 ms..5.3 s), GAIN (5 stops 0.25x..4x Y-axis multiplier, display-time only), and VOLT (bare 3-decimal voltage readout, rolling mean over the full probe buffer scaled to ±10 V chain rail). Voltmeter useful for DC and V/Oct analysis. Sub-display uses dotted-when-idle / solid-when-encoder-grabbed border convention to distinguish read-only readouts from focused editable controls. Probe reset + warmup on decimation change so the display fills cleanly with samples at the new rate. State round-trips across quicksave. Header-only custom `ScopeGraphic` aped from firmware MiniScope; no firmware changes required.

**Larets true stereo** (spreadsheet 2.7.0 -> 2.7.1) - Larets previously processed mono only and duplicated the output to both channels on stereo chains. Now it's a proper stereo unit using the internal-stereo C++ Object pattern: shared sequencer / step program / linked CPR detector / shared FX_SHUFFLE random pick / single viz tap, paired per-channel state for audio buffer + SVF + pitch phase + output crossfade. Linked CPR via max(|L|, |R|) -> one envelope -> same gain to both channels, no stereo image smear under heavy compression. FX_SHUFFLE's per-loop random offset is shared so L and R always play matching slices. Pattern selection (internal-stereo vs dual-instance) chosen because Larets has three sources of cross-channel state that would diverge between independent instances: compressor envelope, audio-rate FX_SHUFFLE random, transform-gate randomization.

**Behavior changes from v2.5.0**: Scope now requires M1/M2 to change timebase/gain (default 2x/1x reproduces v2.5.0 appearance). Larets stereo image is now preserved through the effect chain on stereo chains (patches relying on mono-to-both-channels behavior will hear independent L and R processing).

**Migration**: Larets inlet/outlet names changed (`In/Out` -> `In L/In R/Out L/Out R`). Patches inserted via the menu reconnect automatically; only custom patch scripts that reference Larets inlets by name need updating. No other breaking changes.

**Compatibility**: built against firmware v0.7.0-stolmine.9.2.0, same as v2.5.0. No firmware changes required.

Package versions: scope 1.1.0 -> 1.2.1, spreadsheet 2.7.0 -> 2.7.1. biome, catchall, kryos, mi, peaks, porcelain unchanged.

Full notes + binaries: <github release URL>
