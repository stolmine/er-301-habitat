# Just Friends: habitat port exploration

Status: **scoping / design concept**. No code yet. This doc is the thinking aid for picking JF as the v1 proving unit for habitat's multi-output framework (`docs/multi-output-units-author-guide.md`). Extracted from `planning/stages.md` where the three-way comparison (Stages / Tides 2 / JF) lives.

## Source references

- **Technical map (canonical spec):** https://github.com/whimsicalraps/Mannequins-Technical-Maps/blob/main/just-friends/just-friends.md
- **Just-Type firmware (i2c / Teletype layer):** https://github.com/whimsicalraps/Just-Friends/blob/main/Just-Type.md
- **Firmware repo (binaries only, no C/DSP source):** https://github.com/whimsicalraps/Just-Friends
- **Synth Modes mode catalog:** https://synthmodes.com/modules/just_friends/index.html
- **Version / changelog:** https://www.whimsicalraps.com/pages/jf-latest-version

No open-source DSP. Port is **clean-room implementation from the technical map**. No MI-license entanglements.

## Why JF for habitat's v1 multi-output unit

Decision rationale from `planning/stages.md`:

1. **Cleanest framework proof with genuine coupling.** Static 6-output topology with coupling via INTONE ratio morph, shared shape params, phase-receptivity state (SHIFT), and round-robin in Geode.
2. **Exercises 1D sub-chain presence detection** — right-to-left trigger cascade. Simpler than Stages' 2D gang grouping but the same SDK primitive. Proves the presence-detection contract in a smaller, legible form.
3. **Lowest UI surface of the three candidates.** 5 globals + 2 mode switches + 6 per-voice trigger sub-chains. No per-voice control plane.
4. **Habitat DSP precedent already shipping.**
   - **Helicase** implements JF-style phase-receptivity sync for its FM modulator — direct reuse target for SHIFT mode.
   - **Helicase** also ships 2-op FM — informs FLOOM (v2 deferral).
   - **Varishape Osc** ships polyBLEP morph across sine/tri/saw/square/pulse — reusable for CURVE's audio-rate edges.
5. **Clean-room is less risky than it sounds.** The technical map is well-documented; JF's DSP is not algorithmically exotic (6 slope engines, ratio morph, CURVE piecewise shape, gate-driven phase receptivity).
6. **Derivability breaks unambiguously** via shared-engine state (INTONE morph, CURVE/RAMP/FM global shaping, SHIFT phase-receptivity, Geode round-robin). Cascaded triggers alone don't break derivability (single-trigger patches deliver identical edges to all 6 voices), but shared-param coupling does.

## Module overview

**Physical format:** 26 HP. 6 output jacks (top row), 6 trigger inputs (per-voice, normalled right-to-left), 4 global CV inputs (TIME V/oct, INTONE, FM, RUN), 1 global MIX output.

**6 voice outputs** (named IDENTITY / 1N, 2N, 3N, 4N, 5N, 6N). All share TIME, INTONE, RAMP, CURVE, FM globally. MIX output sums them (Sound = tanh-limited sum; Shape = analog-max of index-scaled voices).

**Two 3-position slide switches** (orthogonal), multiplying into 6 base cells:

- **Range switch:** Sound (audio-rate, ±5V bipolar, TZ-linear FM) / Shape (control-rate, 0–8V unipolar, DC-coupled FM).
- **Mode switch:** Transient (AR, triggers ignored mid-slope) / Sustain (ASR, gate-following) / Cycle (free-running, triggers = phase-reset).

Plus the **RUN** CV input that unlocks alt personalities per cell:

| Base cell | RUN-mode personality |
|---|---|
| Shape/Transient | **SHIFT** — retrigger-point sweep (phase-receptivity) |
| Shape/Sustain | **STRATA** — ARSR with sustain CV |
| Shape/Cycle | **VOLLEY** — burst envelopes |
| Sound/Transient | **SPILL** — impulse-train, IDENTITY clocks 2N–6N |
| Sound/Sustain | **PLUME** — lowpass-gate/vactrol plucks |
| Sound/Cycle | **FLOOM** — 2-op FM, RUN sets mod ratio |

## INTONE ratio morph (core coupling mechanism)

Continuous morph across the 6 voices, not discrete:

