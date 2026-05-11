---
name: NEON in DrumVoice — root cause isolated (stack-local NEON arrays trap)
description: NEON intrinsics work fine in DrumVoice on Cortex-A8 hardware as long as the NEON-touched arrays are CLASS MEMBERS or heap-allocated. Stack-local NEON arrays (with or without alignas/__attribute__((aligned)) annotations) cause GCC `-O3 -ffast-math` to emit `vld1.32 [reg :64]` strict-aligned-hint instructions that trap at runtime when AAPCS-default 8-byte stack alignment doesn't satisfy the hint. Class members get `vld1.32 [reg]` (no hint) — Cortex-A8 NEON hardware handles unaligned. Pattern: store NEON working memory as member fields like Pecto's `mCachedDelaySamples`.
type: feedback
originSessionId: 0063d5be-4d30-4c1e-a7b3-e687f2cae19d
---

## The rule

When using ARMv7-A NEON intrinsics (`vld1q_f32` / `vst1q_f32`) in a DSP unit's hot path on Cortex-A8 / am335x:

- ✅ **Class member arrays** — `float mBuf[N];` declared in the unit class. Heap-allocated by `operator new`. GCC emits `vld1.32 [reg]` (no alignment hint) or proper hints based on actual offset analysis. Hardware handles any alignment.
- ✅ **Heap-allocated buffers via `new` or `posix_memalign`** — same property as class members.
- ✅ **File-scope statics** — `.bss` section, well-aligned by linker.
- ❌ **Stack-local arrays** — `float buf[4];` inside a function. GCC under `-O3 -ffast-math` emits `vld1.32 [reg :64]` based on the inferred-but-not-real alignment, traps on Cortex-A8 → freeze.
- ❌ **`alignas(16)` / `__attribute__((aligned(16)))` on stack locals** — even worse: GCC emits the strict `[reg :128]` form, traps even harder.
- ❌ **Constant-initialized small stack arrays** — `float buf[4] = {1.0f, 2.0f, 3.0f, 4.0f};` — GCC's optimizer treats these specially and tends to emit aligned-hint loads.

## How we proved it

Bisect through Ngoma 2.5.5.107–110:

| Probe | What it did | Result |
|---|---|---|
| .107 | Inert NEON op (`vdupq + vaddq + vgetq_lane`), no memory | LOADS — NEON works at all |
| .108 | Stack-local `alignas(16) float buf[4]` + `vld1q_f32(buf)` | CRASHES |
| .109 | Stack-local plain `float buf[4]` (no alignment attr) | CRASHES — disassembly showed `vld1.32 [lr :64]` despite no annotation |
| .110 | Class-member `float mBuf[4]` populated from runtime input | LOADS — disassembly showed `vld1.32 [r6]` no hint |

The disassembly diff between .109 and .110 (both for NEON loads of 4 floats):

```
.109: vld1.32 {d16-d17}, [lr :64]    ← :64 = 8-byte alignment hint, traps on misalignment
.110: vld1.32 {d16-d17}, [r6]        ← no hint, NEON HW tolerates any alignment
```

## Why GCC over-promotes stack locals

Multiple known-issue threads (citations in commit log of 2.5.5.108-110):

- **GCC PR 66917** — `vld1.64`/`vst1.64` lowered to alignment-hinted forms without proven alignment.
- **Android NDK #640 / Launchpad gcc-arm-embedded #1931559** — clang and gcc-arm-embedded over-emit `:128` hints from `alignas`/`__attribute__((aligned))`.
- **Zephyr #2108** — AAPCS-vs-stack-realignment fragility on pre-GCC 7 ARM cross-compilers. ARM AAPCS guarantees 8-byte stack base; `alignas(16)` triggers a fragile SP-realign prologue that frequently misbehaves.
- **Cortex-A8 TRM §4.2** — NEON loads with explicit alignment qualifier (`[r0 :64]`, `[r0 :128]`) ALWAYS trap on misalignment regardless of SCTLR.A. Plain `[r0]` (no qualifier) goes through hardware unaligned-tolerant path.

GCC's analysis on a stack-local `float[4]` apparently assumes 8-byte alignment (small const-init aggregate inside an 8-byte-aligned stack frame). It emits `:64` hint. But variable's actual offset within the frame might land it at 4-byte alignment in absolute terms → trap.

## How to apply

When porting NEON from a working unit (Pecto, Clouds, Rings) to a new unit:

1. **Mirror their storage class**. Pecto stores its NEON-loaded arrays as class members (`mCachedDelaySamples`, `mCachedTapWeight`). Don't deviate to stack-local even if it seems simpler.
2. **Resist `alignas` and `__attribute__((aligned))` on NEON-loaded data**. They make the bug worse, not better, on this toolchain.
3. **Verify with disassembly**. After building, run:
   ```
   arm-none-eabi-objdump -d testing/am335x/mods/<pkg>/<class>.o | grep -E "vld1\.|vst1\."
   ```
   Look for `[reg]` (good, no hint) vs `[reg :64]` / `[reg :128]` (bad — will trap if not actually that aligned). If you see hints, add `__builtin_assume_aligned` or move data to class members.
4. **Probe small first**. Inert NEON op → class-member load/store → polynomial sine on member buffer → full bank. Each probe a separate version (4th-digit bump) with hardware verification before the next.
5. **Memory hierarchy preference for NEON working sets**:
   1. Class members (best — clean ABI, predictable alignment)
   2. Heap via `posix_memalign` (good — guaranteed alignment but more boilerplate)
   3. File-scope statics (acceptable — but shared across instances)
   4. Stack-local (DON'T — trap landmine on this toolchain)

## Related

- `feedback_neon_delay_gather.md` — successful NEON in MultitapDelay using class-member arrays. Same pattern as the .110 fix.
- `feedback_package_trig_lut.md` — sinf/cosf miscompute from package .so on this hardware; polynomial sine sidesteps that issue too.
- `feedback_runtime_branched_dsp_dispatch.md` — earlier hardware-only crashes with switch/case differential bodies.
- `feedback_identical_means_identical.md` — when in doubt, ape a known-working pattern (Pecto) exactly.

## Background reading (linked in 2.5.5.108-110 commit message)

- [GCC PR 66917](https://www.mail-archive.com/gcc-bugs@gcc.gnu.org/msg462439.html) — vld1/vst1 over-promoted alignment hints
- [Cortex-A8 TRM §4.2](https://developer.arm.com/documentation/ddi0344/k/unaligned-data-and-mixed-endian-data-support/unaligned-data-access-support) — unaligned data access support
- [Android NDK #640](https://github.com/android/ndk/issues/640) — clang `:128` hint over-emission
- [Launchpad gcc-arm-embedded #1931559](https://bugs.launchpad.net/gcc-arm-embedded/+bug/1931559) — unreasonable NEON alignment requirement
- [Zephyr #2108](https://github.com/zephyrproject-rtos/zephyr/issues/2108) — ARM stack alignment vs AAPCS
- [Mozilla bug 549296](https://bugzilla.mozilla.org/show_bug.cgi?id=549296) — Cortex-A8 segfault on unaligned NEON
