# Canals → spreadsheet redesign

Folding the existing `biome::Canals` Three Sisters clone into the
spreadsheet package as a more fully-fledged unit. Driven by an
external research document comparing the current implementation
against the real Three Sisters topology (see *Source references*
below).

Working name: **Canals** (preserve continuity). Open to rename
during Phase 5; spreadsheet conventionally uses single-word
evocative names (Larets, Visadhara, Network, Helicase…) but
Canals already carries the identity.

## Why redesign now

The existing `biome::Canals` is functionally a filter but
diverges from the Three Sisters topology in several substantive
ways that change calibration, character, and modulation
bandwidth. The new research validates a corrected model
(`planning/refs/three-sisters-model/`) with a measurement
harness. Spreadsheet is the right home for the redesign because:

- The framework supports multi-output units (LOW / CENTRE /
  HIGH / ALL — the defining surface of Three Sisters)
- Spreadsheet units conventionally carry visualization and
  audio-rate modulation surfaces (Filterbank, Petrichor,
  Helicase precedents)
- The biome version is a 5-param scope-cut; spreadsheet
  affords the room for the full topology

## Source references

All unzipped to `planning/refs/three-sisters-model/`:

| File | Role |
|---|---|
| `canals-vs-three-sisters-findings.md` | Issue audit, ranked by impact |
| `three_sisters_svf.c` | C reference model (ZDF SVF + block routing) |
| `ts_model.py` | Python mirror of the C model for offline analysis |
| `harness.py` / `measure.py` | Measurement scaffold (sweep + FFT) |
| `responses.png` / `selfosc.png` | Validation plots |

The C model is the source-of-truth target; the Python model is
the offline analysis tool. The PNGs show the canonical filter
response shapes.

## Issue audit (from findings, ranked by impact)

| # | Issue | Severity | Fix |
|---|---|---|---|
| 1 | `g = f*(1+f²·0.333)` — missing leading π → everything tuned 3.14× flat | Calibration broken | `g = tan(π·f)` (libm) or stmlib dirty approximation **WITH** π term: `g = π·f + 0.3736·π³·f³` |
| 2 | LOW/HIGH cascade resonates BOTH stages; real unit only resonates SVF1 | Over-resonates, double peak | Pass `q = q2_fixed` (~0.707) to stage 2 on LOW/HIGH blocks. CENTRE keeps Q on both. |
| 3 | No true self-oscillation (softClip only attenuates) | Defining character missing | tanh on bp integrator state inside the loop: `ic1eq = tanh(2*v1 - ic1eq)`. Validated at 0.013% THD. |
| 4 | High-Q distorts where real unit stays clean (state-slam) | Wrong saturation locus | Move saturation into the resonance feedback loop (tanh inside), not on output of integrators. Issue 3's fix subsumes this. |
| 5 | Anti-resonance = generic dry/wet, not the topology notch | Wrong notch phase & depth | Tap the topology-correct complement: LOW → SVF1.hp, HIGH → SVF1.lp, CENTRE → SVF1.lp + SVF2.hp. Mix `main + anti*comp`. |
| 6 | Control-rate only; one v/oct sample per frame, gated on threshold change | No audio-rate FM, no self-patch | Per-sample coefficient recompute. Polynomial tan approximation keeps CPU acceptable. |
| 7 | Single IN / single OUT (knob-crossfaded) vs 4×4 | Scope cut | Multi-output framework — sub-out 1 = ALL (primary), sub-outs 2-4 = LOW/CENTRE/HIGH. Multi-IN deferred to v2 (see *Open design questions*). |
| 8 | HIGH formant stage order swapped (lp→hp vs real hp→lp) | Minor; changes which tap is the anti-res complement | Mirror real Three Sisters: SVF1 always processes the hp tap first; SVF2 is lp on formant, hp on crossover. |

Plus one observation I had reading the current code (not in
findings):

- The Output crossfade math at `pos > 2.0` (mapping toward ALL)
  uses `wL = t*0.333, wC = t*0.333, wH = (1-t) + t*0.333`. That
  collapses the HIGH stage progressively and biases toward L+C+H
  unevenly — the ALL point isn't a balanced 1/3-each sum. With
  multi-output ALL as its own sub-out, this knob goes away.

