# Hexop FM — 6-op FM voice on the tomf-polygon polyphony bus

## Premise

Plaits' 6-op FM engine (`eurorack/plaits/dsp/fm/dx_units.cc` + `voice.h` +
`operator.h`) is its single most CPU-hungry engine *and* it's the engine
users have flagged as noisy (no oversampling on the 6-op render path
unlike the classic 2-op FM engine).

Two structural facts make a spinoff a better play than in-place Plaits
optimization:

1. **The sine source is a 4096-entry LUT** (`stmlib::Interpolate(lut_sin, …)`
   in `operator.h`). Cortex-A8 NEON has no gather load, so the per-op
   sine read blocks cross-voice batching as long as the LUT is in the
   per-sample path. See `feedback_neon_no_gather_lut_dsp.md`.

2. **The 6 ops within a voice are serial** (modulator → carrier
   dependency chain). The only NEON-parallel axis is **cross-voice** —
   4 voices fanned across NEON lanes, JF / tomf-polygon style. Plaits'
   engine, by contrast, is a single-voice monosynth.

If we replace the sine LUT with a polynomial (5th/7th-order odd-power,
the JF/Visadhara form) and lay the voices out SoA on the tomf-polygon
bus, the unit becomes a **polyphonic 6-op FM voice** that runs cleanly
on Cortex-A8 NEON. We get polyphony, oversampling, and lower CPU all
at once — Plaits' 6-op FM is mono and currently maxed-out at ~25–30 %
CPU; the spinoff targets 8 voices at ~10–15 %.

## Reference precedents (in-repo)

- **`mods/spreadsheet/jf/voice.h`** — the canonical local tomf-polygon
  adaptation. 6 voices, lane = voice. `project_jf_codex` memory walks
  the architecture. Hexop FM mirrors this layout.
- **`mods/spreadsheet/visadhara/morph.h`** — 7th-order odd-power
  polynomial sine, NEON quad form. The sine substitution lifts directly.
- **`feedback_neon_voice_bus_template`** — the cross-voice NEON pattern
  contract: SoA voice state, class-member float[N×4] arrays,
  block-rate parameter bake-in, named broadcast quads, `always_inline`
  shape helpers, `wrap01_4` for phase.
- **`eurorack/plaits/dsp/fm/algorithms.{h,cc}`** — MIT-licensed
  6-op algorithm definitions (32 algorithms, DX7-compatible). Lifts
  cleanly; we re-target the per-op renderer to NEON cross-voice.
- **`eurorack/plaits/dsp/fm/dx_units.cc`** — patch storage + envelope
  rate tables. Reference for envelope timing, not the runtime code.

## Architecture

### Voice layout (SoA, 8 voices = 2 NEON quads)

```cpp
class HexopVoiceBus {
  static constexpr int kNumVoices = 8;          // 2 NEON quads
  static constexpr int kNumOps = 6;

  // Per-op per-voice phase + envelope state. Class-member arrays =
  // heap-allocated = NEON-safe (no :64 stack-local trap).
  float op_phase_[kNumOps][kNumVoices];         // wrapped 0..1
  float op_phase_inc_[kNumOps][kNumVoices];     // baked at block-rate
  float op_env_value_[kNumOps][kNumVoices];     // current envelope output
  float op_env_inc_[kNumOps][kNumVoices];       // current segment rate
  uint8_t op_env_stage_[kNumOps][kNumVoices];   // 0..3 (4-stage DX7-style)

  // Per-op per-voice patch parameters (baked at block-rate from the
  // current preset + per-voice keyboard scaling + velocity sensitivity):
  float op_level_[kNumOps][kNumVoices];         // operator output scale
  float op_feedback_[kNumOps][kNumVoices];      // self-feedback (op 6 typ.)

  // Per-voice global state:
  float voice_phase_inc_base_[kNumVoices];      // note frequency (Hz/sr)
  float voice_gate_[kNumVoices];                // 0/1 gate from picker
  float voice_amp_[kNumVoices];                 // velocity / per-voice level

  // Op output bus (scratch — 4 lanes per quad, holds one block of op output
  // for use as the next op's modulation input per the algorithm opcode list).
  // Sized for one sample of output across 4 lanes; held as a class member
  // to dodge stack-local :64 hints.
  float op_out_scratch_[kNumOps][4];
};
```

