# Drum Voice: Classic Analog-Inspired Drum Synthesizer

Status: **planning**. No code yet.

Package: **spreadsheet** (custom controls with sub-displays, overview viz).

## Aesthetic

Classic analog drum synth building blocks: sine/triangle core with waveshaping, dual-oscillator interaction, pitch sweep, FM noise injection, hold+decay amp envelope, post-processing chain (waveshaper, DJ filter). Single voice — user can instantiate multiple for a kit. Beat-driven, hip-hop leaning, immediate.

## Signal Chain

```
Trigger ──► Pitch Envelope ──► Tone Osc (tri/sine/fold)
                                    │
                            ┌───────┤
                            │   Shape Osc (dampened decay, slight detune)
                            │       │
                Grit ──► FM Noise ──┤
                            │       │
                            ▼       ▼
                         Sum + Amp Envelope
                              │
                         Clipper (rational tanh)
                              │
                         DJ Filter (SVF LP←→HP)
                              │
                         Output VCA (level)
```

## DSP Architecture

### 1. Tone Oscillator — Character Morph

Phase accumulator generating a bandlimited triangle (polyBLAMP at corners). Character parameter [0, 1] controls timbre in three zones:

| Range | Behavior |
|---|---|
| 0.0 – 0.5 | Triangle → Sine morph (linear blend via sine LUT shaper) |
| 0.5 | Pure sine |
| 0.5 – 1.0 | Sine → Triangle fold (increasing fold gain 1→4) |

**Sine shaper**: 256-entry LUT of `sin(x * pi/2)` over [-1, 1]. Applied to the triangle sample. No per-sample transcendentals.

**Fold**: `sineLUT(tri * foldGain)` where `foldGain = 1 + (character - 0.5) * 2 * (maxFolds - 1)`. At drum frequencies (50-500 Hz), fold harmonics stay well under Nyquist even at fold order 4. No oversampling needed.

**PolyBLAMP**: Applied to the raw triangle's derivative discontinuities (corners at phase 0 and 0.5). The shaper and folder are smooth C-infinity functions — no additional anti-aliasing post-shaping.

### 2. Shape Oscillator — Dual Resonator

Emulates the T-bridge dual oscillator of the 808/909. A second oscillator with:

- Same base frequency, optional small detune (0–7 cents via Shape parameter)
- Independent amplitude envelope with dampened decay: `decay2 = decay1 * 0.6`
```
output = toneSample * env1 + shape * shapeSample * env2
```

At Shape=0: single oscillator. As Shape increases, second osc blends in. The marginal frequency difference produces amplitude modulation — the "wobble."

**Subtle pitch droop**: Both oscillators' frequency modulated by `ampEnv * 0.015` — +1.5% at onset, decaying with amplitude. Adds warmth to kicks without heavy analog modeling.

### 3. Pitch Envelope — Sweep + Time

Exponential pitch sweep computed per-sample via precomputed frequency ratio (zero per-sample transcendentals):

At trigger:
```cpp
float freqStart = baseFreq * powf(2.0f, sweepDepth / 12.0f);
float sweepSamples = sweepTime * sampleRate;
float freqRatio = powf(baseFreq / freqStart, 1.0f / sweepSamples);
```

Per sample: `currentFreq *= freqRatio` — single multiply.

**Sweep**: depth in semitones (0–72). Controls how far above base pitch the transient starts.
**Time**: sweep decay time (1–200 ms). Controls speed of the pitch drop.

Useful ranges:
- Kick thump: 12–24 semitones, 20–60 ms
- Deep 808 kick: 24–48 semitones, 40–120 ms
- Tom: 5–15 semitones, 30–80 ms
- Metallic: 12–24 semitones, 15–40 ms

### 4. Grit — FM Noise with Envelope Coupling

FM noise: white noise modulates the phase accumulator directly.

```cpp
float noiseHz = noise * fmDeviation * noiseEnv;
float instFreq = currentFreq + noiseHz;
phase += instFreq / sr;
```

The Grit parameter [0, 1] controls:
- **FM deviation**: quadratic scaling `grit^2 * maxDeviation` for natural feel
- **Noise mix**: direct noise added to signal, scaled by grit
- **Envelope coupling**: above 75% grit, amp decay time scales down to 30% of base. Both tone and noise shorten together (shared envelope), mimicking energy dissipation.