## New unit identity

### Surface

- **2 inlets**: `In` (audio), `V/Oct` (1V/octave pitch CV), `FM`
  (audio-rate frequency modulation, linear or exponential —
  default exponential for self-patch). Maybe also Q CV — see
  *Open design questions*.
- **4 outlets** (multi-output via stolmine framework):
  - **Out 1**: `ALL` (sum of LOW/CENTRE/HIGH) — primary,
    auto-wired by chain insertion. The "compatible with vanilla
    fallback" choice.
  - **Out 2**: `LOW`
  - **Out 3**: `CENTRE`
  - **Out 4**: `HIGH`
- **6 plies**:
  - `Fund` — fundamental pitch offset in semitones (current
    `Fundamental`)
  - `Span` — band spread (semitones from fundamental for
    LOW/HIGH)
  - `Q` — quality / resonance, bipolar (anti-res ↔
    Butterworth ↔ self-osc)
  - `FM` — FM index / depth (attenuverter on the FM input)
  - `Mode` — Crossover / Formant (option, 2-value)
  - `Out` — DEPRECATED — replaced by multi-output sub-outs.
    Removed from the surface.

### Defining character (compared to biome)

1. **Calibrated pitch** — coefficient bug fixed; FREQ knob means
   what it says.
2. **Clean resonance + self-oscillation** — tanh-in-loop
   self-osc, near-sinusoidal tone, 0.013% THD as measured.
   Quality knob at the top end produces a sustained sine choir
   when Centre is selected, or single pings on LOW/HIGH.
3. **Audio-rate FM via dedicated input** — enables the
   2-operator self-patch (route CENTRE out → FM in, sweep
   between coupled and FM regimes).
4. **Parallel 4-output surface** — use any combination of
   LOW/CENTRE/HIGH/ALL as a spectral mixer, three-source
   crossfader, or just monitor a single tap.
5. **Topology-correct anti-resonance notch** — real complementary
   subtraction, not input-minus-output approximation.

## DSP details

### SVF kernel (corrected)

Per `three_sisters_svf.c`. Single-precision throughout (NEON /
A8-friendly per
`feedback_cortex_a8_no_double_in_hot_loops` — though Canals is
mono, so NEON isn't a vector opportunity here, just regular
float).

```cpp
struct SistersSvf {
  float ic1eq = 0.0f, ic2eq = 0.0f;
  // Coefficients baked per sample (audio-rate modulation):
  float g, k, a1, a2, a3;

  void setFreqQ(float fc, float fs, float Q) {
    g  = fastTan(M_PI * fc / fs);  // see polynomial section
    k  = 1.0f / Q;
    float denom = 1.0f / (1.0f + g * (g + k));
    a1 = denom;
    a2 = g * a1;
    a3 = g * a2;
  }

  // Linear tick (non-resonant 2nd stage on LOW/HIGH).
  struct Out { float lp, bp, hp; };
  Out tick(float v0) {
    float v3 = v0 - ic2eq;
    float v1 = a1 * ic1eq + a2 * v3;
    float v2 = ic2eq + a2 * ic1eq + a3 * v3;
    ic1eq = 2.0f * v1 - ic1eq;
    ic2eq = 2.0f * v2 - ic2eq;
    Out o; o.bp = v1; o.lp = v2; o.hp = v0 - k * v1 - v2;
    return o;
  }

  // Nonlinear tick (resonant stages — self-osc + clean high-Q).
  Out tickNL(float v0) {
    float v3 = v0 - ic2eq;
    float v1 = a1 * ic1eq + a2 * v3;
    float v2 = ic2eq + a2 * ic1eq + a3 * v3;
    ic1eq = fastTanh(2.0f * v1 - ic1eq);  // bounded → limit cycle
    ic2eq = 2.0f * v2 - ic2eq;
    Out o; o.bp = v1; o.lp = v2; o.hp = v0 - k * v1 - v2;
    return o;
  }
};
```

### Block routing (corrected)

