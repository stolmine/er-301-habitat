# Network cascade FDN rebuild — postmortem

Status: **abandoned 2026-05-12 at spreadsheet 2.6.1.84 / Catchall 0.3.74**. Reverting audio engine to pre-cascade state (commit `1eb636f`, the last viz-only commit before `6a8abf0` started Phase A audio scaffolding).

## What we set out to do

The original Network unit was a star-multitap delay: 64 taps reading one shared int16 buffer, sign-randomized feedback summed into a single shared write head. Visually it read as a plexus, but the audio engine was a parallel multitap. We wanted to convert it to a true plexus topology — a serial cascade where signal flowed through groups of taps in distance order, with proper FDN cross-feed between groups. Plan in `planning/network-cascade-rebuild-plan.md`.

## What we tried, in order

1. **Per-group sub-windows + cascade chain** (2.6.1.59-65): each group has its own delay-buffer slice, signal flows group 0 → 1 → ... → 15. Phase A scaffolding.
2. **Rank-1 pool injection** (2.6.1.67-72): tail-third groups summed to one pool, allpass-diffused, injected into group 0. Sounded like a resonator — collapsing all cross-feed into one dominant eigenvalue.
3. **Hadamard 16×16 FDN cross-feed** (2.6.1.73): replaced rank-1 pool with full-rank Hadamard mixing. Sounded structurally like an FDN.
4. **Jot per-line T60 attenuation, several formulations** (2.6.1.74-82): per-sample, per-round-trip, with D_avg, with √N corrections. Couldn't get T60 control to track the decay knob reliably across densities.
5. **Single-tap FDN state + feedback budget** (2.6.1.79): per-line state from longest tap (slot 3) to avoid comb peaks; local-fb gain budgeted against FDN.
6. **Jot LP damping on FDN feedback path** (2.6.1.80): per-group LP filter inside the FDN loop to prevent HF accumulation.
7. **conn × decay → T60_eff, dynamic FWHT size aGm** (2.6.1.81-82): conn became master fb amount; sub-Hadamard sized to floor power-of-2 of active groups to preserve energy.
8. **Size² mapping in T60** (2.6.1.83): hack to break size-cancellation in the Jot D_avg math.
9. **Fixed kFdnGain = 0.25** (2.6.1.84): final attempt — drop the 1/√aGm normalization so density UP gave more lushness via the FWHT's √N RMS amplification on the un-normalized matrix.

User feedback at the end: "density seems to have all the control of actual feedback amounts." Decay still wasn't legible.

## The actual structural problem

A multi-tap delay with cascade-routing is *not* a clean FDN. The standard FDN math assumes:
- N single-tap delay lines (each line has one output)
- Matrix applied **per round-trip** (when signal completes one cycle through the delay)
- State vector continuously non-zero across all N lines

Our cascade had:
- 16 groups × 4 taps each (multi-tap output per group)
- Matrix applied **per sample** (FWHT in the per-sample loop)
- State density varied with active group count

These mismatches compounded:

### Per-sample vs per-round-trip Jot calibration

Jot 1991's `decayCoef[g] = 10^(-3 D_g / (T60 × Fs))` is **per-round-trip**. Applied per-sample in our loop, it collapsed T60 by factor `D_g`. We then "fixed" it with `10^(-3/(T60 × Fs))` (per-sample form), which was correct for dense state but gave T60 ≈ T60_target × D_g for sparse state (low density), so decay barely worked at full density but produced runaway-feeling near-lossless behavior at low density. We never found a single formula that gave correct T60 across the density range — the right formula depends on whether state is dense or sparse.

### Multi-tap output |H_g(jω)| comb peaks

Using `groupMono/4` (mean of 4 tap reads) as FDN state gave `|H_g(jω)|` varying from 0 to 1 with comb peaks. The Jot loop math wants `|H_g(jω)| = 1` at all frequencies for clean unitary feedback. Comb peaks meant peak-frequency loop gain could exceed 1 in the linear region (= unstable, tanh-bounded but hot). We switched to single-tap state (slot 3) to fix this — but then "multi-tap" was only for the wet bus, not the FDN, and the structural distinction between "early reflections" and "late tail" became ambiguous.

### Density × Hadamard normalization entanglement

Standard FDN uses `1/√N` matrix normalization → per-line gain scales as `1/√N`. With dynamic `aGm = floor_pow2(aG)`, per-line FDN gain went UP as density went DOWN. Combined with multi-tap wet bus (more taps at high density), this produced the inverse of user expectation: lushness at LOW density, thinness at HIGH density. We tried `aGm`-dependent normalization, fixed normalization, energy-conserving compensation — each fix exposed another asymmetry.

### Size × density × T60 cancellation

