# er-301-habitat v2.5.1

Release date: 2026-05-30
Requires firmware: v0.7.0-stolmine.9.x

Package updates: **scope 1.1.0 -> 1.2.1**, **spreadsheet 2.7.0 -> 2.7.1**. biome, catchall, kryos, mi, peaks, porcelain unchanged.

## Highlights

Two upgrades focused on useful-on-bench daily-driver utility:

- **Scope** finally has a user-controllable timebase (1x-64x), Y-axis gain (0.25x-4x), and a built-in voltmeter readout. The voltmeter folds in what was originally scoped as a separate unit since the probe + buffer infrastructure was already there.
- **Larets** now operates in true stereo with internal-stereo rebuild, linked CPR compression, and a shared FX_SHUFFLE pick so the stereo image stays coherent under heavy effect chains.

## Scope: timebase, gain, voltmeter (scope 1.1.0 -> 1.2.1)

All three variants (Scope, Scope 2x, Scope Stereo) now expose three sub-display controls. State round-trips across quicksave / reload.

### Time selector (M1)

Seven stepped decimation choices. Default 2x matches firmware MiniScope behaviour.

| Setting | Display window @ 48 kHz |
|---|---|
| 1x | 83 ms |
| 2x (default) | 167 ms |
| 4x | 333 ms |
| 8x | 667 ms |
| 16x | 1.33 s |
| 32x | 2.67 s |
| 64x | 5.33 s |

On change the probe buffer is reset and re-enters warmup so the next refresh draws from samples all captured at the new rate — no spliced half-old / half-new waveform during the FIFO rollover.

### Gain selector (M2)

Five stepped Y-axis multipliers (0.25x, 0.5x, 1x default, 2x, 4x). Applied at display time only; the audio passthrough is unaffected.

### Voltmeter readout (S3)

Bare 3-decimal numeric readout, no border (read-only, can't receive focus). Rolling mean of the full probe buffer scaled to the ER-301 ±10 V chain rail. The mean is computed inside the existing refresh, no extra audio-thread work beyond a single sum.

Integration window scales with timebase: stable for DC and V/Oct analysis, integrates to ~0 for AC content. Zoom out to 16x+ for rock-stable V/Oct readings under modulation jitter; zoom in to 1x for instantaneous peak / envelope readings.

Right-justified text keeps the decimal column stable as digits change; clamped to ±9.9995 V to keep the format pinned at the rails.

### Sub-display visual convention

Dotted-vs-solid borders distinguish state. Editable controls (TIME, GAIN) sit in dotted boxes when idle, solid when their slot is encoder-grabbed. VOLT renders as bare numeric (read-only). Box widths auto-expand to fit content so "0.25x" / "64x" don't clip.

### Internals

`ScopeGraphic` is a header-only custom `od::Graphic` aped from the firmware's `MiniScope`. Three header-only classes (`ScopeGraphic`, `ScopeControlBox`, `ScopeVoltsReadout`) handle the new behaviour; no firmware changes are required. The package continues to use the firmware `FifoProbe` infrastructure via the existing public `setDecimation()` accessor.

## Larets: true stereo (spreadsheet 2.7.0 -> 2.7.1)

Larets previously processed mono only; on a stereo chain it dropped the R input and duplicated the mono output to both channels. Now it's a proper stereo unit.

### Shared state (single instance)

- **Linked CPR compressor**: `max(|L|, |R|)` drives a single envelope follower. Same gain reduction applied to both channels. No stereo image smearing under heavy compression.
- **FX_SHUFFLE random pick**: lifted out of `processEffect` into the sequencer-advance section, fired once per loop wrap. L and R always play matching slices of their respective buffers.
- **Step sequencer + program**: `mStep`, `mTickCount`, step `type[]`/`param[]`/`ticks[]` arrays. Transform-gate randomization mutates this shared data, so xform stays coherent across channels.

### Per-channel state (paired `ch[2]`)

Audio buffers, SVF state (FX_FILTER), pitch phase (FX_PITCHSHIFT), downsample state, step-boundary crossfade. Each channel processes its own audio through whatever effect the shared sequencer scheduled.

### Architectural pattern

Larets uses the **internal-stereo C++ Object pattern** (single instance, paired channel state). Most other habitat stereo units use **dual-instance Lua mirroring** (one C++ instance per channel). Pattern selection depends on whether cross-channel state needs to stay coherent — Pecto / Canals / Discont don't have shared sequencer / linked envelope / shared randomness, so dual-instance is fine. Larets has all three, so it requires internal-stereo.

Wire names changed: `In/Out` -> `In L/In R/Out L/Out R`. Patches inserted via the menu reconnect automatically; only custom patch scripts that reference Larets inlets by name need updating.

## Behaviour changes from v2.5.0

- **Scope** now requires M1 / M2 interaction to change timebase / gain. Default 2x / 1x reproduces the v2.5.0 fixed-rate appearance.
- **Scope sub-display** is populated with TIME / GAIN / VOLT readouts; was empty in v2.5.0.
- **Larets** stereo image is preserved through the effect chain on stereo chains. Patches that relied on the prior mono-to-both-channels behaviour will now hear independent L and R processing.
- **Larets CPR** under heavy compression on stereo material ducks both channels by the same amount, rather than two independent envelopes producing visible image asymmetry.

## Migration

Larets wire name change is the only breaking surface, and only for custom patch scripts that explicitly reference `In` / `Out` on Larets. Menu-inserted patches reconnect automatically. No other breaking changes.

## Known issues carried forward

- D8 highlight bug (from v2.4.0) still present.
- Pecto Doppler slew-time exposure still deferred.
- Alembic Phase 6 serialization still deferred.
- Plaits 6-op FM still grainy at native 48 kHz.

## Compatibility

Built against firmware **v0.7.0-stolmine.9.2.0** (same as v2.5.0). No firmware changes required.

## Package version summary

| Package | v2.5.0 | v2.5.1 |
|---|---|---|
| spreadsheet | 2.7.0 | 2.7.1 |
| scope | 1.1.0 | 1.2.1 |
| biome | 2.2.0 | 2.2.0 |
| mi | 1.0.4 | 1.0.4 |
| catchall | 0.4.0 | 0.4.0 |
| kryos | 1.0.0 | 1.0.0 |
| peaks | 1.0.0 | 1.0.0 |
| porcelain | 0.1.0 | 0.1.0 |

Plans: `planning/scope-timebase-gain.md`, `planning/larets-stereo.md`.
