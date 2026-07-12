# Fabula Tank Density — Diagnosis and Redesign Proposal

Status: **proposal only** — no code changes. Precedes implementation.
Relates to: `planning/fabula-design.md`, `mods/zaum/atoms/APFTank.h`.

---

## 1. Diagnosis: Why the Tank is Below the Schroeder Threshold

### 1.1 The Schroeder Threshold

Schroeder's criterion for perceptually smooth artificial reverberation is
approximately **1000 echoes per second** in the impulse response. Below this
rate, discrete repetitions are individually audible: the listener hears a sparse
flutter rather than a continuous noise-like tail. This is the "Schroeder
threshold" referenced in the problem statement.

Source: CCRMA Physical Audio Signal Processing, Schroeder Reverberators chapter —
five series-connected allpass filters achieve ~810 echoes/second, which approaches
but does not reliably exceed the 1000/s goal. Fewer stages fall clearly short.

### 1.2 Current Tank Structure

Per loop, the recirculating path is:

```
tankIn → AP1(N=1087, g=0.70) → D1(7187 smp) → [HF damp] → AP2(N=1471, g=0.50) → D2(5101 smp) → ×g_d → cross-feed
```

Both AP1 and AP2 are **plain Schroeder allpasses** (single-buffer, no nesting).
The four-stage input diffusion chain (229/173/613/449) runs once on the shared
mono sum before the tank, not inside the recirculating path.

### 1.3 Echo Density Estimate for the Current Structure

A plain Schroeder allpass of delay N generates an impulse response whose echo
density starts at 1 echo per N samples and then doubles (approximately) with
each successive allpass stage in series. The relevant analysis is what happens
inside a single loop traversal.

**Per-loop analysis (L loop, Size=0.5, 48 kHz):**

| Stage | Delay (smp) | Duration (ms) | Echoes added per traversal |
|-------|-------------|----------------|---------------------------|
| AP1 (plain, N=1087) | 1087 | 22.6 ms | 1 initial + smear |
| D1 | 7187 | 149.7 ms | 1 tap point |
| AP2 (plain, N=1471) | 1471 | 30.6 ms | 1 initial + smear |
| D2 | 5101 | 106.3 ms | 1 tap point |
| **Round-trip total** | **14846 smp** | **309.3 ms** | |

One complete round trip takes approximately 309 ms. Within a single trip the
allpass chain (AP1 + AP2 in series) generates a diffuse spread, but the
effective initial echo count per loop traversal is low: roughly 2 primary
echoes (one per allpass output smear zone) plus the two delay-line tap points.

**Initial echo density estimate:**

After the first round trip (~309 ms), the count of distinguishable events
entering the ear is roughly 4–6 per trip, spread across 309 ms. That is on
the order of **13–20 echoes per second** from a single loop — far below 1000/s.

The figure-8 cross-feed doubles the effective loop count (L and R each
contribute), so the combined system reaches approximately 25–40 echoes/second
at the start of the tail. Echo density increases with each subsequent round trip
as earlier echoes recirculate and convolve, but the rate of build-up is
determined by how many new echo-paths open per trip. With only 2 plain allpasses
per loop this build-up is very slow: it can take hundreds of milliseconds before
the density crosses 1000/s.

**The result is perceptually exactly what is reported:** the first several hundred
milliseconds of the tail contain clearly audible discrete echoes. The transition
to a smooth noise-like tail happens too late, if at all, at typical Decay settings.

### 1.4 Why the Nesting Was Removed and Why That Matters

The original plan (`fabula-design.md §2`) called for Gardner nested allpasses in
the tank: AP1 outer delay 1087, inner delay 367; AP2 outer delay 1471, inner
delay 491. The inner delay was embedded inside the outer allpass feedback loop.
This nesting is what Lexicon, Alesis, and the Griesinger/Dattorro lineage use
to achieve density beyond what plain series allpasses provide.

In build 0.1.0.2, the nested implementation produced **runaway gain** — the inner
allpass was sharing state with the outer allpass buffer incorrectly, violating the
unity-gain constraint. The fix was to remove nesting entirely and fall back to
plain Schroeder allpasses. This solved the stability problem but eliminated the
primary density mechanism.

