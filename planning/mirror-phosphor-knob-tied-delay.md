# Mirror — phosphor viz Mirror-knob-tied delay

Planning doc for distributing the phosphor viz's visual variation
evenly across the Mirror knob's travel. Builds on the phosphor viz
landed at 2.7.1.37.

**Goal:** the viz currently shows mostly X-shaped degenerate
trajectories until Mirror is in its upper third (where the crusher's
discontinuities decorrelate adjacent samples). Tie the phase-space
delay inversely to the Mirror knob so even smooth content (low
Mirror) produces a 2D phase portrait, while heavily crushed content
(high Mirror) gets a tight phase space because the crusher already
provides decorrelation.

---

## Why this works

Phase space `x[n]` vs `x[n+Δ]` for delay Δ:
- Δ small + smooth signal → adjacent samples nearly equal → trajectory
  near the diagonal → Y-axis reflection produces an X
- Δ large + smooth signal → samples decorrelated → trajectory uses
  more of the plot → 2D figure
- Δ small + discontinuous signal (high Mirror) → already varied → 2D
  figure regardless of Δ
- Δ large + discontinuous signal → could become noise (overshooting
  the decorrelation length of the signal)

So: **inverse-knob mapping puts each Mirror setting at the right Δ
for its content character**. Mirror = 0 needs Δ = 16 (decorrelate the
smooth wavetable envelope). Mirror = 1 needs Δ = 1 (tight phase
space; crusher decorrelation does the rest).

## Mapping function

Linear inverse for v1:

```cpp
// Mirror knob (0..1) → phase space delay (16..1)
int delay = (int)(16.0f * (1.0f - mirrorKnob) + 0.5f);
if (delay < 1)  delay = 1;
if (delay > 16) delay = 16;
```

| knob | delay |
|---|---|
| 0.0 | 16 |
| 0.25 | 12 |
| 0.5 | 8 |
| 0.75 | 4 |
| 1.0 | 1 |

Smooth linear ramp. Discrete integer steps will produce subtle
visual transitions; phosphor decay should mask them.

If audition reveals abrupt visual changes at integer-delay
boundaries, upgrade to a 2-delay blend (plot at floor and ceil
delays with fractional intensity). Defer; probably not needed.

## C++ exposure

Add to `Mirror.h` (SWIG-visible getter alongside the other public
getters):

```cpp
float getMirrorKnob();
```

Implemented in `Mirror.cpp`:

```cpp
float Mirror::getMirrorKnob()
{
  return CLAMP(0.0f, 1.0f, mMirror.value());
}
```

## Graphic changes

In `MirrorPhosphorGraphic::draw`, replace the fixed delay with the
dynamic one:

```cpp
// Compute knob-tied phase space delay (16 at Mirror=0, 1 at Mirror=1).
float k = mpMirror->getMirrorKnob();
int delay = (int)(16.0f * (1.0f - k) + 0.5f);
if (delay < 1)  delay = 1;
if (delay > 16) delay = 16;

// Plot loop bounded so i + delay <= 255.
int plotN = 256 - delay;
for (int i = 0; i < plotN; i++) {
  float s0 = mpMirror->getOutputSample(i);
  float s1 = mpMirror->getOutputSample(i + delay);
  // ... (same as current)
}
```

Loop bound stays adequate — at delay=16, we plot 240 pairs (was 255
at delay=1). Visual density barely changes.

Auto-scale loop unchanged (still uses all 256 samples).

## Edge cases

1. **Knob changes mid-frame**: the delay is sampled once at the top
   of draw(), so all plots in one frame use the same delay. Knob
   transition produces a single-frame discontinuity that the
   phosphor decay smooths over ~218 ms.
2. **Ring buffer wraps**: `getOutputSample(idx)` already handles
   wrap (`(mpInternal->ringPos + idx) & 255`). Delay doesn't change
   this.
3. **Mirror knob CV-modulated at audio rate**: getMirrorKnob()
   returns the block-rate snapshot. Visual response to CV
   modulation will be at ~55 Hz cap from the draw call rate, not
   audio rate. Fine.
4. **Decimation rate interaction**: ring buffer is decimated based
   on Formant. Higher Formant → finer decimation → 16-sample delay
   in ring corresponds to fewer audio samples. The phase-space
   "phase" interpretation in audio terms scales accordingly. This
   is fine — the visual still works as a decorrelator at any pitch.

## Audition expectations

| Mirror knob | Expected viz character |
|---|---|
| 0.0 | Smooth ellipse / 2D figure (delay 16 decorrelates) — not X-shape |
| 0.25 | Slightly more open figure, mild crusher grit visible (delay 12) |
| 0.5 | Mid-character figure with both crusher edges and phase decorrelation (delay 8) |
| 0.75 | Crusher-dominated jagged trajectory, tighter phase space (delay 4) |
| 1.0 | Sharp ring-mod pattern, tight phase space (delay 1) |

Every knob position should produce visually-distinct content. No
more "X shape unless upper third."

## Implementation steps

1. Add `getMirrorKnob` to Mirror.h declaration and Mirror.cpp body
2. Update MirrorPhosphorGraphic::draw to compute dynamic delay and
   use it in the plot loop
3. Bump PKGVERSION 2.7.1.37 → 2.7.1.38
4. Build both arches; verify draw still NEON-hint-clean
5. Install linux; audition the Mirror knob sweep

## Risks

1. **Visual jumps at integer-delay boundaries**: 16 → 15 → ... → 1
   stepping might be visible. Phosphor decay mitigates. If
   objectionable, add 2-delay blend.
2. **Delay-16 too long at high Formant**: at Formant ≈ 4 kHz,
   ring decimation is finer, delay=16 might wrap the entire
   envelope — visual could be noisy. Unlikely problematic in
   audition; the wavetable still has structure within one cycle.
3. **CV-modulating Mirror produces flickering delay**: knob jitter
   at audio rate gets quantized to integer delay each block,
   block-rate flicker if knob hovers between two integer delays.
   Phosphor decay smooths. If visible, snap delay to nearest with
   small hysteresis.

## Out of scope

- 2-delay blend (v1.5 if needed)
- Velocity-vs-position phase space alternative (different planning doc)
- Knob-tied tuning for other parameters (phosphor decay, brightness)
- Stereo-aware variant (still deferred)