At low grit: slightly noisy pitch (analog warmth). At high grit: inharmonic noise transient replacing tone with snappy decay (808 snare territory).

### 5. Amplitude Envelope — Attack / Hold / Decay

One-pole IIR: `env *= alpha` where `alpha = exp(-1 / (decayTime * sr))`.

- **Attack**: 0–50 ms. At 0 = instant (pure percussion). Above 0 = pluck/pad territory.
- **Hold**: 0–500 ms. Sustain at peak before decay begins. Counter-based (hold samples = hold_time * sr).
- **Decay**: 10–5000 ms. Exponential decay to silence.

Attack implemented as a ramp: `env += 1.0 / (attackTime * sr)` per sample until env reaches 1.0, then hold phase begins.

### 6. Post-Chain

#### Clipper (Waveshaper)

Rational tanh approximation (8 operations, no transcendentals):

```cpp
inline float tanhRational(float x) {
    if (x > 3.0f) return 1.0f;
    if (x < -3.0f) return -1.0f;
    float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}
// Normalized: tanhRational(sample * drive) / tanhRational(drive)
```

Drive range: 0 dB (bypass) to 20 dB. The `powf` for drive happens once per block.

#### DJ Filter

Single TPT SVF (Cytomic formulation). One knob sweeps LP ←→ open ←→ HP:

- knob < 0.5: LP mode, cutoff sweeps from low to open
- knob = 0.5: bypass (flat response)
- knob > 0.5: HP mode, cutoff sweeps from open to high

`tanf(pi * freq / sr)` computed once per block. Existing DJFilter in biome is a reference implementation — same topology, embedded into the drum voice's process loop.

**Fixed Q**: `k = 2.0 - 2.0 * 0.5 * 0.95 = 1.05` (matching biome DJFilter default Q=0.5). Not exposed as a parameter — keeps the interface clean.

#### Level

Output VCA. Simple multiply by level parameter.

## Parameters

| Parameter | Type | Range | Default | Notes |
|---|---|---|---|---|
| Trigger | Gate | — | — | Rising edge fires voice |
| V/Oct | GainBias | -5 to 5 V | 0 | 1V/oct pitch tracking |
| Character | GainBias | 0–1 | 0.5 | Tri→Sine→Fold morph |
| Shape | GainBias | 0–1 | 0 | Second oscillator blend |
| Grit | GainBias | 0–1 | 0 | FM noise + envelope coupling |
| Punch | GainBias | 0–1 | 0.3 | Transient emphasis (bonus param) |
| Sweep | GainBias | 0–72 st | 12 | Pitch envelope depth |
| SweepTime | GainBias | 1–200 ms | 30 | Pitch envelope decay |
| Attack | GainBias | 0–50 ms | 0 | Amp envelope attack |
| Hold | GainBias | 0–500 ms | 0 | Amp envelope hold |
| Decay | GainBias | 10–5000 ms | 200 | Amp envelope decay |
| Clipper | GainBias | 0–1 | 0 | Waveshaper drive |
| EQ | GainBias | 0–1 | 0.5 | DJ filter LP←→HP |
| Level | GainBias | 0–1 | 0.8 | Output level |

### Bonus: Punch

Transient emphasis via a parallel fast envelope that adds a brief amplitude spike at the onset. The punch envelope decays in 1–10 ms (much faster than the main decay). At trigger:

```
punchEnv = punchAmount
punchDecay = exp(-1 / (punchTime * sr))  // punchTime ~3ms
```

Per sample: `output *= (1.0 + punchEnv); punchEnv *= punchDecay;`

This adds a click/snap at the transient without changing the body. At 0: no effect. At 1: aggressive transient peak. Especially effective on kicks (adds attack definition) and electronic snares (adds the initial crack).

## UI Layout (6 plies)

| Ply | Main Control | Sub-display (shift toggle) |
|---|---|---|
| 1. Gate | Trigger input | — |
| 2. V/Oct | Pitch (GainBias) | — |
| 3. Character | Character morph + overview viz | shape / grit / punch |
| 4. Sweep | Pitch env depth (GainBias) | sweepTime |
| 5. Decay | Amp decay (GainBias) | hold / attack |
| 6. Level | Output level (MixControl) | clipper / eq / (dj filter?) |