**This is the root cause of the discrete-echo report.** The density the nesting
was supposed to provide is now absent. The Brownian modulation smears eigentones
but does not increase echo count; it only makes existing sparse echoes less
tonally coherent, which is insufficient to cross the Schroeder threshold when the
underlying echo count is an order of magnitude too low.

### 1.5 Valhalla / Industry Framing

Valhalla's blog (nested allpass tag) confirms: "putting allpasses inside of
delayed feedback loops is fundamental to the algorithms of Lexicon, Alesis, and
other high-end reverberator manufacturers." The Dattorro tank is explicitly
designed around this principle. Plain Schroeder allpasses in the tank are a
degraded fallback, not the intended architecture.

The density problem is structural, not tunable with existing parameters.

---

## 2. Options

### Option A — Correct Gardner Nested Allpasses (Primary Fix)

**What it is:** Restore the originally-intended nesting in both tank allpasses,
but implement it correctly with **separate, independent inner buffers** and a
signal flow that provably preserves unity gain.

#### The Runaway Failure Mode

The previous implementation shared the outer allpass buffer with the inner delay,
or incorrectly wired the inner feedback path so that it bypassed the all-pass
cancellation condition. The unity-gain property of a Schroeder allpass requires:

```
H(z) = (-g + z^{-N}) / (1 - g * z^{-N})
```

The numerator and denominator must be **mirror-image** — coefficient magnitude
is the same, sign is negated in the forward path. Any deviation (different `g`
values in feedback vs feedforward, missing negation, incorrect delay pointer)
breaks this and leaves residual energy that accumulates.

#### Correct Nested Allpass Signal Flow

A Gardner nested allpass consists of an outer Schroeder allpass of delay N_out
in which the delay line itself is replaced by a shorter inner Schroeder allpass
of delay N_in, where N_in < N_out and N_out = N_in + N_remainder (the inner
delay "consumes" part of the outer delay).

The correct implementation requires **two separate circular buffers**: one outer
buffer of length N_out and one inner buffer of length N_in. They share no memory.

**Signal flow (single nested allpass, outer delay N_out, inner delay N_in,
outer coefficient g_out, inner coefficient g_in):**

Let `x[n]` be the input. Let `vO[n-N_out]` be the delayed sample from the outer
buffer read N_out samples behind the write head. Let `vI[n-N_in]` be the delayed
sample from the inner buffer read N_in samples behind the write head.

```
Step 1 — Outer AP forward node:
    vO_new = x[n] + g_out * vO[n-N_out]

Step 2 — Outer AP output (this is also the inner AP input):
    inner_in = -g_out * vO_new + vO[n-N_out]

Step 3 — Inner AP forward node:
    vI_new = inner_in + g_in * vI[n-N_in]

Step 4 — Inner AP output (this is what enters the delay network after the AP):
    y_nested[n] = -g_in * vI_new + vI[n-N_in]

Step 5 — Write back:
    outer_buf[w_out] = vO_new;   w_out = (w_out + 1) % N_out
    inner_buf[w_in]  = vI_new;   w_in  = (w_in  + 1) % N_in
```

**Output is `y_nested[n]`.**

This is structurally two back-to-back plain Schroeder allpasses in series, where
the inner AP processes the output of the outer AP's feedforward path. The outer
AP's feedforward output `inner_in` is itself an allpass-processed signal; feeding
it into a second allpass doubles the density of impulse-response events.

#### Why This Is Provably Unity-Gain

Each plain Schroeder allpass `H_i(z) = (-g_i + z^{-N_i}) / (1 - g_i z^{-N_i})`
has `|H_i(e^{jω})| = 1` for all ω when `|g_i| < 1`. A cascade of two such
filters has transfer function `H_outer(z) · H_inner(z)`, which also has magnitude
1 everywhere. The unity-gain property holds for any cascade of allpasses with
the mirror-image coefficient structure above.

