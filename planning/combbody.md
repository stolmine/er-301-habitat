# CombBody: Rainmaker Comb Resonator for ER-301

## Context

The Rainmaker's comb section is a single feedback comb filter whose spectral shape is sculpted by multiple passive read taps on one shared delay line. All taps are fractions of a master "comb size" so the entire harmonic structure transposes together under V/Oct. This is distinct from the existing "Comb Bank" todo item (N independent parallel combs) -- CombBody is one resonant body with pattern-driven harmonic shaping.

We already have Petrichor (multitap delay with independent per-tap processing) and Filterbank (parallel SVFs with scale distribution). CombBody is cheaper than both: no per-tap filters, no granular pitch shift. Just N reads + weighted sum + one feedback loop.

## Files to Create

| File | Purpose |
|------|---------|
| `mods/spreadsheet/CombBody.h` | DSP class header, pimpl pattern |
| `mods/spreadsheet/CombBody.cpp` | DSP: buffer, tap patterns, feedback, resonators, LFO |
| `mods/spreadsheet/CombBodyGraphic.h` | Scrollable tap list display (read-only) |
| `mods/spreadsheet/CombInfoGraphic.h` | Overview: f0 freq, density, pattern, resonator type |
| `mods/spreadsheet/assets/CombBody.lua` | Unit wiring, 7-ply view layout |
| `mods/spreadsheet/assets/CombListControl.lua` | Read-only tap list with density/pattern/slope in expansion |
| `mods/spreadsheet/assets/CombInfoControl.lua` | Overview wrapper (EncoderControl around CombInfoGraphic) |
| `mods/spreadsheet/assets/CombSizeControl.lua` | Size ply with LFO shift-SD |

## Files to Modify

| File | Change |
|------|--------|
| `mods/spreadsheet/spreadsheet.cpp.swig` | Add CombBody, CombBodyGraphic, CombInfoGraphic includes |
| `mods/spreadsheet/assets/toc.lua` | Register CombBody unit |

## C++ Architecture

### Internal Struct
```
buffer (int16 circular, 20s max ~1.875MB)
writeIndex
tapPosition[64]     -- 0-1 fractions of master size (from pattern)
tapWeight[64]       -- amplitude envelope (from slope)
tapEnergy[64]       -- RMS followers for viz
fbFilterState       -- one-pole LP for tone
fbHpState           -- one-pole HP for tone > 0
sitarModState       -- amplitude tracker for Sitar mode
lfoPhase            -- 0-1 accumulator
```

### Parameters
- **Global**: CombSize (0.1ms-20s exp), Feedback (0-0.99), FeedbackTone (bipolar), ResonatorType (0-3), Density (1-64), Pattern (0-15), Slope (0-3), VOctPitch, LFORate/Depth/Waveform, Mix, InputLevel, OutputLevel, TanhAmt
- **Per-tap**: None editable. Derived from Pattern + Slope + Density via `recomputeTaps()`.

### Signal Flow
```
Input -> [write to buffer] -> [read N taps, weight, sum] -> wet output
              ^                                                |
              +-- [tone] <- [nonlinearity] <- [feedback tap] <-+
```

### 16 Tap Patterns
0-7: Uniform, Fibonacci, Early, Late, Middle, Ess, Flat, Rev-Fibonacci
8-15: Randomized variants of 0-7 (seeded perturbation)

### 4 Slope Types
Flat (1.0), Rise (linear up), Fall (linear down), Rise-Fall (sine hump)

### 4 Resonator Types
- **Raw**: wire (tone filter only)
- **Guitar**: one-pole LP damping in feedback (KS frequency-dependent decay)
- **Clarinet**: `x - x^3/3` odd-harmonic nonlinearity
- **Sitar**: `delay_actual = delay_base + k * |fb_signal|` (amplitude-dependent delay mod)

### Reuse from Petrichor
- int16 bufRead/bufWrite helpers + bufReadInterp for fractional delay
- Buffer allocation with fallback halving
- Feedback normalization by tap count: `fb / (1 + 0.15 * sqrt(N))`
- tanh soft limiter on feedback injection
- V/Oct: ConstantOffset * 10 -> octaves, applied as `size * 2^(-voct)`

## Lua UI Layout (7 plies)

