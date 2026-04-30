**er-301-habitat v2.4.0** — 2026-04-29

Two new units: **Ngoma** (analog macro drum voice, spreadsheet) and **Alembic** (sample-trained 4-op phase-mod matrix synth, catchall). Big package reshape: 8 individual MI port packages collapse into a single new **`mi`** package. Pecto zipper-noise fixed. Shift-handling audit shipped.

**New unit: Ngoma** (spreadsheet) — Drum voice with 4-source mix (carrier osc + detuned-unison + sub-sine + 3 inharmonic membrane modes), 2x oversampling, NEON-vectorized phasor + per-partial decay quad. Pitch-morph membrane ratios — wide-spread sub-bass at low pitch, tight cymbal-sheen at high. Rotating cube viz on Character. 7 plies (trig, V/Oct, char, sweep, decay, level, expanded).

**New unit: Alembic** (catchall, experimental) — Sample-trained 4-op phase-mod matrix synth. Loads a sample, analyzes once, populates a 64-slot preset table per instance. 3 independent scan positions traverse trained content. Signal chain: PMM voice → wavetable shaper → 2x TPT SVF filter pair → 10×6×8 routing matrix → 24-tap multitap comb → output limiter. **Ferment** chaos macro (single fader 0-1.5). Sample-pointer excitation reads the source as a runtime modulator. Phase 6 serialization not yet shipped — trained state doesn't survive quicksave round-trip; sample re-analyzes on load.

**New unit: Constant Random** (biome) — Single-fader probability utility. Holds last value or picks fresh random at clock edges, swap chance from the fader.

**New package: `mi`** — Consolidates clouds + commotio + grids + marbles + plaits + rings + stratos + warps into one. All 9 units' functionality preserved. **Saved-patch break**: patches that referenced `clouds.Clouds`, `plaits.Plaits`, etc. need re-load + re-bind to `mi.Clouds` / `mi.Plaits` / etc.

**Pecto** — combSize / V/Oct knob sweeps now produce smooth Doppler-style pitch glide instead of audible zipper noise. Per-sample one-pole LP smoother on baseDelay; matches analog tape-delay character. Behavior change: if you liked the abrupt step character, future release will expose slew time as a sub-param.

**Shift-handling audit** — paramMode UI convention finalized across spreadsheet + biome. D1-D7 shipped. D8 visible-highlight bug on Helicase / Larets / MixInput pinned for next release (encoder works, only the highlight is missing).

**Migration**: saved patches with old MI port references or `biome.Pecto` need re-binding. Pecto migrated to spreadsheet during v2.4.x dev; Alembic ships fresh in catchall (no migration concern).

Package versions: spreadsheet 2.3.1 → 2.6.0, catchall 0.1.0 → 0.3.0, biome 2.1.0 → 2.2.0, mi 1.0.1 (new). All other packages unchanged.

Full notes + binaries: <github release URL>
