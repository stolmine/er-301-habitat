# Ngoma Hard-Crash Debug Pipeline

Status: **planning**. Proposed 2026-04-28 mid-session, awaiting execution
after restart.

Companion to:
- `~/.claude/projects/-home-sure-repos-er-301-habitat/memory/project_ngoma_codex.md`
  — full Ngoma architecture + bisect history through `.169`.
- `~/.claude/projects/.../memory/feedback_neon_intrinsics_drumvoice.md`
  — stack-local NEON `:64` hint trap.
- `~/.claude/projects/.../memory/feedback_neon_hint_surfaces.md`
  — `:64` hints from register spills + auto-vec'd init.
- `~/.claude/projects/.../memory/feedback_runtime_branched_dsp_dispatch.md`
  — switch-with-differential-bodies trap.
- `docs/dev-rig-procedures.md` — RPi 4 aarch64 emu rig.

## The problem in one paragraph

Ngoma in the spreadsheet package hard-crashes am335x hardware on insert.
Crash does NOT reproduce on x86_64 linux emu, RPi 4 / Cortex-A72 aarch64
emu, or under qemu. Bisect across `.165–.169` walked through every
classic codex-cataloged culprit (constructor `:64` hints, process()
register-spill `:64` hints, comparator threshold, applyRandomize tier
dispatch). `.169` is the bisect baseline: DrumVoice.cpp/.h reverted
verbatim to commit `0dd8870` (`.116`) but built against the current SDK.
Awaiting hardware test on 2026-04-28. Two outcomes:
- `.169` still crashes → SDK / shared-infra mismatch. Leading suspect:
  parallel-DSP MVP commits (`378a78a`, `262579d`, `c1a6930`) extended
  `od/tasks/UnitChain.h` private fields.
- `.169` works → bug is in `.117–.119+` Ngoma source. Re-bisect within
  `0dd8870..e65584e..23291ef`.

## Why a pipeline now

Five iterations of the classic-culprit walkthrough (`.165–.169`) didn't
land the fix. We're past the "guess and check" phase. The next iteration
needs structured tooling:
- Live tracing in the emulator to validate non-codegen hypotheses
  (uninit state, ABI structs, ctor/deserialize order, refactor
  regressions).
- Static am335x objdump check as a pre-flight gate before every
  hardware install — fastest way to spot the trapping codegen pattern
  without a hardware round-trip.
- A documented gdb-friendly emu launch path so SIGSEGV / SIGBUS
  surfaces with a backtrace instead of silent freeze.

## The three tiers

### Tier 1 — Linux emu (live logging + gdb)

What it can catch: logic bugs, ABI/struct-layout mismatches, init-order
issues, uninitialized state, refactor regressions, generic C++ undefined
behavior.

What it can't catch: am335x-specific codegen traps (NEON `:64`/`:128`
hints, switch-with-differential-bodies). For those see Tier 2.

Components:
- `make spreadsheet ARCH=linux` then auto-copy `.pkg` to `~/.od/rear/`
  (per `feedback_linux_build_auto_install`).
- Launch wrapper script at `tools/run-emu-gdb.sh` that runs
  `~/repos/er-301-stolmine/testing/linux-x86_64/emu/emu.elf` under gdb
  with:
  - SIGSEGV / SIGBUS auto-bt
  - Optional breakpoint on `stolmine::DrumVoice::DrumVoice` and
    `stolmine::DrumVoice::process`
  - stdout/stderr to `/tmp/emu-ngoma.log`
- Compile-gated tracing in DrumVoice.cpp/h. Add `#ifdef DRUMVOICE_TRACE`
  fprintf hooks at: ctor entry, deserialize entry, first process() call,
  trigger rising-edge, envelope phase transitions, applyRandomize entry
  + tier value. Off by default; enable with
  `make spreadsheet ARCH=linux DRUMVOICE_TRACE=1`.

### Tier 2 — am335x static analysis (objdump pre-flight)

The codex already documents the recipe. Pull it out into a script that
runs as part of every am335x build.

Components:
- `tools/check-neon-hints.sh <unit>.o` — runs the documented
  arm-none-eabi-objdump pipeline. Default scope: whole `.o` (not just
  process()), since per `feedback_neon_hint_surfaces` register-spill +
  auto-vec'd init both surface outside the hot loop.
- Output format: per-symbol grouped list of `vld1` / `vst1` ops with
  `:64` / `:128` hints. Distinguish:
  - `[sp :64]` single-D-register store → safe (AAPCS guarantees 8-byte
    SP). Filtered out.
  - `[sp :64]` quad-D `{dN-dN+1}` → suspect (register-spill across
    function call); flagged.
  - `[reg :64]` non-stack → suspect (stack-local NEON or
    auto-vectorized init); flagged.
- Wire into `make spreadsheet ARCH=am335x` as a post-build step that
  prints the warning summary but doesn't fail the build.
- Intentionally permissive: present even known-OK hints with the unit's
  current state so we have a baseline to compare against on edits.

