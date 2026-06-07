# Phase 0c findings — Three Sisters model vs hardware

Output of `planning/refs/three-sisters-hardware/compare.py` against
the 25-capture corpus, plus interpretation for the C++ redesign.

Companion: `planning/canals-baseline-scorecard.md` (per-take
numbers), plot overlays at `planning/refs/three-sisters-hardware/analysis/`.

## TL;DR

**The Python model's topology is correct.** Every divergence we
found is either (a) a calibration constant the model defaults wrongly,
(b) a hardware tolerance/drift that the model could capture if we
chose to model it, or (c) an analysis artifact (peak-detection
on rolloff shapes). No fundamental "the model is wrong about how
this filter works" findings emerged.

The eight issues from the original `canals-vs-three-sisters-findings.md`
audit all stand. The redesign plan in
`planning/canals-spreadsheet-redesign.md` doesn't need substantive
revision — only calibration constants and a few new wrinkles
discussed below.

## Calibration constants for the C++ implementation

These should land in the C++ Canals as default parameter mappings:

| Constant | Model default | Hardware-fitted value | Use |
|---|---|---|---|
| `F0_NOON_XOVER` | 110 Hz | **420 Hz** | Noon-FREQ center cutoff in XOVER mode |
| `F0_NOON_FORMANT` | (same as XOVER) | **670 Hz** | Noon-FREQ center in FORMANT mode (+8 semitones above XOVER — likely an analog circuit detail, possibly bias-point shift in mode switch path) |
| `SPAN_NOON_RATIO` | (knob-mapped) | **~1.0-1.2×** | Stages converge at FREQ when SPAN is at noon. SPAN min collapses fully, SPAN max separates ~2x. |
| `Q_NOON` | (knob-mapped) | **~0.7** | Butterworth-ish at noon |
| `Q_3OC` | (knob-mapped) | **~14** (per stage; ~28 stacked on CENTRE) | Strong but not self-osc |
| `Q_CW` | (knob-mapped) | **~50+** | Self-osc edge |
| Q knob curve | linear | **cubic or higher** | Q knob is nonlinear; CCW half goes through 0.3 (anti-res) → 0.5 → 0.7 at noon, then CW half ramps fast to ~50 |

These all sit in the Lua wrapper's `biasMap` or in C++ block-rate
coefficient bake. None of them affect topology.

## Wrinkles discovered (not in the original findings doc)

### 1. FORMANT mode shifts the center frequency

XOVER noon: 420 Hz. FORMANT noon: 670 Hz. Same FREQ knob position,
different effective center. ~+8 semitones difference (× 1.595).

Hypotheses:
- Three Sisters' FORMANT mode might do `clo = chi = FREQ × constant`
  where constant > 1 (vs model assumption `clo = chi = FREQ`)
- Or the FREQ knob's exponential mapping has a mode-dependent
  bias point
- Or the FORMANT output tap (lp of SVF2 after hp of SVF1) presents
  a peak offset from the cutoff at high Q (consistent with the
  BP-cascade resonance behavior)

The most parsimonious explanation is option 3: the BP magnitude
of an HP→LP cascade at cutoff fc peaks slightly above fc due to
the HP's −3dB shoulder. The model already accounts for this
analytically; what it doesn't account for is **how much** the
peak shifts in the limit of equal cutoffs.

**For C++ redesign**: model FORMANT with an empirical f0 multiplier
or just match the model's analytical prediction at high Q with
hardware. Document the difference; don't add a kludge constant.

### 2. SPAN at noon is unity (or very close)

At Q=3oc on CENTRE in XOVER mode, hardware shows a **single peak**
at ~420 Hz rather than the two split peaks the model predicts when
SPAN > 1.

Interpretation: noon SPAN corresponds to a small octave separation
(maybe 0.1-0.3 octave), enough to be different from "fully collapsed"
but not enough to resolve as two peaks at any Q below maximum.

**For C++ redesign**: knob mapping is `span_octaves = knob × N`
where N is the full-CW value (probably 2-3 octaves). Noon = N/2,
half-octave separation. Match by ear if hardware capture from
SPAN extremes is needed.

### 3. Self-oscillation THD is ~1-2%, not 0.013%

Model's `svf_tick_nl` with negative damping + tanh-on-bp produces
nearly pure sine (0.013% THD as plotted in `selfosc.png`).

Hardware shows 1.220% THD at cv0v self-osc, 2.240% at cv-1v
self-osc. **100x more harmonic content** than model.

Hypotheses:
- The analog circuit has multiple nonlinearity sources (transistors,
  diodes, supply rails) beyond the single tanh in the model
- The model's THD measurement is at the ideal limit-cycle amplitude;
  real circuit may have a larger limit cycle that's pushed further
  into nonlinear regime
- Could just be the model's k-negative value differs from the
  hardware operating point

