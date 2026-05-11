---
name: "Structurally identical" means IDENTICAL, byte-for-byte
description: When comparing a new implementation against a known-working reference pattern (Pecto, Petrichor, etc.) and claiming equivalence, "identical" must mean line-by-line identical — not "similar in spirit" or "same shape." Small code-gen-relevant differences (if-chain vs switch, shared vs fresh map, lambda-in-lambda vs flat body, parameter order, predicate composition) can all diverge under `-O3 -ffast-math` on Cortex-A8 hardware while behaving fine on x86 emu. Every divergence from the reference pattern must be either called out explicitly as a deliberate deviation OR reverted to the reference.
type: feedback
originSessionId: 0063d5be-4d30-4c1e-a7b3-e687f2cae19d
---
## The rule

If I'm basing a new C++ / Lua / DSP implementation on a reference unit (because that unit is known-working on hardware), and I claim the new thing is "structurally identical" or "apes the reference exactly," that claim must be provable by a line-by-line diff against the reference — with every remaining difference called out explicitly as intentional.

If there's no diff-close-to-zero, it isn't identical. Don't use the word.

## Why this matters

ER-301 hardware builds `-mcpu=cortex-a8 -mfpu=neon -mfloat-abi=hard -O3 -ftree-vectorize -ffast-math`. That profile is aggressive enough that small code-level differences can produce different assembly and different runtime behavior than x86 emu, even when the source "looks equivalent." Observed divergence points during the Ngoma 2.5.5.6–2.5.5.92 bisect (which ate a full day because I kept claiming equivalence without verifying):

- `if (target <= 2) { ... }` chain vs `switch (target) { case 0: ... }` — different branch codegen; Pecto/Petrichor never use if-chains on float-derived predicates. The if-chain crashed hardware on fire; switch-case worked.
- `factorMap = Encoder.getMap("[0,1]")` (shared cached map) vs Pecto's `factorMap = floatMap(0, 1)` (fresh per-call) — different object lifetime / mutability semantics.
- Lambda-in-lambda `rndTimbre = [&]() { rnd(...); }` (capturing another lambda by reference) vs flat `case 0: rnd(...); rnd(...); break;` — different C++ codegen under `-O3`.
- `addParameter(mXformDepth); addParameter(mXformTarget);` vs Pecto's reverse order `addParameter(mXformTarget); addParameter(mXformDepth);` — probably benign for name-based lookup, but not the reference order.
- Lua adapter declaration order / addMonoBranch order diverged from Pecto's interleaved `addObject → hardSet → tie → addMonoBranch per-adapter` pattern. Mine batched all of each phase. Probably benign; but NOT identical.

Each of these I waved past with "structurally identical" when they weren't. The eventual fix was to match Pecto literally — switch/case, flat bodies, fresh maps, same order — and then the code ran.

## How to apply

When basing new code on a reference pattern:

1. **Verify identical structurally**: do a literal line-by-line diff between my new code and the reference. Every difference should be explained in a comment or in the commit message.
2. **Don't use "structurally identical" loosely**: if I'm tempted to use the phrase, first run the diff. If the diff isn't trivial, say "based on X with the following deliberate deviations: ..." instead.
3. **On hardware bisects specifically**: when a change works in emu but crashes hardware, the suspect is always a divergence from the reference pattern. Don't search for novel bugs before eliminating every divergence.
4. **When iterating small fixes**: revert ALL the way to the reference pattern first, THEN add back one delta at a time, versioned + commit-separated. This is the bisection discipline that actually worked for Ngoma 2.5.5.8 → 9 → 91 → 92 → 93.

The trap: because C++ / Lua are flexible, many variations of a pattern are semantically equivalent. That flexibility lets me think "same thing, just rewritten." It's not. On hardware with aggressive compiler flags, rewrites can break. Treat the reference as ritual when hardware is in play.

## Related

- `feedback_package_version_bump.md` — even after identifying the fix, the version-bump / extract dance has to be right or stale builds mask the fix.
- `feedback_package_trig_lut.md` — similar story: sinf/cosf from package .so miscompute on hardware where x86 emu is fine.
- `feedback_neon_delay_gather.md` — hardware-only performance tuning that emu never exposes.
