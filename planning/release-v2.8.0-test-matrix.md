# v2.8.0 test matrix — everything changed since v2.7.0

Generated 2026-08-05. Baseline: **v2.7.0** (2026-07-15). Verify on hardware.

## Release set

Published in v2.7.0: `biome catchall mi peaks scope spreadsheet` — six packages.
That stays the shipping set, minus nothing, plus a pending decision on `house`.

| Package | v2.7.0 | v2.8.0 | Ship | Why |
|---|---|---|---|---|
| spreadsheet | 2.8.3 | 2.8.3.90 | yes | largest change set |
| biome | 2.2.1 | 2.2.1.15 | yes | 4 new units + a breaking control change |
| scope | 1.2.1 | 1.2.6 | yes | 3 new units + sub-display |
| catchall | 0.4.0 | 0.4.0.1 | yes | mix detent only |
| mi | 1.0.4 | 1.0.4 | yes | unchanged, republish |
| peaks | 1.0.0 | 1.0.0 | yes | unchanged, republish |
| **house** | 0.1.0.36 | 0.1.0.43 | **decide** | never published; would be a package debut |
| porcelain | 0.1.0 | — | no | user excluded |
| kryos | 1.0.0 | — | no | user excluded, `kryos-load-hang` still blocked |
| zaum | 0.2.0.64 | — | no | user excluded |
| anamnesis | 0.2.0.83 | — | no | user excluded, `anamnesis-insert-crash` open |
| stolmine | 1.3.0 | — | no | legacy pre-split package, never in the asset set |

`house` is the only real question. It has never shipped, so including it debuts 8
Airwindows reverb ports plus TickerTape and Lacquer as a new package. Its
optimization item (`house-suppress-customs-optimize-ports`) is still wip with
Galactic and BrightAmbience3 unconverted, and the hybrid-float work in this cycle
is offline-verified only.

---

## Tier 1 — breaking changes and whole-engine replacements

Test these first. Each can invalidate an existing patch or has replaced a shipped
DSP wholesale.

| # | Unit | What changed | Test | Expect |
|---|---|---|---|---|
| 1.1 | **Ngoma** | Entire voice engine replaced by the modal transplant. Preset **schema 5**. | Load a **pre-v2.8 Ngoma patch**. | Loads without crash; laws differ, so the sound will not match. Confirm no hang. |
| 1.2 | **Ngoma** | Insert path (this unit hard-hung hardware pre-2.4.1). | Insert / delete / re-insert 10x on hardware. | No hang, no data abort. |
| 1.3 | **Ngoma** | Clipper default **1.0 → 0.0**. | Insert fresh, trigger. | Ships noticeably cleaner/quieter than v2.7.0. Clipper up adds heft. |
| 1.4 | **Ngoma** | NEON SoA kernel, 14 modes. | CPU meter, mono chain. | ~10% mono. Flag if materially higher. |
| 1.5 | **Ngoma** | Shape / Character / Grit / Decay all refitted. | Sweep each full throw. | No dropouts, no NaN silence, no runaway. Known misses in `ngoma-known-residuals`. |
| 1.6 | **Constant Random** | **BREAKING**: Slew is now **seconds**, was a 0-1 amount. | Load a pre-v2.8 patch using Slew. | Slew reads as seconds — a stored 0.5 now means 0.5 s, not 38 ms. Expected; needs re-dialling. |
| 1.6b | **Constant Random** | Slew throw is 0 ms .. 786 s; **0 is a true hard jump**. | Set slew to the bottom of the throw; trigger fast rates. | Pure sample-and-hold, stepped, zero glide. One detent up gives 3 ms. |
| 1.7 | **Constant Random** | Rate map linear 0.01-100 → **0-100, coarse 0.1 Hz**. | Dial rate near 0.05 Hz. | One coarse detent moves 0.1 Hz, not 1.0. Slow end usable. |
| 1.8 | **Constant Random** | **0 Hz = pause** (was a 0.01 Hz floor). | Set rate 0, wait 5 min. | Output freezes on the held value. No new steps at all. |
| 1.9 | **Constant Random** | Level default **1.0 → 0.5**, map now bipolar `[-1,1]`. | Insert fresh; then set level negative. | Default swings ±5 V not ±10 V. Negative level inverts. |
| 1.10 | **Vitrail** | New to users (built and moved packages post-v2.7.0). | Insert on hardware; sweep Cut A/B, Res, Gain. | No hang. Self-osc and comb behaviour audible. CPU acceptable. |

## Tier 2 — shared code touched (regression risk beyond the unit changed)

`ModeSelector.lua` and the mix-control sweep touched code shared across many
units. The default paths should be unchanged, but that is the claim under test.

