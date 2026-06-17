# Mirror — Feedback ply (Mirror output → envelope phase)

Planning doc for the v1 Feedback control and the v1.5 matrix
expansion. Builds on the wavetable engine + AW crusher landed at
2.7.1.28.

**Status:** design (2026-06-17). Single-destination implementation
first; sub-display matrix queued for v1.5.

**Motivation:** Mirror's output is already a rich, user-controlled
modulation source (envelope content at low knob, harmonically
dense and aliased at high knob). Self-modulating that signal back
into the architecture creates an emergent feedback loop where the
aliasing intensity feeds the aliasing source. Paradigm-coherent
self-reference.

---

## v1 — single destination: envelope phase

Mirror's post-crusher output is routed back as audio-rate FM on
the envelope phase accumulator. The one-sample delay (storing
previous sample's `folded` in Internal state) breaks the algebraic
loop.

### Inner loop change

Current envelope phase advance:
```cpp
float modScale = expf(modOut * modDepth * 3.0f);
float envInc = formantFreqTarget * invSr * modScale;
s.envPhase += envInc;
```

Updated:
```cpp
float modScale = expf(modOut * modDepth * 3.0f);
// Feedback: previous-sample Mirror output exponentially scales
// envelope rate. Same log-symmetric pattern as modScale.
float fbScale = expf(s.prevMirrorFeedback * feedbackDepth * 2.0f);
float envInc  = formantFreqTarget * invSr * modScale * fbScale;
s.envPhase += envInc;

// ... wavetableLookup, mirror.tick ...
float folded = s.mirror.tick(clean);

// Store for next sample's feedback
s.prevMirrorFeedback = folded;
```

### Scaling rationale

- **Exponential scaling** matches Mod Depth's pattern (always positive,
  log-symmetric). Keeps `envInc` from going zero or negative.
- **2.0 multiplier** = ±2 octaves at full depth (Mod Depth uses 3.0 =
  ±3 octaves). Feedback is slightly less aggressive because the
  source signal (`folded`) is already broader-spectrum than `modOut`
  (a pure sine). User controls intensity via the Feedback knob.
- **Post-Mirror tap**: feedback uses the actual aliased output
  (post-crusher) rather than the pre-Mirror clean envelope. This
  is the paradigm-coherent choice — alias content feeds itself.

### New state

```cpp
struct Mirror::Internal {
  // ... existing fields ...
  float prevMirrorFeedback;
};

void Init() {
  // ... existing inits ...
  prevMirrorFeedback = 0.0f;
}
```

### New parameter

```cpp
od::Parameter mFeedback{"Feedback", 0.0f};
```

Range 0..1, default 0 (no feedback). ParameterAdapter accepts CV
via the standard Lua wiring pattern.

### New Lua ply

```lua
local feedback = self:addObject("feedback", app.ParameterAdapter())
feedback:hardSet("Bias", 0.0)
tie(op, "Feedback", feedback, "Out")
self:addMonoBranch("feedback", feedback, "In", feedback, "Out")
```

ViewControl is a standard GainBias with the 0..1 map.

### Audition checklist

| Feedback | Mirror low | Mirror mid | Mirror high |
|---|---|---|---|
| 0.0 | clean (current behavior) | crushed (current behavior) | brutal (current behavior) |
| 0.3 | subtle inharmonic shimmer on envelope | crushed + cascading harmonics | brutal + chaotic emergence |
| 0.7 | strong FM character, sidebands | dense inharmonic structure | runaway / wild |
| 1.0 | full self-modulation | likely noise-leaning | full chaotic regime |

The interesting region for performance: Feedback ~0.3-0.7 with
Mirror 0.5-0.85. Pushing both to 1.0 is the noise-source territory.

### CPU impact

Per sample: 1 `expf`, 1 mul, 1 store. ~5 ops. Negligible.

### Risks

1. **Instability at extreme settings.** High Feedback × high Mirror
   could produce output that exceeds the soft-clip limiter
   regularly, perceived as constant clipping. The pseudoSaturate15
   final-stage limiter (existing) should cap this; verify.
2. **DC bias drift.** Feedback into envInc means envelope rate
   wanders — over long timescales the average rate could drift
   away from nominal. The exponential scaling keeps the geometric
   mean of fbScale at exp(0)=1 since `folded` averages near zero
   for symmetric content, but bit-quantized content may have DC
   that breaks this assumption. Watch for slow pitch drift in
   audition.
3. **k=0 must be perfectly transparent.** At Feedback=0:
   `expf(0)=1`, `fbScale=1`, no audible difference. Verify via
   A/B with 2.7.1.28.
