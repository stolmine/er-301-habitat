# mi mode/engine-switch crash — root cause + AAPCS barrier resolution (2026-05-16)

**Status: RESOLVED at mi 1.0.3.13 (8 calls) and minimized at 1.0.3.14 (single call).** Habitat-side codegen issue, not firmware. Closes the open hypotheses in `planning/mi-mode-selector-crash-firmware-bisect.md` and `er-301/docs/HABITAT_MI_MODE_SELECTOR_CRASH.md`.

## TL;DR

**Crash**: Plaits engine switch and Clouds mode switch hard-fault on am335x hardware on the first audio frame after the swap. Works in emu. Affects mi only. Rings is untouched (its model switch doesn't do the same swap pattern).

**Cause**: At `-O3 -ffast-math` GCC keeps NEON values live in caller-saved registers (D0-D7, D16-D31) across the swap path inside `plaits::Voice::Render`. Some Cortex-A8 pipeline state from before the swap interferes with the new engine's first NEON instructions.

**Fix**: Insert at least one call to an in-package `extern "C" __attribute__((noinline)) void mi_barrier_noop()` at the swap boundary. Forces GCC to emit AAPCS-mandated `vstr/vldr` register spill+reload around the call, which "rinses" whatever stuck NEON state was the trigger.

## What we ruled OUT (over the 1.0.3.5→.13 bisect)

| Version | Change | Result |
|---|---|---|
| 1.0.3.5 | `logInfo()` calls + `int lastLoggedEngine` member added | **works** (first observation that diagnostic logs masked the bug) |
| 1.0.3.6 | Pure 1.0.3.4 source, just version bump | crashes (baseline confirmed) |
| 1.0.3.7 | + `volatile int layoutProbe_` before arena (struct layout shift) | crashes → **arena alignment / struct layout ruled out** |
| 1.0.3.8 | + `logInfo()` calls only in `eurorack/plaits/dsp/voice.cc` swap path | works → **isolated voice.cc instrumentation as the masking variable** |
| 1.0.3.9 | + 8× `for (volatile int _i=0; _i<2000; ++_i){}` pure busy-wait delays (~110 μs total) in same 8 slots | crashes → **pure timing latency ruled out** |
| 1.0.3.10 | + 8× `asm volatile("" ::: "memory")` compiler reorder barriers (codegen-changing, zero runtime) | crashes → **GCC instruction-reorder ruled out** |
| 1.0.3.11 | + 8× `asm volatile("dmb sy" ::: "memory")` CPU memory barriers | crashes → **memory-ordering race ruled out** |
| 1.0.3.12 | + single `vmsr fpscr, %0` write to RunFast mode (FZ\|DN, exceptions cleared) at swap entry | crashes → **FPSCR state corruption ruled out** |
| 1.0.3.13 | + 8× call to `extern "C" __attribute__((noinline)) void mi_barrier_noop()` (empty body, separate .o) | **works** → mechanism is the function-call boundary forcing AAPCS NEON spill |
| 1.0.3.14 | reduced to **1** `mi_barrier_noop()` call as first statement inside swap branch | **works** → minimum confirmed: one call at swap entry is sufficient |

## What the binary diff showed (1.0.3.7 no-logs vs 1.0.3.8 with-logs)

Both versions: identical 454 NEON instruction count in `plaits::Voice::Render`, identical 4 indirect `blx` calls (for the 4 virtual engine-method dispatches), zero `:64`/`:128` alignment hints in Render.

Difference: 1.0.3.8 has 8 additional direct `bl _logBriefNice` calls in the swap region (offsets 0x11c–0x230). Code shift: +196 bytes in Render, +651 bytes total in voice.o text section, +new .rodata entries for the format strings. No new relocations of significance.

1.0.3.13 binary check confirmed: 8 `bl mi_barrier_noop` calls + 11 `vstr [sp, #N]` spills + 1 `vstmdb`/`vpush` multi-register spill in Render. Same AAPCS-mandated spill pattern as 1.0.3.8 had around logInfo.

## Mechanism (best inference)

Cortex-A8 NEON pipeline has in-order issue with bypass paths and a write buffer. When `-O3 -ffast-math` keeps a NEON value live in a physical register across heavyweight memory-touching operations (arena Free, engine ctor via virtual call, post-processor reset, UserData load), one of those values gets into an "uncommitted" pipeline state. Specifically, we suspect a denormal or otherwise IEEE-edge-case value that the next NEON op (in the freshly-Init'd engine's first Render call) trips on — exception bounces to "support code" per the Cortex-A8 TRM 13.5.4, but no support code is installed on the audio thread → undefined instruction fault.

Why all the other fixes failed:
- **Pure delay** doesn't change which registers hold what — the live value stays in the register.
- **DMB sy** drains the write buffer for memory, not the NEON FP pipeline.
- **FPSCR RunFast write** doesn't change values already in registers — only affects subsequent computation. But the trigger value is already-computed.
- **Compiler barriers** force code-shift but don't force register spill.

Why the function call fixes it:
- AAPCS mandates the caller spill all caller-saved NEON registers (D0-D7, D16-D31) before the call.
- `vstr` writes the bit pattern to L1 cache; `vldr` after the call reloads it into a (possibly different) physical register.
- The round trip forces NEON pipeline commit and removes any "stuck" state. The reloaded value is a clean register-to-register copy of the bits, immune to whatever pipeline hazard was the trigger.

This is consistent with the existing memory family on Cortex-A8 NEON sensitivity at `-O3 -ffast-math`:
- `feedback_neon_intrinsics_drumvoice` — stack-locals trigger trapping `vld1.32 [reg :64]` hints
- `feedback_neon_hint_surfaces` — register-pressure spills surface alignment hints
- `feedback_runtime_branched_dsp_dispatch` — runtime if/switch chains hang A8

## Files landed for the fix

- `mods/mi/MiBarrier.cpp` — defines `extern "C" __attribute__((noinline)) void mi_barrier_noop() {}`. Build picks it up via the existing `MOD_CPP = $(wildcard $(MOD_DIR)/*.cpp)` rule in `mod.mk`.
- `eurorack/plaits/dsp/voice.cc` — `extern "C" void mi_barrier_noop();` at namespace scope; calls to `mi_barrier_noop()` in the engine-swap branch of `plaits::Voice::Render`.
- `mods/mi/Clouds.cpp` — (pending Phase 2) add the same call around `s.processor.set_playback_mode()`.

## Open follow-ups (post-1.0.3.14)

1. **1.0.3.14 — DONE**: minimized to ONE `mi_barrier_noop()` at the start of the swap branch. Verified working on hardware. This is the production form.
2. **Clouds — landed in 1.0.3.15**: same pattern applied around `set_playback_mode()` in `mods/mi/Clouds.cpp` (declaration at namespace scope, single call at the top of the `if (mode != s.cachedMode)` branch). Verify mode-switch on hardware as part of .15 install.
3. **Audit other mi units**: any unit that does heavyweight state swap in `process()` (RingsVoice does not — confirmed by its non-affected status). Stratos / WarpsModulator / Commotio / MarblesT / MarblesX / Grids — none currently do similar arena re-init in audio thread, so no action needed unless a future change introduces one. If one does, add the same call.
4. **Optional**: promote `MiBarrier.cpp` to a shared utility header (`mods/mi/MiBarrier.h`) for forward declarations; the .cpp definition stays as-is in its own .o so GCC can't inline.
5. **Memory + cross-doc updates**: `feedback_neon_aapcs_call_barrier.md` (DONE), this doc (DONE), `er-301/docs/HABITAT_MI_MODE_SELECTOR_CRASH.md` (CLOSED).

## Cross-references

- `planning/mi-mode-selector-crash-firmware-bisect.md` — the firmware-side bisect (now superseded by this finding — the firmware was innocent the whole time)
- `er-301/docs/HABITAT_MI_MODE_SELECTOR_CRASH.md` — original firmware-side handoff note (close out)
- `planning/plaits-6op-os-rollback.md` — predecessor session that initiated the rollback to 1.0.3.4 baseline
- Memory: `feedback_neon_aapcs_call_barrier.md` — the durable rule
- Memory: `feedback_neon_intrinsics_drumvoice` / `feedback_neon_hint_surfaces` / `feedback_runtime_branched_dsp_dispatch` — sibling Cortex-A8 NEON codegen pitfalls