```cpp
struct CanalsBlock { SistersSvf s1, s2; };

// LOW: SVF1 resonant lp → SVF2 fixed lp(xover) / hp(formant)
float lowTick(CanalsBlock &b, float in, float fcLow,
              float fs, float q, float qFixed, bool xover, float &compOut) {
  b.s1.setFreqQ(fcLow, fs, q);
  b.s2.setFreqQ(fcLow, fs, qFixed);
  auto o1 = b.s1.tickNL(in);
  auto o2 = b.s2.tick(o1.lp);
  compOut = o1.hp;
  return xover ? o2.lp : o2.hp;
}

// HIGH: SVF1 resonant hp → SVF2 fixed hp(xover) / lp(formant)
float highTick(CanalsBlock &b, float in, float fcHigh,
               float fs, float q, float qFixed, bool xover, float &compOut) {
  b.s1.setFreqQ(fcHigh, fs, q);
  b.s2.setFreqQ(fcHigh, fs, qFixed);
  auto o1 = b.s1.tickNL(in);
  auto o2 = b.s2.tick(o1.hp);
  compOut = o1.lp;
  return xover ? o2.hp : o2.lp;
}

// CENTRE: BOTH SVFs resonant. SVF1 hp@cLow → SVF2 lp@cHigh (xover)
//                              or @cFreq for both (formant)
float centreTick(CanalsBlock &b, float in, float cLow, float cHigh,
                 float fs, float q, bool xover, float &compOut) {
  float chi = xover ? cHigh : cLow;  // formant: collapse to one freq
  b.s1.setFreqQ(cLow, fs, q);
  b.s2.setFreqQ(chi, fs, q);
  auto o1 = b.s1.tickNL(in);
  auto o2 = b.s2.tickNL(o1.hp);
  compOut = o1.lp + o2.hp;
  return o2.lp;
}
```

Per-block output = `main + anti * comp` where `anti ∈ [0, 1]`
is derived from the Q knob's negative half.

### Polynomial approximations

**fastTan** for `g = tan(π·f)` where `f = fc/fs ∈ [0.001, 0.499]`:

```cpp
// stmlib FREQUENCY_DIRTY corrected with π factor.
// Valid range: f ∈ [0, ~0.4]. At f = 0.499 use libm tan() instead
// for high-freq fidelity (rare in audio range, ~24kHz @ 48k).
static inline float fastTan(float pif) {
  float p2 = pif * pif;
  return pif * (1.0f + p2 * (0.3333333f + p2 * (0.1333333f + p2 * 0.0539683f)));
}
```

That's a 4-term Taylor expansion of `tan(x) = x + x³/3 + 2x⁵/15
+ 17x⁷/315 + ...`. ~7 multiplies + 3 adds = ~15 cycles A8.

**fastTanh** for the in-loop nonlinearity:

```cpp
// Padé[3/2] tanh approximation: x*(27+x²)/(27+9x²)
// Accurate to ~0.5% for |x| < 3, smoothly saturating beyond.
static inline float fastTanh(float x) {
  float x2 = x * x;
  return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}
```

3 multiplies + 1 divide ≈ 35 cycles A8 (the divide dominates).
Could replace with reciprocal-multiply if divide cost shows in
profiling.

### Per-sample coefficient update budget

Per sample, mono:
- 3 blocks × 2 SVFs = 6 SVF instances
- Each setFreqQ: fastTan + 2 multiplies + 1 reciprocal (denom)
  = ~25 cycles
- Each tick: ~12 multiplies + 8 adds = ~25 cycles
- Each tickNL: above + fastTanh = ~60 cycles

Per sample budget (worst case, CENTRE in xover with NL on both):
- 6 setFreqQ ≈ 150 cycles
- 4 tickNL + 2 tick ≈ 290 cycles
- Anti-res mix, output sum, ALL mix ≈ 20 cycles
- **Total ≈ 460 cycles per sample**

At 48k mono on Cortex-A8 (720 MHz): `460 × 48000 = 22M cycles/s
= ~3% CPU`. Comparable to a single voice of Helicase.

If profile shows hot, the per-sample setFreqQ can be amortized
to per-N-samples (e.g. every 4) without losing audio-rate FM
character — the tan approximation slews fast enough that
even 4-sample coefficient steps are inaudible above ~50Hz FM.

## Multi-output framework integration

Per `docs/multi-output-units-author-guide.md` and
`feedback_unit_output_names_table`:

```lua
function Canals:init(args)
  args.title = "Canals"
  args.mnemonic = "Cn"
  args.channelCount = 4
  args.subOutLabels = { "ALL", "LOW", "CENTRE", "HIGH" }
  Unit.init(self, args)