**The runaway condition cannot occur if:** (1) both `g_out` and `g_in` are
strictly less than 1 in magnitude, (2) each buffer is separate and sized exactly
N_out and N_in respectively, (3) the negation in each feedforward output
(`-g * vNew`) is present and correctly signed.

The most common implementation error is using `+g` in the feedforward path
instead of `-g`, which turns the allpass into a comb filter with positive
feedback — that will run away. The other common error is using the same buffer
for both stages (the "shared buffer" shortcut from the Csound nested form
described by Mikelson), which corrupts state unless the exact Csound read/write
ordering is reproduced precisely. The safest implementation for our codebase is
the two-separate-buffer form shown above.

#### Difference Equations in Full (Ready for Implementation)

For **AP1 in the tank** (outer N_out=1087, inner N_in=367, g_out=0.70, g_in=0.50):

```
// Read delayed samples
double vO_delayed = outerBuf1[w_out1];        // v_out[n - 1087]
double vI_delayed = innerBuf1[w_in1];         // v_in[n - 367]

// Outer forward node
double vO_new = tankIn + g_out1 * vO_delayed; // g_out1 = 0.70

// Inner AP input = outer AP feedforward output
double inner1_in = -g_out1 * vO_new + vO_delayed;

// Inner forward node
double vI_new = inner1_in + g_in1 * vI_delayed; // g_in1 = 0.50

// Nested output
double ap1_out = -g_in1 * vI_new + vI_delayed;

// Write back
outerBuf1[w_out1] = (float)vO_new;
innerBuf1[w_in1]  = (float)vI_new;
w_out1 = (w_out1 + 1 >= N_OUT1) ? 0 : w_out1 + 1;   // N_OUT1 = 1087
w_in1  = (w_in1  + 1 >= N_IN1)  ? 0 : w_in1  + 1;   // N_IN1  = 367
```

For **AP2 in the tank** (outer N_out=1471, inner N_in=491, g_out=0.50, g_in=0.50):

```
// Read delayed samples
double vO_delayed = outerBuf2[w_out2];        // v_out[n - 1471]
double vI_delayed = innerBuf2[w_in2];         // v_in[n - 491]

// Outer forward node
double vO_new = dampedD1 + g_out2 * vO_delayed; // g_out2 = 0.50

// Inner AP input
double inner2_in = -g_out2 * vO_new + vO_delayed;

// Inner forward node
double vI_new = inner2_in + g_in2 * vI_delayed; // g_in2 = 0.50

// Nested output
double ap2_out = -g_in2 * vI_new + vI_delayed;

// Write back
outerBuf2[w_out2] = (float)vO_new;
innerBuf2[w_in2]  = (float)vI_new;
w_out2 = (w_out2 + 1 >= N_OUT2) ? 0 : w_out2 + 1;   // N_OUT2 = 1471
w_in2  = (w_in2  + 1 >= N_IN2)  ? 0 : w_in2  + 1;   // N_IN2  = 491
```

The existing `house::allpassNestedStep` helper can execute each step — it is
literally called twice per AP, once for the outer stage and once for the inner,
using independent `vDelayed` values from independent buffers.

#### Echo Density Improvement

Two plain allpasses in series (the current state) add approximately 2× the echo
density per loop traversal. Two nested allpasses (four total plain APs in
cascade: outer1 → inner1 → outer2 → inner2) approximately double again: the
cascade of 4 effective allpass stages multiplies echo density by roughly 4× per
traversal vs the current 2× — and because the inner delays are shorter (367 and
491 samples vs 1087 and 1471), the initial density events arrive sooner within
each traversal. The first events from the inner APs arrive within ~8–10 ms of
loop entry rather than 22–31 ms. This matters greatly for the perceptual
smoothness of the onset.

Rough new estimate: 4 effective AP stages per loop, both L and R, with the
build-up rate roughly doubling per traversal. Density crosses the 1000/s
threshold within the first 100–150 ms of the tail rather than 300–500 ms.

#### CPU and Memory Cost