4. **Algebraic loop concern.** The 1-sample delay
   (`prevMirrorFeedback`) properly breaks the loop. Confirm by
   tracing: envInc uses prev-sample Mirror output, NOT current.

---

## v1.5 — sub-display matrix (3 destinations)

When the v1 single-destination Feedback is auditioned and feels
right, expand to a 3-destination matrix on the Feedback knob's
shift-toggle sub-display.

### Destinations

1. **Phase** (current v1) — FM on envelope rate. Characterful, the
   primary "self-modulation" sound.
2. **Shape** — Mirror output drives wavetable frame position.
   Audio-rate frame scanning. Casio-CZ-style phase-distortion
   character emerges; the wavetable scan rate is determined by
   Mirror's spectrum.
3. **Mod** — Mirror output perturbs mod oscillator phase.
   Sync-timing jitter. V/Oct tracking suffers at high depth →
   percussive / textural character vs. playable instrument.

### Sub-display UI

The Feedback knob top-level is the **master depth**. Shift-toggle
reveals a sub-display with three GainBias-style readouts:

```
┌──────────────────────────────────────┐
│  [Phase]    [Shape]    [Mod]         │
│   0.50       0.20       0.00          │  ← per-destination depth
│  ─────      ─────      ─────          │
│ shift-press knob to focus a readout   │
└──────────────────────────────────────┘
```

Each per-destination depth is a multiplier on the master Feedback
knob value. At master=1.0 with all three at 0.33, modulation
energy is balanced across the three; at master=1.0 with Phase=1.0
and the others at 0, behavior matches v1.

Pattern matches `feedback_parammode_convention` (canonical shift-
toggle controls in spreadsheet package — paramMode flag,
persistence across cursor leave, ShiftHelpers keyboard).

### Implementation sketch

```cpp
od::Parameter mFeedback{"Feedback", 0.0f};        // master
od::Parameter mFbPhase{"FbPhase", 1.0f};          // depth-on-phase
od::Parameter mFbShape{"FbShape", 0.0f};          // depth-on-shape
od::Parameter mFbMod{"FbMod", 0.0f};              // depth-on-mod
```

Inner loop:
```cpp
float master = mFeedback.value();
float dPhase = master * mFbPhase.value();
float dShape = master * mFbShape.value();
float dMod   = master * mFbMod.value();

// Phase destination (as in v1)
float fbScale = expf(s.prevMirrorFeedback * dPhase * 2.0f);
envInc *= fbScale;

// Shape destination
float effectiveShape = shape + s.prevMirrorFeedback * dShape * 0.3f;
// (clamp 0..1)

// Mod destination
modPhaseJitter = s.prevMirrorFeedback * dMod * 0.05f;
s.modPhase += modInc + modPhaseJitter;
```

UI work: add the sub-display matching the Mirror reset pattern
(see `MirrorResetControl.lua` or similar). Defer until v1
auditioned.

### Why defer to v1.5

- v1 single-destination is the minimum viable experiment
- Audition will tell us whether Phase is the right primary
  destination, or if Shape/Mod is more compelling as the v1 default
- Matrix UI is non-trivial; only worth building if Feedback proves
  itself

---

## Phased implementation

Single phase for v1, all atomic:

1. Add `mFeedback` parameter to `Mirror.h`
2. Add `prevMirrorFeedback` to `Internal`, initialize 0
3. Inject `fbScale` into the envelope phase advance in
   `Mirror::process()`
4. Update `prevMirrorFeedback` after `mirror.tick()` in the
   same inner loop iteration
5. Lua wrapper: add Feedback ply (object + view + branch + expanded
   layout)
6. Bump PKGVERSION 2.7.1.28 → 2.7.1.29
7. Build both arches, install linux
8. Verify Mirror::process still NEON-hint-clean
9. Audition per checklist above

---

## Out of scope

- v1.5 matrix sub-display (deferred to its own work)
- Stereo Phase 3 (still deferred from original design doc)
- Voss-McCartney 1/f drift (still queued; might cross-pollinate with
  Feedback — drift on Feedback depth would be a Thread 3 + Feedback
  combination)
- Custom viz (still Phase 4d of original design doc)

---

## TL;DR

Add a Feedback ply (0..1) that exponentially scales the envelope
phase advance by the previous-sample Mirror output. ±2 octaves at
full depth. Single new parameter + one new Internal state float +
~5 ops/sample. The aliasing intensity now feeds the aliasing
source — paradigm-coherent self-reference. Matrix expansion to 3
destinations (Phase / Shape / Mod) queued for v1.5 sub-display.
