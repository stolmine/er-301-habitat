# er-301-habitat v2.5.1

Release date: 2026-05-30

Package updates: **scope 1.1.0 -> 1.2.1**, **spreadsheet 2.7.0 -> 2.7.1**. biome, catchall, kryos, mi, peaks, porcelain unchanged.

## Highlights

Two upgrades targeting useful-on-bench daily-driver utility:

- **Scope** finally has a user-controllable timebase (1x-64x), Y-axis gain (0.25x-4x), and a built-in voltmeter readout. The voltmeter folds in what was originally scoped as a separate unit — the probe + buffer infrastructure was already there, so a dedicated voltmeter unit didn't earn its slot.
- **Larets** now operates in true stereo. Internal-stereo rebuild rather than dual-instance mirroring, with linked CPR compression and a shared FX_SHUFFLE pick so the stereo image stays coherent under heavy effect chains.

## Scope: timebase, gain, voltmeter (scope 1.1.0 -> 1.2.1)

The three scope variants (Scope, Scope 2x, Scope Stereo) now expose three sub-display controls. All three controls are shared across L and R graphics in Scope Stereo — one Time / Gain pair drives both. State round-trips across quicksave / reload.

### Time selector (M1)

Seven stepped choices mapping to FifoProbe decimation. Default 2x matches the firmware MiniScope default. Encoder steps through the choices when M1 is focused.

| Setting | Decimation | Display window @ 48 kHz |
|---|---|---|
| 1x | 1 | 83 ms |
| 2x (default) | 2 | 167 ms |
| 4x | 4 | 333 ms |
| 8x | 8 | 667 ms |
| 16x | 16 | 1.33 s |
| 32x | 32 | 2.67 s |
| 64x | 64 | 5.33 s |

On change, the probe buffer is reset and re-enters a brief warmup so the next display refresh draws from samples all captured at the new rate. Without this, the FIFO carried a mix of old-rate and new-rate samples for up to ~10 seconds while it rolled over, producing a visibly "spliced" half-old half-new waveform during the rollover.

### Gain selector (M2)

Five stepped choices, applied at display time only — audio passthrough is unaffected.

| Setting | Y-axis multiplier |
|---|---|
| 0.25x | 0.25 |
| 0.5x | 0.5 |
| 1x (default) | 1.0 |
| 2x | 2.0 |
| 4x | 4.0 |

### Voltmeter readout (S3)

Bare 3-decimal numeric readout in the third sub-display column, no border (it's read-only and can't receive encoder focus). Rolling mean of the full probe buffer scaled to the ER-301 ±10 V chain rail. The mean is computed inside the existing `calculate()` refresh, so there's no extra audio-thread work beyond a single sum.

Integration window scales with timebase, since the probe buffer's content duration scales with decimation:

| Timebase | Integration window |
|---|---|
| 1x | ~167 ms |
| 2x | ~333 ms |
| 16x | ~2.7 s |
| 64x | ~10.7 s |

Behaviour by signal type:

- **DC / constant CV**: shows the input voltage essentially instantaneously, regardless of timebase.
- **V/Oct pitch CV**: integrates to the true V/Oct value within a few hundred ms at default timebase. Zoom out to 16x+ for rock-stable readings under any modulation jitter.
- **AC audio signals**: averages to ~0 (the DC component). Useful as a DC-offset sanity check on signals you don't expect to have one.
- **Slow LFO / envelope**: shows a smoothed average; for true peak / instantaneous readings, zoom in to 1x.

Right-justified text so the decimal point sits at a consistent column as digits change. Clamped to ±9.9995 V display range to keep the format stable at the rails.

### Sub-display visual convention

The sub-display now uses dotted-vs-solid borders to distinguish state. TIME and GAIN are editable, VOLT is read-only:

| State | TIME box | GAIN box | VOLT |
|---|---|---|---|
| Cursor elsewhere on chain | dotted | dotted | bare |
| Cursor on main display (no encoder grab) | dotted | dotted | bare |
| M1 pressed (encoder grabbed → TIME) | **solid** | dotted | bare |
| M2 pressed (encoder grabbed → GAIN) | dotted | **solid** | bare |
| Up pressed (encoder released) | dotted | dotted | bare |

Box widths auto-expand to fit the contained text, so "0.25x" or "64x" don't clip against a "2x"-sized box.

### Internals

