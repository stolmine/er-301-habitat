# Mirror — MirrorBlock refactor to AW-style aliasing crusher

Planning doc for refactoring the MirrorBlock stage in the Mirror
unit. Builds on the wavetable engine landed at 2.7.1.27.

**Status:** design (2026-06-17). Replaces the current pure-S&H
MirrorBlock with an Airwindows-pattern cycle-counter wrapped around
multiple alias-compounding stages, all driven from the single
Mirror knob.

**Motivation:** Current MirrorBlock is divider-clocked S&H only.
With the new wavetable envelope as input (intrinsically bandlimited
smooth shapes), S&H is near-transparent at low formant rates — no
above-Nyquist content for it to fold. User reports Mirror has
mild audible effect even at div=64. Need to make Mirror dramatic
regardless of input bandwidth.

---

## Architectural framing

Airwindows undersample pattern is conceptually two pieces bolted
together:

1. **Cycle counter + on-cycle compute** — counter increments per
   sample; DSP body runs only when `counter == 0`. We keep this
   structural pattern.
2. **Bezier-smoothed reconstruction** — between sample points, a
   Catmull-Rom-style cubic interpolates. Anti-aliasing on
   reconstruction. **We invert this** — use harsh reconstruction
   (zero-order hold, or polarity-flip for Nyquist injection).

This is paradigm-coherent. AW uses the structure for warm character
via *subtle* aliasing; Mirror uses the structure for character via
*extreme* aliasing.

---

## Five stages, all driven from one knob

```
input
  │
  ▼
┌─────────────────┐
│ Stage 1: Drive  │  pre-saturation, tanh-shape
│ (pre-sat)       │  → pushes content above the new Nyquist before S&H
└─────────────────┘
  │
  ▼
┌─────────────────┐
│ Stage 2: Cycle  │  counter % divisor == 0 -> "on" sample
│ counter         │  else -> "off" sample (use reconstruction)
└─────────────────┘
  │
  ▼
On-cycle:                Off-cycle:
┌─────────────────┐      ┌─────────────────┐
│ Stage 3: Quant  │      │ Stage 4: Recon  │
│ bit reduction   │      │ ZOH or Nyq-flip │
│ → held = quant  │      │ → from held     │
└─────────────────┘      └─────────────────┘
        │                        │
        └────────────┬───────────┘
                     ▼
                output
```

### Stage 1 — Pre-saturation

Tanh-driven soft clip at the input. Scaled by knob.

```cpp
float driveAmount = preSatFromKnob(k);  // 0.0 at k=0, ~6.0 at k=1
float driven = (driveAmount > 0.01f)
  ? tanhf(in * (1.0f + driveAmount)) * (1.0f + driveAmount * 0.5f)
  : in;
```

At k=0, no drive (passthrough). At k=1, heavy soft clip → odd
harmonics → input bandwidth multiplied → more for downstream
stages to fold.

Cost: one `tanhf` per sample. Cortex-A8 has fast Padé already
available (`fast_tanh` from Helicase/Parfait).

### Stage 2 — Cycle counter

Same as current S&H structure. Increments each sample, wraps at
divisor.

```cpp
counter++;
if (counter >= divisor) counter = 0;
```

### Stage 3 — Quantize + hold (on-cycle only)

When `counter == 0`, capture the pre-saturated input, quantize to
N bit levels, store in `held`:

```cpp
if (counter == 0) {
  float scaled = driven * (float)bitLevels * 0.5f;
  held = floorf(scaled + 0.5f) / ((float)bitLevels * 0.5f);
}
```

(Centered quantization — input range ±1 maps to ±bitLevels/2, so
2-bit means levels at {-1, -0.5, 0, 0.5, 1}.)

Quantization creates harmonics from any input, including DC. This
is the key step for "dramatic at any input bandwidth."

### Stage 4 — Reconstruction (off-cycle)

Two modes blended by the knob:

**ZOH (zero-order hold)** — output = `held`. Standard alias-
preserving recon, dominant for knob < ~0.85.

**Nyquist-flip** — output = `held × (counter & 1 ? -1 : 1)`.
Multiplies alternate samples by −1, synthesizes content at SR/2
(Nyquist itself). Maximum-possible aliasing.

Blended:
```cpp
float flipAmount = nyqFlipFromKnob(k);  // 0 at k<0.85, ramps to 1 at k=1
float sign = ((counter & 1) ? -1.0f : 1.0f);
float flipped = held * sign;
float out = held + (flipped - held) * flipAmount;
```

