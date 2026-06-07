# Canals — Three Sisters hardware capture checklist

Tactical session doc for the Phase 0b hardware capture battery
(see `planning/canals-spreadsheet-redesign.md`). Use at the rig.
Check off each capture as it's done.

## Pre-flight

- [ ] **Three Sisters** patched in Eurorack rig, output bus to audio interface input
- [ ] **Audio interface** connected: input gain set so peak-program is around −6 dBFS on Three Sisters' loudest tap (CENTRE at Q noon should be representative)
- [ ] **Sample rate** in DAW set to **48 kHz** (matches `measure.py`'s `FS`; change Python constant if you prefer 96k and re-run write_sweep)
- [ ] **`sweep.wav`** loaded onto front SD at `/mnt/ER-301/sweep.wav` and accessible via Sample Player unit on ER-301
- [ ] **ER-301 sample player output** routed to audio interface output → Three Sisters input (IN = the ALL input on the module, since v1 Canals is single-input)
- [ ] **Recording template** in DAW: 1 mono track per take, name template `ts_<mode>_<out>_<freq>_<q>.wav`, monitor enabled on track input for level checks
- [ ] **Photo**: take a phone photo of the rig + initial Three Sisters knob positions for archival
- [ ] **Notes file** open (paper or text) for any per-capture anomalies

## File naming convention

`ts_<mode>_<out>_<freq>_<q>.wav`

- `mode`: `xover` | `formant`
- `out`: `low` | `centre` | `high` | `all`
- `freq`: knob position in clock notation — `9oc`, `12oc`, `3oc` — OR V/Oct CV level — `cv-1v`, `cv0v`, `cv+1v`
- `q`: `ccw` (full anti-res) | `9oc` | `12oc` (Butterworth-ish) | `3oc` | `cw` (full / self-osc edge)

Examples: `ts_xover_centre_12oc_3oc.wav`, `ts_formant_low_cv+1v_12oc.wav`

For ringdowns and self-osc captures, use the suffixes
`_ring.wav` and `_osc.wav` respectively.

## Set A — Baseline shapes, XOVER mode

Knobs: FREQ noon, SPAN noon, Q noon, V/Oct CV at 0V (or
unpatched). Input = `sweep.wav` looped or one-shot.

- [ ] `ts_xover_all_12oc_12oc.wav`     — record from ALL output
- [ ] `ts_xover_low_12oc_12oc.wav`     — record from LOW output
- [ ] `ts_xover_centre_12oc_12oc.wav`  — record from CENTRE output
- [ ] `ts_xover_high_12oc_12oc.wav`    — record from HIGH output

## Set B — Baseline shapes, FORMANT mode

Same knobs as Set A but Mode switch to FORMANT.

- [ ] `ts_formant_all_12oc_12oc.wav`
- [ ] `ts_formant_low_12oc_12oc.wav`
- [ ] `ts_formant_centre_12oc_12oc.wav`
- [ ] `ts_formant_high_12oc_12oc.wav`

## Set C — V/Oct calibration sweep

XOVER mode, OUT = CENTRE, Q noon, SPAN noon. Vary V/Oct CV
across 4 positions (use a precise 1V/oct source — Quantizer
or precision adder). Record from CENTRE output each time.

- [ ] `ts_xover_centre_cv-2v_12oc.wav`  (−2V → −2 oct from base)
- [ ] `ts_xover_centre_cv-1v_12oc.wav`  (−1V)
- [ ] `ts_xover_centre_cv+1v_12oc.wav`  (+1V)
- [ ] `ts_xover_centre_cv+2v_12oc.wav`  (+2V)

(The cv0v baseline is captured in Set A as `ts_xover_centre_12oc_12oc.wav`.)

This set lets us verify the v/oct exponential conversion across
4 octaves of CV range.

## Set D — Q variation, CENTRE block

XOVER mode, OUT = CENTRE, FREQ noon, SPAN noon, V/Oct = 0V.
Vary the Quality knob across 5 positions.

- [ ] `ts_xover_centre_12oc_ccw.wav`   — full CCW (max anti-resonance)
- [ ] `ts_xover_centre_12oc_9oc.wav`   — 9 o'clock (mild anti-res / Butterworth)
- [ ] `ts_xover_centre_12oc_3oc.wav`   — 3 o'clock (mid-strong resonance)
- [ ] `ts_xover_centre_12oc_cw.wav`    — full CW (self-osc edge)

(The 12oc Q baseline is in Set A as `ts_xover_centre_12oc_12oc.wav`.)

CENTRE chosen because it has TWO resonant stages — gives the
clearest view of dual-resonant behavior. We'll do a second pass
on LOW for the single-stage comparison.

## Set E — Q variation, LOW block (single-stage reference)

Same as Set D but OUT = LOW. Validates that LOW only has ONE
resonant peak (per Issue #2 in findings) — biome::Canals has
two, so this is a critical comparison.

- [ ] `ts_xover_low_12oc_ccw.wav`
- [ ] `ts_xover_low_12oc_9oc.wav`
- [ ] `ts_xover_low_12oc_3oc.wav`
- [ ] `ts_xover_low_12oc_cw.wav`

(The 12oc Q baseline is in Set A as `ts_xover_low_12oc_12oc.wav`.)

## Set F — Ringdown captures (Q-vs-frequency)

XOVER mode, OUT = CENTRE, SPAN minimum (collapse to single peak
where possible), Q at 3 o'clock (just below self-osc — strong
resonance with measurable decay). V/Oct varied across 3
positions for Q-vs-frequency mapping.

Input source: **impulse** — easiest is a Pulse unit on the ER-301
firing a single short trigger into Three Sisters IN, OR
percussion sample with most energy in a single sample. Need a
clean transient; 30-50ms of audio captures the ringdown.

For each capture: trigger ONE impulse, record ~500ms of
ringdown (let the resonance decay below noise floor).

- [ ] `ts_xover_centre_cv-1v_3oc_ring.wav`  (low-pitch ringdown)
- [ ] `ts_xover_centre_12oc_3oc_ring.wav`   (mid-pitch ringdown)
- [ ] `ts_xover_centre_cv+1v_3oc_ring.wav`  (high-pitch ringdown)

These feed `ping_q()` in `measure.py` to extract (f0, tau, Q)
per cutoff and verify how the real unit's Q tracks (or doesn't
track) with pitch.