| # | Scope | What changed | Test | Expect |
|---|---|---|---|---|
| 2.1 | **ModeSelector consumers** | `normalized` + `discreteJumpStep` added; default path meant to be untouched. | Cycle the selector on **Canals, Pecto, Parfait, Rauschen, Tomograph, Petrichor, Mirror, Etcher, Ballot, Excel**. | Each still steps one option per turn exactly as in v2.7.0. |
| 2.2 | **Vitrail routing** | Coarse = 1 entry, fine = 1 entry at double travel, **shift = jump 5**. | Turn coarse, toggle fine, hold shift and turn. | Coarse lands on single pairs; shift crosses families. **Shift-jump is unproven** — if shift is swallowed upstream it silently falls back to coarse. |
| 2.3 | **Vitrail routing** | Normalized to 0-1 for CV. | Patch a 0-1 LFO into routing, gain up. | Sweeps all 50 pairs across the CV range. |
| 2.4 | **Mix detents** (27 controls) | All now `Encoder.getMap("unit")`, coarse 0.01. | **Impasto** and **Parfait** mix knobs specifically. | 0.01 per detent, was 0.1. Ten times finer — the most noticeable of the set. |
| 2.5 | **Mix detents** | Same sweep, other units. | Pecto, Petrichor, Tomograph, Network, Colmatage, Helicase, Larets, Fabula, Station X, Som. | 0.01 coarse everywhere; no control lost its range. |
| 2.6 | **biome f0 controls** | Moved to the `oscFreq` octave map. | Bletchley Park, Varishape Osc, Varishape Voice pitch dials. | Octave-per-detent feel, not linear Hz. |

## Tier 3 — new units (never auditioned)

| # | Unit | Package | Test | Expect |
|---|---|---|---|---|
| 3.1 | **Expo D** | biome | Trigger; sweep Decay and Curve. | Decay-only envelope; curve default is fully exponential. |
| 3.2 | **Expo AD** | biome | Trigger; sweep Attack, Decay, both Curves. | AD shape; retrigger mid-decay restarts (may click — accepted v1). |
| 3.3 | **Fade Mixer 6 / 8** | biome | Patch 6 and 8 sources; sweep Fade. | Crossfades across all inputs; endpoints pick in1 and in6/in8. |
| 3.4 | **Fade Mixer** (all 3) | biome | **Mute and solo per channel** — these did nothing before 2.2.1.4. | Mute gates that input only; solo acts within this mixer, not the chain. |
| 3.5 | **Fade Mixer** (all 3) | biome | Config menu → Fade → **snap**; sweep Fade. | Hard N-to-1 switching, **no click** at the switch point. End inputs own half-width zones by design. |
| 3.6 | **Fade Mixer** (all 3) | biome | Toggle back to smooth. | Identical to v2.7.0 behaviour (proven bit-identical offline). |
| 3.7 | **Spectrogram 3 / 4 / 6** | scope | Insert each; feed broadband. | Wider canvas; 4 and 6 ply are backed by a 512-pt FFT (256 real bins). |
| 3.8 | **Spectrogram** (all) | scope | Sub-display: S1 freq log/lin, S2 amp log/lin/exp, S3 peak readout. | Modes switch and **persist across save/reload**. Peak reads the dominant frequency + dB. |
| 3.9 | **Spectrogram** (all) | scope | Encoder on S1/S2. | One step per ~4 detents, acceleration-independent. |
| 3.10 | **Vitrail tunnel viz** | spreadsheet | Watch the Clock Src ply while playing. | Reads as depth at 42x64; rotation tracks A/B drift (Src=Both); resonance steps the polygon; Cut A/B imbalance banks it. Check frame rate. |

## Tier 4 — lower risk

| # | Item | Test | Expect |
|---|---|---|---|
| 4.1 | **Larets** step toggle | Overview sub-display sub1; set random. | Lit = random. **No step ever fires twice in a row.** Survives save/reload. |
| 4.2 | **Larets** skew | Overview expansion. | Skew still present and editable — it was displaced from the sub-display, not removed. |
| 4.3 | **Rauschen** Cellular | Select algorithm 11; sweep X/Y; menu → Reseed. | Emergent CA field; reseed re-rolls it. |
| 4.4 | **Suppressed units absent** | Search the picker for Plenum, Moire, Vivary, Tessera, Ferrum, RotCoat, Filament, Carriage. | **None appear.** All still compile into their packages. |
| 4.5 | **NR** | Open the unit. | The circle control reads "Pattern". |
| 4.6 | **Canals** | Search the picker for "three sisters". | No result. Search "canals" / "formant" still finds it. |
| 4.7 | **house** (if shipping) | A/B kWoodRoom, WoodenBox, Verbity, Lacquer, TickerTape vs v2.7.0 build. | Tone-identical — hybrid-float claims 1 LSB, corr 1.0000000, but that is **offline-verified only**. |
| 4.8 | **house** (if shipping) | CPU on each converted reverb. | Should be materially cheaper; f64 op count dropped 75-81% on the converted units. |

## Known-broken / accepted going in

- **Ngoma** has three documented misses vs its reference: missing sub-partials,
  Character not reaching the fully-overfolded extreme, Shape's harmonic
  trajectory diverging. Parked deliberately (`ngoma-known-residuals`).
- **Constant Random** patches from v2.7.0 will load with the wrong slew time and
  no migration is possible — the old 0-1 range overlaps valid new second values.
  Needs a release-note callout.
- **Vitrail** shift-jump on routing is unverified (see 2.2).
- **Fade Mixer** snap end-zones are half-width; equal-width is a one-line change
  if it reads wrong.
