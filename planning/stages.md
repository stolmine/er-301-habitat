# Stages: habitat port exploration

Status: **scoping / design concept**. No code yet. This doc captures the session's decisions so far, the open questions, and the reasons we may or may not pick Stages as the habitat multi-output framework's proving unit.

Source references:
- Stock DSP: `eurorack/stages/` (MI original).
- Alt firmware: Qiemem's fork, v1.1.0 (https://github.com/qiemem/eurorack/tree/v1.1.0/stages).
- Manual: https://pichenettes.github.io/mutable-instruments-documentation/modules/stages/manual/
- Multi-output framework: `docs/multi-output-units-author-guide.md`, charter at `planning/redesign/07-multi-output-units.md`.

## Why Stages is a strong candidate

Passes the derivability test unambiguously: in a ganged chain, adjacent channels share an envelope-segment cursor, loop boundaries, and phase. Reconstructing this downstream from a single output is not possible; the coupling is the point of the unit.

It would be habitat's first unit on the multi-output framework and the first direct encounter with sub-chain presence detection.

## Proposed scope (v1)

Port the **Qiemem Modes 1–3** family (Standard / Advanced Segment Generator / Slow LFO) as a single habitat unit, selectable via top-level `Mode` option. **Defer DAHDSR (Qiemem mode 4)** and **Ouroboros harmonic oscillators (modes 5–6)** — those are different enough UIs to justify separate units if demand justifies.