Voice count of 8 (2 quads) instead of JF's 6 — FM chord stacks
naturally want a power-of-2 count and won't waste lane masking. Picker
behaves like JF's: hold/strum, retrigger, voice stealing.

### Inner loop shape (per output sample, 2× oversampled)

```cpp
for (int os = 0; os < 2; os++) {
  // Cross-voice 4-lane pass. One pass per quad of voices (kNumVoices/4).
  for (int vbase = 0; vbase < kNumVoices; vbase += 4) {
    // For each op in algorithm-evaluation order:
    for (int op = 0; op < kNumOps; op++) {
      // Load 4 voices' worth of op state
      float32x4_t v_phase = vld1q_f32(&op_phase_[op][vbase]);
      float32x4_t v_phase_inc = vld1q_f32(&op_phase_inc_[op][vbase]);
      float32x4_t v_env = vld1q_f32(&op_env_value_[op][vbase]);
      float32x4_t v_level = vld1q_f32(&op_level_[op][vbase]);

      // Build modulation input: algorithm-specific sum of earlier op
      // outputs from op_out_scratch_, plus feedback if applicable.
      // The algorithm opcode table compiles down to a small fixed
      // chain of vld1q_f32 + vaddq_f32 calls — no per-sample
      // branching (algorithm is patch-rate, not sample-rate).
      float32x4_t v_mod_input = build_mod_input_neon(algorithm, op, ...);

      // Polynomial sine at (phase + mod_input):
      float32x4_t v_sine_arg = vaddq_f32(v_phase, v_mod_input);
      // wrap01_4 + 7th-order odd-power, lifted from visadhara/morph.h:
      float32x4_t v_op_out = sine_poly_4lane(v_sine_arg);

      // Apply operator envelope × per-op level:
      v_op_out = vmulq_f32(v_op_out, vmulq_f32(v_env, v_level));
      vst1q_f32(&op_out_scratch_[op][0], v_op_out);

      // Advance phase: wrap01_4(phase + phase_inc):
      v_phase = wrap01_4(vaddq_f32(v_phase, v_phase_inc));
      vst1q_f32(&op_phase_[op][vbase], v_phase);

      // Advance envelope (scalar-friendly: branches on stage transitions,
      // but stage transitions are sparse — typical per-block, not per-sample.
      // Do scalar tail OR per-voice unrolled NEON exponential update with
      // class-member stage table. JF's envelope handling is the reference.)
      advance_envelope(op, vbase);
    }

    // Sum carrier ops per algorithm into the voice output bus:
    float32x4_t v_voice_out = sum_carriers_neon(algorithm, ...);
    // Accumulate into output sample buffer (horizontal sum + amp + pan).
    accumulate_to_output(v_voice_out, vbase);
  }
}
```

### Sine polynomial

7th-order odd-power, NEON quad form, **always_inline**:

```cpp
inline __attribute__((always_inline))
float32x4_t sine_poly_4lane(float32x4_t phase01) {
  // phase01 in [0..1) → x in [-π..π]:
  float32x4_t x = vmulq_f32(vsubq_f32(phase01, vdupq_n_f32(0.5f)),
                             vdupq_n_f32(2.0f * 3.14159265f));
  float32x4_t x2 = vmulq_f32(x, x);
  // Coefficients tuned for max-error ~-90 dB across [-π,π]:
  // sin(x) ≈ x · (a + b·x² + c·x⁴ + d·x⁶)
  float32x4_t r = vmlaq_f32(vdupq_n_f32(C0),
                  vmlaq_f32(vdupq_n_f32(C1),
                  vmlaq_f32(vdupq_n_f32(C2), vdupq_n_f32(C3), x2), x2), x2);
  return vmulq_f32(x, r);
}
```