- **INTONE fully CW:** overtone series 1:2:3:4:5:6 (octave, octave+5th, 2oct, 2oct+M3, 2oct+5th).
- **INTONE at noon:** unison with slight detune spread either side (supersaw territory).
- **INTONE fully CCW:** undertone series (1N = 6:1, 6N = 1:1 — produces minor-triad voicings).

TIME sets IDENTITY's absolute rate; the other five track proportionally via INTONE. Continuous, simple exp-map to implement.

## Trigger normalization (right-to-left cascade)

Tech map verbatim:

> "Each trigger input is normalled into the trigger gate input to its left. This means that a trigger patched to an input will cascade to the other unpatched inputs to its left until it reaches a patched trigger input."

> "A trigger patched to _6N_ (while no other trigger inputs are patched) will excite all 6 inputs. With one trigger patched to _6N_ and another patched to _2N_, the _6N_ trigger will cascade to _5N_, _4N_, and _3N_. The _2N_ trigger will excite _2N_ and _IDENTITY_."

> "Inserting another trigger/gate source (or dummy cable) into a trigger input will break the corresponding normal."

**Habitat translation:** each voice N has a mono trigger sub-chain. At frame boundary, for each N from 6 down to 1, if voice N's sub-chain is empty, use the nearest patched sub-chain to its right as the signal source. This is 1D sub-chain presence detection — the framework's proving primitive in its simplest form.

## Per-mode trigger semantics

Tech map reserves "hard-sync" for the Cycle modes specifically. Port dispatch must honor the three-way mechanism split (phase-reset / AR-start-respecting-active-state / gate-follow):

| Cell | What a cascaded trigger does |
|---|---|
| Shape/Transient | Start AR from minimum; retriggers **ignored while active** |
| Shape/Sustain | Gate-sensitive: rise+sustain on high, fall on low |
| Shape/Cycle | **Hard-sync** (phase-reset a running slope) |
| Sound/Transient | Each rising edge excites slope (impulse-train clocking) |
| Sound/Sustain | Gate-sensitive clock, trapezoid width follows gate duration |
| Sound/Cycle | **Hard-sync** (phase-reset) |

## Phase-receptivity (SHIFT mode only)

Tech map verbatim: "This mechanism applies only to SHIFT mode. Standard modes lack this mid-cycle retrigger gating."

In SHIFT (Shape/Transient + RUN patched):

- RUN = **+5V** → equivalent to standard Shape/Transient (retrigger only at end-of-cycle).
- RUN = **−5V** → always receptive (classic hard-sync-ish).
- **Intermediate** → continuously sweeps the receptivity threshold mid-cycle.

**Habitat precedent:** Helicase's phase-receptivity sync between carrier and modulator is a direct reuse target. Same state-machine-per-voice shape.

## Output topology — LOCKED 2026-04-30

**7 sub-outs, MIX-first ordering:**

| Sub-out | Label | Signal |
|---|---|---|
| 1 (primary) | `mix` | tanh-limited sum of all 6 voices (in Sound range); analog-max of index-scaled voices in Shape range |
| 2 | `1N` | IDENTITY voice |
| 3 | `2N` | 2N voice |
| 4 | `3N` | 3N voice |
| 5 | `4N` | 4N voice |
| 6 | `5N` | 5N voice |
| 7 | `6N` | 6N voice |

**Why MIX as primary (sub-out 1).** Author-guide convention: sub-out 1 auto-wires on vanilla and is what a chain inserts into. MIX prioritizes the audio-usage default — drop the unit on a chain and you get a hex-voiced signal out of the box without per-voice routing. The hardware module's MIX is also a separate jack, so this matches hardware semantics.

**Why ordered MIX → 1N..6N (not 1N → 6N → MIX).** The ordering is what the M6 cycler walks. Putting MIX first means M6's first stop is the most likely consumer; per-voice taps are deeper in the cycle, matching their lower-frequency utility.

**Vanilla compatibility.** On vanilla firmware, only sub-out 1 (and sub-out 2 on stereo chains) is reachable from the picker. With MIX as sub-out 1, vanilla users get a fully-functional unit even without multi-out picker support. The Page 1 OUT crossfader fader is the *vanilla path* to per-voice access: set OUT to 1..6 to swap MIX for a specific voice on the primary outlet. CV-able, so the OUT fader is also useful on stolmine for animation effects (sweeping through voices at audio/control rate, scanning-style).