### Tier 3 — aarch64 emu on RPi rig (optional this session)

Already documented in `docs/dev-rig-procedures.md`. Sometimes catches
ARM-codegen-but-not-am335x issues that x86_64 emu misses. Lower priority
than Tiers 1+2; defer unless Tier 1 produces something interesting.

## Order of execution after restart

1. Confirm the hardware result on `.169` (the BISECT BASELINE) — this
   immediately bifurcates the search:
   - Still crashes → focus on SDK/ABI tier (Tier 1 most useful;
     parallel-DSP MVP integration probe).
   - Works → re-bisect Ngoma `.117..` source forward (Tier 2 most
     useful; codegen pre-flight on each step).
2. Build out Tier 2 (`tools/check-neon-hints.sh`) FIRST — it's the
   smallest, highest-signal piece; works without any code edits;
   instantly tells us whether the current `.169` Ngoma binary has any
   trap-shaped hints we missed.
3. Then Tier 1: gdb wrapper script, then DRUMVOICE_TRACE instrumentation.
4. Defer Tier 3 unless Tiers 1+2 produce a new lead.

## Files this pipeline will touch

- `tools/check-neon-hints.sh` (new)
- `tools/run-emu-gdb.sh` (new)
- `mods/spreadsheet/DrumVoice.cpp` (compile-gated trace fprintfs)
- `mods/spreadsheet/DrumVoice.h` (compile-gated trace declarations)
- `mods/spreadsheet/mod.mk` (DRUMVOICE_TRACE flag plumbing, optional
  post-build objdump invocation)

## Open questions

- Is there an existing `tools/` dir in this habitat repo, or do we
  inherit from the firmware's `tools/`? (Habitat repo has `scripts/`
  but no `tools/` at the moment — confirmed by `ls`.) **Resolved
  2026-04-28: created `tools/` and dropped `check-neon-hints.sh`
  there.**
- The DRUMVOICE_TRACE hooks need to use stable identifiers that survive
  am335x → linux differences. fprintf to stderr is cheapest; consider
  also gating to a single voice instance to avoid log floods on multi-
  instance patches.
- For Tier 2: `arm-none-eabi-objdump` location — verify it's on `$PATH`
  or default the script to look under `~/ti`. **Resolved 2026-04-28:
  `/usr/bin/arm-none-eabi-objdump` on $PATH.**

## Execution log — 2026-04-28

### Hardware test of `.169` (BISECT BASELINE)

`.169` (DrumVoice cpp+h verbatim from commit `0dd8870` / `.116`, current
SDK) **still crashed on insertion.** This puts us on the SDK / shared-
infra branch of the bifurcation.

### Tier 2 implementation + first run

`tools/check-neon-hints.sh` written and run against
`testing/am335x/mods/spreadsheet/DrumVoice.o`. Output classification:

```
== stolmine::DrumVoice::DrumVoice()
  [SUSPECT] 58c: vst1.64 {d16}, [lr :64]   ; this+0x4C0 (1216)
  [SUSPECT] 598: vst1.64 {d16}, [ip :64]   ; this+0x4D0 (1232)
  [SUSPECT] 5a0: vst1.64 {d16}, [r2 :64]   ; this+0x4E0 (1248)
  [SUSPECT] 5ac: vst1.64 {d16}, [r3 :64]   ; this+0x4F0 (1264)

== stolmine::DrumVoice::process()
  [safe   ] bbc: vld1.64 {d22}, [sp :64]
  [safe   ] c0c: vst1.64 {d22}, [sp :64]
  [SUSPECT] c20: vst1.64 {d26}, [r3 :64]
```

Constructor hints write zeros to four contiguous 16-byte regions at
`this+1216/1232/1248/1264` — the four `float[4]` member arrays GCC
auto-vectorizes during synthesized member init. **These are the same
hints `.165` explicitly cleared via the in-class `= nullptr` drop +
single memset; reverting source to `.116` reintroduced them.**

The process() suspect at 0xc20 stores `{d26}` (8 bytes) to a heap
pointer freshly loaded from `[sp, #152]`. Indirect; alignment depends
on what was stored at sp+152. Likely a register-spill surface from the
`.166` exploration.

### Probe #1 — parent class size diff (NEGATIVE)

Hypothesis: parent class (`od::Object` chain) grew in the SDK between
`.116`-era and current, shifting DrumVoice member offsets so the
auto-vectorized `:64` writes no longer land 8-byte aligned.

Method: `git log --since=.116-date` on
`er-301/od/objects/Object.h`, `Inlet.h`, `Outlet.h`, `Parameter.h`,
`Option.h`, `StateMachine.h`, `extras/ReferenceCounted.h`,
`extras/Profiler.h`. Plus per-commit stat on the parallel-DSP MVP
chain (`090a966`, `1a2b824`, `378a78a`, `262579d`, `c1a6930`).