Visadhara's coefficients can be lifted verbatim. Audition is the gate:
the FM tonal-character question (see top-of-file conversation) — the
polynomial-sine substitution is the *premise* of this unit's existence,
not a debatable optimization. If audition reveals it produces an
unacceptable FM tone, we re-cost with a small NEON-permute-friendly
polynomial (5th-order) or fall back to a 256-entry table with vtbl,
but the baseline is 7th-order poly.

### Patch / preset format

Phase 1: **single algorithm + 6-op level/ratio/feedback envelope
controls exposed as Plaits-style macros** (no DX7 patch loading).
~16 unit controls; same UX shape as Alembic / JF.

Phase 2: **DX7 patch loader** as follow-up — `eurorack/plaits/data/`
has the .syx bank Plaits ships, lifts to `mods/<pkg>/hexop/patches/`.

### Algorithm subset

Start with 8 of the 32 DX7 algorithms — the ones with the most
distinctive timbral character:

- Alg 1 (stack of 5 mods → 1 carrier) — classic bell
- Alg 5 (3 parallel 2-op pairs) — clean chord pads
- Alg 7 (2 carriers, 4 mods variants) — bright leads
- Alg 16 (1 mod → 1 carrier, others additive) — warm bass
- Alg 21 (4 parallel pairs) — strings
- Alg 22 (1 mod into 3 carriers) — bell pads
- Alg 31 (5 carriers + 1 mod) — additive-ish
- Alg 32 (6 carriers parallel) — pure additive

Phase 2 adds the remaining 24. The algorithm dispatch is opcode-driven
(per `algorithms.h`), so adding algorithms is data, not code.

## Package siting

**Suggested package: `spreadsheet`** — sits alongside JF / Alembic /
Visadhara / Ngoma in the polyphonic-voice family. Re-uses the
spreadsheet build flags, viz toolkit, and naming convention.

**Unit-name candidates** (user choice, no third-party branding):

- **Hexop** — six-operator FM, generic
- **Belltree** — bell-character connotation
- **Voltigeur** — habitat-style French name (acrobatic jumper)
- **Pyrope** — gemstone, fits Alembic / Visadhara aesthetic
- **Synapse** — modulation routing connotation

Plan refers to "Hexop" as a working placeholder.

## File map