**Memory (per loop):**
- Outer AP1 inner buffer: 367 samples × 4 bytes = 1.5 KB
- Outer AP2 inner buffer: 491 samples × 4 bytes = 2.0 KB
- Same for R loop: ×2
- Total new memory: approximately **14 KB** (4 inner buffers of ~4 KB combined)

Existing outer AP buffers (kTA1=1087, kTA2=1471, both L and R) are unchanged.

**CPU cost:** Four additional multiply-add operations per sample per loop (two
for the inner AP forward node, two for the inner AP output). Both loops together:
~8 additional MACs per sample. At 48 kHz on Cortex-A72 this is negligible —
well under 0.01% additional CPU.

**Stability:** Guaranteed by |g_out| < 1 and |g_in| < 1. Existing g_d < 1 cap
and Spiral governors remain unchanged. No new stability risk.

**Structural impact:** Minimal. The change is localized to the two AP steps per
loop. The cross-feed, modulation, damping, and governor logic are untouched.

**Priority: HIGH. This is the primary fix.**

---

### Option B — More Allpass Stages in the Tank Loop

**What it is:** Add one or two additional plain Schroeder allpasses in the tank
recirculating path, e.g. insert a third AP (N≈683, g≈0.50) between D1 and D2.

**Echo density improvement:** Each additional plain AP stage approximately doubles
the impulse-response density at that stage. A third AP in the chain would yield
roughly 4× current density — comparable to Option A in terms of final count, but
with a different timing profile: the extra density arrives later in the loop
(after D1, not split across shorter inner stages).

**Tradeoffs:**
- More memory than inner-buffer nesting if outer delays are long. A new outer
  AP at N=683 adds 2.7 KB per loop side.
- Slightly more CPU: one additional allpassNestedStep call per loop (4 MACs
  per sample, 8 total for both sides). Still negligible.
- Longer loop traversal time (an additional 683 samples = 14.2 ms per trip)
  changes the effective RTT and thus the RT60. The Decay parameter mapping
  needs recalibration.
- **Does not fix the timing of early density** as effectively as nesting: the
  new AP is placed after a long delay (D1 at 7187 samples), so the density
  increase doesn't arrive until late in the traversal.
- Structural change to the loop ordering is visible and would need review.

**Verdict:** A viable supplemental measure but less elegant and less
architecturally faithful to the Dattorro/Griesinger design than Option A.
Best combined with A, not used instead of it.

---

### Option C — Richer Input Diffusion Chain

**What it is:** Increase the number of input diffusion allpass stages from 4 to
6 (adding two stages after ID4, e.g. N=349 and N=233), or increase the input
diffusion coefficients toward 0.85.

**Echo density improvement:** Input diffusion does not directly increase the
recirculating echo density — it only smears the initial transient before it
enters the tank. More diffusion stages improve the **onset** of the tail (making
the first 50 ms smoother) but do not affect the sustained echo density of the
recirculating loops. The Schroeder threshold problem is in the loop, not at the
onset.

**Tradeoffs:**
- Small memory cost (~2–4 KB for two additional input diffusion buffers).
- Negligible CPU increase.
- Increasing input diffusion coefficient too high (> 0.80) can make the onset
  feel diffuse or "washy" even on sharp transients — a perceptual tradeoff.
- Does not address the structural cause of the discrete-echo report.

**Verdict:** A cosmetic improvement to the onset, not a fix for the discrete-echo
problem. Worthwhile as a polish step after Option A, not as a primary fix.

---

### Option D — Additional Echo-Density Tricks from the Literature

Three specific techniques from the sources:

**D1. Modulated input allpass (Valhalla discipline):** Apply slow independent
LFO modulation to the input diffusion allpass delay lengths. This breaks the
periodicity of the input smearing and prevents the onset from having recurring
peaks at multiples of the input AP delays. Already partially addressed by
Brownian modulation on the tank delays; applying a slow random walk to one or
two input AP read lengths would extend this discipline to the onset.

**D2. Allpass loop within the tank (Gardner "allpass ring"):** Rather than a
linear chain AP1→D1→AP2→D2, create a short inner allpass loop within the tank.
Gardner's approach feeds the end of an allpass chain back into its own input with
a single LP in the feedback path. This is structurally more complex than the
figure-8 Dattorro tank and would require significant rearchitecting; it conflicts
with the existing cross-feed topology. Not recommended without a much larger
redesign scope.