When `D_avg` scales linearly with size and `T60_eff` is independent of size, the Jot exponent `D_avg / (T60 × Fs)` becomes size-invariant — meaning low size with full settings had near-lossless per-round-trip gain, producing spikes. The `size²` mapping in T60 was a hack to break this cancellation, but it then handicapped small-size ringing that should have been desirable. The cancellation is intrinsic to the math; only by reshaping the FDN structure does it go away.

## Lessons

### Don't retrofit FDN onto a multi-tap cascade

The clean approach is to start with a working FDN structure (yrn1's 4-line topology, Mverb, gverb, Schroeder/Jot reference designs) and layer the visualization on top. The multi-tap cascade structure carries its own mathematical baggage (per-tap H_g comb peaks, sparse vs dense state regimes, per-sample matrix application timing) that fights the FDN math in non-obvious ways.

### Test T60 numerically, not just by ear

We spent many iterations chasing "decay still doesn't work" by audition. A simple impulse response test rig (feed an impulse, measure when output crosses -60 dB, compare to T60_target) would have made the per-sample-vs-per-round-trip math error obvious immediately. Build that next time.

### Per-sample vs per-round-trip is a non-trivial choice

In a delay-line FDN, the matrix application happens once per round-trip per signal cycle. Whether you implement that as "matrix per sample applied to continuous-state vector" or "matrix triggered at delay-line round-trip boundaries" changes the math substantially. The first works for fully-dense state; the second works for sparse state. The cascade with varying density crossed regimes mid-session.

### Multi-tap and FDN-feedback want to be different signals

`|H_g(jω)|` for multi-tap is ≠ 1, so multi-tap is a *coloration* mechanism, not a *unitary feedback line* mechanism. Real FDN reverbs separate "early reflections" (multi-tap delay with no feedback) from "late tail" (single-tap-per-line FDN). Trying to make one signal serve both led to comb peaks contaminating the feedback eigenvalues.

### The √N matrix scaling has a perceptual sign

Standard FDN normalization (1/√N per line) gives constant *total* feedback energy regardless of N. But human expectation for a "density" knob is the opposite: more lines should give more apparent feedback. This isn't a bug — it's a mismatch between math convention and UX expectation. Either commit to "density is mode count, conn is feedback amount" (orthogonal) or pick a non-standard normalization that gives the UX-intuitive scaling.

### Stable architectural reset > incremental fixes

We made ~20 commits trying to fix incremental issues. Each fix exposed a new asymmetry. The signs of "going in circles" were:
- Reverting changes from 2 commits ago to fix issues introduced by the last commit
- Math derivations that gave contradictory results depending on assumptions
- Adding parameter-coupling hacks (size² in T60) to break symmetries the math itself produced

At those points, the right move was to stop and reach for a known-working reference design rather than continuing to patch. We should have called this several commits earlier.

## Where we could still improve (if revisited)

1. **Use a known-good FDN as the base.** Mverb (Jon Christopher Nelson, CC-BY-SA), yrn1's FDN.lua (BSD-2-Clause, Tom Erbe topology), or gverb (LGPL). Start from a working reverb, then add the visualization layer.

2. **Separate early reflections from late tail.** Keep the 64-tap multi-tap reads as a parallel-multitap *early reflections* unit, audible directly. Add a separate single-tap FDN late-tail processor with its own controls. Conn could mix between them or scale the late tail.

3. **Build a T60 measurement test rig.** Feed an impulse at the start of each block, measure output decay, log T60. Catches math errors that aren't audible at moderate settings.

4. **Decouple parameter math.** Choose a fixed FDN structure (e.g., 4 or 8 fixed lines) and have density/conn/decay control orthogonal aspects of it. Don't tie matrix dimension to user-facing knobs.

5. **Per-line frequency-dependent damping (true Jot).** Each line gets its own damping filter calibrated so all lines have same T60 at all frequencies. Requires careful filter design but gives natural-sounding tails without our LP-cutoff hack.

6. **Accept that the "plexus" structural identity may not be musically necessary.** v1 of Network was a working and fun parallel-multitap effect. The plexus-cascade rebuild was an aesthetic alignment with the visualization, not a sound-design improvement the user was asking for. Sometimes the right answer is "the v1 is good; the visualization can lie."

## Files touched and reverted

- `mods/spreadsheet/Network.h` — restored to commit `1eb636f`
- `mods/spreadsheet/network/geometry.h` — restored (revert removes `recomputeCascadeTaps`)
- `mods/spreadsheet/mod.mk` — version bump only
- `planning/network-cascade-rebuild-plan.md` — kept (historical record)
- `planning/network-design-notes.md` — kept (historical record)

Cascade commits in history: `6a8abf0` through `6d9ab6c`. Plan and addendum commits: `acf9f50`, `e1062db`. All preserved.