At knob 0.85, no flip. At knob 1.0, full Nyquist-flip dominant.

---

## Mirror knob → stage parameter mappings

Single knob k ∈ [0, 1] drives all four stages via separate curves:

| Stage | Parameter | Curve | k=0 | k=0.25 | k=0.5 | k=0.75 | k=0.85 | k=1.0 |
|---|---|---|---|---|---|---|---|---|
| Pre-sat | drive | linear ramp from 0 | 0 | 1.5 | 3.0 | 4.5 | 5.1 | 6.0 |
| Counter | divisor | log to 64 | 1 | 3 | 8 | 23 | 32 | 64 |
| Quantize | bit levels | log down from 65536 | 65536 | 4096 | 256 | 32 | 16 | 4 |
| Recon | nyq flip amt | 0 below 0.85, smoothstep up | 0 | 0 | 0 | 0 | 0 | 1 |

This gives a knob that:
- 0 to 0.25: from clean to mild grit (subtle drive + 3:1 SR / 12-bit)
- 0.25 to 0.5: classic 8-bit sampler character (8:1 SR / 8-bit + drive)
- 0.5 to 0.85: crunchy lo-fi crusher (deeper SR/bit reduction)
- 0.85 to 1.0: brutal, Nyquist-flip ramps in for ring-mod-extreme

---

## Implementation

### MirrorBlock refactor

Single struct with all state and knob mapping internalized. The
process() loop just calls `tick(in, knob)`:

```cpp
struct MirrorBlock {
  int   divisor;
  int   counter;
  int   bitLevels;
  float bitScale;        // = bitLevels * 0.5
  float bitInvScale;     // = 1 / (bitLevels * 0.5)
  float held;
  float driveAmount;
  float flipAmount;

  inline void Init() {
    divisor = 1;
    counter = 0;
    bitLevels = 65536;
    bitScale = 32768.0f;
    bitInvScale = 1.0f / 32768.0f;
    held = 0.0f;
    driveAmount = 0.0f;
    flipAmount = 0.0f;
  }

  // Recompute stage parameters from a single knob value. Called
  // at block rate to avoid per-sample mapping cost.
  inline void setKnob(float k) {
    if (k < 0.0f) k = 0.0f;
    if (k > 1.0f) k = 1.0f;

    // Divisor: log to 64
    float divf = expf(k * 4.158883f);  // ln(64)
    int d = (int)(divf + 0.5f);
    if (d < 1)  d = 1;
    if (d > 64) d = 64;
    if (d != divisor) {
      divisor = d;
      if (counter >= divisor) counter = 0;
    }

    // Bit levels: log from 65536 down to 4
    // ln(65536/4) = ln(16384) = 9.704
    float bitsf = expf(9.704061f * (1.0f - k));
    int b = (int)(bitsf + 0.5f);
    b *= 4;  // ensure even
    if (b < 4)     b = 4;
    if (b > 65536) b = 65536;
    bitLevels = b;
    bitScale = (float)bitLevels * 0.5f;
    bitInvScale = 1.0f / bitScale;

    // Pre-sat drive: linear ramp
    driveAmount = k * 6.0f;

    // Nyquist-flip: 0 below 0.85, smoothstep to 1 at 1.0
    if (k < 0.85f) {
      flipAmount = 0.0f;
    } else {
      float t = (k - 0.85f) * (1.0f / 0.15f);
      flipAmount = t * t * (3.0f - 2.0f * t);  // smoothstep
    }
  }

  inline float tick(float in) {
    // Stage 1: pre-saturation
    float driven = (driveAmount > 0.01f)
      ? fast_tanh(in * (1.0f + driveAmount)) * (1.0f + driveAmount * 0.5f)
      : in;

    // Stage 2: cycle counter
    bool onCycle = (counter == 0);
    counter++;
    if (counter >= divisor) counter = 0;

    // Stage 3: quantize + hold (on-cycle only)
    if (onCycle) {
      float scaled = driven * bitScale;
      held = floorf(scaled + 0.5f) * bitInvScale;
    }

    // Stage 4: reconstruction (ZOH blended with Nyquist-flip)
    if (flipAmount < 0.001f) return held;
    float sign = (counter & 1) ? -1.0f : 1.0f;
    float flipped = held * sign;
    return held + (flipped - held) * flipAmount;
  }

  inline void resetCounter() { counter = 0; }
};
```

### Mirror::process()