Target: 6 CV outputs, one per stage. No gate outputs (matches hardware — Stages has no gate outs; leftmost stage of a gang emits the aggregate envelope/sequencer/LFO, interior stages emit "segment activity signals" = ramps 8V→0V during that stage's active span).

## Output semantics

Per manual: in a gang, the **leftmost stage's output = aggregate contour** (stitched envelope/sequencer/LFO); **interior stages' outputs = segment activity ramps** (8V→0V during active span). This maps to the `Slave` mode already present in `segment_generator.cc:511-518` which emits `1 - phase` during the monitored segment's active span.

All 6 outputs are always "live" — the gang topology determines what each carries, not whether it emits.

## Gang detection via sub-chain presence

Hardware uses physical gate-jack normalization. Habitat equivalent: **empty gate sub-chain ⇔ unpatched**. Each stage owns a mono gate sub-chain on page 1. If the sub-chain has zero units and no external input, that stage is "unpatched" and absorbs into the gang to its left.

Polling rate: the stock chain_state state machine runs every 4 blocks (~80 Hz at 31.25 kHz). Lua-side frame callback polling at comparable rate is sufficient. Strategy:

- Lua polls each of the 6 gate branches, computes a 6-bit `chainMask`, passes to C++ DSP.
- C++ re-runs configuration when mask changes (analogous to stock `ChainState::Configure()`).
- All serial-link code (LeftToRightPacket / RightToLeftPacket) in `chain_state.cc` is discarded — single-unit port only.

## Per-stage UI layout

**3 slots per page hard cap.** Two pages per stage.

### Page 1 — structural (always present)
1. **Gate sub-chain** (mono branch). Acts as both trigger source and gang-boundary probe. Comparator under the hood, visible as a mono branch to the user.
2. **Segment type** — 4-way enum: `RAMP / HOLD / STEP / RANDOM`.
3. **Loop state** — 4-way enum: `off / single / loop-start / loop-end`. Replaces the hardware two-button chord for multi-stage loop declaration. Validation: only one `loop-start` and one `loop-end` per gang; both must live in the same gang.

### Page 2 — parameters (adaptive per type)
1. **Slider** (primary continuous, CV-addressable). Label/units adapt to `(type, gated, looping)`. **paramMode shift-toggle alt = polarity uni/bi** — polarity is sign-semantics of the slider, textbook Decision 8 bias-bound sub with visible highlight.
2. **Knob** (secondary continuous). Label adapts to `(type, gated, looping)`. **paramMode shift-toggle alt = retrig (RAMP only)** — retrig modifies how the knob's shape is re-engaged mid-cycle.
3. **Conditional slot** — composed on type change:
   - `quant` (4-way: unquant/chrom/maj/pent) when type ∈ {STEP, HOLD}
   - empty otherwise (RAMP, RANDOM)

This is a double-paramMode page — two shift-toggles sharing one page. **Not currently precedented; would become D9 extension to the paramMode convention memory** if adopted.

### Slider / knob semantic table (adaptive labels)

| Type | Gated | Looping | Slider [C] | Knob [A] | Notes |
|---|---|---|---|---|---|
| RAMP | — | no | duration | curve | slew limiter (Qiemem base behavior) |
| RAMP | yes | no | duration | curve | decay env (gate-triggered) |
| RAMP | — | yes | freq | waveform | free-running LFO |
| RAMP | yes | yes | freq | shape | clocked LFO (div/mult from gate) |
| HOLD | yes | no | level | duration | pulse generator |
| HOLD | no | no | level | delay | CV delay |
| HOLD | yes | yes | level | duration | gate gen (or probability in Adv) |
| HOLD | no | yes | offset | atten | Shades-style atten (Adv only) |
| STEP | yes | — | target | glide | latching S&H / sequencer |
| STEP | no | — | target | glide | continuous S&H with glide |
| RANDOM | no | no | freq (V/oct) | portamento | uniform random CV |
| RANDOM | no | yes | freq | b ∈ [1,6] | double-scroll chaos |
| RANDOM | yes | no | flip-prob | steps 1–16 | Turing machine |
| RANDOM | yes | yes | r ∈ [3.5,4] | portamento | logistic map |

## Features adopted from Qiemem

**Per-segment toggles:**
- Polarity (uni/bi) — applies to all types except non-looping RAMP (Qiemem rule). Surfaced as paramMode alt on slider.
- Retrig on/off — RAMP only. Surfaced as paramMode alt on knob. Off = Maths/DUSG function-gen behavior, clock dividers, subharmonics.
- Quantization scale — STEP and HOLD. Surfaced as conditional slot 3.

**4th segment type: RANDOM** — four sub-behaviors from (gated, looping): uniform / chaos / Turing / logistic.

**Transparent improvements (no UI surface):**
- Arbitrarily-slow clocked LFOs (no 5-sec reset timeout).
- Audio-rate clocked LFO detection with optimized algorithm.
- RAMP start-AND-end value tracking (vs. stock start-only) — enables pseudo-VCA, crossfade via STEPs.
- Bipolar STEP extended to 2 octaves of quantized values.

## Features dropped from Qiemem

- **RAMP frequency/time range 3-way select** (slow/med/fast). Decision: drop. Niche feature, never heard of a real user caring. One slot saved on page 2.

## Features deferred

- **DAHDSR mode** (Qiemem mode 4) — different UI shape (sliders as DAHDSR params, not per-segment), probably a separate habitat unit.
- **Ouroboros harmonic oscillator** (modes 5–6) — 6 audio-rate oscillators with harmonic ratios, root + 5 partials. Different unit class, probably separate.

## Technical risks

1. **am335x trig bug** — any `sinf`/`cosf` used in ramp shape warping or LFO generation must go through the 72-entry LUT (reference: `mods/spreadsheet/FilterResponseGraphic.h` `kLutCos`/`kLutSin`). Emu unaffected, so requires hardware validation before declaring done.
2. **Sample rate rescale** — stock is 31.25 kHz. Habitat runs at 48 kHz. All time/frequency LUTs (`lut_env_frequency` in `segment_generator.cc:135`, etc.) need rescale.
3. **Serial-link code deletion** — all LeftToRight/RightToLeft packet plumbing goes; replaced by Lua-side chainMask poll.
4. **Sub-chain presence detection** — how exactly to query a mono branch for "has any unit or input?" from Lua. Need to confirm the ER-301 SDK API for this (branch unit count? first unit nil check? onShow ticks?). This is the most novel part of the port.
5. **Audio-rate LFO CPU budget** — 5 clocked LFOs max recommended per Qiemem. Habitat's 6-stage layout could hit this ceiling; profile on hardware.
6. **Loop validation UX** — the page-1 loop enum's validation (single loop-start + loop-end per gang, both in same gang) has to fail gracefully if the user flips a chain topology that breaks the invariant. Probably auto-clear the invalidated endpoints with a visible transient indicator.

## Open design questions

1. **paramMode on two faders of one page** (slider+polarity AND knob+retrig for RAMP). No precedent in habitat. Does the D8 visible-highlight invariant hold with two simultaneous highlights? Propose: test as D9 extension candidate; if it reads badly in hardware, fall back to Option B (asymmetric 2-page / 3-page per type).
2. **Aggregate-contour viz** — the multiband spreadsheet tradition (Parfait/Impasto per-band FFT) suggests per-gang contour visualization in the overview graphic. Render envelope shape, sequencer step pattern, LFO waveform as appropriate per gang configuration. Open: how much overview real estate per gang? 6-way split is tight on 128x64 hardware screen.
3. **CV input routing per stage** — each stage's slider is CV-addressable. That's 6 CV inputs total. In habitat, each becomes a separate `GainBias` sub-control with its own sub-chain for CV. 6 sub-chains on page 2 + 6 gate sub-chains on page 1 = 12 sub-chains total. Confirm this is feasible without runaway layout complexity.
4. **Multi-stage loop endpoint commit** — when user sets `loop-end` after `loop-start`, and both live in the same gang, does the loop activate immediately or require a commit gesture? Probably immediate with visible bracket markers on the overview.

## Alternative candidates

User flagged that Just Friends or Tides 2 might be better first-target units for the multi-output framework. Research complete.

### Tides 2 summary

**Source:** `eurorack/tides2/` (MIT original).

- **4 CV outputs**, under framework's 9-cap.
- **Qualifies** as multi-out: state-coupling (phase offsets, ratio indices, amplitude distribution) cannot be reconstructed from a single output downstream.
- **Output modes** (user-selectable via `output_mode` in settings): GATES (slope + ramp + EOA + EOR), AMPLITUDE (4 gain levels of same waveform, switched by `shift`), SLOPE_PHASE (phase-shifted copies, step = shift/3), FREQUENCY (4 independent frequencies from 21-entry ratio tables). Shift parameter has **context-dependent meaning** per mode.
- **Ramp modes:** AD, AR, LOOPING.
- **Range switch:** control (LFO) vs audio.
- **Sample rate:** 62.5 kHz. DAC at half rate. Block 8 samples.
- **DSP:** `poly_slope_generator.h/cc` (core, 24 templated render functions for 3 ramp × 4 output × 2 range), `ramp_generator.h` (4-channel phase/freq), `ramp_shaper.h` (slope + BLEP + waveshape), `ramp_extractor.h` (beat-tracking for external clock).
- **Port complexity per agent:** ~40-50% harder than Stages at DSP level. Templated cross-product inflates compile surface; polyphonic state management is tight; `RampExtractor` is a sophisticated clock-tracker that'd need porting. **BUT** UI is a handful of globals (freq / shape / slope / smoothness / shift / fm / range / mode) — no per-output control plane. Net habitat scope probably **less than Stages overall** despite the harder DSP.
- **Framework fit:** static 4-output topology with no grouping/gang concept. Does not exercise sub-chain presence detection; simpler framework demo.

### Just Friends summary

Full port design lives in **`planning/just-friends.md`**. Condensed:

- 6 voice outputs + MIX (candidate sub-out 7). Clean-room port from the [technical map](https://github.com/whimsicalraps/Mannequins-Technical-Maps/blob/main/just-friends/just-friends.md); no open-source DSP.
- Two 3-position switches × RUN-mode alts = 6 base cells + 6 alt personalities (SHIFT / STRATA / VOLLEY / SPILL / PLUME / FLOOM).
- Coupling via INTONE ratio morph (overtone ↔ unison ↔ undertone), shared TIME/RAMP/CURVE/FM globals, per-voice phase-receptivity state (SHIFT), Geode round-robin.
- Per-voice trigger inputs with **right-to-left cascade** normalization. Habitat port exercises 1D sub-chain presence detection as the framework's proving primitive.
- Qualifies as multi-out via shared-engine state; cascaded triggers alone don't break derivability.
- Habitat DSP precedents: Helicase (phase-receptivity sync, 2-op FM → FLOOM), Varishape Osc (polyBLEP → CURVE morph).
- MVP scope: Sound+Shape base ranges × Transient/Sustain/Cycle × globals + triggers + SHIFT. Defer PLUME/FLOOM/SPILL/STRATA/VOLLEY/Geode/i2c.

### Three-way comparison

| Axis | Stages (Qiemem M1-3) | Tides 2 | Just Friends |
|---|---|---|---|
| Outputs | 6 | 4 | 6 (+ mix?) |
| Qualifies as multi-out? | ✓ | ✓ | ✓ |
| DSP source availability | Stock + fork, open | Stock open | **None** (binaries + tech map) |
| UI control-plane size | 2 pages × 3 slots × 6 stages + gate chains | ~8 globals + mode | 5 globals + 2 mode switches + 6 triggers |
| Sub-chain presence detection | **Required** (2D gang detect) | No | **Required** (1D trigger cascade) |
| Sample rate rescale | 31.25k → 48k | 62.5k → 48k (w/ DAC half-rate nuance) | N/A (clean room) |
| Trig LUT work | Yes (shape warping, LFO) | Yes (polyBLEP, waveshape) | Yes (sine in CURVE morph, slope shaping) |
| Habitat DSP precedent | Nothing close | Petrichor pitch, Varishape polyBLEP | Helicase FM + phase-receptivity sync |
| Musical breadth | Very high (env/seq/LFO/S&H/Turing/chaos/delay) | Moderate (poly LFO/AD/AR/audio osc) | Very high (drone/env/Geode/PLUME/FLOOM) |
| Framework stress value | **Highest** — proves sub-chain presence detection | Lowest — static topology | Mid — static but genuine coupling |
| Overall port complexity | High (DSP + novel SDK) | Mid-high (DSP heavy, UI light) | Mid (clean-room, UI simple, reuses habitat precedent) |

### Recommendation: JF as v1 multi-output unit, Stages as v2

**JF first.** Rationale:

1. **Cleanest framework proof with genuine coupling.** Static 6-output topology with coupling via INTONE ratio morph, shared shape params, phase-receptivity state (SHIFT), and round-robin in Geode. **Also exercises 1D sub-chain presence detection** for the right-to-left trigger cascade — simpler flavor than Stages' 2D gang grouping but the same SDK primitive. Publishes the framework to habitat users with a small, legible first unit that still proves the presence-detection contract.
2. **Lowest UI surface.** 5 globals + 2 mode switches + 6 per-voice trigger sub-chains. No per-voice control plane. Vastly less Lua than Stages' 2×3×6 matrix.
3. **Habitat DSP precedent in hand.** Helicase already ships JF-style phase-receptivity sync and 2-op FM; that code can inform FLOOM and the base slope engine's re-trigger logic. Varishape Osc's polyBLEP is reusable for the CURVE morph's audio-rate edges.
4. **Clean-room is less risky than it sounds.** Technical map is well-documented; JF's DSP is not algorithmically exotic (6 slope engines, ratio morph, CURVE piecewise, gate-driven phase receptivity). No MI-license entanglements since nothing is copied.
5. **MVP scope is tight:** Sound+Shape + Transient/Sustain/Cycle + INTONE + CURVE + RAMP + FM + per-voice triggers. Defer PLUME/FLOOM/SPILL/STRATA/VOLLEY/SHIFT (RUN-mode alts) and Geode for v2.
6. **Derivability test lands unambiguously** — via shared-engine state (INTONE morph, CURVE/RAMP/FM global shaping, SHIFT phase-receptivity, Geode round-robin). Cascaded triggers alone don't break derivability (single-trigger patches deliver identical edges to all 6 voices), but shared-param coupling does.

**Stages as v2.** The sub-chain presence detection work is genuinely valuable, but it should land after the framework has one shipping exemplar. v2 justification:
- Tests dynamic sub-out grouping (a framework capability the charter anticipates but hasn't exercised).
- Opens the door to other "normalized-jack" ports (Maths, Function, any module with gate-normalization semantics).
- By then, the framework's author guide and M6 cycling UX will be battle-tested.

**Tides 2 as fallback if JF's clean-room scope feels risky.** Tides has open DSP, simpler UI than Stages, 4 outputs. But framework stress value is lowest (static topology, no interesting grouping story), and the DSP is more work than it looks at first (24 templated renders, RampExtractor, 62.5 kHz → 48 kHz rescale with DAC-half-rate nuance). Recommend only if JF's spec-only situation becomes a blocker.

## Next steps

1. **Decision:** pick v1 target (recommended: JF).
2. If JF: create `planning/just-friends.md` mirroring this doc's structure. Enumerate MVP modes (Sound+Shape × Transient/Sustain/Cycle), control layout, per-voice gate sub-chains, C++ shell around 6-instance slope engine with INTONE/RAMP/CURVE/FM globals. Probe ER-301 SDK for multi-output sub-out labeling.
3. If Stages: probe ER-301 SDK for sub-chain presence detection API (`branch:getUnitCount()`? inlet-connectivity query?), confirm 12 sub-chain layout is viable, draft the C++ DSP shell around stock `segment_generator.cc` with Qiemem's random/polarity/retrig/quant additions.
4. If Tides: draft `planning/tides.md` and port starting from the `poly_slope_generator` templates, reducing the render-function matrix to the habitat-relevant subset.
5. This doc stays as the reference for the second multi-output unit (Stages), whenever that lands.
