# Pecto: Rainmaker Comb Resonator for ER-301

## Context

The Rainmaker's comb section is a single feedback comb filter whose spectral shape is sculpted by multiple passive read taps on one shared delay line. All taps are fractions of a master "comb size" so the entire harmonic structure transposes together under V/Oct. This is distinct from the existing "Comb Bank" todo item (N independent parallel combs) -- Pecto is one resonant body with pattern-driven harmonic shaping.

We already have Petrichor (multitap delay with independent per-tap processing) and Filterbank (parallel SVFs with scale distribution). Pecto is cheaper than both: no per-tap filters, no granular pitch shift. Just N reads + weighted sum + one feedback loop.

## Files to Create

| File | Purpose |
|------|---------|
| `mods/spreadsheet/Pecto.h` | DSP class header, pimpl pattern |
| `mods/spreadsheet/Pecto.cpp` | DSP: buffer, tap patterns, feedback, resonators |
| `mods/spreadsheet/PectoInfoGraphic.h` | Info display: f0 freq, density, pattern, resonator type |
| `mods/spreadsheet/assets/Pecto.lua` | Unit wiring, 6-ply view layout |
| `mods/spreadsheet/assets/PectoInfoControl.lua` | Density ply with pattern/slope/resonator sub-display |

## Files to Modify

| File | Change |
|------|--------|
| `mods/spreadsheet/spreadsheet.cpp.swig` | Add Pecto, PectoInfoGraphic includes |
| `mods/spreadsheet/assets/toc.lua` | Register Pecto unit |

## C++ Architecture

### Internal Struct
```
buffer (int16 circular, 20s max ~1.875MB)
writeIndex
tapPosition[64]     -- 0-1 fractions of master size (from pattern)
tapWeight[64]       -- amplitude envelope (from slope)
fbFilterState       -- one-pole LP for Guitar resonator
sitarModState       -- amplitude tracker for Sitar mode
```

### Parameters
- **Global**: CombSize (0.1ms-20s exp, as fundamental Hz), Feedback (0-0.99), ResonatorType (0-3), Density (1-64), Pattern (0-15), Slope (0-3), VOctPitch, Mix, InputLevel, OutputLevel, TanhAmt
- **Per-tap**: None editable. Derived from Pattern + Slope + Density via `recomputeTaps()`.
- No LFO, no tone control -- users bring their own modulation, resonator type handles timbral character.

### Signal Flow
```
Input -> [write to buffer] -> [read N taps, weight, sum] -> wet output
              ^                                                |
              +-- [nonlinearity/filter] <- [feedback tap] <----+
```

Feedback is taken from the longest tap (tap N-1), passed through the resonator nonlinearity, and written back to the buffer at the write head.

### 16 Tap Patterns (from Rainmaker)
0: Uniform, 1: Fibonacci, 2: Early, 3: Late, 4: Middle, 5: Ess, 6: Flat, 7: Rev-Fibonacci
8-15: Randomized variants of 0-7 (seeded perturbation)

Tap positions are fractions of master comb size: `tapPosition[i] = pattern_func(i, density)`. All taps scale together under V/Oct.

### 4 Slope Types
- Flat (all taps equal amplitude)
- Rise (linear ramp up)
- Fall (linear ramp down)
- Rise-Fall (sine hump, center taps loudest)

### 4 Resonator Types
- **Raw**: direct wire (no processing on feedback path)
- **Guitar**: one-pole LP damping in feedback (Karplus-Strong frequency-dependent decay)
- **Clarinet**: `x - x^3/3` odd-harmonic nonlinearity
- **Sitar**: `delay_actual = delay_base + k * |fb_signal|` (amplitude-dependent delay modulation)

### Reuse from Petrichor
- int16 bufRead/bufWrite helpers
- Buffer allocation with fallback halving
- Feedback normalization: `fb / (1 + 0.15 * sqrt(N))`
- Linear feedback with tanh safety limiter (>1.5 amplitude only)
- V/Oct: ConstantOffset * 10 -> octaves, applied as `size * 2^(-voct)`

