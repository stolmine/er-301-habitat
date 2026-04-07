# Parfait: multiband saturation for ER-301

## Context

Multiband saturation/distortion effect inspired by Ableton Roar, condensed for the ER-301's 6-ply interface. 3-band processing with per-band shaper, filter, and weight-based crossover. Users bring their own feedback and modulation via external patching; Spectral Follower can be used to isolate bands externally.

## Architecture

3-band multiband mode only. Input is split by weight-proportional crossover (Linkwitz-Riley or similar phase-coherent split). Each band has independent shaper + filter. Bands are summed, compressed, and mixed with dry signal.

### Signal Flow
```
Input -> [Drive] -> [Tone] -> [3-band split by weight] -> Band 0: [Shaper] -> [Filter] -+
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

### Skew Control

Single control that shifts all three band weights together, bunching crossovers toward low or high end of the spectrum. Borrows from Etcher's weight/skew implementation (power curve warping on segment boundaries). Skew at center = equal weights. Skew left = crossovers bunch low (more spectrum in high band). Skew right = crossovers bunch high (more spectrum in low band). Individual band weights in expansion views allow fine tuning on top of skew.

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
| Skew | -1 to 1 | Crossover weight distribution bias |
| Compress | 0-1 | Stereo bus compressor (Streams-style, single knob) |
| SC HPF | on/off | Sidechain highpass on compressor (expansion-only) |
| Output Level | 0-4 | Wet path output |
| Tanh | 0-1 | Output saturation |
| Dry/Wet | 0-1 | Mix |

### Compressor

Streams-style single-knob compressor on the summed wet signal, tuned for stereo bus use. The knob controls a linked ratio + auto-threshold curve with soft knee -- 0 = transparent, 1 = heavily squashed. Program-dependent envelope: fast fixed attack (~1ms), adaptive release (short bursts get fast release, sustained signal gets slow release). No separate ratio/threshold controls. SC HPF toggle applies ~100Hz highpass to sidechain analysis to prevent bass pumping.

Reference: `eurorack/streams/compressor.h` (Streams by Emilie Gillet, MIT). Follower class has per-band auto attack/decay coefficients.

## UI Layout (6 plies)

### Ply 1: Drive

**Main control**: GainBias for drive level (-12 to +24 dB)

**Sub-display**: `tone` / `freq`
- Tone Amount: readout, -1 to 1
- Tone Frequency: readout, Hz

**Expansion view**: drive, tone amount, tone freq

### Ply 2-4: Band Low / Mid / High (FFT spectrum display)

Each band ply shows 1/3 of a continuous FFT spectrum display. The three plies stitch together visually to form one wideband spectrum analyzer.

**Main graphic**: FFT spectrum for this band's frequency range.
- Peak: outlined by a contour line (fast attack, slow decay hold)
- RMS: filled region with brightness gradient (full bright at bottom, fading toward peak edge)
- Crossover boundary: the ply edge IS the crossover. Frequency label at bottom edge shows crossover Hz.
- When weights change, the frequency range assigned to each ply changes. More weight = more spectrum compressed into the same pixel width.
- Muted bands dim to GRAY3.

**Sub-display (default)**: `amt` / `bias` / `type`
- Amount: GainBias readout, 0-1
- Bias: GainBias readout, -1 to 1
- Type: Readout with addName labels (soft/hard/fold/rect/crush/sine/poly)

**Shift sub-display**: `wt` / `freq` / `morph`
- Weight: GainBias readout, 0-1
- Filter Freq: GainBias readout, Hz
- Filter Morph: Readout with addName labels (LP/BP/HP/ntch)

**Expansion view**: mute (far left), amt, bias, type, wt, freq, morph, Q

### Ply 5: Skew

**Main control**: GainBias for skew (-1 to 1, default 0)

Skew redistributes all three band weights from a single control. Center = equal weights. Negative = crossovers bunch toward low frequencies. Positive = crossovers bunch toward high frequencies. Individual band weights (in band expansion views) apply on top of skew.

### Ply 6: Mix

**Main control**: MixControl (reuse from Petrichor/Pecto)

**Shift sub-display**: `comp` / `output` / `tanh`
- Compress: readout, 0-1 (Streams-style single knob)
- Output Level: readout, 0-4
- Tanh: readout, 0-1

**Expansion view**: mix, comp, output, tanh, SC HPF (toggle)

## FFT Spectrum Display

256-point FFT at 48kHz = 128 usable bins (Nyquist half). Three band plies = ~126 pixels total width. Nearly 1 bin per pixel -- no interpolation needed.

### Implementation
- FFT computed in C++ process() at display refresh rate (~10-25fps, not every audio frame)
- Bin magnitudes stored in shared array accessible to all 3 band graphics
- Each band graphic draws its portion of the spectrum based on current crossover frequencies
- Per-bin peak hold (fast attack, ~1s decay) and RMS smoothing (50-100ms)
- Logarithmic magnitude scaling (dB)
- Frequency axis is logarithmic within each band's range

### Cross-ply stitching
- Each graphic is independent but knows the full crossover state
- Left edge of band N+1 aligns with right edge of band N at the same magnitude
- Crossover frequency label drawn at ply boundary
- 1-pixel seam between plies is acceptable

### Standalone reuse
- The FFT analyzer can be extracted as a standalone visualization unit (like Scope) for general spectral analysis
- Same graphic class, just one instance spanning full width instead of 3 band portions

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
float fftInput[256];         // circular FFT input buffer
float fftMagnitude[128];     // latest magnitude spectrum
float fftPeak[128];          // peak hold per bin
float fftRms[128];           // RMS smoothed per bin
int fftWriteIndex;           // circular buffer position
int fftFrameCounter;         // decimation counter for FFT rate
```

