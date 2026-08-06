# er-301-habitat v2.6.1

Release date: 2026-06-22

Package updates: **biome 2.2.0 -> 2.2.1**, **spreadsheet 2.7.1 -> 2.8.1**. catchall, kryos, mi, peaks, porcelain, scope unchanged.

This is the deferred follow-up to v2.5.1 — the prior v2.6.0 release was cut and retracted the same day; v2.6.1 supersedes it with a more developed Canals in spreadsheet (the 4-input + normalling topology landed after the retraction) plus the Canals migration completed cleanly between packages.

## Highlights

Two pieces of headline work plus a pair of safety / robustness fixes:

- **Mirror** — entirely new unit in spreadsheet. A complex oscillator built around the deliberate use of aliasing as the synthesis paradigm. Wavetable formant engine, mod-driven phase threshold sync, four-stage destructive aliasing crusher, true stereo via sync-threshold-derived phase offset, custom Y-axis-reflected phase-space phosphor viz on the overview ply. 6 plies, 8 outlets for self-patching.
- **Canals** — substantial DSP refresh + **moved from biome to spreadsheet** (removed from biome). The unit that shipped in biome 2.2.0 was an earlier crossover-filter model; the new spreadsheet version is a full rebuild against an external ZDF SVF reference + hardware capture corpus, with audio-rate modulation, 2× oversampling, multi-output picker, and a new 4-input + normalling topology that mirrors the hardware's ALL + per-block input arrangement. Existing biome.Canals patches will need to be re-pointed at spreadsheet.Canals.
- **Parfait** — safety-clamp fix. The discontinuous if-then-tanh limiter at the post-bandsum stage was generating spectral splatter on every threshold crossing (~0.36-magnitude value-discontinuity); replaced with smooth C∞ pseudo-saturate that asymptotes to the same ±1.5 rail. ~90 dB cleaner above 5 kHz in measurement.
- **Filterbank** — user-reported "fails to load" bug fix. A single malformed `.scl` file in the front-SD `/scales` directory was throwing inside the Scala parser and killing the whole unit's construction. Parser call now `pcall`-guarded; bad files are silently skipped, all other valid files load normally.

## Mirror (new unit, spreadsheet)

Aliasing as a deliberate synthesis paradigm — the inverse of every other synthesis approach. Subtractive filters above-Nyquist content out; additive specifies in-band partials directly; FM modulates carriers to produce bandlimited sidebands. Mirror generates above-Nyquist content and then explicitly controls how it folds back into the audible band.

### Signal chain

```
V/Oct -> Pitch -> mod osc (perceived pitch)
                    │
                    └─> Sync edge on mod-phase wrap
                        retriggers the wavetable envelope at the rate
                        set by Formant × Sync-Threshold-derived ratio

Shape knob scans through a 16-frame wavetable (square gate / saw / triangle
/ exp decay / half-sine bell / Gaussian / pluck / anti-pluck / two-peak
lobed / three-peak / damped sine / inverse exp / full sine / damped square
/ sinc / triple-impulse). The envelope IS the audio.

The signal then passes through the four-stage Mirror crusher:
  1. Pre-saturation (tanh-driven harmonics)
  2. Divider-clocked S&H (1..64x undersampling)
  3. Bit-depth quantization (16-bit -> 2-bit)
  4. Reconstruction blend (ZOH below 0.85 knob, Nyquist polarity flip ramps in above)

Two carrier pipelines run in parallel (L/R). The envelope-phase offset
between them is derived from the Sync Threshold knob position so stereo
width IS the chaos axis: lock zones produce L = R (mono center); chaos
midpoints produce maximum stereo width.
```

### Controls (6 plies)

| Ply | Primary control | Shift sub |
|---|---|---|
| **Pitch** | V/Oct view control | — |
| **Shape (overview)** | Wavetable position 0..1 (frames 0..15) | Freq / Form / Fbck readouts |
| **Mod Depth** | Audio-rate FM on envelope rate (±3 octaves) | — |
| **Sync Threshold** | Cubic-around-Fibonacci-locks {1, 2, 3, 5, 8, 13} | — |
| **Mirror** | 4-stage destructive crusher driven from one knob | Mirror Reset switch |
| **Level** | Output gain | — |

Press Enter on the Shape ply to enter the expansion view: full GainBias faders for Fundamental, Formant, and Feedback as their own plies. The shift-sub overview gives quick simultaneous edits; the expansion gives sustained sound-design surface.

### Outlets (8)

