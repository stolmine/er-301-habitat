# Rings non-modal NEON pass + modal tighten

**Plan persisted from `~/.claude/plans/let-s-go-detune-post-cheeky-tome.md`
2026-05-15, per `feedback_persist_plans_to_repo`. Source of truth
for the implementation.**

## Context

After Filterbank shipped (2.6.2.42), `planning/neon-opportunities.md`
had Rings non-modal modes at #1 in recommended-next-moves. User chose
comprehensive scope (both alt modes + modal revisit). Plan-agent
analysis revealed harder structural realities than the audit doc
implied — most importantly that **`SineFm` is a 4096-entry LUT
lookup** in `eurorack/rings/dsp/fm_voice.h`, not a polynomial, and
Cortex-A8 NEON has **no gather load**. That makes cross-voice FM
batching marginal or negative at typical polyphony counts.

Honest scope:
- **Modal tighten** — solid 10–15% kernel speedup, low risk → SHIP.
- **FM** — not NEON-viable on Cortex-A8 without polynomial-sine
  substitution (tone-audition decision, separate work) → CLOSE OUT.
- **String** — best-case ~25% mode CPU at poly=1 sympathetic
  (8 strings); worst case ~10% at poly=4 → SPIKE-GATED REFACTOR.

## Phases

### Phase 1 — Modal kernel tighten

