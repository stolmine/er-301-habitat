# Biome expo envelopes: Expo D + Expo AD

Status: planning (2026-07-22). Ledger: `biome-expo-envelopes` (todo). Two small
trigger-fired envelope generators for the biome package, each with a curve /
expo-variation control that morphs the segment contour. Deliberately simple and
cheap - no NEON, trivial per-sample cost.

## Units

- **Expo D** - decay-only. Trigger -> jump to peak (1.0) -> shaped decay to 0.
  Percussion/pluck envelope. Retrigger restarts from peak.
- **Expo AD** - attack-decay. Trigger -> shaped attack 0 -> 1 over Attack time
  -> shaped decay 1 -> 0 over Decay time. Fire-and-forget (gate length ignored;
  it is AD, not AR/ADSR). Retrigger restarts the attack.

Both are transient (one-shot) envelopes fired by a trigger edge, matching the
"AD" percussion-EG idiom, not gated sustain envelopes.

## Signal model (per sample)

A linear phase accumulator runs 0 -> 1 over the active segment's time, then
passes through a variable-curvature transfer function. State machine:

```
IDLE           -> on trigger edge: go to first segment (ATTACK for AD, DECAY for D), phase=0
ATTACK (AD)    -> phase += dt/attackTime; when phase>=1: phase=0, go DECAY
DECAY          -> phase += dt/decayTime;  when phase>=1: go IDLE, out=0
out = level * shape(segmentRamp, curve)
```

- Expo D: DECAY ramp is `1 - phase` (falls 1 -> 0), shaped by Decay Curve.
- Expo AD: ATTACK ramp is `phase` (rises 0 -> 1) shaped by Attack Curve; DECAY
  ramp is `1 - phase` shaped by Decay Curve.

Trigger edge detection: Comparator/gate inlet read with the habitat `> 0.5f`
threshold (NOT `> 0.0f` - see feedback_comparator_gate_threshold; 0.0 trips on
fuzz/DC and can hang the device). Rising edge = fire.

## The curvature morph (the "expo variation" control)

One control per shaped segment. Curve knob `c` in [0,1] (or bipolar [-1,1]):
morphs the ramp contour linear <-> exponential.

Two candidate shaping functions (decide at build; both are cheap, per-sample):

1. **Rational bend (RECOMMENDED - one divide, no libm):**
   `shape(x, k) = x / (1 + k*(1 - x))`, with `k` mapped from the Curve control
   (k=0 -> linear; k>0 -> convex/expo-like; negative k -> concave/log-like).
   Bounded, monotonic, cheap. Bake `k` per block from the control.
2. **Power-law:** `shape(x, p) = x^p`, `p = 2^(control*range)`. More textbook
   "exponential" feel but a per-sample `powf` is expensive on Cortex-A8 - only
   acceptable if `p` is baked per block and `powf` is called at block rate on a
   phase-quantized LUT, or replaced with an exp/log identity. Prefer (1).

Note the ramp fed to `shape()` is always the RISING form 0->1 for attack and the
FALLING form (or `shape(1-phase)`) for decay, so one shaping function serves both;
apply to the segment's normalized progress and orient per segment.

## Controls

**Expo D:** Trigger (inlet), Decay (time), Curve (expo-variation), Level (output
scale, default 1.0). Output.

**Expo AD:** Trigger (inlet), Attack (time), Decay (time), Attack Curve, Decay
Curve, Level. Output. (Separate per-segment curve controls - the user asked for
expo-variation "controls", plural; a single shared Curve is the fallback if the
control count feels heavy.)

Time controls: use a time-unit dial map consistent with other habitat time
controls (reference GatedSlewLimiter / the SDK envelope units for the exact
map + range; typical ~1ms .. several seconds, log-ish taper).

## Conventions to observe

- Mod gain defaults to **0** on every modulatable control (mod-gain-default-zero):
  CV into a Time/Curve/Level inlet does nothing until the user raises its gain.
- Trigger inlet gate threshold `> 0.5f`.
- Bare control descriptions - NO parentheticals
  (feedback_no_parenthetical_descriptions / control-descriptions-drop-parentheticals).
- No third-party branding in names/descriptions.
- Both arches build every build; install linux pkg to `~/.od/rear/`; bump the
  biome 4th dev digit per build.
- am335x: trivial cost (one value/sample), NO NEON, no doubles in the hot path,
  no per-sample libm (that is why the rational bend is preferred). Keep the
  state machine branchless where cheap, but a per-sample envelope with a couple
  of state branches is fine at this cost.

## Build steps

1. C++ atom/object: `ExpoEnvelope.h/.cpp` (or one class parameterized by mode
   AD/D) - Inlet trigger, Outlet, params Attack/Decay/AttackCurve/DecayCurve/
   Level; the state machine + rational-bend shaper above. Consider a single
   shared C++ object with an `mHasAttack` flag so Expo D and Expo AD share code.
2. SWIG bind into biome; add to `mods/biome/biome.cpp.swig` + mod.mk sources.
3. Lua units `ExpoD.lua` / `ExpoAD.lua` (thin), addFader for time/curve/level
   (gain 0 default via the shared helper pattern), trigger inlet.
4. toc.lua entries (category "Envelope"/"Modulation"; check the convention).
5. Both arches, install to emu, audition: decay shape sweeps linear->expo across
   the Curve throw; AD attack+decay fire correctly; retrigger restarts.

## Open questions

1. **AD curve control count** - two (Attack Curve + Decay Curve) vs one shared
   Curve. Plan assumes two; drop to one if the panel feels heavy.
2. **Bipolar curve?** - should the Curve control also reach the concave/log side
   (control center = linear), or only linear->expo? Bipolar is more useful and
   free with the rational bend; recommend bipolar unless it complicates the UI.
3. **Level control** - keep, or fix output at 0..1 and let downstream scale?
   Recommend keeping Level (cheap, common EG feature).
4. **End-of-cycle output?** - an optional EOC/EOR trigger out at segment end is a
   nice-to-have; defer unless wanted (keeps v1 single-output-simple).
5. **Retrigger during decay** - restart from peak (Expo D) / from attack (AD) is
   assumed; confirm vs "continue from current level" (analog-AD behavior).