## Lua UI Layout (6 plies)

1. **tune** -- V/Oct (Pitch control, ConstantOffset)
2. **f0** -- Comb size as fundamental frequency (GainBias, Hz)
3. **density** -- Tap count 1-64 (GainBias integer), sub-display: pattern / slope / resonator (all with addName labels)
   - Expansion view: density, pattern, slope, resonator as full controls
4. **feedback** -- Feedback amount 0-0.99 (GainBias, simple, no shift SD)
5. **xform** -- TransformGateControl (reuse from Petrichor), targets adapted for Pecto
6. **mix** -- MixControl (reuse), shift-SD: input / output / tanh

No per-tap serialization needed. Standard Unit.serialize suffices.

## Xform Gate (Randomization)

Gate input on Comparator triggers randomization. Follows Petrichor's TransformGateControl pattern.

### Targets (single selector, 0-8)
0. **all** -- randomize everything below
1. **size** -- comb size / fundamental
2. **feedback** -- feedback amount
3. **resonator** -- resonator type (integer 0-3)
4. **density** -- tap count (integer 1-64)
5. **pattern** -- tap pattern (integer 0-15)
6. **slope** -- slope type (integer 0-3)
7. **mix** -- wet/dry mix
8. **reset** -- restore all to defaults

### C++ Implementation
- `od::Parameter *mBias*` pointers for each randomizable param (set from Lua via `setTopLevelBias`)
- `mXformTarget`, `mXformDepth` parameters
- `fireRandomize()` called from gate rising edge or manual trigger
- `applyRandomize()` uses depth to scale random deviation from current value
- Integer params (resonator, pattern, slope, density) snap to valid integer range

### Lua Wiring
- Comparator on XformGate inlet
- TransformGateControl (reuse from Petrichor) with target names adapted
- Sub-display: target / depth / fire
- `setTopLevelBias(0..N, adapter:getParameter("Bias"))` for each randomizable param

## Build Order

### Phase 1: Skeleton
- Pecto.h/cpp with buffer alloc, passthrough, CombSize/Feedback/Mix params
- Minimal Pecto.lua with GainBias controls (6 plies, no custom controls yet)
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

### Phase 5: Full UI
- PectoInfoGraphic.h (density display with pattern/resonator info)
- PectoInfoControl.lua (density ply with pattern/slope/resonator sub-display)
- Full 6-ply Pecto.lua with MixControl and TransformGateControl reuse
- Xform wiring with Pecto targets

### Phase 6: Polish
- Stereo support (mono flag, OutR)
- Parameter defaults tuning
- CPU profiling on Cortex-A8

## Verification

- 64 taps at 48kHz should be well within CPU budget (just N reads + N multiplies per sample)
- Feedback at 0.99 must not blow up (linear fb with tanh safety limiter)
- V/Oct: 1V = double frequency (halves comb size)
- Patterns: uniform vs fibonacci should sound clearly different
- Sitar mode: amplitude-dependent pitch wobble audible at high feedback
- Guitar mode: high frequencies decay faster (KS damping)
- Xform gate fires randomization, integer params snap correctly
- All 6 plies display correctly in collapsed and expanded views

## Key Reference Files
- `mods/spreadsheet/MultitapDelay.h/.cpp` -- buffer, feedback, tap patterns
- `mods/spreadsheet/Filterbank.h/.cpp` -- energy followers, dirty-check pattern
- `mods/spreadsheet/assets/MultitapDelay.lua` -- ply layout, control reuse
- `mods/spreadsheet/assets/TransformGateControl.lua` -- xform gate pattern
- `mods/spreadsheet/assets/MixControl.lua` -- MixControl with shift-SD
- `eurorack/stmlib/dsp/delay_line.h` -- interpolation reference