end

function Canals:onLoadGraph(channelCount)
  local op = self:addObject("op", libspreadsheet.Canals())
  connect(self, "In1", op, "In")
  -- Outputs 1..4: ALL, LOW, CENTRE, HIGH
  connect(op, "All",    self, "Out1")
  connect(op, "Low",    self, "Out2")
  connect(op, "Centre", self, "Out3")
  connect(op, "High",   self, "Out4")
  -- ... ParameterAdapter ties for Fund, Span, Q, FM, Mode ...
end
```

Sub-out 1 = ALL because that's the most useful default when
inserting on a chain — the unit auto-wires to a representative
mix. LOW/CENTRE/HIGH on subs 2-4 expose the parallel taps.

## Visualization

Filter response graphic: real-time 3-peak display showing the
LOW/CENTRE/HIGH band positions as moved by Fund + Span. Peaks
animate with audio-rate FM. Per `feedback_viz_encoder_capture_architectural`:
use tile-granular drawing + state cache for low CPU.

Existing reference: Filterbank's `FilterResponseGraphic.h` has
the response-curve drawing primitives we'd reuse. Single-channel
output of Canals lets us draw a clean magnitude vs frequency
curve with three peaks instead of Filterbank's N-band display.

Open question: should the graphic also show self-osc level (a
distinct "in self-oscillation" indicator when bp state crosses a
threshold)? Decide during visualization phase.

## Implementation phases

**Phase 0 — Measurement + validation** (separate session, ~60-90 min capture + ~60 min analysis)

The Python reference model is a HYPOTHESIS about the real Three
Sisters; only hardware closes the loop. This phase establishes
the ground-truth target before any C++ work begins.

Sub-steps:

- **0a. Python harness self-test** — run `python measure.py` in
  a venv with numpy/scipy/matplotlib. Expected: RMS error sub-1dB
  on the synthetic DUT pipeline, fc/q/q2 fit within a few percent
  of injected truth. Validates the analysis chain.
- **0b. Real Three Sisters hardware capture battery** — play
  `sweep.wav` (generated by `write_sweep`) into the module via
  audio interface, record each output back. Battery covers:
  - Each block isolated: LOW / CENTRE / HIGH / ALL outs, IN=ALL
  - XOVER and FORMANT modes
  - 4 FREQ positions across V/Oct range (calibration)
  - 4 Q positions (anti, noon, ~75%, full CW — self-osc edge)
  - Ringdown captures (impulse → recording) at 3-4 cutoffs for Q-vs-freq
  - Self-osc capture at Q max, FREQ=363Hz, no input, ~5-10s record
    (for THD comparison against model's 0.013% claim)
  - Total: ~24 captures, 30-60 min at the rig + 30 min documentation
- **0c. Validate Python model against hardware** — `analyze_sweep`
  each hardware capture, compare to `block_magnitude` with same
  nominal params. If RMS error within ~1dB across audible band,
  model is sound. If gaps surface, fix the model FIRST before
  redesigning the C++ implementation against a flawed target.
- **0d. C++ harness for `biome::Canals` baseline** — standalone
  binary linking `SistersSvf.h` + `Canals` routing. Reads
  sweep WAV, processes with fixed params, writes output WAV.
  Repeat for each (block, mode, Q, FREQ) point in the battery.
  Run all outputs through `analyze_sweep` + `fit_block_to_mag`.
  Produces the baseline divergence scorecard.
- **0e. Document scorecard** — `planning/canals-baseline-scorecard.md`
  with each (block, mode, setting) showing measured vs target
  in dB error / fc error / Q error. This is what each subsequent
  phase moves toward zero.

Outputs from Phase 0 that drive the rest of the work:
- Validated Python model (or corrected model if hardware exposed gaps)
- Hardware capture corpus (committed to `planning/refs/three-sisters-hardware/`)
- C++ measurement harness reusable for spreadsheet::Canals
- Baseline scorecard per (block, mode, Q, FREQ)

**Phase 1 — Carbon copy to spreadsheet** (~30 min)
- Copy biome `Canals.cpp/.h/.lua` + `SistersSvf.h` to spreadsheet
  with `stolmine` namespace renamed to `stolmine` (or
  `spreadsheet` — check convention by examining existing
  spreadsheet code)
- Register in `mods/spreadsheet/spreadsheet.cpp.swig`
- Add to `mods/spreadsheet/assets/toc.lua`
- Bump spreadsheet PKGVERSION
- Build both arches, install linux, verify it loads + sounds
  identical to biome version

**Phase 2 — Frequency calibration + Q-placement** (~30 min)
- Fix `fastTan` to include π (Issue #1)
- LOW/HIGH use `qFixed = 0.7071` on stage 2 (Issue #2)
- Fix HIGH formant stage order (Issue #8)
- Add topology-correct anti-res complement taps (Issue #5)
- Audition: pitch should be calibrated; LOW/HIGH should sound
  flatter (less compound resonance)

**Phase 3 — Self-oscillation + clean resonance** (~45 min)
- `fastTanh` Padé approximation
- `tickNL` variant with in-loop tanh on bp state (Issues #3, #4)
- Wire NL into resonant stages only (LOW.s1, HIGH.s1, CENTRE.s1,
  CENTRE.s2)
- Audition: Q at max should produce sustained sine choir on
  CENTRE, single pings on LOW/HIGH. Clean tone, no static.

**Phase 4 — Audio-rate FM + dedicated FM input** (~30 min)
- Per-sample coefficient recompute (drop the `prev*` gating)
- Add FM inlet + FM ply (depth/index)
- Audio-rate cutoff = base + voct[i] + FM_depth × fmIn[i]
- Audition: self-patch CENTRE → FM in → 2-op FM character

**Phase 5 — Multi-output surface** (~30 min)
- `channelCount = 4`, `subOutLabels = {"ALL", "LOW", "CENTRE", "HIGH"}`
- 4 outlets in C++ Object
- Drop the Output crossfade knob; the parallel outs replace it
- Build both arches, install, verify M6 cycles through subs

**Phase 6 — Filter response visualization** (~60 min)
- New `CanalsResponseGraphic.h` (no-out-of-line-virtuals per
  `feedback_no_out_of_line_virtuals`)
- 3-peak response curve with audio-rate sample of current
  fundamental + span + Q
- Audio-rate FM modulation should be visible as peak movement
- Audition + iterate

**Phase 7 — Deprecate biome::Canals** (~15 min)
- Remove from `mods/biome/assets/toc.lua`
- Mark Canals.cpp/.h with `// SUPERSEDED BY spreadsheet::Canals`
  banner comment (don't delete; preserve for diff reference)
- Note in CHANGELOG-equivalent (todo.md)

**Phase 8 — Tune + ship** (~30-60 min)
- Calibrate Q curve (current cubic is fine for biome, audition
  against the corrected model)
- Calibrate FM depth range
- AW-style audit per `feedback_aw_param_default_subtle`:
  defaults should produce immediately characterful sound
- Verify CPU on hardware (~3% mono is the projection)
- Write release notes

## AW scalar / defaults audit

Per `feedback_aw_param_default_subtle`, defaults should be
characterful, not transparent:

| Ply | Default | Why |
|---|---|---|
| Fund | 0.0 (= 261Hz / middle C) | Center of audible range, calibrated |
| Span | 0.3 (~14 semitones) | Wide enough to hear distinct LOW/HIGH bands |
| Q | 0.4 | Audibly resonant, well below self-osc edge |
| FM | 0.0 | Off by default (no audio at FM in unless patched) |
| Mode | 0 (Crossover) | More musically useful as a default than Formant |

## Cortex-A8 considerations

- All math single-precision float (mono, no NEON vectorization
  to gain from)
- No stack-local NEON arrays (per `feedback_neon_intrinsics_drumvoice`),
  but irrelevant here — no NEON intrinsics used
- `-fno-tree-vectorize` already set globally for am335x in
  spreadsheet's mod.mk (per `feedback_disable_tree_vectorize_am335x`).
  Verify before building.
- Polynomial approximations (fastTan, fastTanh) keep us off the
  libm call boundary that triggers AAPCS NEON spill issues (per
  `feedback_neon_aapcs_call_barrier`)
- Per-sample work is sequential (depends on prior states); no
  vectorization opportunity. Just clean scalar code.

## Open design questions

1. **Quality bipolar or two-knob (Q + AntiRes)?** Current biome
   is bipolar Q. Splitting into separate Q (0..1) and AntiRes
   (0..1) knobs gives more independent control but adds a ply.
   Bipolar is more compact; both fit on a 6-ply surface. I'd
   lean bipolar for v1 — matches the real unit's single Quality
   knob.

2. **Multi-input or single IN?** Real Three Sisters has
   LOW/CENTRE/HIGH/ALL ins (4 inputs feeding the 3 blocks
   selectively, with ALL feeding all three). For v1: single IN
   that feeds all three blocks (= the real unit's ALL input
   behavior). Multi-input deferred. Yes/no?

3. **Mode picker — 2 modes (current) or expand?** Crossover +
   Formant are the real unit's two modes. A third "parallel
   bandpass" mode could be cute (independent LOW/CENTRE/HIGH
   bandpasses, no cross-band routing) but isn't documented in
   real Three Sisters. Stay 2-mode.

