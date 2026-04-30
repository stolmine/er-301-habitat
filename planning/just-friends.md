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
| 8 | **RUN** + CV | GainBias. Bipolar (±5V on hardware). Drives SHIFT phase-receptivity sweep. |
| 9 | **OUT** (output crossfader) | Canals-pattern `ModeSelector` 0..6 (LinearDialMap, 7-position rounded). Selects which signal goes to the **primary outlet**: 0 = MIX, 1 = IDENTITY (1N), 2 = 2N, ..., 6 = 6N. CV-able so the primary out can sweep across outputs at audio/control rate. |

Scroll depth: 9 plies. Comparable to Plaits' 6 + 3 menu items, no worse than Helicase.

### Page 2 — gate inputs per function generator (6 plies)

| Ply | Control |
|---|---|
| 10 | **gate 1N** (IDENTITY) |
| 11 | **gate 2N** |
| 12 | **gate 3N** |
| 13 | **gate 4N** |
| 14 | **gate 5N** |
| 15 | **gate 6N** |

Right-to-left cascade resolved Lua-side via the mask pattern from `docs/multi-output-units-author-guide.md` (sub-chain presence detection): for each cell N from 6 down to 1, if `branch:getInputSource(1) == nil`, that voice's effective trigger source is the nearest patched neighbor's. Mask is pushed to C++ as a parameter; C++ reads all 6 inlets and dispatches.

### Total ply count: 15

Single-row scroll. Matches existing precedent (Plaits 6, Helicase 7, Larets 8, Alembic 8). Long but legible — gate sub-chains are visually distinct (`Gate` ply graphic) from globals so muscle memory locates the gate bank quickly at the right edge.

### Config menu (header hold) — v2 only

RUN-mode personality toggles (when PLUME/FLOOM/SPILL/STRATA/VOLLEY ship), Just-Type / Geode static mode selection. v1 ships with no menu.

## MVP scope (v1)

**In scope:**
- Sound + Shape base ranges
- Transient / Sustain / Cycle mode switch
- TIME (V/oct), INTONE, RAMP, CURVE, FM globals (all with CV)
- 6 per-voice trigger sub-chains with right-to-left cascade
- Phase-receptivity via RUN CV in SHIFT cell (reuses Helicase logic)
- MIX output (sub-out 7 TBD vs. IDENTITY aliasing)

**Explicitly deferred to v2:**
- **PLUME** (vactrol/LPG model, Sound/Sustain + RUN) — non-trivial DSP
- **FLOOM** (2-op FM, Sound/Cycle + RUN) — reuses Helicase but needs re-wiring
- **SPILL** (impulse-train, Sound/Transient + RUN)
- **STRATA** (ARSR, Shape/Sustain + RUN)
- **VOLLEY** (burst envelopes, Shape/Cycle + RUN)
- **Geode** (Just-Type round-robin voice allocator)
- **i2c / Teletype integration** — habitat has no i2c at all; out of scope permanently

## Port complexity / technical risks

1. **Clean-room from tech map.** Not binary-matching exact DSP — the habitat port will sound *like* JF (harmonically coupled slope engines), not identical sample-for-sample. Acceptable; document the difference.
2. **Trig LUT on am335x.** CURVE's log→lin→exp→sine→rect morph likely involves `sinf`; must go to the 72-entry LUT (reference: `mods/spreadsheet/FilterResponseGraphic.h` `kLutCos`/`kLutSin`). Emu-only validation will miss this.
3. **Through-zero linear FM in Sound range.** Tractable but needs care — habitat doesn't currently have TZ-linear FM elsewhere (Helicase's FM is lin/expo but not TZ-linear).
4. **6 slope engines running at once.** CPU budget concern at audio rate. Profile on hardware early. May need vectorization.
5. **Sub-chain presence detection API.** First time habitat exercises this. Confirm the ER-301 SDK primitive: `branch:getUnitCount()`? `branch:isEmpty()`? `inlet:isConnected()`? Needs probing before any code.
6. **Phase-receptivity state-machine per voice.** 6 instances of Helicase's existing logic. Verify it scales (particularly the transition ordering when INTONE remaps frequencies).
7. **Right-to-left cascade at frame boundary.** Lua-side cascade computation, C++-side consumption. Cascade changes rarely (only when user patches/unpatches), so no audio-rate overhead.

## Open design questions

1. **Range-switch sample-rate implications.** Sound range runs voices at audio-rate; Shape at control-rate. Internally the habitat DSP must probably run at 48 kHz always, with Shape-range voices simply clamped to control-rate frequencies. Confirm CPU.
2. **TZ-linear FM implementation.** Through-zero linear FM requires specific phase-accumulator handling (bipolar FM signal crosses zero → phase can run backward). Design the slope engine's phase accumulator with this in mind from the start, even for v1 clamp-to-Sound.
3. **Geode scope (v2 or later).** Geode fundamentally changes the unit's use model (polyrhythmic allocator). Might belong in its own unit rather than a mode of the main JF unit. Decide when v2 scoping arrives.

**Resolved 2026-04-30:**
- ~~Sub-out 7 for MIX or absorb into IDENTITY?~~ → MIX = sub-out 1 primary; 1N..6N = sub-outs 2..7. See Output topology.
- ~~RAMP CV bipolar handling.~~ → bipolar GainBias directly per author-guide control-polarity convention.
- ~~Trigger sub-chain cascade API ergonomics.~~ → mask-based dispatch per author guide; primitive verified (`branch:getInputSource(1)`).

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
3. **Review Helicase's phase-receptivity code** as the direct reuse basis for SHIFT mode (per-voice state machine identical shape).
4. **Review Varishape Osc's polyBLEP** for CURVE morph reusability across sine/tri/saw/square/pulse audio-rate edges.
5. **Draft the slope-engine struct** — per-voice state object carrying phase, cycle flag, receptivity flag, plus shared-param pointers. 6 instances managed in DSP class.
6. **Profile plan:** 6 slope engines at 48 kHz on am335x — confirm CPU budget before committing. NEON-vectorize across the 6 voices' phase-accumulator + polyBLEP if needed; reuse Ngoma's 4-lane phasor pattern as starting template.
7. **Trig LUT sweep.** Audit any `sinf`/`cosf` in the slope-engine + CURVE morph + analog-max MIX combiner; route through `kLutSin`/`kLutCos` per author-guide trig-bug section.
