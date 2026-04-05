# Multiband Saturation: Roar-inspired multiband distortion for ER-301

## Context

Multiband saturation/distortion effect inspired by Ableton Roar, condensed for the ER-301's 6-ply interface. 3-band processing with per-band shaper, filter, and weight-based crossover. Users bring their own feedback and modulation via external patching; Spectral Follower can be used to isolate bands externally.

## Architecture

3-band multiband mode only. Input is split by weight-proportional crossover (Linkwitz-Riley or similar phase-coherent split). Each band has independent shaper + filter. Bands are summed, compressed, and mixed with dry signal.

### Signal Flow
```
Input -> [Tone] -> [3-band split by weight] -> Band 0: [Shaper] -> [Filter] -+
                                             -> Band 1: [Shaper] -> [Filter] -+-> [Sum] -> [Compress] -> [Mix] -> Output
                                             -> Band 2: [Shaper] -> [Filter] -+
```

### Weight-Based Crossover

Each band has a weight parameter (0-1). The three weights are normalized to determine proportional spectrum coverage. Crossover frequencies are derived:
- Total weight = w0 + w1 + w2
- Band 0 (low): 20Hz to `20 * (20000/20)^(w0/total)` Hz
- Band 1 (mid): end of band 0 to `20 * (20000/20)^((w0+w1)/total)` Hz
- Band 2 (high): end of band 1 to 20kHz

Equal weights (0.33/0.33/0.33) give logarithmically even thirds. Zeroing a weight collapses that band (effectively mutes it). Cranking one weight gives that band most of the spectrum.

## Per-Band Parameters (x3)

| Parameter | Range | Description |
|-----------|-------|-------------|
| Amount | 0-1 | Distortion/saturation intensity |
| Bias | -1 to 1 | Asymmetric waveshaper offset |
| Shaper Type | 0-N | Waveshaping algorithm (see below) |
| Weight | 0-1 | Relative spectrum coverage |
| Filter Freq | 20-20000 Hz | Post-shaper filter cutoff |
| Filter Morph | 0-1 | Continuous sweep: LP -> BP -> HP -> notch |
| Filter Q | 0-1 | Resonance (expansion-only) |
| Mute | on/off | Hard bypass for A/B (expansion-only) |

### Shaper Types

Subset of Roar's 12, chosen for distinct character and CPU efficiency:

| Index | Name | Description |
|-------|------|-------------|
| 0 | Soft | tanh soft saturation |
| 1 | Hard | hard clip |
| 2 | Fold | triangle wavefolder |
| 3 | Rectify | full-wave rectifier (octave up) |
| 4 | Crush | bit reduction with compander |
| 5 | Sine | sinusoidal waveshaper |
| 6 | Polynomial | x - x^3/3 odd-harmonic |

7 types. Can expand later. All are stateless (no memory between samples) so CPU is trivial.

### Filter Morph

Single continuous parameter sweeps through filter responses using a 2-pole SVF:
- 0.0 = LP (low-pass)
- 0.33 = BP (band-pass)
- 0.66 = HP (high-pass)
- 1.0 = Notch

Implemented as weighted sum of SVF outputs: `out = lp*a + bp*b + hp*c` where a/b/c are derived from the morph position. Smooth interpolation between types.

## Global Parameters

| Parameter | Range | Description |
|-----------|-------|-------------|
| Drive | -12 to +24 dB | Input level before band split |
| Tone Amount | -1 to 1 | Pre-split tilt EQ (+ bright, - dark) |
| Tone Freq | 50-5000 Hz | Tilt EQ shelving frequency |
| Compress | 0-1 | Output compression amount (one-knob) |
| SC HPF | on/off | Sidechain highpass on compressor |
| Input Level | 0-4 | Wet path input (sub-display) |
| Output Level | 0-4 | Wet path output (sub-display) |
| Tanh | 0-1 | Output saturation (sub-display) |
| Dry/Wet | 0-1 | Mix |

### Compressor

Simple one-knob compressor on the summed wet signal. Amount controls ratio (1:1 to inf:1) with auto-threshold. SC HPF toggle applies ~100Hz highpass to the sidechain analysis to prevent bass from pumping.

## UI Layout (6 plies)

### Ply 1-3: Bands (Low / Mid / High)

Each band is its own ViewControl with:

**Main graphic**: Shaper transfer curve visualization for that band. Shows the waveshaper shape based on current type + amount + bias. Dimmed when muted.

