---
name: Use 0.5 (not 0.0) as the gate threshold for Comparator-driven C++ inlets
description: When detecting gate / trigger rising edges from an `app.Comparator()` Out wired to a C++ inlet, threshold the buffer with `> 0.5f`, not `> 0.0f`. Loose 0.0 thresholds trip on uninitialized buffer fuzz, comparator hysteresis tails, or DC-offset-shaped gate residue, producing rapid false triggers that can hang the device when the rising-edge handler does nontrivial work.
type: feedback
originSessionId: 0063d5be-4d30-4c1e-a7b3-e687f2cae19d
---
## The rule

In C++ DSP code that takes a `Comparator` (or any gate-shaped) signal via an `od::Inlet` and detects rising edges:

```cpp
// CORRECT: threshold against the midpoint of a 0/1 gate.
bool high = inlet.buffer()[i] > 0.5f;
bool rise = high && !mWasHigh;
mWasHigh = high;
if (rise) doSomething();
```

Not:

```cpp
// WRONG: trips on fuzz / residue / DC-offset.
bool high = inlet.buffer()[i] > 0.0f;
```

Pecto, Larets, MultitapDelay, Helicase, and every other in-tree consumer of a Comparator-driven trigger uses the 0.5f convention.

## Why 0.0 bites

A `Comparator` in `setTriggerMode()` emits clean 0/1 transitions in steady state, but the buffer underneath can carry:
- Uninitialized contents on the first process call after construction (framework allocates from a pool; zero-init isn't guaranteed in all paths).
- Tail samples of a previous trigger pulse decaying through filter / hysteresis state (small positive values < 0.5).
- DC offset bleed from a downstream signal path that was patched then unpatched.
- Float fuzz around zero from chain summation in the `Comparator`'s upstream branch.

Any of those produces samples in `(0, 0.5)`. A `> 0.0f` threshold reads them as "high" — which by itself is fine, but combined with the rising-edge state machine the result is a thicket of false rises whenever the value oscillates around zero.

## Why this can hang the device

Spurious rising edges fire whatever the rising-edge handler does. If the handler is cheap (set a flag, increment a counter), the symptom is just "the unit randomizes too often" or "the sequencer steps when it shouldn't." If the handler does anything substantial — `applyRandomize()` writing to N ParameterAdapter biases via `hardSet`, recompute of cached state, allocation — the cost compounds:

- Buffer contains noise around zero → many false rises per block (~30/block worst case at 64-sample blocks).
- 750 blocks per second at 48k SR → tens of thousands of fires per second.
- Each fire executes `applyRandomize` with N hardSets (Pecto: 7, Ngoma: 14).
- Audio thread starves trying to keep up; the framework's parameter-write path takes locks under contention.
- Device hangs (audio thread can no longer finish a block before the next deadline).

Observed on Ngoma 2.5.4–2.5.5 on am335x. The handler was `applyRandomize()` writing 14 biases. Switching the threshold from `0.0f` to `0.5f` likely fixed the hang. Tracked under `mods/spreadsheet/DrumVoice.cpp` xform-gate detection (commit `573aee9`).

## How to apply

When writing or reviewing C++ code that consumes a Comparator gate:

1. Check the threshold literal — must be `0.5f` (or any value cleanly inside `(0, 1)` like `0.1f`, but 0.5 is the convention).
2. Check the corresponding Lua wiring uses `app.Comparator()` with `setTriggerMode()` or `setGateMode()` — not a raw signal that could carry arbitrary float ranges.
3. If the threshold inadvertently catches a non-Comparator signal (e.g. a CV input directly), document why and consider hysteresis.

If you observe device hangs on a unit that does anything in a rising-edge handler, the gate threshold is the first thing to check.
