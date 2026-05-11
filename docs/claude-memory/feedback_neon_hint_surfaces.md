---
name: NEON :64/:128 alignment hints surface beyond stack-local arrays
description: GCC under -O3 -ftree-vectorize emits `:64` / `:128` alignment hints on NEON ops not just for stack-local NEON arrays (covered in feedback_neon_intrinsics_drumvoice) but also from (a) register-pressure spills across function calls inside hot loops, and (b) auto-vectorization of "non-DSP" init code at construction time. Both trap on Cortex-A8 when actual alignment doesn't match. Objdump check must cover the whole .o, not just process().
type: feedback
originSessionId: cur
---

## The rule extension

`feedback_neon_intrinsics_drumvoice` covers the canonical case: stack-local `float[4]` arrays loaded via `vld1q_f32` get `:64` hint promotion and trap. This memory adds two more trap surfaces that the same objdump discipline catches.

## Surface 1 -- register-pressure spills across function calls

**Pattern**: A per-sample inner loop hoists multiple NEON quads (vRow0..3, vRatioF0Sp, vDetuneSp, vLevel ...) outside the loop, then the loop calls a function that clobbers caller-save NEON registers (e.g. `simd_sin`). GCC must save the live-across-call quads to stack. The spill emits:

```
vst1.64 {d16-d17}, [sp :64]   <-- BAD: quad spill to stack with hint
vld1.64 {d18-d19}, [sp :64]
```

`:64` requires 8-byte alignment. AAPCS guarantees the SP base is 8-byte aligned, but the *offset within the frame* depends on GCC's frame layout for the spill slot. If the slot lands at a 4-byte-but-not-8-byte offset, the hint mismatches and the load traps on Cortex-A8.

**Verified case (AlembicVoice phase 3a, 2.5.5.124)**: vRow0..3 + vRatioF0Sp + vDetuneSp + vLevel (7 quads) hoisted across `simd_sin`. GCC spilled multiple quads to `[sp :64]`. Process()-time trap.

**Fix**: Load NEON quads *inside* the per-sample loop, just before use, so they become dead before the function call and don't need to survive it. Only quads used *after* the function call should hoist (vLevel in our case -- one quad fits callee-save without forcing a spill). Cost: re-load on every iteration (~6 quad ops/sample), dwarfed by the function-call cost being protected.

**Single-D `[sp :64]` is fine**: `vst1.64 {d7}, [sp :64]` -- AAPCS guarantees 8-byte SP, single d-register is 8 bytes, hint matches. Not a hazard. Only the *quad* (`{d16-d17}`) `[sp :64]` form is the trap.

## Surface 2 -- auto-vectorized init / non-DSP code at construction

**Pattern**: A constructor or init function does a scalar loop over a float array (e.g. linear interpolation to fill a preset table). GCC at `-O3 -ftree-vectorize` converts the loop to NEON ops. Even though the source code doesn't use NEON intrinsics, the resulting object emits:

```
vld1.64 {d18-d19}, [r9 :64]   <-- load from .rodata or .bss with hint
vst1.64 {d16-d17}, [r1 :64]   <-- store to .bss with hint
vld1.64 {d20-d21}, [sp :64]   <-- spilled temporary
```

The `.rodata` / `.bss` arrays are typically only 4-byte aligned (default for `float`). The `:64` hint requires 8-byte alignment. Mismatch -> trap. The crash fires *at unit construction*, before any audio processing.

**Verified case (AlembicVoice phase 3a, 2.5.5.124-126)**: `fillPhase3Presets` did `for (int slot=0; slot<64; slot++) for (int f=0; f<29; f++) sPresetTable[slot][f] = endA[f] + u * (endB[f] - endA[f]);` and got auto-vectorized into NEON ops with `:64` hints against the static-const endA/endB arrays. Trapped on insert. Symptom: hardware crash on insert; emu fine.

**Fix**: `__attribute__((noinline, optimize("no-tree-vectorize")))` on the init function. **Both** are required:
- `noinline` prevents GCC from inlining the function into the constructor (which would defeat the optimize attribute).
- `optimize("no-tree-vectorize")` disables auto-vec for the function body.

After the fix the function compiles to scalar `vldr s12, [pc, ...]` / `vstr` (single-precision FPU), no NEON, no hints.

**Alternative fixes (less surgical)**:
- `alignas(16) static float endA[29] = ...;` -- forces 16-byte alignment in the linker, hint matches. May or may not work depending on GCC's hint emission and toolchain quirks; per `feedback_neon_intrinsics_drumvoice` `alignas` on stack locals can make things WORSE, but on .rodata/.bss it's controlled by the linker so should be fine.
- Volatile-cast the writes to defeat vectorization. Hacky.
- Hand-roll the loop in a way GCC can't vectorize (data dependencies between iterations). Fragile.

The function attribute is the most robust.

## Updated objdump checklist

After any C++ DSP edit on a spreadsheet unit, before installing on hardware:

```bash
arm-none-eabi-objdump -d testing/am335x/mods/spreadsheet/<Unit>.o \
  | awk '/^[0-9a-f]+ <.+>:$/{symbol=$0} /:64|:128/{print symbol": "$0}'
```

This dumps every quad/d alignment hint and the function it lives in. Then:

1. **In any function**, `vld1.32` / `vst1.32` / `vld1.64` / `vst1.64` on `{dN-dN+1}` (pair = quad) with `:64` or `:128` is **suspect**.
2. **In any function**, single-register `{d7}` / `{d8}` etc. with `:64` to `[sp]` is **safe** (AAPCS 8-byte SP alignment).
3. **In `process()`**, suspect ops on `[sp ...]` mean register-pressure spill across a function call -- fix by loading inside the per-sample loop.
4. **In the constructor or any init code**, suspect ops on `[r* :64]` mean auto-vectorized scalar init -- fix with `__attribute__((noinline, optimize("no-tree-vectorize")))`.
5. **In `process()` on `[r* :64]`** means a stack-local NEON load -- fix by class-member storage per `feedback_neon_intrinsics_drumvoice`.

## Relation to other memories

- `feedback_neon_intrinsics_drumvoice` -- the foundational stack-local NEON case. This memory extends it.
- `feedback_identical_means_identical` -- when adding NEW spreadsheet code that compiles fine on linux but crashes hardware, the suspect is hint-emission divergence from a known-working unit. Compare objdumps with a working reference (DrumVoice, Helicase).
- `feedback_runtime_branched_dsp_dispatch` -- different mechanism (codegen for switch-with-differential-bodies) but same overall lesson: hardware-only crashes that emu can't surface.

## Reference commits

- `f620063` (AlembicVoice phase 3a, 2.5.5.124-128) -- both surfaces hit during the same Phase 3a bisect.