| File | Status | Purpose |
|---|---|---|
| `mods/spreadsheet/Hexop.cpp` | NEW | od::Object subclass — outputs, inlets, picker hookup |
| `mods/spreadsheet/Hexop.h` | NEW | declarations |
| `mods/spreadsheet/hexop/voice_bus.h` | NEW | HexopVoiceBus class declaration |
| `mods/spreadsheet/hexop/voice_bus.cc` | NEW | NEON inner loop + envelope advance + algorithm dispatch |
| `mods/spreadsheet/hexop/algorithms.h` | NEW | Lifted from `eurorack/plaits/dsp/fm/algorithms.h`, retarged to NEON ops |
| `mods/spreadsheet/hexop/sine_poly.h` | NEW | NEON polynomial sine (lifted from visadhara/morph.h) |
| `mods/spreadsheet/hexop/picker.h` | NEW | Voice stealing / hold-strum / retrigger (mirror JF's picker) |
| `mods/spreadsheet/hexop.lua` | NEW | Unit Lua wrapper + control layout |
| `mods/spreadsheet/assets/units/hexop.lua` | NEW | Asset side of unit-Lua |
| `mods/spreadsheet/HexopGraphic.h` | NEW | Custom viz (deferred — start with stock fader pack) |
| `mods/spreadsheet/mod.mk` | EDIT | Add Hexop sources, PKGVERSION bump |
| `mods/spreadsheet/spreadsheet.lua` | EDIT | Register unit in picker |

## Phases

### Phase 0 — Sine polynomial audition (1 day, blocking gate)

Build a side-by-side audition: a tiny test harness or a temp Plaits
mod variant where the existing `lut_sin` is swapped for the polynomial.
Audition the 6-op engine specifically. Document outcome in this file.

**Gate**: if polynomial sine produces an acceptable FM tone, proceed
to Phase 1. If not, revisit: 256-entry table with vtbl, or accept the
LUT and run scalar 6-op at lower polyphony.

### Phase 1 — Mono Hexop (single voice, no polyphony)

Build the algorithm engine + envelopes + polynomial sine, run a
**single voice scalar** version first. Verify:

- Output matches a known-good DX7 emulator output (bit-exact not
  required; perceptual match on alg 1 + alg 5 reference patches).
- Envelope shapes match DX7 reference timing.
- Algorithm topology correct (modulator routing).

Single voice is a useful checkpoint: separates algorithm correctness
from NEON scaffolding.

### Phase 2 — Polyphonic NEON Hexop

Add the SoA voice bus, JF-style picker, NEON inner loop, 2×
oversampling. Audition all 8 starter algorithms at poly=8.
Target: 12–18 % CPU at poly=8, full-density modulation.

### Phase 3 — DX7 patch loader (optional, follow-up)

Lift `eurorack/plaits/dsp/fm/patch.h` + `dx_units.cc` patch storage.
Expose DX7 .syx bank loading via a unit-Lua patch-picker (or pre-bake
the patches into Lua-loaded resource arrays). Adds ~32 patches per
bank as preset variants.

### Phase 4 — Full 32-algorithm support

Expand from 8 starter algorithms to all 32. Mostly data + algorithm
dispatch table extensions, minimal new code.

### Phase 5 — Viz

Per `feedback_viz_encoder_capture_architectural`: frame-cached, no
encoder capture risk. Candidates: per-op envelope level bars, algorithm
topology diagram, voice-allocation indicator.

## Verification gates

- Phase 0: sine polynomial A/B audition logged in this file.
- Phase 1: scalar single-voice FM output sanity-checked against
  reference patches (alg 1 bell, alg 5 EP).
- Phase 2: NEON hint audit (`tools/check-neon-hints.sh
  testing/am335x/mods/spreadsheet/libspreadsheet.so` — zero new
  SUSPECT hints). Hardware audition at poly=8 with chord stacks.
  CPU at ≤18 % target.
- Phase 3: patch loading round-trips a DX7 .syx bank.
- Phase 4: at least 4 patches per algorithm sound recognizably DX7-ish.

## Risks & mitigations

| Risk | Mitigation |
|---|---|
| Polynomial sine produces "wrong" FM character | Phase 0 audition is a hard gate before any unit work begins |
| Algorithm dispatch becomes per-sample branching | Compile algorithm to fixed-length opcode list at patch-rate; runtime is straight-line per algorithm (per Plaits' approach) |
| Envelope stage transitions break NEON 4-lane flow | Stages transition sparsely (per-block, not per-sample); use scalar tail for transition frame, NEON main path for steady-state. Mirror JF's slope-engine stage-transition pattern. |
| `:64` hints on scratch arrays | Class-member `op_out_scratch_` — heap-allocated, no stack-locals in NEON path; objdump audit per `feedback_neon_intrinsics_drumvoice` |
| Auto-vec init trap on the SoA zero-fills | File-level `#pragma GCC optimize("no-tree-vectorize")` in `voice_bus.cc` per `feedback_neon_hint_surfaces` |
| Voice-stealing artifacts (clicks on note-steal) | Reuse JF picker (proven); add per-op release-on-steal envelope (envelope to 0 over 32 samples before reassigning) |
| Aliasing on high modulation index even with 2× OS | Bump OS to 4× on a polyphony-headroom-conditional basis (compile-time flag, audition gated) |

## Out of scope

- Plaits 6-op FM in-place optimization (this unit *replaces* it; the
  Plaits CPU-reduction plan in `planning/plaits-cpu-reduction.md`
  accordingly excludes engines 2/3/4).
- 4-op FM engines (Plaits' `four_op_fm_engine`) — separate decision;
  could mirror this architecture later or stay in Plaits.
- LFO modulation (DX7 LFO) — Phase 3+ when patch loading lands.
- Microtuning (DX7 supports it) — defer indefinitely.