Result: **none of the parallel-DSP commits touch any class on the
DrumVoice ancestor path.** They only modify `od/tasks/UnitChain.{cpp,h}`,
`od/tasks/Task.{cpp,h}`, `od/tasks/TaskScheduler.{cpp,h}`. `c1a6930`
explicitly reverted the `Task.h` vtable change, so `Task` layout is
unchanged too. UnitChain *did* grow (mLoadEwmaPct, mInProcess,
mPendingRemovals, mIsChannelChain) but DrumVoice does not inherit
from UnitChain.

So DrumVoice's class-member offsets at `.169` should be identical to
`.116`-era. Size-shift hypothesis is dead.

### Probe #2 — re-apply `.165` ctor fix on `.169` (`.170`)

Hardware: **still crashed.** Tier 2: ctor `:64` hints clear. Crash is
post-ctor. Ctor `:64` hints are not the trigger.

### Probe #3 — force-clean SWIG wrapper regenerate (`.171`)

Hardware: **still crashed.** SWIG wrapper had been stale since `.165`,
but regenerating it didn't change behavior.

### Probe — Tier 2 across all spreadsheet `.o` files (the killer)

Ran `tools/check-neon-hints.sh` against every spreadsheet object on
`.171`:

| Unit | SUSPECT count | HW status |
|---|---|---|
| Pecto | 16 (ctor 9× quad-D `{d16-d17}`) | ✓ works |
| AlembicVoice | 9 (process() includes quad-D) | ✓ works |
| Etcher | 13 (process() quad-D ld/st) | ✓ works |
| GateSeq | 7 (ctor + applyTransform quad-D) | ✓ works |
| Larets | 3 | ✓ works |
| MultitapDelay | 5 | ✓ works |
| Filterbank | 1 | ✓ works |
| pffft | 169 (FFT lib) | ✓ works |
| **DrumVoice** | **1** (smallest!) | **✗ crashes** |

**The NEON `:64`-hint trap hypothesis is dead.** Pecto in particular
ships 9 quad-D `:64` ctor stores (the *exact* shape `feedback_neon_
intrinsics_drumvoice.md` claims is the trap pattern) and runs cleanly
on Cortex-A8. DrumVoice has fewer hints than every other working unit.

### Tier 1 — emu observability (`tools/run-emu-gdb.sh` + DRUMVOICE_TRACE)

Built linux package with `DRUMVOICE_TRACE=1`, ran emu, inserted Ngoma.
Trace:

```
[DV] ctor enter this=0x...
[DV] ctor pre-Internal alloc / post-Internal alloc / ctor exit
[DV] setTopLevelBias which=0..13 (all 14 land)
[DV] process FIRST CALL
[DV] process buffers (all ptrs valid)
```

Happy path on emu — no SIGSEGV/SIGBUS, no crash. Confirms insertion
sequence: ctor → addInput/Output/Parameter → `new Internal()` →
initLUT → 14× setTopLevelBias → first process(). All in order.

### Probe #4 — diff vs Pecto/Petrichor xform pattern (the breakthrough)

User insight: *"this has only been a problem since implementing
different destination groups on xform; we got it working with 4 groups
that were all the same; differentiating is what caused the crash."*

Pecto's `applyRandomize` (Pecto.cpp:350-390) uses `switch(target)` with
9 differentiated case bodies and runs fine. So the codex's
"switch-with-differential-bodies traps Cortex-A8" hypothesis is also
disproven by Pecto.

DrumVoice's `.98` pattern (preserved in `.116`/`.169`) uses monotonic
boolean masking — `bool envOn = (target<=2); float depthEnv = envOn ?
depth : 0.0f;` etc. Disassembly of `.171`'s applyRandomize shows GCC
compiles this into actual `bgt`/`ble` branches at offsets 0xe0/0xf4/
0x10c, not CMP+MOVCC. Codex's claim that this pattern is "branchless"
was wrong at the codegen level.

### Probe #5 — gut applyRandomize, all groups identical (`.172`)

Removed all tier differentiation. `target` ignored. All 10 doRnd calls
always execute with raw `depth`/`spread`. Mirrors `.94`-era
"4 groups all the same" working state.

Hardware: **survives insertion. Stable.** Crash hypothesis confirmed
narrower: the trigger is specifically inside differentiated dispatch,
not anywhere upstream (ctor, NEON, SWIG, ABI, mBias wiring all fine).

### Adjusted next steps

- **Probe #6:** clone Pecto's exact switch pattern into DrumVoice.
  Known-working topology on hardware. If it works, we have a usable
  re-introduction of differentiation. If not, there's something more
  subtle — possibly the *combination* of Ngoma's specific param set
  and switch dispatch, or codegen sensitivity to a specific case body.
- **Probe #7 (if #6 fails):** binary-bisect the differentiation —
  add ONE masked group at a time on top of `.172` to find the minimal
  reproducer.
- **Codex / memory cleanup:** invalidate the `:64` hint and "switch-
  with-differential-bodies" hypotheses; capture this finding so future
  work doesn't re-tunnel.