**Sub-display (default)**: `amt` / `bias` / `type`
- Amount: GainBias readout, 0-1
- Bias: GainBias readout, -1 to 1
- Type: Readout with addName labels (soft/hard/fold/rect/crush/sine/poly)

**Shift sub-display**: `wt` / `freq` / `morph`
- Weight: GainBias readout, 0-1
- Filter Freq: GainBias readout, Hz
- Filter Morph: Readout with addName labels (LP/BP/HP/ntch)

**Expansion view**: mute (OptionControl, far left), amt, bias, type, wt, freq, morph, Q

### Ply 4: Drive

**Main control**: GainBias for drive level

**Sub-display**: `tone` / `freq`
- Tone Amount: readout, -1 to 1
- Tone Frequency: readout, Hz

### Ply 5: Compress

**Main control**: GainBias for compression amount

**Sub-display**: `sc hpf` (toggle readout or OptionControl-style)

### Ply 6: Mix

**Main control**: MixControl (reuse from Petrichor/Tomograph)

**Shift sub-display**: `input` / `output` / `tanh`

## 3-Ply Visual Design

The three band graphics should visually read as one continuous display when adjacent:
- Each graphic draws its band's shaper transfer curve
- A subtle vertical line at band boundaries shows the crossover point
- Muted bands dim to GRAY3
- The transfer curve responds to amount (scales the nonlinearity) and bias (shifts the curve horizontally)

Each graphic is independent (no shared C++ state needed) -- they each query the same DSP object for their band's parameters and the crossover frequencies for boundary drawing.

## C++ Architecture

### MultibandSaturator class

Pimpl pattern (Internal struct). Per-band state:
```
struct BandState {
    stmlib::Svf crossoverLP, crossoverHP;  // band splitting
    stmlib::Svf postFilter;                // per-band post-shaper filter
    float shaperAmount, shaperBias;
    int shaperType;
    float filterFreq, filterMorph, filterQ;
    float weight;
    bool muted;
};
```

Global state:
```
float toneFilterState;       // one-pole tilt EQ
float compEnvelope;          // compressor envelope follower
float compGain;              // computed gain reduction
```

### Process flow per sample:
1. Apply drive
2. Apply tone tilt EQ
3. Split into 3 bands (cascaded Linkwitz-Riley from derived crossover freqs)
4. Per band: apply shaper(type, amount, bias), apply morph filter
5. Sum bands (skip muted)
6. Apply compressor
7. Mix dry/wet with input/output/tanh

### Crossover implementation

Use 2nd-order Linkwitz-Riley (two cascaded SVF lowpasses) for phase-coherent splitting. Two crossover frequencies derived from weights. Recompute when weights change (dirty-check pattern from Petrichor).

## Package

Goes in **biome** package. The shapers are all simple math (no external DSP libraries needed). The SVF filters reuse stmlib.

## Files

| File | Purpose |
|------|---------|
| `mods/biome/MultibandSaturator.h` | DSP class header |
| `mods/biome/MultibandSaturator.cpp` | DSP implementation |
| `mods/biome/ShaperGraphic.h` | Per-band transfer curve display |
| `mods/biome/assets/MultibandSaturator.lua` | Unit wiring, 6-ply layout |
| `mods/biome/assets/BandControl.lua` | Per-band ViewControl with shift sub-display |

## Files to Modify

| File | Change |
|------|--------|
| `mods/biome/biome.cpp.swig` | Add MultibandSaturator, ShaperGraphic |
| `mods/biome/assets/toc.lua` | Register unit |

## Build Order

1. **Skeleton**: MultibandSaturator class, passthrough, basic Lua with 6 GainBias plies
2. **Band split**: weight-based crossover with Linkwitz-Riley
3. **Shapers**: implement 7 shaper types, amount/bias control
4. **Filters**: SVF morph filter per band
5. **Compressor**: one-knob with SC HPF
6. **Visuals**: ShaperGraphic transfer curve per band
7. **BandControl**: shift sub-display, expansion view, mute
8. **Polish**: MixControl reuse, drive tilt EQ, defaults tuning

## Key References

- Ableton Roar manual (pages 596-604 of Live 12 manual)
- `mods/biome/Discont.h/.cpp` -- existing waveshaper implementations (fold, tanh, softclip, hardclip, sqrt, rectify, crush)
- `mods/spreadsheet/MultitapDelay.lua` -- MixControl, FeedbackControl reuse pattern
- `mods/spreadsheet/assets/TransformGateControl.lua` -- shift sub-display pattern
- `eurorack/stmlib/dsp/filter.h` -- SVF for crossover and post-shaper filters
