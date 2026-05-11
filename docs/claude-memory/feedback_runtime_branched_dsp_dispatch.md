---
name: Runtime-branched DSP dispatch can crash am335x even when individual paths work
description: On Cortex-A8 under `-O3 -ffast-math`, a hot-path DSP function that branches on a runtime float-derived value (switch, if-chain, function-pointer table, member-fn-ptr dispatch — anything that produces multiple distinct compiled paths through one entry point) can crash the unit on hardware while loading fine in the x86_64 emulator. Fix: collapse to a SINGLE method with a fixed-shape body, encode tier behavior via branchless arithmetic masks (multiplying out the depth/intensity by 0 to no-op a group). Pattern reference: Ngoma's `DrumVoice::applyRandomize` 2.5.5.98.
type: feedback
originSessionId: 0063d5be-4d30-4c1e-a7b3-e687f2cae19d
---

## The rule

When a DSP method needs to do "do A, B, C unconditionally and D, E only if some-runtime-flag," do NOT express the conditional as control flow:

```cpp
// CRASHES on am335x (verified Ngoma 2.5.5.92, .93, .95, .96, .97):
void applyRandomize() {
  int target = ...;                                 // float-derived
  switch (target) {
    case 0: doA(); doB(); doC(); doD(); doE(); break;
    case 1: doA(); doB(); doC(); doD(); break;
    case 2: doA(); doB(); doC(); break;
    case 3: doA(); break;
  }
}

// Also crashes (if-chain variant):
if (target <= 2) { doD(); doE(); }
if (target <= 1) { doF(); }

// Also crashes (fn-ptr table variant):
static const ApplyFn table[4] = { &applyAll, &applyB, &applyC, &applyD };
(this->*table[target])();
```

Instead, write **one method with a fixed-shape body** and use **branchless arithmetic masks** to "no-op" the calls that should not take effect:

```cpp
// LOADS on am335x (Ngoma 2.5.5.98):
void applyRandomize() {
  int target = ...;
  float depth = ...;
  float spread = ...;

  // Group masks via ternary -> CMP+MOVCC on Cortex-A8, not branches.
  bool envOn   = (target <= 2);
  bool sweepOn = (target == 0);

  float depthEnv   = envOn   ? depth  : 0.0f;
  float spreadEnv  = envOn   ? spread : 0.0f;
  float depthSweep = sweepOn ? depth  : 0.0f;
  float spreadSweep= sweepOn ? spread : 0.0f;

  doRnd(p1, mn, mx, depth,      spread);       // always
  doRnd(p2, mn, mx, depthEnv,   spreadEnv);    // tiered
  doRnd(p3, mn, mx, depthSweep, spreadSweep);  // tiered
}
```

The randomizer helper must collapse cleanly when `depth=0 AND spread=0`:
- `center = spread*(mn+mx)*0.5 + (1-spread)*cur = cur`  (since spread=0)
- `dev = depth * range * 0.5 = 0`  (since depth=0)
- `v = center + r*dev = cur`  → unchanged

If you only mask depth, the spread term still pulls the value toward range midpoint. **Mask both.**

## Why this rule exists

Ngoma 2.5.5.92 → 2.5.5.97 was a 7-version bisect that tried every C++ topology for "switch on a tier value with different bodies per case" and all crashed Cortex-A8 hardware on fresh insert (loads instantly in x86_64 emu). 2.5.5.94 with switch+identical bodies loaded — that was the breakthrough datapoint that proved switch itself is fine, but switch with **differential** case bodies is poisoned for this unit specifically.

