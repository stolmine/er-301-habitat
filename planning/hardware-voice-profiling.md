# Hardware Voice Profiling for Emulation

Reusable workflow to reverse-engineer a hardware synth / drum / DSP voice into a
habitat unit, from: the module's manual, MIDI (or gate/CV) control for
repeatable excitation, audio recording, and sox (plus light FFT scripting) for
measurement and validation.

The core idea is **graybox system identification**: the manual supplies the
model *structure*, and measurement fits the *parameters*. That is a much easier
problem than pure black-box guessing, so the manual is the single biggest lever.

## Naming (read first)

Per `feedback_no_third_party_branding`: shipped units and every persisted file
(this doc, the ledger, commit messages) use **generic functional names**, never
the source product or company name. Source hardware is referenced only through
local files under `planning/refs/<slug>/` (manual, panel photos), not by brand
in prose. Confirm the functional unit name with the user at build time.

## The rig: what each piece contributes

- **Manual -> model structure.** Topology / signal flow, parameter list and
  ranges, MIDI and CV implementation. Turns black-box into graybox.
- **MIDI control -> repeatable, scriptable excitation.** Hold everything fixed,
  step one parameter across its range, trigger identically, record each step.
  This is what recovers each control's transfer curve, and it automates into an
  unattended capture matrix (script MIDI-send + record + slice).
- **Recording -> the observable output.**
- **sox -> measurement + validation.** Envelopes (`stat`/`stats`), spectra
  (`spectrogram`, `stat -freq` FFT dumps), band isolation, trim/resample, and
  differencing for the null test.
- **Light FFT scripting (numpy) -> pitch-envelope and partial/modal extraction**
  where sox is clumsy. Not strictly required, but it sharpens pitch and modal
  work a lot.

## Method

1. Read the manual -> hypothesized signal flow + parameter/range table + MIDI map.
2. Lock a clean capture chain: fixed level, known sample rate, DC-trim, identical
   trigger. Capture a neutral reference hit.
3. **Solo the components** using parameters: transient/click alone, tonal body
   alone, noise alone. Model each independently.
4. **One-parameter sweeps**: hold the rest fixed, step one control via MIDI,
   record each step -> measure the affected quantity -> fit the knob->DSP curve
   (its law and units). Use 2D sweeps to find interactions.
5. **Nonlinearity probe**: sweep drive/level, watch harmonic generation -> fit the
   saturation shape.
6. **Filter probe**: excite with noise or an impulse across cutoff settings ->
   magnitude response -> estimate filter type / slope / resonance.
7. Build the graybox emulation: C++ atom for the model, Lua control maps from the
   fitted curves.
8. Validate with the null test (below); iterate against the residual.

## Validation: the null test (the acceptance gate)

Play identical MIDI into the emulation, record, time-align, and difference
against the original:

```
sox -m orig.wav -v -1 emu.wav diff.wav
sox diff.wav -n stat
```

Residual energy is an objective error to minimize; its spectrum shows *what* is
wrong. Drive it down per target. This is the pass/fail gate, not an ear-only A/B.

## sox starter commands (refine per module)

```
# level / envelope stats
sox hit.wav -n stat
sox hit.wav -n stats
# time-frequency picture
sox hit.wav -n spectrogram -o hit.png
# FFT magnitude dump (then peak-pick for partials)
sox hit.wav -n stat -freq 2> hit.fft
# auto-trim to the hit
sox raw.wav hit.wav silence 1 0.01 -60d 1 0.1 -60d
# isolate a band
sox hit.wav band.wav sinc <lo>-<hi>
# null difference against the emulation
sox -m orig.wav -v -1 emu.wav diff.wav
```

## Effectiveness and limits (calibrated)

- **High confidence**: amplitude envelopes (attack/decay/release times + shape),
  static spectral signature, monophonic pitch and pitch envelopes, and parameter
  transfer curves. Analog-modeling / simple FM / subtractive digital voices with
  a manual reach a near-indistinguishable core, roughly 80-90% perceptual match;
  the tail is subtle nonlinearity and exact aliasing character.
- **Partial**: strong nonlinearities / wavefolding (many curves look alike), the
  exact "digital" artifacts (aliasing, bit/SR reduction are visible in the
  spectrum but exact reproduction needs the internal sample rate), and
  velocity->timbre maps.
- **Hard**: internal feedback or chaotic voices (input/output does not uniquely
  determine internals, so approximation only). Sample-ROM voices collapse to
  capture-plus-model, which is sampling rather than synthesis emulation.
- **Force multipliers**: the manual (graybox vs black-box) and MIDI (systematic
  parameter maps). Biggest limiter of pure sox: pitch/modal extraction, so add a
  little FFT scripting.

## Compound-architecture handling

Multi-block modules (serial/parallel DSP stages, or multiple engines/modes) need
**decomposition before fitting**:

- Enumerate the blocks from the manual/panel; find the parameters that solo or
  bypass each stage.
- Profile each stage independently at its boundaries (feed a known signal in
  where the panel allows, measure the output).
- Establish the routing and order (serial vs parallel, pre vs post) via targeted
  A/Bs.
- Where a stage cannot be soloed, hold it at a known-neutral setting and subtract
  its effect via the null test.
- Reassemble, then validate the full chain with the null test.
- Multi-mode voices: treat each mode as a separate profiling target that shares
  the capture harness.

## Target queue

1. **Compound DSP module (first).** On-hand hardware; a slightly compound
   architecture, so expect extensive per-stage decomposition. Inputs pending:
   manual, panel photos/graphics -> `planning/refs/compound-dsp-voice/`.
   Architecture is TBD from those; functional unit name TBD once mapped. This one
   is the workflow shakedown.
2. **Multi-engine digital drum voice (later).** Profile per mode. Inputs pending:
   manual, MIDI implementation chart, panel graphics ->
   `planning/refs/multimode-drum-voice/`. Generic per-mode functional names at
   build time.

## Inputs needed from the user (per target)

- Manual PDF -> `planning/refs/<slug>/manual.pdf`.
- Panel photo / graphics: layout, control labels, ranges.
- MIDI implementation chart: CC map, note/trigger mapping, parameter addressing.
  This is critical; without it, automated sweeps fall back to slow manual steps.
- Any CV/gate specifics if MIDI does not reach all parameters.
- The generic functional name(s) desired for the shipped unit(s).

## Open questions

- Does each target expose all parameters over MIDI, or are some panel-only (which
  caps what can be swept automatically)?
- Internal sample rate / known aliasing character (for digital-artifact fidelity)?
- Any intentional per-hit randomness or analog-style variation to model
  statistically over many captures?