**D3. Additional output taps in the wet sum:** Tap intermediate points inside
the loop (e.g. the AP1 output, not just D1 and D2) and add them to the wet
output. This does not increase true echo density in the recirculating signal, but
it increases the number of decorrelated signal paths reaching the output and can
make a sparse tail perceptually smoother. Cost: near zero (just reading an
already-computed value). Downside: possible coloration if the tap points are not
well-separated in time.

**Verdict:** D3 is nearly free and worth adding alongside Option A. D1 is a
modest quality improvement. D2 is a major architectural change, defer.

---

## 3. Recommended Path

### Primary recommendation: Option A (correct nested allpasses) + D3 (extra output taps)

**Step 1 — Implement correct Gardner nesting.**

Add four new float buffers to `APFTank` (inner buffers for AP1 and AP2, both
L and R loops), each independently zeroed in the constructor, with their own
write-head indices. Implement the difference equations from §2A above.

Initialize all four inner buffers to zero in the constructor (same as current
outer buffers). The `allpassNestedStep` helper already handles the per-step
math; call it twice per AP instead of once.

**Step 2 — Add intermediate output taps.**

After Step 1 is implemented, read the `ap1_out` value (the nested AP1 output,
which arrives before D1) into the wet tap sum alongside `d1Read` and `d2Read`.
This requires a minor adjustment to the wet sum in the existing output section.
Weight should be lower than the delay-line taps (e.g., 0.3×ap1_out + 0.35×d1Read
+ 0.35×d2Read) to avoid AP coloring the wet mix.

**Step 3 — Recalibrate Diffusion parameter.**

With nesting active, the effective diffusion at any given coefficient setting
will be higher. The Diffusion parameter control (currently inert, scheduled for
0.1.0.7) should map g_out and g_in independently or together. A reasonable
starting point: Diffusion 0→1 maps g_out linearly from 0.40 to 0.75 and g_in
from 0.30 to 0.55, keeping both sub-0.80 for stability margin.

### Validation Plan

**Numerical (impulse response):**
1. Feed a unit impulse into Fabula with Decay=0.0 (minimum g_d), Mod=0.0,
   Damp=0.0 (flat, no modulation or damping to isolate structure).
2. Record 500 ms of the impulse response.
3. Compute echo density: count the number of local maxima above a threshold
   (-40 dB below the peak) in overlapping 10 ms windows, normalize to events/second.
4. Plot density vs time. Target: exceed 1000 events/s within the first 100 ms
   of the tail, and within 50 ms after Option A is implemented.
5. Compare before and after nesting: the build-up curve should be noticeably
   steeper and achieve the 1000/s threshold at least 2× earlier.

**Perceptual gate:**
1. Feed a single short click (or a dry snare hit).
2. At Decay=0.5, Mix=1.0 (wet only), Mod=0.0: listen for discrete echoes in
   the first 200 ms.
3. Pass condition: no individually audible repetition within the first 150 ms.
   The onset should transition to a smooth noise-like tail before the first
   round-trip time (~310 ms).
4. Sweep Decay 0→1: confirm the tail remains smooth and the onset density
   holds across the Decay range.
5. Sweep Diffusion 0→1 with nesting enabled: confirm the low end (0.0) is still
   acceptably dense (nesting means even low g gives some diffusion) and the
   high end (1.0) does not produce instability or metallic ringing.

**Regression (stability):**
1. Set Decay=1.0 (g_d at cap), feed white noise, run for 30 seconds.
   Confirm no runaway. The Spiral governor should remain inactive (signal should
   never reach its threshold). Any activation of the governor indicates a
   g_in or g_out is misconfigured.
2. Verify that the inner buffer write heads advance independently (check by
   inserting a temporary assert that w_in never equals w_out).

### Postmortem Discipline (so we do not repeat the runaway)

The previous nesting attempt failed because the inner buffer state was not
properly separated. The lessons to bake into the implementation:

