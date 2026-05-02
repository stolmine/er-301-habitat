# visadhara/ — header-only DSP for the Visadhara percussion voice

NEON 4-lane DSP scaffolding for habitat's Visadhara unit
(`mods/spreadsheet/Visadhara.cpp`).

## Attribution

The 4-lane SIMD voice pattern (`float32x4_t` *is* the per-voice fan-out)
is the same shape we ship in `jf::four::Voice` (`mods/spreadsheet/jf/`),
which itself adapts tomf's polygon pattern from `er-301-custom-units`
with verbal blessing. See `mods/spreadsheet/jf/README.md` for the
parent attribution. Visadhara's voice is a purpose-built variant —
single shared trigger, AR-only envelope, no Cycle/Sustain dispatch —
not a direct reuse of the jf::four::Voice class.

## Files

- `voice.h` — 6-lane voice (two 4-lane bundles, lanes 6,7 masked off).
  Per-voice phase, AR envelope state, applied to a morph-LUT-based
  waveshape with per-voice frequency multiplier and amplitude scalar.
- `morph.h` — wave-shape morph helper (sin → tri → saw → sq continuous
  blend). Header-only inline.
- `folder.h` *(Phase 2)* — threshold-reflection folder with dynamic
  stages + amplitude compensation.
- `noise.h` *(Phase 2)* — LCG noise + octave decimation.
- `pmm.h` *(Phase 4)* — 3-op phase-mod operator chain for Metal mode.

## NEON memory rules

Per `feedback_neon_intrinsics_drumvoice` and `feedback_neon_hint_surfaces`:

- All NEON working memory lives as class members or heap-allocated
  Internal struct fields, never stack-locals.
- `make_4` / `make_mask` use vsetq_lane (no stack-local arrays as
  vld1q sources).
- Per-sample output writes go through vgetq_lane to scalar variables;
  no `float voices[N]; vst1q_f32(voices, ...)` pattern.
- `__attribute__((optimize("no-tree-vectorize")))` on `process()`.
