# Three Sisters hardware capture corpus — 2026-06-07

Audio captures of a real Whimsical Raps Mannequins Three Sisters
filter module, recorded for the canals → spreadsheet redesign work
(see `planning/canals-spreadsheet-redesign.md`).

These are the **ground-truth target** for redesign Phase 0c: the
Python model in `planning/refs/three-sisters-model/` is a
hypothesis; comparing it to this corpus validates (or corrects)
the model before the C++ implementation is held to either.

## Rig

- **Audio interface**: MOTU M4 (USB Class 2, 48 kHz, 24-bit)
- **Excitation playback**: sox → pipewire pulse compat → MOTU
  Line2 sink (= analog outputs 3+4 on the M4)
- **Pre-amp / level conversion**: ER-301 acts as line→euro amplifier.
  A VCA at 1.5× gain on the input chain compensates for the
  MOTU output level into the euro signal range.
- **DUT**: Three Sisters. Signal flow:
  - MOTU output 3 → ER-301 input → ER-301 chain (sample player +
    1.5× VCA) → ER-301 output → Three Sisters **ALL IN**
- **Return path**: Three Sisters output tap (varies per take:
  LOW / CENTRE / HIGH / ALL) → MOTU Line5 source (= analog inputs
  3+4, line level)
- **CV source**: ER-301 Constant CV unit → Three Sisters V/Oct
  input (used in Sets C, F, G)

## Files

- `sweep.wav` — exponential sine sweep, 20 Hz → 20 kHz over 8 s,
  stereo, −1 dBFS peak with 10 ms Hann fades. Used for transfer
  function captures (Sets A–E).
- `impulse.wav` — 1 ms Hann-windowed white-noise burst at t=100ms,
  1 s total duration, stereo, −1 dBFS peak. Broadband excitation
  for ringdown captures (Set F).
- `generate_excitation.py` — reproducible regenerator for both,
  uses fixed seed for the noise burst.
- `raw/` — 25 hardware captures, naming convention:

  `ts_<mode>_<out>_<freq>_<q>[_ring|_osc].wav`
  - `mode`: `xover` | `formant`
  - `out`: `low` | `centre` | `high` | `all`
  - `freq`: knob clock position (`12oc`) OR V/Oct CV (`cv-2v` ..
    `cv+2v` with `cv0v` baseline)
  - `q`: `ccw` | `9oc` | `12oc` | `3oc` | `cw`
  - `_ring` suffix = ringdown capture (impulse, ~1.2s)
  - `_osc` suffix = self-oscillation capture (no input, 10s)

## Capture sets

| Set | Count | Purpose |
|---|---|---|
| A | 4 | XOVER baseline — all 4 outputs at noon knobs |
| B | 4 | FORMANT baseline — same outputs/knobs |
| C | 4 | V/Oct calibration at ±1V, ±2V (CENTRE out) |
| D | 4 | Q sweep on CENTRE (CCW / 9oc / 3oc / CW) |
| E | 4 | Q sweep on LOW — for Issue #2 dual-stage vs single-stage comparison |
| F | 3 | Ringdowns at −1V / 0V / +1V (CENTRE, SPAN noon, Q 3oc) |
| G | 2 | Self-oscillation tone (Q CW, no input, 10s) at 0V and −1V |

The noon-Q CENTRE baseline (`ts_xover_centre_12oc_12oc.wav`)
serves double duty as the Set C cv=0V capture and the Set D
12oc-Q capture (same params). Similar reuse on Set E with
`ts_xover_low_12oc_12oc.wav`.

## Excitation level notes

Captures land at varied levels because:

- ER-301 1.5× VCA was set conservatively (room for high-Q
  resonance peaks without input-side clipping)
- Three Sisters output level varies with Q, FREQ, and topology
- Pipewire mono→stereo handling: WAVs are stereo (both channels
  carry the same signal) to avoid downmix halving

Typical observed peaks:
- Baseline (noon Q): −22 to −27 dBFS
- High Q (3 o'clock): −12 to −16 dBFS
- Self-oscillation (Q CW): −13 dBFS sustained
- Ringdowns: weaker SNR around −35 dBFS (broadband burst doesn't
  couple optimally to a narrow-Q resonator — Farina-deconvolved IR
  from sweep captures may provide cleaner Q estimates)

All captures verified non-clipping. Normalization happens at the
analysis stage; absolute levels are not load-bearing.

## Headline observations (from capture-time stats alone)

These are visible in the per-take peak/RMS/frequency stats before
any FFT analysis:

1. **Self-oscillation is real and clean**. Q full CW with no
   input produces a sustained tone, symmetric peaks
   (max +0.225 / min −0.225 at 0V), crest factor 1.44 (pure
   sine is 1.41). Confirms the model's tanh-in-loop self-osc
   approach is the right family.
2. **Self-osc fundamental at noon FREQ ≈ 304 Hz**; at V/Oct=−1V
   ≈ 163 Hz. Theoretical 1V/oct tracking would predict 152 Hz at
   −1V — ~13 cents sharp. Real analog drift signature; the
   redesign needs to expose the same tracking accuracy ceiling
   (don't try to be more accurate than the analog target if
   matching feel is the goal).
3. **CENTRE Q hotter than LOW Q at the same Q knob position**.
   At Q=3oc: CENTRE = −11.9 dBFS peak, LOW = −15.9 dBFS — about
   4 dB hotter. Confirms CENTRE's dual-resonant stages stack
   while LOW's single resonant stage doesn't. Direct evidence
   for Issue #2 in the findings doc.
4. **High-Q CENTRE shows asymmetric peaks** (max +0.302 / min
   −0.318 at Q CW). Sign of nonlinear regime — likely
   saturation in the resonance feedback loop. Single-stage LOW
   at Q CW stays symmetric (+0.178 / −0.177). Tells us the
   nonlinearity is per-stage and stacks audibly only when both
   stages are pushed.

## Analysis next steps (Phase 0c)

1. **Verify Python model against this corpus**. For each capture,
   run `analyze_sweep()` from `planning/refs/three-sisters-model/measure.py`
   to deconvolve transfer function, then overlay with
   `block_magnitude()` using the same nominal params. RMS error
   in audible band should be <1 dB if the model is sound.
2. **Quantify deviations**. Where model and hardware disagree,
   decide: model bug (fix model first), measurement artifact
   (re-take or note), or expected analog tolerance (document
   in design plan).
3. **Build the scorecard**. `planning/canals-baseline-scorecard.md`
   captures per-(block, mode, knob-setting) measured-vs-target
   numbers. This becomes the per-phase progress tracker for the
   C++ redesign work.

## Known caveats / things to flag in analysis

- Ringdown SNR weaker than expected — analysis pipeline should
  reach for the sweep-derived IR ringdown if direct ringdown
  capture proves too noisy for clean Q fitting.
- Self-osc captures contain ~10s of sustained tone but the first
  few hundred ms after the recorder starts may include startup
  transients as the filter's limit cycle stabilizes — trim or
  window appropriately.
- Set F SPAN was changed from "minimum" (per original checklist)
  to "noon" mid-session because at SPAN=min the impulse couldn't
  excite the narrow CENTRE peak with usable amplitude. The Set
  F captures therefore reflect CENTRE with both stages active
  at noon SPAN separation, not collapsed single peak.