`ScopeGraphic` is a header-only custom `od::Graphic` subclass aped from the firmware's `MiniScope`. Three header-only classes in the package (`ScopeGraphic`, `ScopeControlBox`, `ScopeVoltsReadout`) handle the new behavior; no firmware changes are required. The package's `libscope.so` continues to use the firmware-provided `FifoProbe` infrastructure via the existing public `setDecimation()` accessor.

## Larets: true stereo (spreadsheet 2.7.0 -> 2.7.1)

Larets previously processed mono only; on a stereo chain it would drop the R input and duplicate the mono output to both channels. Now it's a proper stereo unit, with cross-channel state correctly shared rather than duplicated.

### What's shared

A single Larets instance owns shared state for everything that must stay coherent across channels:

- **Linked CPR compressor**: `max(|L|, |R|)` drives a single envelope follower. The same gain reduction is applied to both channels, so stereo material doesn't get its image smeared by asymmetric compression on transients. This is the standard stereo-compressor pattern.
- **FX_SHUFFLE per-loop random pick**: the random buffer-offset pick is lifted out of `processEffect` into the sequencer-advance section, fired once per loop wrap. L and R always play matching slices of their respective audio buffers.
- **Step sequencer**: `mStep`, `mTickCount`, `mDivCount`, `mClockPeriodSamples` — driven by the shared Clock comparator, single instance.
- **Step program**: `type[]`, `param[]`, `ticks[]` arrays are single-copy. Transform-gate randomization (`applyTransform()`) operates on this shared data once per gate edge, so xform mutations stay coherent across channels regardless of how many times the transform fires.

### What's independent

Per-channel state lives in a paired `ChannelState ch[2]`:

- Audio circular buffers (`kBufferSize` floats each)
- SVF state (FX_FILTER `ic1eq` / `ic2eq`)
- Pitch phase (FX_PITCHSHIFT `pitchPhase`)
- Downsample state (FX_DOWNSAMPLE `holdSample` / `decimCounter`)
- Step-boundary output crossfade (`prevOutput` / `crossfadeCounter`)

This is the right split: each channel processes its own audio independently through whatever effect the shared sequencer has scheduled, but the timing and global character are locked.

### Architectural choice

Larets uses the **internal-stereo C++ Object pattern** rather than the **dual-instance Lua mirroring** pattern used by Pecto, Canals, Discont, Filterbank, Helicase. The latter pattern is correct when a unit has no cross-channel state that needs to stay coherent — independent SVF state on L vs R is fine, for instance. The former is required when shared sequencer / linked envelope / shared randomness would diverge between two independent C++ instances.

Larets has all three: an audio-rate xform-gate randomizer, an audio-rate FX_SHUFFLE random pick, and a CPR envelope. Two independent Larets instances would have produced visible failures on all three (compressor smear, drifting step programs after the first xform fire, divergent shuffle slices). The internal-stereo pattern is the correct architecture.

This decision logic is now documented in `feedback_stereo_pattern_selection` memory for future stereo work.

### Migration

Inlet / outlet names changed: `In/Out` → `In L/In R/Out L/Out R`. Patches inserted via the menu re-connect automatically since the framework picks up the new chain wiring. Only custom patch scripts that reference Larets inlets by name will need updating.

## Behaviour changes from v2.5.0

- **Scope** now requires `M1` / `M2` interaction to change timebase / gain. Default 2x timebase + 1x gain reproduces the v2.5.0 fixed-rate appearance.
- **Scope sub-display** is now populated with TIME / GAIN / VOLT readouts; v2.5.0 sub-display was empty.
- **Larets** stereo image is preserved through the effect chain on stereo chains. Patches that relied on the prior mono-to-both-channels behavior (e.g. Larets after a stereo source as a "force-mono" trick) will now hear independent L and R processing.
- **Larets CPR** under heavy compression on stereo material now ducks both channels by the same amount, rather than two independent envelopes producing visible image asymmetry.

## Known issues carried forward

- D8 highlight bug (from v2.4.0) still present.
- Pecto Doppler slew-time exposure still deferred.
- Alembic Phase 6 serialization still deferred.
- Plaits 6-op FM still grainy at native 48 kHz (still cooking).
- Biome is at internal version 2.2.0.1 with vendor-only CloudSeed DSP scaffolding (Phase A/B complete). No new biome units in this release; CloudSeed remains in progress.

## Compatibility

Built against firmware **v0.7.0-stolmine.9.2.0** (same as v2.5.0). No firmware changes required for this release.

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