1. **tune** -- V/Oct (Pitch control, ConstantOffset)
2. **taps** -- CombListControl: read-only tap list, expansion: density/pattern/slope
3. **overview** -- CombInfoControl: f0 Hz, density, pattern, resonator type
4. **size** -- CombSizeControl: comb size GainBias, shift-SD: LFO rate/depth/waveform
5. **feedback** -- FeedbackControl (reuse): shift-SD: tone + resonator type
6. **xform** -- TransformGateControl (reuse): targets for all params
7. **mix** -- MixControl (reuse): input/output/tanh

No per-tap serialization needed (taps are derived). Standard Unit.serialize suffices.

## Xform Gate (Randomization)

Gate input on Comparator triggers randomization of top-level parameters. Follows Petrichor's TransformGateControl pattern exactly.

### Targets (single selector, 0-12)
0. **all** -- randomize everything below
1. **size** -- comb size
2. **feedback** -- feedback amount
3. **tone** -- feedback tone
4. **resonator** -- resonator type (integer 0-3)
5. **density** -- tap count (integer 1-64)
6. **pattern** -- tap pattern (integer 0-15)
7. **slope** -- slope type (integer 0-3)
8. **lfo rate** -- LFO rate
9. **lfo depth** -- LFO depth
10. **lfo wave** -- LFO waveform (integer 0-3)
11. **mix** -- wet/dry mix
12. **reset** -- restore all to defaults

### C++ Implementation
- `od::Parameter *mBias*` pointers for each randomizable param (set from Lua via `setTopLevelBias`)
- `mXformTarget`, `mXformDepth` parameters
- `fireRandomize()` called from gate rising edge or manual trigger
- `applyRandomize()` uses depth to scale random deviation from current value
- Integer params (resonator, pattern, slope, density, lfo wave) snap to valid integer range

### Lua Wiring
- Comparator on XformGate inlet
- TransformGateControl (reuse from Petrichor) with target names adapted for CombBody
- Sub-display: target / depth / fire
- `setTopLevelBias(0..N, adapter:getParameter("Bias"))` for each randomizable param

## Build Order

### Phase 1: Skeleton
- CombBody.h/cpp with buffer alloc, passthrough, CombSize/Feedback/Mix params
- Minimal CombBody.lua with GainBias controls
- SWIG + toc registration
- **Test**: unit loads, audio passes through

### Phase 2: Single-tap comb + V/Oct
- One delay read + feedback loop in process()
- V/Oct tracking via ConstantOffset
- **Test**: pitched resonance at 1/combSize Hz, V/Oct transposes correctly

### Phase 3: Multi-tap patterns
- recomputeTaps() with all 16 patterns + 4 slopes
- Density param, read N taps per sample
- **Test**: density changes harmonic richness, patterns sound distinct

### Phase 4: Resonator types
- Raw, Guitar, Clarinet, Sitar feedback nonlinearities
- **Test**: each produces distinct timbral character

### Phase 5: LFO
- Phase accumulator, 4 waveforms (sine/tri/chirp/S&H)
- Modulate comb size
- **Test**: vibrato/chorus effect

### Phase 6: Full UI
- CombBodyGraphic.h, CombInfoGraphic.h
- CombListControl.lua, CombInfoControl.lua, CombSizeControl.lua
- Full 7-ply CombBody.lua
- TransformGateControl with CombBody targets + xform wiring

### Phase 7: Polish
- Stereo support (mono flag, OutR)
- Parameter defaults tuning
- CPU profiling on Cortex-A8

## Verification

- 64 taps at 48kHz should be well within CPU budget (just N reads + N multiplies per sample)
- Feedback at 0.99 must not blow up (tanh limiter + normalization)
- V/Oct: 1V = double frequency
- Patterns: uniform vs fibonacci should sound clearly different
- Sitar mode: amplitude-dependent pitch wobble audible at high feedback
- Xform gate fires randomization, integer params snap correctly
- All 7 plies display correctly in collapsed and expanded views

## Key Reference Files
- `mods/spreadsheet/MultitapDelay.h/.cpp` -- buffer, feedback, tap patterns
- `mods/spreadsheet/Filterbank.h/.cpp` -- energy followers, dirty-check pattern
- `mods/spreadsheet/assets/MultitapDelay.lua` -- 7-ply layout, control reuse
- `mods/spreadsheet/assets/TapListControl.lua` -- list control scrolling
- `mods/spreadsheet/assets/TransformGateControl.lua` -- xform gate pattern
- `eurorack/stmlib/dsp/delay_line.h` -- interpolation reference