1. **Separate buffers, separate write heads.** The inner AP is not a tap of the
   outer AP's buffer. It has its own circular buffer, its own write head, and its
   own zeroing in the constructor. This is non-negotiable.

2. **Verify unity gain before wiring into the tank.** Before connecting the
   nested AP into the recirculating path, test it in isolation: feed a sine
   sweep, measure output magnitude vs frequency. It must be flat (±0.1 dB).
   If it is not flat, the g signs or buffer pointers are wrong.

3. **Ship the outer allpass first, inner second.** In the next sub-phase, add
   only the outer AP changes and verify stability at the audition gate. Then
   add inner AP changes in the following sub-phase. Do not combine both changes.

4. **Coefficient bounds.** g_out and g_in must both satisfy |g| < 1.0.
   The Diffusion parameter must be clamped to produce values in [0.0, 0.95]
   regardless of Diffusion input. Hard-cap both in code, not just in the
   parameter map.

---

## 4. Exact Constants for the Recommended Structure

These are ready for the implementation agent. All values are at 48 kHz.
Buffer sizes are listed as exact allocation sizes (no extra headroom needed —
inner AP buffers are not modulated).

### New Buffers Required

```cpp
// AP1 inner (Gardner nest) — L and R loops, one buffer each
static const int kTA1i = 367;   // inner delay, AP1 (already declared in APFTank.h)
// New arrays:
float mTA1i_L[kTA1i];   // 367 samples, ~1.5 KB
float mTA1i_R[kTA1i];   // 367 samples, ~1.5 KB
int   mWrTA1i_L = 0;
int   mWrTA1i_R = 0;

// AP2 inner (Gardner nest) — L and R loops, one buffer each
static const int kTA2i = 491;   // inner delay, AP2 (already declared in APFTank.h)
// New arrays:
float mTA2i_L[kTA2i];   // 491 samples, ~2.0 KB
float mTA2i_R[kTA2i];   // 491 samples, ~2.0 KB
int   mWrTA2i_L = 0;
int   mWrTA2i_R = 0;
```

Total new memory: 4 × (367 + 491) × 4 bytes = 4 × 858 × 4 = **13.7 KB**.

### Coefficients

```cpp
// Tank AP outer coefficients (unchanged from current plain AP values)
const double gTA1_out = 0.70;   // AP1 outer g
const double gTA2_out = 0.50;   // AP2 outer g

// Tank AP inner coefficients (new; tuned for density without metallic ring)
const double gTA1_in  = 0.50;   // AP1 inner g  (Dattorro recommendation)
const double gTA2_in  = 0.50;   // AP2 inner g
```

When the Diffusion parameter is wired (sub-phase 0.1.0.7 or later):
```cpp
// Example mapping: Diffusion in [0,1]
double diff = (double)diffusionParam;
double gTA_out = 0.40 + diff * (0.75 - 0.40);  // outer: 0.40..0.75
double gTA_in  = 0.30 + diff * (0.55 - 0.30);  // inner: 0.30..0.55
// Hard caps:
if (gTA_out > 0.95) gTA_out = 0.95;
if (gTA_in  > 0.95) gTA_in  = 0.95;
```

### Per-Loop Signal Flow (L loop, AP1 + AP2 with nesting)