## Set G — Self-oscillation captures

XOVER mode, OUT = CENTRE, Q full CW (self-osc), no input
(unpatch IN entirely or send silence), record sustained tone
for ~10 seconds.

- [ ] `ts_xover_centre_cv0v_cw_osc.wav` — FREQ noon, ~base pitch
- [ ] `ts_xover_centre_cv-1v_cw_osc.wav` — FREQ −1V, lower self-osc tone

Verifies the model's claimed "near-pure sine, 0.013% THD"
self-oscillation. The Python model's reference plot
(`planning/refs/three-sisters-model/selfosc.png`) is what we
compare against.

If Three Sisters DOESN'T self-oscillate at full CW Q on the
real unit, NOTE that — the model assumes slightly-negative
damping in the loop; the real analog circuit might have a
slightly higher threshold or use a different mechanism.

## Post-capture

- [ ] All takes saved with naming convention; double-check no
      `.wav` is mis-named (mis-named captures break the analysis
      pipeline silently — `analyze_sweep('sweep.wav', 'wrong.wav')`
      just returns garbage if the recording doesn't match the sweep)
- [ ] **Peak audit**: every recording's peak should be below
      0 dBFS (no clipping). If anything clipped, re-take.
- [ ] **Silence check**: every recording should have meaningful
      signal energy. A take where the routing was wrong shows up
      as silence or noise — re-take.
- [ ] **Archive**: tar or zip the captures together as
      `ts_hardware_<YYYY-MM-DD>.zip`
- [ ] Copy archive to `planning/refs/three-sisters-hardware/`
      in the repo (create the dir; this is the captured corpus
      for the redesign work)
- [ ] Add a short README at
      `planning/refs/three-sisters-hardware/README.md` noting:
  - Capture date
  - Audio interface make/model
  - Three Sisters revision (if known — front-of-PCB date code)
  - Cable lengths / impedance notes
  - Any anomalies noticed during capture

## Capture count summary

| Set | Captures |
|---|---|
| A — Baseline XOVER | 4 |
| B — Baseline FORMANT | 4 |
| C — V/Oct calibration | 4 |
| D — Q variation CENTRE | 4 |
| E — Q variation LOW | 4 |
| F — Ringdowns | 3 |
| G — Self-osc | 2 |
| **Total** | **25** |

8s sweeps × 17 captures = ~2.5 min audio. Ringdowns + osc ~30s.
Plus DAW transport overhead, knob photos, file management.
**Budget 60-90 min at the rig.**

## When you're done

The corpus in `planning/refs/three-sisters-hardware/` becomes
the source-of-truth scorecard. Next session:

- Phase 0c: run each capture through `analyze_sweep` and overlay
  against the Python model's `block_magnitude` with the same
  nominal params. Validate the model.
- Phase 0d: build the C++ harness, run biome::Canals through the
  same battery, plot biome vs validated model vs hardware.
  The π bug should be obvious as a uniform ~3.14× peak-frequency
  offset across every capture.
- Phase 0e: write up `canals-baseline-scorecard.md` with per-
  capture measured-vs-target deltas. That doc becomes the
  per-phase progress tracker for the redesign.