**OUT crossfader vs M6 cycler — both ship.** On stolmine, M6 in the local picker selects a sub-out for a specific consumer chain (per-consumer, static). The OUT fader sweeps the *primary outlet's* signal source dynamically (per-source, CV-able). They're not redundant — they serve different musical roles.

**Derivability:** fails unambiguously via shared-engine state. Cascaded triggers alone don't break derivability (all voices see identical edges on single-trigger patch). What does: INTONE ratio morph, CURVE/RAMP/FM global shaping, per-voice phase-receptivity state (SHIFT), Geode round-robin allocation counter.

## UI layout (v1) — LOCKED 2026-04-30

ER-301's "page" convention is a single horizontal ply row that the user scrolls through (not paginated cards). Conceptually grouped here as Page 1 (globals + audio output access) and Page 2 (gate sub-chains). All plies sit in the same `expanded` view.

**Habitat convention:** signal-level inputs (trigger, gate, clock, CV) live on main view, never hidden behind config menus.

### Page 1 — global macros + output access

Reuses the Plaits / Helicase synth-voice ply order convention (V/oct-style first, then global shapers), with a Canals-style output crossfader on the final ply.

| Ply | Control | Notes |
|---|---|---|
| 1 | **Mode** (T/S/C) | 3-position fader (LinearDialMap 0..2, rounded). Modeled on Canals `ModeSelector`. Alt name: TSC. |
| 2 | **Range** (Sound / Shape) | 2-position fader (LinearDialMap 0..1, rounded). Modeled on Canals `ModeSelector`. |
| 3 | **TIME** (V/Oct + offset) | `Pitch` ply, like Plaits/Helicase tune. |
| 4 | **INTONE** + CV | GainBias. Bipolar (±1) — overtone/unison/undertone morph. |
| 5 | **RAMP** + CV | GainBias. Bipolar per technical map. |
| 6 | **CURVE** + CV | GainBias. Bipolar (log↔lin↔exp↔sine↔rect morph). |
| 7 | **FM** + CV | GainBias. Bipolar. |
| 8 | **OUT** (output crossfader) | Canals-pattern `ModeSelector` 0..6 (LinearDialMap, 7-position rounded). Selects which signal goes to the **primary outlet**: 0 = MIX, 1 = IDENTITY (1N), 2 = 2N, ..., 6 = 6N. CV-able so the primary out can sweep across outputs at audio/control rate. |

**Note:** RUN ply is omitted in v1 (no alt-mode personalities ship). v2 adds RUN back as ply 8 ahead of OUT (becomes ply 9), pushing total to 9 + 6 gates = 15 plies.

Scroll depth (v1): 8 plies. Comparable to Plaits' 6 + 3 menu items.

### Page 2 — gate inputs per function generator (6 plies)

| Ply | Control |
|---|---|
| 9 | **gate 1N** (IDENTITY) |
| 10 | **gate 2N** |
| 11 | **gate 3N** |
| 12 | **gate 4N** |
| 13 | **gate 5N** |
| 14 | **gate 6N** |

Right-to-left cascade resolved Lua-side via the mask pattern from `docs/multi-output-units-author-guide.md` (sub-chain presence detection): for each cell N from 6 down to 1, if `branch:getInputSource(1) == nil`, that voice's effective trigger source is the nearest patched neighbor's. Mask is pushed to C++ as a parameter; C++ reads all 6 inlets and dispatches.

### Total ply count: 14 (v1) / 15 (v2 with RUN)

Single-row scroll. Matches existing precedent (Plaits 6, Helicase 7, Larets 8, Alembic 8). Long but legible — gate sub-chains are visually distinct (`Gate` ply graphic) from globals so muscle memory locates the gate bank quickly at the right edge.

### Config menu (header hold) — v2 only

RUN-mode personality toggles (when PLUME/FLOOM/SPILL/STRATA/VOLLEY ship), Just-Type / Geode static mode selection. v1 ships with no menu.

## MVP scope (v1) — TIGHTENED 2026-04-30

v1 ships the **6 default base cells only** — Range × Mode = Sound/Shape × Transient/Sustain/Cycle. **No alt-mode RUN personalities.** This is a clean unit on its own: hex-voiced harmonically-coupled slope engine with per-cell trigger dispatch, INTONE morph, shared CURVE/RAMP/FM shaping, MIX summing.