```
// AP1_L (nested: outer kTA1=1087, inner kTA1i=367)
double vO1_delayed = (double)mTA1_L[mWrTA1_L];    // outer buffer read
double vI1_delayed = (double)mTA1i_L[mWrTA1i_L];  // inner buffer read
double vO1_new     = tankIn_L + gTA1_out * vO1_delayed;
double inner1_in   = -gTA1_out * vO1_new + vO1_delayed;
double vI1_new     = inner1_in + gTA1_in * vI1_delayed;
double ap1Out_L    = -gTA1_in * vI1_new + vI1_delayed;
mTA1_L[mWrTA1_L]   = (float)vO1_new;
mTA1i_L[mWrTA1i_L] = (float)vI1_new;
mWrTA1_L  = (mWrTA1_L  + 1 >= kTA1)  ? 0 : mWrTA1_L  + 1;
mWrTA1i_L = (mWrTA1i_L + 1 >= kTA1i) ? 0 : mWrTA1i_L + 1;

// ... D1_L read (unchanged, modulated) ...
// ... HF damp (unchanged) ...

// AP2_L (nested: outer kTA2=1471, inner kTA2i=491)
double vO2_delayed = (double)mTA2_L[mWrTA2_L];
double vI2_delayed = (double)mTA2i_L[mWrTA2i_L];
double vO2_new     = dampedD1_L + gTA2_out * vO2_delayed;
double inner2_in   = -gTA2_out * vO2_new + vO2_delayed;
double vI2_new     = inner2_in + gTA2_in * vI2_delayed;
double ap2Out_L    = -gTA2_in * vI2_new + vI2_delayed;
mTA2_L[mWrTA2_L]   = (float)vO2_new;
mTA2i_L[mWrTA2i_L] = (float)vI2_new;
mWrTA2_L  = (mWrTA2_L  + 1 >= kTA2)  ? 0 : mWrTA2_L  + 1;
mWrTA2i_L = (mWrTA2i_L + 1 >= kTA2i) ? 0 : mWrTA2i_L + 1;
```

R loop is symmetric — replace every `_L` suffix with `_R` and use `mTA1i_R`,
`mTA2i_R`, `mWrTA1i_R`, `mWrTA2i_R` with the same constants.

### Wet Tap Update (Option D3)

```cpp
// Current:
double wetL = (d1Read_L + d2Read_L) * 0.5;

// Proposed (with ap1_out as early-onset tap):
double wetL = ap1Out_L * 0.25 + d1Read_L * 0.375 + d2Read_L * 0.375;
```

Weights are a starting point; calibrate by ear. The ap1_out tap is unmodulated
(it comes before D1), so it adds early density without the pitch-wander of the
Brownian-modulated taps.

---

## 5. Summary

| Issue | Root Cause | Fix |
|-------|-----------|-----|
| Discrete echoes in tail | Plain Schroeder APs in tank — 2 effective stages per loop gives ~25 echoes/s initial density vs 1000/s threshold | Restore Gardner nested APs (Option A): 4 effective stages, ~14 KB new memory, ~8 MACs/sample extra |
| Previous nesting ran away | Inner buffer shared with outer buffer, or missing `-g` in feedforward | Two separate buffers per nested AP; verify unity gain in isolation before wiring into loop |
| Brownian mod insufficient alone | Modulation smears eigentones but cannot create echoes that do not exist | Not a density mechanism; remains as lushness layer on top of corrected density |

**The fix is Option A, implemented incrementally: outer AP first (verify stability
at gate), inner AP second (verify density at gate), then D3 tap weighting as
a final polish.**

---

## Sources

- [Dattorro 1997, Effect Design Part 1 (CCRMA)](https://ccrma.stanford.edu/~dattorro/EffectDesignPart1.pdf)
- [Schroeder Reverberators — CCRMA Physical Audio Signal Processing](https://ccrma.stanford.edu/~jos/pasp/Schroeder_Reverberators.html)
- [Nested Allpass Filters — CCRMA PASP](https://ccrma.stanford.edu/~jos/pasp/Nested_Allpass_Filters.html)
- [Implementing the Gardner Reverbs in Csound (Mikelson)](https://www.eumus.edu.uy/eme/ensenanza/electivas/csound/materiales/book_chapters/24mikelson/24mikelson.html)
- [Reverb Part 2 — Natural Rooms with Allpass Rings (Spitzfaden, 2025)](https://reillyspitzfaden.com/posts/2025/09/reverb-part-2/)
- [Valhalla DSP — Nested Allpass tag](https://valhalladsp.wordpress.com/tag/nested-allpass/)
- [Valhalla DSP — Getting Started with Reverb Design Part 2](https://valhalladsp.com/2021/09/22/getting-started-with-reverb-design-part-2-the-foundations/)
- [Dattorro Reverb Improvements — KVR Audio forum](https://www.kvraudio.com/forum/viewtopic.php?t=564078)
