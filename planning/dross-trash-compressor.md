# Design note: Dross - a trash compressor

Status: design note / not started. Ledger item `dross-trash-compressor`.

User request 2026-08-14: a "trash" compressor, "perhaps based on Goodhertz Vulf".
This note is the research pass plus the distillation: what Vulf actually is,
which of its mechanisms carry the character, and what a 301 unit should keep.

Fills the largest hole in the collection's dynamics coverage. Impasto (3-band)
plus the firmware's Limiter is the entire dynamics story today, against nine
reverbs. `aw-batch2-ports` would eventually bring Airwindows Dynamics3, but that
is a clean broadband compressor; nothing in the catalog or the backlog is a
character compressor.

Clean-room posture: Vulf Compressor is proprietary and there is no source. This
is built from published behavior (the manual, the vendor's own origin story) and
from the documented character of the hardware it descends from. Same posture as
`port-gplv3-cleanroom`. No code, coefficients, or measurements are taken from the
plugin.

## Lineage

Not a drum machine. The **Boss SP-303 Dr. Sample** (2001), a sampler, and
specifically its **Vinyl Sim** effect - which carried forward into the SP-404 and
SP-505. Goodhertz's own framing: "Sometime around the turn of the century, an
unknown Japanese DSP engineer engineered a radically weird compression algorithm
for the Boss SP-303 Dr. Sample Sampler." It stayed obscure among instrumental
beatmakers (Dilla, Madlib) until Jack Stratton found it described on a forum and
got Devin Kerr and Rob Stenson to rebuild it; Kerr then extended it.

The consistently reported signature is **ducking**: drums pull the whole program
in and out. Plus a squashed, gritty, lo-fi tightness. Nobody writing about it
describes it as transparent, and the plugin is explicitly sold as a compressor
that changes the direction of a song rather than one that removes dynamic range.

Sources: `goodhertz.com/vulf-comp/`, `manuals.goodhertz.com/3.13/vulf-comp/`,
Sound on Sound's Goodhertz review, the Zumic writeup of Stratton's own account.

## What Vulf exposes

Front panel is six controls: Input Gain, **Compression Amount**, **Wow/Flutter
Amount**, **Lo-Fi Amount**, Output Gain, Master Mix. The character lives in the
advanced page:

- **Comp Attack / Release Time Constant** - explicitly *unitless multipliers*,
  the manual's stated reason being that the compressor "does not have a
  traditional fixed attack time." There is **no threshold and no ratio at all**.
  The behavior is described as a "complex interaction of signal dynamics and
  feedback."
- **Digital Ref Level** (-36..0 dB, default -18) - stands in for the threshold.
  Defines where the algorithm assumes the signal sits, and it also sets how hard
  the lo-fi stage is driven, so it changes character rather than only amount.
- **Lo-Fi Crunch** (0..100% THD), **Lo-Fi Noise Gain**, **Lo-Fi Type** =
  Analog / 1990's Digital / 1980's Digital, differing in aliasing character.
- **Wow/Flutter Speed** quantized to **33 1/3 / 45 / 78 RPM**, with Mix and a
  stereo Phase offset.
- **Sidechain Tilt** (-100..+100) shaping what the detector hears; plus
  Sidechain Listen and an external blend.
- HQ mode (~50 ms latency, 2-4x CPU), pre/post gain trims, meters.

## The four mechanisms worth taking

All four are cheap on Cortex-A8. Everything else is packaging.

1. **Feedback detector, no threshold, no ratio.** Detector reads the *output*;
   gain is a smooth function of `env/ref`. One knob. This alone produces the
   always-on, program-dependent, not-quite-controllable feel. A feedback loop
   also self-softens its own knee, which is why no ratio control is needed or
   even meaningful.
2. **Reference level instead of threshold.** This translates unusually well to
   the 301, which has no 0 dBFS convention and where units routinely see signals
   anywhere from modulation-level to full scale. Ref becomes the character knob:
   the same Squash setting at -30 dB ref and at -6 dB ref are different effects.
3. **Undersampled, unsmoothed gain reduction.** A 2001-era DSP would not have
   recomputed gain every sample. Running the detector once per 8-16 samples and
   applying the result as a step gives gritty, snapped transients for *negative*
   CPU cost. This is a hypothesis about why the original sounds broken, not a
   documented fact - but it is the highest fun-per-cycle idea in the note, and it
   is free to try. Falsifiable in an afternoon offline.
4. **Wow and flutter at record rates.** ~0.555 Hz for 33 1/3 rpm, plus flutter
   partials, on a short interpolated delay. This is what makes the thing read as
   a record rather than as a compressor, and it is what stops Dross being "one
   more comp."

Explicitly dropped: HQ mode, stereo phase, external sidechain and listen,
pre/post trims, meters, and two of the three lo-fi types until one proves it
sounds different enough to earn an option slot.

## Two additions beyond Vulf

We are not cloning, so the unit should have its own idea.

- **Grit tracks gain reduction.** Rather than an independent Lo-Fi Amount, a
  Track sub-param bends bit depth and decimation toward the GR envelope, so the
  loudest hits are the dirtiest. This is the coupling the original only implies
  through Ref Level, made a control.
- **Dropout.** The reported signature of the hardware is ducking; taken past its
  limit that becomes a hole. When GR crosses a settable depth, collapse the gain
  briefly and let it recover. Cheap, and it is the "trash" in trash compressor.
  Depth of 0 must be a bit-identical bypass of the stage.

## Control surface

Name: **Dross** (the scum drawn off molten metal). Fits the spreadsheet
material-word naming: Breccia, Sediment, Impasto, Parfait, Lacquer.

Package: **spreadsheet**, next to Impasto and Parfait.

| control | range | sub-params |
|---|---|---|
| **Squash** | 0..1, CV | Attack x, Release x, Tilt (detector EQ, bipolar) |
| **Ref** | -36..0 dB | - |
| **Grit** | 0..1 | Flavor (option), Noise, Track |
| **Wow** | 0..1 | Speed (33 / 45 / 78), Flutter |
| **Dropout** | 0..1 | Depth, Hold |
| **Mix** | 0..1 | - |

Six front controls is at the top of what a spreadsheet unit carries, but Squash
and Mix alone must behave like a usable compressor with everything else at zero -
that is the "useful" half of the brief. The trash lives in Grit, Wow and Dropout,
all of which default to 0 except a small Wow, matching Vulf's own default of 15%.

Per `feedback_ui_labels`, every label, button and description gets updated
together when a parameter's behavior changes. Per `feedback_not_minimal`, this is
a creative unit, so the interesting parts get implemented faithfully - the
undersampled detector and the dropout stage are the point, not optional polish.

## DSP sketch

- **Detector.** `env` from the *output* through a one-pole with asymmetric
  rise/fall coefficients; Attack/Release are unitless multipliers on a fixed
  base pair, as Vulf does, because a feedback loop makes stated milliseconds a
  lie anyway. Program dependence comes from a **dual-stage release**: a fast
  stage plus a slow stage whose coefficient depends on how deep the fast stage
  went. That is the honest reading of "no traditional fixed time constant."
- **Gain law.** `gr_dB = -k * log2(env / ref)` above ref, soft-kneed by the
  feedback topology itself; back to linear via `exp2_poly`. Recomputed only
  every K samples (K from the Grit-adjacent undersample setting, fixed at first);
  held flat between updates.
- **Output stage.** Apply gain through `tanh_poly` so deep GR also distorts,
  which is the cheapest way to get "the compressor is the distortion."
- **Grit.** Lacquer's mixed-rate pattern: downsample shell, quantizer, noise,
  smoothing on the way out. Flavors differ in whether quantization lands before
  or after the HF chop and how much aliasing is allowed through.
- **Wow/flutter.** Two or three summed sines at record-derived rates driving a
  short interpolated delay. Cheap; the only new buffer in the unit.
- **Dropout.** Comparator on the GR envelope with hysteresis and a hold counter,
  multiplying a fast declick ramp into the gain.

Estimated cost: everything here is per-sample scalar and simple. This should be
one of the cheaper spreadsheet units. NEON matters only in the shaper and the
grit stage.

## Reuse

Most of the DSP is already in the tree.

- `mods/spreadsheet/MultibandCompressor.{h,cpp}` - detector and gain-computer
  math (Giannoulis/Massberg/Reiss), SoA rise/fall coefficients. Collapses to one
  band. This is the closest existing relative.
- `mods/spreadsheet/util/neon_math.h` - `log2_poly` / `exp2_poly` for the dB
  path, `tanh_poly` (Pade 3/3) for the output nonlinearity. Standalone header,
  scalar fallbacks for emu.
- `er-301/mods/core/objects/Limiter.{h,cpp}` - three production waveshapers
  (tanh via Lambert continued fraction, x/sqrt(x^2+1), cubic), all NEON,
  extractable as ~50-line functions.
- `er-301/mods/core/objects/env/EnvelopeFollower.{h,cpp}` - reference for
  envelope state management.
- `mods/house/atoms/Lacquer.h` - the downsample-shell -> aliasing -> smoothing
  pattern for the Grit stage, already documented and NEON-shaped.
- `mods/spreadsheet/Larets.cpp` - `FX_DOWNSAMPLE` / `FX_BITCRUSH` as a working
  reference; Breccia's crush is the newer one, with the libm already audited out
  of its sample loop.

Genuinely new code: the feedback loop, the undersampled detector, the
wow/flutter delay, the dropout gate.

## Cautions

- **`feedback_runtime_branched_dsp_dispatch`** - Grit flavor and Dropout state
  must not become runtime switches inside the per-sample loop. Hoist to block
  rate, keep the sample path branch-light, check the objdump, the way Breccia's
  build was checked.
- **No libm in the sample loop.** Breccia found three (powf, floorf, two double
  floor) by audit after the fact. Audit this one before it ships, not after.
- **Bit-identical bypasses.** Grit=0, Wow=0 and Dropout=0 each need to be exact
  identity, established by a null test rather than by construction - Breccia's
  bit-identity claim was by-construction and is still owed an empirical A/B.
- **Feedback stability.** A feedback detector with an undersampled gain update
  can oscillate. The gain update rate and the release floor together set the
  loop's own resonance; needs a sweep before the control ranges are pinned.
- **Stereo.** Per `biome-discont-mix-left-only` and `drywet-crossfade-audit`:
  bind Mix on both instances, decide the stereo link question explicitly (Vulf
  links by default), and use equal-power crossfade if the wet path decorrelates.
- The am335x build check greps must use exit codes, not `' error'` - gcc emits
  colourized `error:` and a failing ARM build has reported 0 errors before.

## Phases

1. **Offline model.** Python model of the feedback detector plus undersampled
   gain, driven with a breakbeat. Falsify or confirm mechanism 3 - does stepped
   GR actually sound like the character, or just like aliasing. Also fixes the
   gain law and the dual-stage release constants. Nothing is built until this
   render is convincing.
2. **Core unit.** Squash / Ref / Mix, feedback detector, dual-stage release,
   tanh output stage. Must stand on its own as a compressor at this point.
3. **Grit.** Lo-fi stage with Flavor, Noise, Track. Null test at 0.
4. **Wow.** Modulated delay with Speed and Flutter. Null test at 0.
5. **Dropout.** Comparator, hold, declick ramp. Null test at 0.
6. **Hardware.** CPU on A8, insert/delete, serialization round-trip, listening
   pass on drums specifically - that is what the lineage is for.