### v1 — in scope
- Sound + Shape base ranges (audio-rate vs control-rate slopes; bipolar ±5V vs unipolar 0–8V output domain).
- Transient / Sustain / Cycle mode switch (the three trigger-response semantics from the tech map).
- TIME (V/Oct), INTONE, RAMP, CURVE, FM globals (all with CV).
- 6 per-voice trigger sub-chains with right-to-left cascade.
- MIX output as primary sub-out (sub-out 1).
- OUT crossfader on Page 1 for vanilla-friendly per-voice access.

### v2 — alt RUN-mode personalities
RUN CV unlocks one of six alt personalities depending on the active base cell. **All deferred to v2.** Includes: SHIFT (Shape/Transient + RUN — phase-receptivity sweep), STRATA (Shape/Sustain + RUN — ARSR), VOLLEY (Shape/Cycle + RUN — burst envelopes), SPILL (Sound/Transient + RUN — impulse-train, IDENTITY clocks 2N–6N), PLUME (Sound/Sustain + RUN — vactrol/LPG plucks), FLOOM (Sound/Cycle + RUN — 2-op FM).

v1 omits the RUN ply from Page 1 entirely (was ply 8 in earlier drafts). RUN CV is non-functional in default-mode-only operation per tech map; an inert ply would be confusing. v2 adds back: RUN as ply 8, RUN-mode personality table dispatched off active cell.

### v3 — Just-Type (poly voice + Geode)
Just-Type is JF's i2c-driven poly voice and Geode round-robin allocator. Out of scope for v1 *and* v2. **Likely impossible** without external sequencing infrastructure habitat doesn't have. Rationale: Just-Type's contract is i2c teletype messages assigning voices to notes/gates programmatically — habitat has no i2c bus, and the hardware ER-301 firmware doesn't expose one either. Geode + poly voice could conceivably be a *standalone separate unit* that uses internal-trigger allocation (Geode-as-its-own-unit was already flagged as a v2 question), but this would be a clean-room reinterpretation, not a JF mode.

**i2c / Teletype integration:** out of scope permanently.

### Why this scope holds together
- 6 base cells × per-mode trigger dispatch is non-trivial and is the heart of JF's musical character. v1 nails this.
- INTONE morph + CURVE/RAMP/FM shaping + MIX summing are the core coupling that makes JF *not* decompose to parallel chains — they all ship in v1, so the multi-out justification is preserved.
- Alt-mode personalities (PLUME's vactrol model, FLOOM's 2-op FM, SPILL's impulse train) are each their own sub-DSP project. Deferring them de-risks v1 to a manageable scope while still proving the framework's flagship use.

## Port complexity / technical risks

1. **Clean-room from tech map.** Not binary-matching exact DSP — the habitat port will sound *like* JF (harmonically coupled slope engines), not identical sample-for-sample. Acceptable; document the difference.
2. **Trig LUT on am335x.** CURVE's log→lin→exp→sine→rect morph involves `sinf`; must go to the 72-entry LUT (reference: `mods/spreadsheet/FilterResponseGraphic.h` `kLutCos`/`kLutSin`). Emu-only validation will miss this.
3. **Through-zero linear FM in Sound range.** Tractable but needs care — habitat doesn't currently have TZ-linear FM elsewhere (Helicase's FM is lin/expo but not TZ-linear). Phase accumulator must allow negative-going phase delta when the FM signal swings below zero.
4. **6 slope engines running at once.** CPU budget concern at audio rate. Resolved by NEON 4-lane SIMD pattern below — see Voice topology / NEON.
5. **Right-to-left cascade at frame boundary.** Lua-side cascade computation, C++-side consumption. Cascade changes rarely (only on user patch/unpatch); no audio-rate overhead. Pattern verified per author guide.

(SHIFT phase-receptivity dropped — deferred to v2 with the rest of the alt-mode personalities.)

## Voice topology / NEON

Reuses tomf custom-units' polygon pattern (proven on hardware in `er-301-custom-units/mods/polygon/voice.h`). Polygon ships 4 / 8 / 12 voices via `MultiVoice<GROUPS>` where each group is a 4-lane NEON `four::Voice` processing 4 polyphonic instances simultaneously through `float32x4_t` lanes.