4. **Linear or exponential FM input?** Three Sisters' real FM
   input is documented as 1V/oct exponential (i.e. functionally
   identical to V/Oct, just with a separate attenuverter).
   Linear FM would enable 2-op FM character closer to a Yamaha
   DX. Default exponential matches real unit; linear could be a
   Mode option in the FM signal path. Defer to v1.5.

5. **Q CV input?** Adds another inlet. Useful for sympathetic
   resonance patches (envelope → Q for percussion-style swells).
   Not present on real unit. Defer for v1.

6. **Name?** Stay "Canals" (continuity) or rename per spreadsheet
   convention (single evocative word). Most votes for "stay" —
   the package's identity already includes Canals.

7. **Output knob — fully gone, or repurposed?** The Output
   crossfade knob disappears with multi-output, but the surface
   loses a knob. Could repurpose as a "Drive" or "Mix" knob.
   Default plan: leave the slot empty; 5-ply surface is fine.

8. **Filter response graphic interactivity?** Static display
   driven by params, or click/drag-able to set Fund/Span
   directly? Default: static display (matches Filterbank
   convention); add interaction later if useful.

## Memory cross-references

- `docs/multi-output-units-author-guide.md` — sub-out contract,
  vanilla compatibility
- `feedback_unit_output_names_table` — auto-generated Out1..Out99
  in stolmine post-2026-04-30
- `feedback_aw_param_default_subtle` — defaults produce
  characterful sound out of the box
- `feedback_cortex_a8_no_double_in_hot_loops` — keep single-
  precision in hot loop
- `feedback_disable_tree_vectorize_am335x` — global flag in mod.mk
- `feedback_no_out_of_line_virtuals` — graphics class virtual
  methods must be header-defined
- `feedback_viz_encoder_capture_architectural` — graphic CPU lessons
- `feedback_spreadsheet_effect_positioning` — Canals already lives
  in this territory map (resonant filter cousin to Tomograph
  /Pecto/Network)
- `planning/refs/three-sisters-model/` — source-of-truth model
  and validation harness

## Identity in one sentence

A multi-output resonant filter unit with calibrated pitch, clean
self-oscillation, audio-rate FM, and parallel LOW/CENTRE/HIGH
taps — a 4-output spectral splitter, three-source crossfade
buss, or 2-op self-patch oscillator depending on how it's wired.