File: `mods/mi/rings/dsp/resonator.cc` (208 lines, already partially
NEON'd). The current inner loop (lines 107–157) drains the NEON
pipeline every quad via `vst1q_f32(bp_vals, v_bp)` + 4 scalar reads
+ 4 scalar `mla` for the odd/even accumulation. On Cortex-A8 each
NEON→memory→scalar transition is a real pipeline crossing cost.

Changes:

1. **Add `amp_scratch_[4]` class member** to `Resonator` (in
   `mods/mi/rings/dsp/resonator.h`). Class-member ⇒ heap-allocated
   ⇒ NEON-safe per `feedback_neon_intrinsics_drumvoice` (no `:64`
   trap surface for a stack-local).

2. **Inner loop rewrite**: keep `odd`/`even` accumulators in NEON
   quads through the entire per-sample inner loop. Per-iter, write
   amplitudes into the class-member scratch, load as a quad, apply
   via `vmlaq_f32` into accumulator quads with lane masks for odd
   (lanes 0, 2) vs even (lanes 1, 3). Horizontal-sum to scalar
   `odd`/`even` ONCE per per-sample iteration via the `vadd_f32`
   + `vpadd_f32` cascade.

3. **Padding instead of scalar tail**: drop `num_modes_simd =
   num_modes & ~3` round-down + scalar tail. Switch to Filterbank's
   pattern — pad to next multiple of 4, set `g_=r_=h_=0` for
   padding modes in `ComputeFilters()`. SVF math naturally
   produces zero contribution (g=0 freezes state at 0, no NaN).

Expected: ~10–15 % modal kernel speedup. Modest absolute win on top
of the already-shipped ~3–4×, but unifies the kernel idiom with the
Filterbank precedent and the planned Phase 3 pattern.

### Phase 2 — FM audit close-out (documentation only)

No code. Update `planning/neon-opportunities.md` FM entry: from
"candidate" to "deferred — Cortex-A8 NEON no-gather constraint;
revisit only if polynomial-sine substitution is independently
accepted."

Optional new memory `feedback_neon_no_gather_lut_dsp.md` for
permanent cross-reference: "Cortex-A8 NEON has no gather load. Any
LUT-based DSP step (sine LUT, waveshaper LUT) is the unvectorizable
bottleneck — surrounding arithmetic can be lane-parallel but the
LUT step costs more than it saves unless replaced with a
polynomial. Polynomial substitution requires tone audition."

### Phase 3 — String mode (spike then conditional refactor)

**3a. Measurement spike (~1 day, hardware required)**. Instrument
existing `String::ProcessInternal` (`eurorack/rings/dsp/string.cc`)
to measure share of CPU spent in FIR + IIR + dispersion filter
stages vs `ReadHermite` + `ParameterInterpolator` overhead. Crude
on-hardware approach: build a variant with each String stage
wrapped in `noinline + optimize("O0")` and observe encoder
responsiveness at maxed poly=1 sympathetic-string-quantized
(8 strings, dispersion on).

**Gate**: if filter share ≥ 40 % → proceed to 3b. If < 30 % →
close out String entry in the audit doc.

**3b. StringBank refactor (conditional)**. New files:
`mods/mi/rings/dsp/string_bank.{h,cc}` in the override directory
(no upstream changes). Layout: per-string AoS for heavy delay-line
state (~12 KB each), SoA for the small filter / control state.
Per-sample loop: scalar gather (`ReadHermite` per string — inherent
since no NEON gather on A8) → 4-at-a-time NEON filter chain on the
gathered samples + SoA filter state → scalar scatter (writes per
string). Pad to multiple of 4 with `dispersion_amount_=0`,
`svf_g_=0` for padding lanes.

Restructure `Part::RenderStringVoice` (in `mods/mi/rings/dsp/part.cc`)
to call `string_bank_[voice].Process` instead of iterating
`String string_[]`. Replace `String string_[kNumStrings]` member in
`Part` with `StringBank string_bank_[kMaxPolyphony]`.

File-level `#pragma GCC optimize("no-tree-vectorize")` on
`string_bank.cc` per `feedback_neon_hint_surfaces`.

Expected: ~25 % mode CPU at poly=1 sympathetic (8 strings); ~10 %
at poly=4 (2 strings/voice).

## File map

| File | Phase | Change |
|---|---|---|
| `mods/mi/rings/dsp/resonator.h` | 1 | Add `amp_scratch_[4]` class member |
| `mods/mi/rings/dsp/resonator.cc` | 1 | Inner-loop accumulator rewrite, padding |
| `mods/mi/rings/dsp/part.cc` | 3b | RenderStringVoice → StringBank call |
| `mods/mi/rings/dsp/part.h` | 3b | Replace `String string_[]` with `StringBank string_bank_[]` |
| `mods/mi/rings/dsp/string_bank.h` (NEW) | 3b | Class declaration |
| `mods/mi/rings/dsp/string_bank.cc` (NEW) | 3b | Implementation + NEON kernel |
| `mods/mi/mod.mk` | 1, 3b | PKGVERSION bump per phase |
| `planning/neon-opportunities.md` | 1, 2, 3 | Audit-doc updates per phase |

Parallel (memory, post-plan-persist):
- `feedback_neon_soa_svf_bank.md` (NEW) — captures the Filterbank/
  Tomograph + Rings-modal SoA SVF bank pattern, user's parallel
  request.
- `feedback_neon_no_gather_lut_dsp.md` (NEW, Phase 2 optional) —
  the Cortex-A8 no-gather constraint memory.

## Reference precedents

- `mods/mi/rings/dsp/resonator.cc` — existing SoA SVF NEON kernel
  (Phase 1 modifies, Phase 3 mirrors).
- `mods/spreadsheet/Filterbank.cpp` — canonical SoA SVF bank in
  spreadsheet (2.6.2.42, zero NEON hints, hardware-verified 8 %
  stereo at 16 bands).
- `eurorack/rings/dsp/string.cc` — upstream String class (Phase 3a
  measures it, Phase 3b mirrors its DSP into StringBank).
- `eurorack/rings/dsp/fm_voice.h` — `SineFm` LUT lookup, the
  structural reason FM can't NEON cleanly on A8.

## Risks & mitigations

| Risk | Mitigation | Reference |
|---|---|---|
| Phase 1 accumulator quads emit new `:64` hints | Class-member `amp_scratch_`; mandatory `check-neon-hints.sh` post-build | `feedback_neon_intrinsics_drumvoice` |
| Phase 1 padding breaks modal sound (state leak) | `g=0` algebraically freezes state at 0; verified pattern from Filterbank | — |
| Phase 3 spike measurement noisy | Multiple Rings instances + max polyphony to amplify; ±5pp tolerance on the 40 % gate | — |
| StringBank breaks non-sympathetic STRING (1 string) | StringBank handles N=1 cleanly via padding-to-4 | — |
| Auto-vec init trap in `string_bank.cc` | File-level `no-tree-vectorize` pragma | `feedback_neon_hint_surfaces` |
| Hardware regression on String mode | Emu A/B + hardware audition on all 4 string-family models, poly=1 and poly=4, dispersion on/off | — |
| FM users disappointed by close-out | Document structural reason; polynomial sine remains on the books | — |

## Out of scope / follow-ups

- **Polynomial sine substitution for FM mode** — unlocks cross-voice
  NEON but changes timbre. Separate audition gate.
- **Plaits / Warps / Stratos NEON passes** — next audit candidates
  after Rings closes.
- **Modal: amp pre-batch into SoA scratch** — marginal additional
  win, revisit only if Phase 1 hardware audition leaves headroom.

## Verification (per phase)

### Phase 1
1. Build linux + am335x (mi package).
2. `tools/check-neon-hints.sh testing/am335x/mods/mi/libmi.so` —
   zero new SUSPECT vs pre-edit baseline.
3. Emu A/B vs pre-Phase-1: identical / float-rounding-level output
   on modal mode with swept impulse + pink noise.
4. Hardware audition: 24-band modal drone, sweep position. No
   audible regression.
5. Commit + PKGVERSION bump.

### Phase 2
1. Edit `planning/neon-opportunities.md` FM entry.
2. (Optional) Write `feedback_neon_no_gather_lut_dsp.md`.
3. Commit. No build/install needed.

### Phase 3a (spike, hardware required)
1. Build instrumented variant.
2. Run on hardware at worst case (poly=1 sympathetic, 8 strings,
   dispersion on).
3. Record per-stage share. Save to a planning markdown note.
4. Decision: proceed to 3b or close out.

### Phase 3b (conditional)
1. Implement StringBank.
2. Build + hint audit (zero new SUSPECT in `libmi.so`).
3. Emu A/B: all 4 string-family models, poly 1 + 4, dispersion
   on/off. RMS diff ≤ 1e-4.
4. Hardware audition: each model at poly=1 and poly=4.
5. Commit + PKGVERSION bump.