### Overview Visualization — Rotating Cube

On the Character ply: a 3D rotating cube rendered on the 42×64 display. Surface deformation and texturing driven by synthesis parameters.

**Rendering**: Orthographic projection, 8 vertices, 6 faces. Back-face culling via 2D cross product of projected edges. Filled faces with depth-based 4-bit grayscale shading (GRAY4–GRAY13). Edges drawn slightly brighter for contrast. Faces sorted back-to-front (3-element insertion sort). Slow continuous rotation (~0.5 rpm).

Scanline fill: each face split into 2 triangles, filled via horizontal scanlines using `fb.hline()`. Stipple texturing via `hline` dotting parameter for density-mapped surface detail.

**Parameter-to-deformation mapping:**

| Parameter | Deformation |
|---|---|
| Character (0→0.5→1) | Cube → sphere morph → radial fold ripple. Vertices displaced along normals toward sphere of radius sqrt(3). Above 0.5, sinusoidal radial oscillation adds fold texture. |
| Shape | Face pinch — top/bottom faces compress inward, thinning the cube into a slab. Visual analog of the second oscillator thinning the waveform. |
| Grit | Vertex jitter via integer hash (per-vertex, per-frame noise displacement). Scale increases with grit. Also increases rotation speed at high grit. |
| Punch (trigger) | Momentary scale burst — cube pops outward on trigger, decays back over ~300ms. Edge brightness flashes WHITE on hit frame. |

**CPU cost**: 4 sinf/cosf per frame (rotation angles), 8 vertex transforms (64 multiplies), 8 optional sqrtf (sphere morph), ~80 hline calls for fill. Under 0.1ms/frame — no caching needed.

**File**: `mods/spreadsheet/DrumCubeGraphic.h` — self-contained od::Graphic subclass with `follow(DrumVoice*)` pattern.

### Sub-display Pattern

Character ply uses the established shift-toggle pattern (Pattern A from shift audit). Three sub-buttons:
- Sub1: shape readout + encoder control
- Sub2: grit readout + encoder control  
- Sub3: punch readout + encoder control

Decay ply sub-display:
- Sub1: hold readout
- Sub2: attack readout
- Sub3: (spare)

Level ply: MixControl variant with clipper/eq/filter as the three readouts.

## CPU Budget

| Component | FLOPs/sample |
|---|---|
| Triangle + polyBLAMP | 15 |
| Sine LUT shaper | 5 |
| Fold (conditional) | 5 |
| Shape osc (second phasor + env) | 10 |
| Pitch envelope (1 multiply) | 1 |
| FM noise (LCG + multiply) | 5 |
| Amp envelope (1 multiply + hold logic) | 3 |
| Punch envelope | 2 |
| Clipper (rational tanh) | 10 |
| DJ SVF (TPT, 1 pole) | 12 |
| Level VCA | 1 |
| **Total** | **~69** |

69 FLOPs × 48000 = 3.3 MFLOPS. Trivial for AM335x.

## Implementation Order

1. **DSP core**: Trigger detection, phase accumulator, pitch envelope, amp envelope, basic sine output
2. **Character morph**: Sine LUT, tri→sine blend, fold above midpoint
3. **Shape oscillator**: Second phasor with dampened decay + detune
4. **Grit**: FM noise, envelope coupling above 75%
5. **Punch**: Fast transient envelope
6. **Post-chain**: Clipper, DJ filter (fixed Q=0.5), level
7. **Cube graphic**: Rotating cube with parameter deformation, scanline fill, depth shading
8. **Lua unit**: Plies, controls, sub-displays (Character+cube viz, Decay+hold/attack, Level+clipper/eq)

## Files

- `mods/spreadsheet/DrumVoice.h` — DSP header (SWIG-visible getters for Character, Shape, Grit, gate state)
- `mods/spreadsheet/DrumVoice.cpp` — DSP implementation
- `mods/spreadsheet/DrumCubeGraphic.h` — Rotating cube visualization
- `mods/spreadsheet/assets/DrumVoice.lua` — Unit definition
- `mods/spreadsheet/assets/DrumVoiceCharacterControl.lua` — Character ply custom control with cube viz + shape/grit/punch sub-display
- `mods/spreadsheet/assets/DrumVoiceDecayControl.lua` — Decay ply with hold/attack sub-display