**JF mapping:**
- `MultiVoice<2>` → 2 groups × 4 lanes = 8 lanes total
- Use 6 lanes (1N…6N), mask 2 lanes off via gate=0 (envelope inert, oscillator output AND'd to zero)
- Wasted 2 lanes is acceptable — NEON throughput dwarfs the 25% lane-utilization cost vs scalar

**Why this pattern over our recent NEON forays:**
- Polygon's `four::Voice` composes proven NEON primitives (`dsp::four::Vpo`, `osc::four::DualPhaseReverseSync`, `env::four::SlewEnvelope`, `filter::svf::four::Lowpass`, `util::four::*`). All have shipped on Cortex-A8 since polygon's release without the `:64`-hint codegen issues that bit Ngoma + Pecto.
- Lane data structure is *struct-of-arrays* implicit — the `float32x4_t` *is* the per-voice fan-out, not a vectorization of a scalar pipeline. GCC has nothing to auto-vectorize and therefore nothing to mis-align.
- Class-member storage of NEON state matches `feedback_neon_intrinsics_drumvoice` lesson (heap-allocated, no stack-local NEON arrays).

**Vendoring decision:** polygon's DSP infrastructure lives in `er-301-custom-units/common/dsp/{osc.h, env.h, filter.h, pitch.h, latch.h, slew.h, ...}`. **Vendor the needed subset into the JF package**, don't reach across packages — per the cross-package dependency audit policy. Custom-units uses MIT license (verify before importing); files vendored in-tree become part of habitat going forward.

**Subset needed for JF v1 (estimated):**
- `dsp/osc.h` — phase accumulator, hard-sync, polyBLEP. Adapt for slope-engine semantics (rise+fall instead of saw/tri).
- `dsp/env.h` — `four::SlewEnvelope`, `four::Coefficients` for Transient/Sustain mode envelopes.
- `dsp/pitch.h` — `four::Vpo` for V/Oct + INTONE per-voice pitch deltas.
- `dsp/latch.h` — `four::GateToTrigger` for trigger edge detection.
- `util/math.h` (or subset) — NEON math helpers (`fast_exp_ns_f32`, `fclamp_unit`, `mix`).
- `hal/neon.h` — already in firmware SDK; do not vendor.

CURVE's piecewise morph is JF-specific — implement fresh as a 4-lane NEON shaper. polyBLEP for the rect endpoint.

## Resolved design decisions (2026-04-30)

All eight prior open questions resolved against the tech map and existing habitat code. Verbatim tech-map quotes follow each item.

### 1. TIME range and V/Oct mapping — RESOLVED

> "*shape*: minutes to milliseconds, from CCW to CW
> *sound*: milliseconds to microseconds (a.k.a. Hz to kHz), from CCW to CW
>
> The value of the *TIME* CV input is added to the *TIME* knob. The CV input is a highly accurate, exponential input: a 1V increase at the *TIME* input results in the speed (or frequency) of *IDENTITY* doubling. As such, the *TIME* CV input is labeled 'v/8,' short for '1V/8ve' or 'one-volt-per-octave.'
>
> The range of the *TIME* jack is roughly -2V to +5v. Voltages outside this range will be clamped at the limits."

**Implementation:**
- Two operating windows on TIME knob, gated by Range. Shape window: minutes (~60s = 0.0167 Hz) at full CCW to milliseconds (~1 ms = 1 kHz) at full CW. Sound window: ~20 Hz at full CCW to ~80 kHz at full CW.
- V/Oct CV: standard 1V/8ve exponential, **added** to knob position — i.e. CV scales the knob's selected center frequency by 2^V. Match `Pitch` ply with Helicase-style Vpo handling.
- Clamp CV to [-2V, +5V] per tech map. (At 1V/8ve this is 7 octaves of CV travel, plenty.)

### 2. CURVE morph — RESOLVED (continuous)

> "*CURVE* affects the shape of slopes as they rise and fall without affecting the durations of either stage. It takes the linear slopes determined by *TIME*, *INTONE* and *RAMP* and applies a lookup table to bend them into other shapes without affecting the timing. **Fine shapes are available with continuous blending between them.**"
>
> "When the *CURVE* knob is at noon, sweeping the *CURVE* CV input from -5V to +5V is equivalent to sweeping the knob from fully CCW to fully CW. *CURVE* CV is added to the *CURVE* knob."

**Implementation:** continuous blend, full CCW = rect → log → lin (noon) → exp → sine = full CW. Five anchor shapes; pixel-smooth morph between adjacent pairs. CV maps -5V → -1.0 knob equivalent, +5V → +1.0; bipolar GainBias.

### 3. MIX behavior across ranges — RESOLVED

> "***shape*** — *MIX* jack behavior: Each slope's output value is divided by its index. The *MIX* jack then outputs the largest of the resulting values (analog max or 'OR'). *IDENTITY* is divided by 1 (unaffected), *2N* is divided by 2, *3N* divided by 3, and so on. This provides a unique modulation source otherwise requiring a large patch.
>
> ***sound*** — *MIX* jack behavior: The *MIX* jack creates an equal mix of all the current slopes, limiting the final amplitude to ~15V peak-to-peak w/ tanh shaping."

**Implementation:**
- **Shape MIX:** `mix = max(v[1], v[2]/2, v[3]/3, v[4]/4, v[5]/5, v[6]/6)`. NEON: pairwise reduction over the index-scaled lanes.
- **Sound MIX:** `mix = tanhf(sum_of_voices)`, scaled so final amplitude caps near ±2.5 (15Vpp ≈ ±7.5V on hardware → habitat's normalized ±1 range with appropriate gain).

### 4. FM control — RESOLVED (bipolar two-destination)

> "Turning the FM knob clockwise increases the depth of the modulation linearly applied to all slopes equally.
>
> Turning the knob counterclockwise from noon increases the depth of the modulation applied to each slope generator in the *INTONE* style, i.e. in proportion to each channel's index (*IDENTITY* is unaffected, *2N* the least affected, *6N* the most affected).
>
> The FM knob thus controls whether the *FM INPUT* jack is linearly applied to the *INTONE* parameter (CCW) or linearly applied to the *TIME* parameter (CW), as well as the depth of the *FM INPUT* applied to either parameter."
>
> *sound* range: "The *FM INPUT* jack is AC-coupled (i.e. a DC-blocker is applied to the input) in order to achieve high-quality, linear, through-zero frequency modulation."

**Implementation:** FM ply is a single bipolar GainBias. CW-positive → linear FM to TIME (all voices equally, classic TZFM). CCW-negative → linear FM to INTONE (per-voice index-weighted, IDENTITY unaffected, 6N maximally affected). At noon, FM jack ignored.

**TZFM phase accumulator:** Helicase pattern, proven on hardware:
```cpp
phase += increment + fmAmount;  // fmAmount can be signed
phase -= floorf(phase);          // floorf handles negative wrap correctly
```
NEON 4-lane: polygon's `osc::four::DualPhaseReverseSync` already accepts signed delta (`vbslq_f32(reverse, -deltaA, deltaA)`) and uses `util::four::wrap`. Reuse directly — don't reinvent.

In Sound range, apply DC-blocker on the FM inlet buffer (one-pole HPF, ~5 Hz cutoff) per tech map. In Shape range, FM inlet is DC-coupled (no filter).

### 5. OUT crossfader interpolation — RESOLVED (config option)

Parametrize as a config menu option. Two modes:
- **Smooth** (default): cosine-taper crossfade between adjacent voices at intermediate OUT values. Best for CV sweeps and audio-rate animation.
- **Snap**: rounded-integer selection. Best for clean per-voice routing without bleed.

Set on insert, persisted, served from a `Unit:onShowMenu` config item.

### 6. Range-switch sample-rate implications — DEFERRED to profiling

DSP runs at 48 kHz always; Shape range clamps voice frequencies to LFO domain via the TIME map. Will measure CPU on hardware; expected fine given polygon's headroom but unconfirmed.

### 7. `dsp::four::*` vendoring — RESOLVED (port with attribution)

`er-301-custom-units` has no LICENSE file, but tomf has given verbal blessing to learn from his implementations. **Port the needed subset with attribution.** Add tomf to package README author byline:

```
Just Friends voice port — Bram Myers (habitat). 4-lane NEON DSP
infrastructure adapted from tomf's er-301-custom-units (polygon)
with permission. Original code copyright tomf.
```

Vendor target: `mods/<jf-pkg>/voice/{osc.h, env.h, pitch.h, latch.h, util.h}` (subset of `er-301-custom-units/common/{dsp,util}/`). Adapt namespaces to `mannequins::voice::four::` or similar to keep collision-free if other packages later port custom-units code.

### 8. CURVE 4-lane shaper — RESOLVED (LUT)

256-entry LUT × 5 anchor shapes (rect, log, lin, exp, sine) = 1280 entries × float = 5KB ROM. Per-sample: 4-lane index lookup (`vld1q_*` with gather, or scalar gather + `vsetq_*`) + linear interpolation between adjacent shapes by CURVE morph value. Cheap and predictable; matches `feedback_package_trig_lut` lesson for `sinf`/`cosf`.

(Alternative considered: analytic per-shape function — rejected on per-sample CPU vs the LUT memory cost being trivial.)

### Resolved earlier
- ~~Sub-out 7 for MIX or absorb into IDENTITY?~~ → MIX = sub-out 1 primary; 1N..6N = sub-outs 2..7. See Output topology.
- ~~RAMP CV bipolar handling.~~ → bipolar GainBias directly per author-guide control-polarity convention.
- ~~Trigger sub-chain cascade API ergonomics.~~ → mask-based dispatch per author guide; primitive verified (`branch:getInputSource(1)`).
- ~~Phase-receptivity state-machine scaling.~~ → SHIFT deferred to v2; v1 has no phase-receptivity logic.
- ~~Geode scope (v2 or later).~~ → v3 or never. Likely impossible without external sequencing infrastructure habitat doesn't have. If revisited, becomes its own unit (clean-room reinterpretation), not a JF mode.
- ~~SHIFT in v1 vs v2.~~ → v2 with the rest of the alt-mode personalities.

## Open UI questions

1. **Overview graphic.** What does the JF overview viz look like? Options:
   - 6 stacked voice waveforms (like Tomograph's radial overview, but linear)
   - Harmonic ratio visualization showing INTONE's current voicing
   - Contour preview per voice based on RAMP/CURVE
   - Something aggregating — maybe the MIX output's waveform with voice-activity markers
2. **RUN CV placement.** It's a single bipolar CV that affects different things in different cells (SHIFT threshold / STRATA sustain / VOLLEY burst / SPILL timing / PLUME vactrol / FLOOM ratio). Adaptive label per-cell like Stages' adaptive slider labels — but v1 only ships SHIFT, so v1 label is just "RUN" or "shift" without adaptive variation.

**Resolved 2026-04-30:**
- ~~Per-voice output labels.~~ → `{"mix", "1N", "2N", "3N", "4N", "5N", "6N"}` (≤6 chars cleanly). MIX-first matches output topology decision.

## Next steps

1. **Validate `subOutLabels` length 7** — extend QuadLFO reference to 7 outs as a half-day pre-flight; confirms picker overlay + M6 cycler render cleanly. Logged firmware-side in `er-301/docs/planning/just-friends-sdk-questions.md`.
2. **Pick package home.** JF voice + DSP both ship in a single package — no cross-package dependencies (general repo policy; see todo). Open: which package? Likely `spreadsheet` (tier-1, alongside Helicase/Ngoma/Pecto), or new `mannequins` if other Mannequins ports follow (Three Sisters, Cold Mac, Mangrove). Decide before code.
3. **License + vendor decision for `dsp::four::*`.** Confirm `er-301-custom-units` LICENSE permits vendoring `common/dsp/` subset; if so, copy the needed files into the JF package's tree (subdirectory like `mods/<pkg>/voice/`) to keep the cross-package boundary clean. No `#include "<otherrepo>/..."` paths.
4. **Review Varishape Osc's polyBLEP** for CURVE morph reusability across sine/tri/saw/square/pulse audio-rate edges.
5. **Draft the per-cell trigger dispatch table** — six base cells × Transient/Sustain/Cycle semantics. Code as a switch on (Range, Mode) inside the trigger-edge handler, dispatching to `startAR()` / `setGate()` / `phaseReset()` per cell.
6. **Profile plan:** `MultiVoice<2>` × 6 active lanes at 48 kHz on am335x. Confirm CPU budget before committing. Polygon's existing performance on hardware is the lower bound — its 8-voice config runs at audio-rate without issue, so 6 active lanes should be fine.
7. **Trig LUT sweep.** Audit any `sinf`/`cosf` in the slope-engine + CURVE morph + MIX combiner; route through `kLutSin`/`kLutCos` per author-guide trig-bug section. Polygon uses scalar libm in places — vendor copies will need this swap before hardware-shipping.