Single change in the audio loop block setup:

```cpp
// (was) int divisor = mirrorDivisorFromKnob(mirrorKnob);
//       if (divisor != s.mirror.divisor) { ... }
// (new)
s.mirror.setKnob(mirrorKnob);
```

Then in the inner loop, `s.mirror.tick(clean)` is unchanged.

Remove the now-unused `mirrorDivisorFromKnob()` and
`MIRROR_DIVISOR_MAX` constant.

---

## CPU impact

Per sample, new stages add:
- 1 `fast_tanh` (~5 ops) — at k > 0
- 2 multiplies + 1 floor for quantize — at on-cycle samples only
  (i.e. once per `divisor` samples)
- 1 mul + 1 add for nyq-flip — at k > 0.85

Net: ~5–10 ops/sample max. Cortex-A8 cost: probably <0.5% CPU on
top of the existing pipeline. Well within budget.

---

## Risks

1. **k=0 must be perfectly transparent.** If at k=0 there's any
   audible distortion, users will perceive the unit's "clean"
   state as already broken. Verify: `divisor=1, bits=65536,
   drive=0, flip=0` should produce `out = held = in` exactly.
2. **bit-quantization at low knob is still mild — 12-bit / 8-bit
   reduction needs the pre-sat to be audible.** Make sure preSat
   ramps in fast enough at low knob (linear from 0 means k=0.25
   = drive 1.5 = mild but audible tanh shaping).
3. **Nyquist-flip ramp must be smooth.** A sudden engage at k=0.85
   would click. Smoothstep curve from 0 to 1 over the top 15% of
   knob travel handles this.
4. **Quantization can DC-bias the signal.** If the input has any
   DC offset, the quantized values will cluster around quantized
   DC. The downstream DC HPF in Mirror::process handles this.
5. **At divisor=64, bits=4, the output is mostly silence with
   occasional spikes.** Verify level matches with the previous
   build at moderate knob settings; don't ship with output
   feeling much quieter or louder.
6. **Hardware safety: no NEON intrinsics, no out-of-line virtuals,
   scalar VFP only.** Already verified pattern via prior phases;
   should not regress.

---

## Phased implementation

Single phase, atomic change:

1. Refactor `MirrorBlock` struct with all five stages + knob
   mapping
2. Update `Mirror::process()` to call `setKnob` once per block
3. Remove old `mirrorDivisorFromKnob` helper
4. Bump PKGVERSION 2.7.1.27 → 2.7.1.28
5. Build both arches, install linux
6. Verify Mirror::process still NEON-hint-clean
7. Audition: confirm k=0 transparent, k=0.5 audibly crushed, k=1
   brutally aliased

---

## Audition checklist

At each knob position, with default Shape=0.13, Formant=110,
Sync=0, ModDepth=0.5:

- **k=0**: clean wavetable triangle, identical to no-Mirror
- **k=0.25**: subtle bit-crush grit, slight high-frequency shimmer
  from pre-sat
- **k=0.5**: classic 8-bit sampler character, audible sample-rate
  reduction, sharp transitions
- **k=0.75**: crunchy lo-fi, broken-up envelope, distinct alias
  landings
- **k=0.85**: transition zone, last "musical" position before
  Nyquist-flip
- **k=1.0**: brutal ring-mod-like character, content at SR/2,
  almost-noise

Combined with Sync (high anchors push formant rate up) and Mod
Depth (FM rate sweep), Mirror should now have substantial bite
across the whole control surface.

---

## Out of scope

- Separate knobs for Rate / Bits / Drive / Flip — single knob is
  the design intent. If audition shows we want independent
  control, add later.
- Soft-knee saturation (instead of hard tanh) — for v1 the hard
  tanh is fine.
- Audio-rate modulation of Mirror knob — same ParameterAdapter
  surface as before; CV input still works.
- Alternative reconstruction (e.g. Catmull-Rom for "smooth" mode)
  — paradigm-coherent rejection.

---

## TL;DR

Mirror knob becomes a 4-stage destructive aliasing crusher: pre-
saturate (drive harmonics above Nyquist), undersample at divisor
1..64 (S&H rate reduction), bit-reduce 16-bit → 2-bit (creates
harmonics from any input), and at the top of the knob ramp in
Nyquist-polarity-flip (synthesizes SR/2 content). Single knob,
single struct, ~10 ops/sample max. AW undersample architecture
with inverted reconstruction choices — alias-preserving instead
of alias-rejecting.