The root cause is not fully understood. Hypotheses (none confirmed):
- Compiler codegen variance under `-O3 -ffast-math` for differential-body cases produces machine code that the Cortex-A8 prefetcher / branch predictor doesn't tolerate when this specific unit's footprint is loaded
- DrumCubeGraphic continuously polls `op:getCharacter/Shape/Grit` every draw frame, which is not a pattern present in Pecto/Petrichor/MultitapDelay (where switch+differential works fine); concurrent reads while applyRandomize is executing different code paths may interact badly with cache lines
- Non-default `Gain` on adapters in the bias-pointer set (Sweep=72, SweepTime=0.5, Decay=2, Hold=0.5, Attack=0.05) introduces tied-parameter recomputation at different tick boundaries
- Function-local `static const` member-function-pointer arrays use `__cxa_guard_acquire` that may misbehave on this firmware's ABI

Pecto and Petrichor use switch with differential bodies and load fine. **What's different about Ngoma is at least one of the three above factors** — but identifying which precisely would require disassembly comparisons we have not done.

## Corollary — adding heavy work to one branch of a previously-OK switch can re-trigger the bug

Discovered Ngoma 2.5.5.115. The `s.envPhase` switch in DrumVoice's `process()` had been there forever — small case bodies (cases 0/1/2/3 for idle/attack/hold/decay), and never crashed. When per-partial NEON env decay (one `vld1q_f32 + vmulq_f32 + vst1q_f32` triplet) was inserted into `case 3:` (the decay branch only — the other cases didn't get it), the unit froze on hardware again. Loading fine in emu.

The fix (2.5.5.116): pull the NEON quad **outside the switch** and run it **unconditionally** every sample. Math stays safe because:
- In idle phase (envPhase 0), envelopes are 0 from env-end reset; `0 * coeff = 0`.
- In attack/hold (envPhase 1/2), envelopes have been reset to 1.0 at trigger; decaying them immediately is correct (real drum partials begin decaying from the strike, no attack on partials).
- In decay (envPhase 3), this is the work we wanted anyway.

**Rule**: when adding new per-sample work to an existing switch, ask "is this work specifically only for this case?" If not — and it usually isn't — pull it out. Even if the existing switch has worked for years on hardware, adding heavy work (especially NEON ops or function calls) to ONE case can imbalance the case bodies enough to retrip the codegen pattern that caused .92–.97 crashes.

**Practical canary**: if a working unit suddenly freezes after you added work to one branch of a switch (or one if-block of an if-chain) that previously loaded fine, suspect this immediately. Move the work outside the conditional and re-test.

## How to apply

When porting a randomization / dispatch / "do subset of work based on runtime tier" feature into a unit:

1. **Default to a single method with a flat-shape body.** Always do every operation in the same order. Always.
2. **Encode tier behavior via arithmetic masks**: multiply gain/depth/intensity by 0 to no-op an operation. Avoid making the operation itself conditional.
3. **Make the masking branchless**: ternaries (`?:`) that produce floats compile to CMP+MOVCC on ARM and don't introduce control flow. Plain `if` blocks do introduce control flow and may trip this same hazard.
4. **Verify the helper handles the masked case as a true no-op.** If the math has a "shift toward center" term (Ngoma's randomizeValue uses `spread`), masking only `depth` is not enough; you need to mask every coefficient that affects the output.
5. If you MUST have switch-with-differential-bodies, **first confirm that the unit you're modifying does not have a heavy graphic polling its targeted params at draw rate**. If it does, default to the masking pattern; the switch will likely crash hardware even though it works in emu.

This is a hardware-only failure mode. The emulator will run the broken code fine. Don't validate this kind of refactor in emu only — always test on hardware before claiming the topology works.

## Related

- `feedback_identical_means_identical.md` — the Ngoma 2.5.5.6→.8 bisect series; same lesson about why "structurally similar" patterns can diverge on hardware
- `feedback_package_trig_lut.md` — sinf/cosf miscompute on package .so on hardware where x86 emu is fine
- `feedback_package_version_bump.md` — version-bump discipline, which made the bisect possible by forcing re-extraction of every probe
- `feedback_neon_delay_gather.md` — also hardware-only behavior that emu doesn't surface
