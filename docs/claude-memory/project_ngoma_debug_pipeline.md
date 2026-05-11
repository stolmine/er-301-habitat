---
name: Ngoma debug pipeline — three tiers, plan persisted to repo
description: Active investigation. Ngoma hard-crashes am335x; emu fine on x86_64 + aarch64. Three-tier debug pipeline persisted to planning/ngoma-debug-pipeline.md. Tier 2 ran 2026-04-28 — `.169` (BISECT BASELINE) crashed on hardware; ctor `:64` hints reappeared (regressed from `.165` fix); parent class size hypothesis negative. Next probe: re-apply `.165` ctor fix on `.169`.
type: project
originSessionId: a8674788-9c6a-428e-b919-08de80d97524
---
Active across sessions. Pick up here on next restart.

## Status as of 2026-04-28 (post-Tier 2)

`.169` (DrumVoice cpp+h verbatim from `.116`, current SDK) **CRASHED on
hardware insertion.** SDK / shared-infra branch confirmed.

Tier 2 (`tools/check-neon-hints.sh`) ran against `.169`'s DrumVoice.o:
- 4 SUSPECT ctor `:64` hints reappeared at `this+0x4C0/4D0/4E0/4F0` —
  the contiguous `float[4]` member arrays GCC auto-vectorizes during
  synthesized member init. Same surface `.165` had cleared via in-class
  `= nullptr` drop + memset; reverting source to `.116` reintroduced them.
- 1 SUSPECT process() `:64` hint at 0xc20 (heap pointer loaded from sp+152).

Probe #1 (parent class size diff): **NEGATIVE.** Parallel-DSP MVP
commits don't touch DrumVoice's ancestor classes — only UnitChain /
Task / TaskScheduler. DrumVoice's class layout at `.169` should be
identical to `.116`-era.

Open probes:
- **#2 (next):** apply `.165` ctor fix on top of `.169` source.
  Smallest delta; verifiable via Tier 2 before install. Diagnostic
  even if not a fix.
- **#3 (parallel):** SWIG wrapper staleness in spreadsheet package
  (per `feedback_swig_header_dep`); full SWIG regenerate.
- **#4 (deferred):** Tier 1 emu DRUMVOICE_TRACE + gdb wrapper.

## Why: 

Five iterations (`.165–.169`) of guess-and-check classic-culprit
walkthrough didn't land the fix. Need structured tooling for next pass.

## How to apply

1. **Read** `planning/ngoma-debug-pipeline.md` first — full plan,
   including file-touch list and order of execution.
2. **Order**: confirm `.169` hardware result → build Tier 2 first
   (`tools/check-neon-hints.sh`, smallest+highest-signal) → then Tier 1
   (gdb wrapper + DRUMVOICE_TRACE compile-gated tracing) → defer Tier 3
   (aarch64 RPi rig) unless leads warrant.
3. **Don't drift**: emulator can't reproduce the actual am335x trap (it's
   codegen-specific). Tier 1 catches non-codegen bugs only. Tier 2 (the
   objdump check) is the decisive diagnostic for the trap pattern.

## Cross-references

- Architecture & bisect history: `project_ngoma_codex.md`
- Trap surfaces: `feedback_neon_intrinsics_drumvoice.md`,
  `feedback_neon_hint_surfaces.md`, `feedback_runtime_branched_dsp_dispatch.md`
- aarch64 rig procedures: `docs/dev-rig-procedures.md` in repo
- Plan-persist discipline: `feedback_persist_plans_to_repo.md`