| # | Name | What it is | Useful for |
|---|---|---|---|
| 1 | Out | Main output (L) | Audio |
| 2 | Out R | Main output (R) — independent pipeline | Audio (stereo chain) |
| 3 | Clean | Pre-Mirror wavetable envelope (L) | Patch reference; bandlimited carrier |
| 4 | Drive | Post-pre-sat, pre-S&H (L) | Tanh-driven envelope without crushing |
| 5 | Held | Post-quantize stair-step (L) | Discrete bit-crush character |
| 6 | Fold | Alias residual: Mirror output minus Clean (L) | Self-patch for cascading inharmonic feedback |
| 7 | Sync | Gate on internal sync edges | Clock source for downstream sequencers/envelopes |
| 8 | Mod | Raw internal modulator sine | Sync'd modulation source for other units |

The unit ships its modulation matrix through the patch bay rather than via internal routing — multi-out → CV-input is more flexible than any built-in matrix could be.

### Visual

The Shape ply's main graphic is a 96 × 64 phosphor phase-space scope of the L output, reflected across the Y axis so the image is always left-right symmetric about the vertical centerline. Phase-space delay is tied inversely to the Mirror knob: clean settings get a long delay (16 ring-buffer samples) so smooth wavetable content produces a 2D phase portrait; crushed settings get a tight delay (1) because the crusher's own discontinuities already provide decorrelation. Overshoot folds reflectively back into the visible field rather than clamping at the edges — the same paradigm-coherent mechanic the audio Mirror block uses.

## Canals (DSP refresh + new ownership + 4-input topology, spreadsheet)

The Canals that shipped in biome 2.2.0 was an earlier crossover-filter model living in the wrong package. v2.6.1 retires biome.Canals and ships a full rebuild as spreadsheet.Canals.

### What changed (vs the biome version)

- **Hardware-matched self-oscillation amplitudes** across all three blocks. LOW / CTR / HIGH peaks within 2% of measured hardware (sim 0.67 / 1.13 / 0.65 vs hw 0.68 / 1.12 / 0.65). Achieved through frequency-compensated damping (`damp × ctr_f / f_stage`) and per-block post-gain compensation.
- **2× oversampling** on the inner SVF loop. Audio-rate modulation of cutoff and span now stays clean past the previous "gurgle" threshold; FM and audio-rate sweeps sound coherent throughout the perceptual range.
- **Per-sample V/Oct read** for audio-rate FM character — was block-rate before.
- **Audio-tuned in-loop saturator**: `x / (1 + (|x|/2.5)^4)^(1/4)` pseudo-saturate, p=4. CENTRE block 3rd harmonic measures at -33 dB matching hardware's -31 dB (was -22 dB with the prior Padé tanh approach).
- **CENTRE topology fix**: SVF2 is Butterworth (not resonant). Hardware spectral analysis showed a single peak at lowF, not the dual peaks the original reference described.
- **Anti-resonance topology**: complementary-output tap mix gives proper bipolar Q character. LOW=SVF1.hp, CTR=SVF1.lp + SVF2.hp, HIGH=SVF1.lp.
- **Multi-output picker** (sub-outs Out / Out R / LOW / CENTRE / HIGH). Per-block taps for surgical extraction.
- **Self-oscillation kickstart** from denormal flush at 1.18e-17 — avoids the silent-state trap where the integrator state never gets perturbed.

### 4-input + normalling topology (new in this release)

The reference hardware has separate ALL / LOW / CENTRE / HIGH input jacks with normalling: patching into one of the per-block jacks overrides the ALL feed for that specific block. The new Canals topology mirrors this:

- Main `In1` carries the ALL signal that feeds all three filter blocks by default.
- Three dedicated audio-rate inlets — `Low In`, `Centre In`, `High In` — act as per-block normalling jacks.
- Patching a signal into one of them (either via M+ply subchain assign OR via the chain input picker) overrides ALL for that block. CTR and HIGH still receive ALL if their per-block inlets are unpatched.
- A new menu option **ALL Input** can be set to `disabled` to mute the ALL feed entirely — unpatched blocks then go silent rather than tracking the main input.

### Overview ply

A new top-of-expansion **overview ply** (button `in`) consolidates all three per-block input branches under one sub-display. The main graphic shows the live routing state:

- Three horizontal stripes (LOW at bottom, CENTRE middle, HIGH at top) each running a MiniScope-style min/max scope of the post-routing input feeding that block.
- "ALL" overlay in a small 3×5 pixel font appears on stripes where the block is using ALL fallback. The overlay text uses per-pixel inversion against the scope intensity for legibility regardless of waveform density.
- Band signifier (`L` / `C` / `H`) overlaid in the upper-right corner of each stripe, same per-pixel inversion.

Each sub-button on the overview sub-display dives into the corresponding per-block subchain — same mechanic as pressing S1 on a regular branch ply, but consolidated so the three per-block inlets occupy one ply slot instead of three.

The routing state is auto-detected by polling each branch's chain state at the 55 Hz display-frame signal (no manual menu toggling needed). Patched detection covers both "units assigned inside the subchain" AND "external source subscribed to the input via the chain picker."

### Existing biome.Canals patches