### Process flow per sample:
1. Apply drive
2. Apply tone tilt EQ
3. Split into 3 bands (cascaded Linkwitz-Riley from derived crossover freqs)
4. Per band: apply shaper(type, amount, bias), apply morph filter
5. Sum bands (skip muted)
6. Apply compressor (Streams-style, program-dependent release)
7. Mix dry/wet with output/tanh
8. Accumulate FFT input buffer; compute FFT every N frames

### Crossover implementation

Use 2nd-order Linkwitz-Riley (two cascaded SVF lowpasses) for phase-coherent splitting. Two crossover frequencies derived from weights + skew. Recompute when weights or skew change (dirty-check pattern from Petrichor/Pecto).

### Skew implementation

Port from Etcher's weight/skew pattern. Three base weights (per-band, user-editable) are warped by a global skew parameter using power curve: `warped_weight[i] = pow(base_weight[i], 2^skew)`. Skew=0 passes through, positive/negative bunches weights toward one end.

## Package

Goes in **spreadsheet** package (multi-band/multi-step units). The shapers are all simple math (no external DSP libraries needed). The SVF filters reuse stmlib. FFT can use a simple radix-2 DIT implementation (128 bins, no NEON needed at display rate).

## Files

| File | Purpose |
|------|---------|
| `mods/biome/MultibandSaturator.h` | DSP class header |
| `mods/biome/MultibandSaturator.cpp` | DSP implementation |
| `mods/biome/SpectrumGraphic.h` | FFT spectrum display (per-band portion) |
| `mods/biome/assets/MultibandSaturator.lua` | Unit wiring, 6-ply layout |
| `mods/biome/assets/BandControl.lua` | Per-band ViewControl with shift sub-display |

## Files to Modify

| File | Change |
|------|--------|
| `mods/biome/biome.cpp.swig` | Add MultibandSaturator, SpectrumGraphic |
| `mods/biome/assets/toc.lua` | Register unit |

## Build Order

1. **Skeleton**: MultibandSaturator class, passthrough, basic Lua with 6 GainBias plies
2. **Band split**: weight-based crossover with Linkwitz-Riley, skew control
3. **Shapers**: implement 7 shaper types, amount/bias control
4. **Filters**: SVF morph filter per band
5. **Compressor**: Streams-style single-knob with program-dependent release
6. **FFT**: 256-point FFT, magnitude/peak/RMS tracking, SpectrumGraphic
7. **BandControl**: shift sub-display, expansion view, mute
8. **Polish**: MixControl reuse, drive tilt EQ, defaults tuning

## Key References

- Ableton Roar manual (pages 596-604 of Live 12 manual)
- `eurorack/streams/compressor.h` -- Streams compressor (Emilie Gillet, MIT). Program-dependent envelope, soft knee, ratio/threshold.
- `eurorack/streams/follower.h` -- Streams envelope follower. Per-band auto attack/decay coefficients.
- `mods/biome/Discont.h/.cpp` -- existing waveshaper implementations (fold, tanh, softclip, hardclip, sqrt, rectify, crush)
- `mods/biome/assets/Pecto.lua` -- MixControl reuse, DensityControl shift sub-display pattern
- `mods/spreadsheet/assets/TransformGateControl.lua` -- shift sub-display pattern
- `mods/spreadsheet/Etcher.h/.cpp` -- weight/skew distribution for crossover
- `eurorack/stmlib/dsp/filter.h` -- SVF for crossover and post-shaper filters