**For C++ redesign**: choices —
- (a) Accept idealized clean self-osc (model's behavior); ship as
  "more pure than original" character
- (b) Add a second nonlinearity (e.g., spiralFastSaturate on the
  output, or tanh on ic2eq as well as ic1eq) to introduce harmonic
  content matching the real unit's signature
- (c) Make the nonlinearity intensity user-controllable as a hidden
  "warmth" knob

Recommendation: **option a** for v1 (ship clean self-osc; it's
not strictly worse, just different). Revisit if user feedback
says the missing harmonic warmth is a problem.

### 4. V/Oct tracking is excellent — don't add drift

Set C and Set G show 1V/oct tracking within ±0.08 octave (=~1.4
semitones at the extremes, mostly under 1 semitone). The model
assumes perfect tracking and that's close enough to hardware.

Earlier hypothesis from session: "real hardware has ~13 cents
of drift; add to C++ implementation for analog feel." **Drop
this**. The drift exists but at sub-perceptual amounts; the model's
exact tracking is fine.

### 5. LP/HP rolloff peak detection in compare.py is broken

For LOW (XOVER) at Q ≤ 0.7, there's no peak — just LP rolloff.
The argmax of the magnitude is at the lowest analyzed frequency
(20 Hz). Hardware capture also has rolloff but with various
analog artifacts; argmax picks a different "peak" around 300-400 Hz.

This is a **script artifact**, not a model issue. The scorecard's
huge peak errors on Set E rows at low Q are noise.

**Fix for future iterations**: detect response shape (LP/HP/BP) and
use shape-appropriate characteristic frequency:
- BP: peak frequency (argmax)
- LP: −3dB cutoff (where magnitude falls to 0.707 × DC value)
- HP: −3dB cutoff (where magnitude rises to 0.707 × infinity value)

Not blocking for redesign work; flagged for v2 of the compare script.

## Confirmed from original findings audit

| Issue # | Status | Evidence |
|---|---|---|
| 1. π missing from frequency coefficient | **Still applies to biome::Canals** (we didn't measure biome here). Trivial fix in C++ Canals. |
| 2. LOW/HIGH dual-stage resonance | **CONFIRMED on hardware**: CENTRE Q=3oc is +3.6 dB hotter than LOW Q=3oc → real Three Sisters has single resonance on LOW. |
| 3. Self-oscillation real | **CONFIRMED**: Q=CW with no input sustains tone, near-sinusoidal (crest 1.44). tanh-in-loop is the right family. |
| 4. High-Q distortion locus | Not directly measured; harmonic THD analysis (Issue 3 family) suggests the saturation is in the resonance feedback as the model assumes. |
| 5. Anti-resonance topology notch | Not separately measured; the captures at Q=CCW are baseline characterization, not notch-depth analysis. Add a dedicated test if needed. |
| 6. Control-rate only | Out of scope for hardware characterization; this is an implementation choice for the C++ unit. |
| 7. Single in / single out | Hardware confirms 4-out fan-out is the defining surface. |
| 8. HIGH formant stage order | Set B HIGH formant has +0.55 oct peak error suggesting topology difference but not definitively the swapped order; needs targeted measurement. |

## Implications for the redesign

1. **Defaults**: change `F0` constant to 420 Hz (XOVER). Add a
   FORMANT-mode F0 multiplier of ~1.595 (or compute analytically
   from the topology if we can derive it).
2. **SPAN knob mapping**: noon = small (~0.1-0.3 octave), max =
   ~2-3 octaves. Roughly linear in octaves.
3. **Quality knob mapping**: nonlinear, with strong CW expansion:
   - CCW half: ramp through anti-resonance (q ∈ [0.3, 0.7])
   - CW half: cubic or quartic expansion through resonance
     (q ∈ [0.7, 50+])
4. **Self-osc**: model says clean sine; hardware says 1-2% THD.
   Ship clean by default; add color later if requested.
5. **V/Oct**: exact 1V/oct in the implementation, no drift compensation.
6. **FORMANT topology**: hardware behavior subtly differs from model;
   investigate before locking. Possible direction: measure at SPAN
   extremes in FORMANT mode to characterize how SPAN affects the
   formant peak position.

## Open questions for next analysis session

1. **biome::Canals direct measurement** — the original Phase 0d
   plan included running biome::Canals through the same sweep
   battery via a C++ harness. We haven't built that harness yet.
   It would quantify the π bug directly and show the dual-stage
   over-resonance on LOW. Maybe skip if the redesign plan is
   already clear enough.
2. **SPAN knob characterization** — extreme positions (full CCW,
   full CW) would tell us the SPAN curve. The current corpus only
   has SPAN at noon.
3. **FORMANT topology deep-dive** — if the +8 semitone offset is
   structural, we want to understand the analog circuit detail
   that produces it before locking the C++ mapping.
4. **LP/HP cutoff calibration via -3dB fitting** — a v2 of the
   compare script would extract cutoff frequencies properly and
   give us calibrated mappings for the LP/HP rolloff shape vs Q.

None of these are blocking for starting Phase 1 (C++ skeleton).
The redesign can proceed with the calibration constants we have
and these questions can be answered during the iteration.