Patches saved with biome.Canals will fail to load on this release (biome.Canals is removed). Re-point them at spreadsheet.Canals. The unit's audio behavior is the same character — the redesign was DSP fidelity + control surface, not a re-design of how it sounds at default settings.

### Known open items (carry-forward)

- SPAN curve profiling against hardware (capture battery designed at `planning/canals-span-volume-capture-checklist.md`). Audition feedback indicates the SPAN feel measurably differs from hardware; analysis pending.
- Volume modulation across cutoff sweep + low-band retention at high Q — both have capture batteries designed; analysis pending.
- Some residual FM gurgle at very high modulation rates. 2× OS handles most of it; sharper decimator or 4× OS would address residual.

None of these block the release.

## Parfait (safety clamp fix, spreadsheet)

The post-bandsum safety limiter at `MultibandSaturator.cpp:749-751` was:

```cpp
if (wet > 1.5f || wet < -1.5f)
  wet = fast_tanh(wet * 0.67f) * 1.5f;
```

Looks innocuous, but it's discontinuous in value — not just derivative:

| `wet` | branch | output |
|---|---|---|
| 1.5 (just below threshold) | linear | **1.500** |
| 1.5 + ε (just above) | tanh | `tanh(1.005) × 1.5` ≈ **1.142** |

Every threshold crossing emitted a ~0.36-magnitude step. That's a wideband splatter that aliases on each crossing — exactly the "extra distortion as the Mix knob drops" character users reported. (At Mix = 1 the heavily-saturated wet masked the artifact; lowering Mix unmasked it against the dry.)

Fix: replace with `pseudoSaturate15(wet) = wet / sqrt(sqrt(1 + (|wet|/1.5)^4))`. Same ±1.5 asymptote, C∞ everywhere, slope = 1 at origin (transparent at small signals), no derivative spike anywhere.

Measured impact (200 Hz sine driven to amplitude 2.5):
- H99 (19.8 kHz): -50.7 dB → -180 dB
- H51 (10.2 kHz): -57.9 dB → -180 dB
- H25 (5 kHz): -39.9 dB → -116.6 dB
- Mean energy above 5 kHz: ~90 dB cleaner

That's the entire alias-source spectrum gone.

## Filterbank (parse fix, spreadsheet)

User reported `Failed to construct unit: Filterbank` on stolmine 9.5.1 with spreadsheet 2.7.1. Stack walked back to `core.Quantizer.Scala.load` at `Scala.lua:65`:

```
attempt to perform arithmetic on a nil value
```

The line:

```lua
table.insert(tunings, toCents * math.log(tonumber(ratio_parts[1]) / tonumber(ratio_parts[2])))
```

`Utils.split(line, '/')` was returning fewer than 2 parts (or non-numeric parts) for a particular malformed `.scl` file in the user's front-SD `/scales` directory. `tonumber()` returned nil; arithmetic on nil raised; raise propagated all the way up through `Filterbank:loadUserScales -> onLoadGraph -> init -> Factory.instantiate` and the unit failed to construct.

Fix: `pcall` the per-file `Scala.load` call so a single bad file gets skipped silently. All other valid files continue to load into their fader slots.

## Upgrade notes

- Compatible with the firmware that v2.5.1 targeted (`0.7.0-stolmine.9.5.x`). No firmware update required.
- **biome.Canals patches require migration to spreadsheet.Canals**. The biome version is removed; the spreadsheet version is the canonical linked-filter model going forward and is the more capable unit.
- Two packages have new versions this cycle: **biome (2.2.1)** removes Canals; **spreadsheet (2.8.1)** ships Mirror + Canals + the safety/parse fixes. catchall, mi, peaks, scope unchanged.
- The user-facing API of every other existing unit is unchanged. No quicksave compatibility concerns outside of biome.Canals.

## Carried-forward known issues

Same as v2.5.0/v2.5.1 — see the v2.5.0 release notes. Nothing new added; nothing resolved that wasn't already.

Kryos (spectral freeze) still WIP, not attached. Porcelain still at 0.1.0, not attached.

## Internals / dev notes

- House package (in-development AW-port reverb work) continues to advance on the dev branch but is not yet released — not attached to this build.
- The Mirror unit's name is its working codename per the design doc. The unit may be renamed in a future release before final promotion.
- For full technical detail on the Mirror architecture: see `planning/mirror-unit-design.md`, `planning/mirror-block-aw-refactor-plan.md`, `planning/mirror-formant-wavetable-drift-plan.md`, `planning/mirror-feedback-plan.md`, `planning/mirror-phosphor-viz-plan.md`, `planning/mirror-phosphor-knob-tied-delay.md`, `planning/mirror-research-notes.md`.
- For the Canals 4-input + normalling topology: see `planning/canals-4-input-normalling.md`.
- For Canals DSP redesign: see `planning/canals-spreadsheet-redesign.md`, `planning/canals-audio-rate-mod.md`, `planning/canals-internal-external-calibration.md`.
