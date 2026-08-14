# Design note: Gesso - a glue bus compressor

Status: design note / not started. Ledger item `gesso-glue-compressor`.

User request 2026-08-14, second of the dynamics pair: "glue would be nice...
something like the classic SSL G comp", with the hint that there is precedent in
the Airwindows catalogue (cloned at `~/repos/airwindows`).

Companion to `dross-trash-compressor`. Dross is character - a feedback comp that
ducks and disintegrates. Gesso is the other half of the dynamics gap: the boring,
useful one that makes a submix cohere. They share most of their detector
infrastructure, so building both costs less than building either twice.

Name: **Gesso**, the glue-and-chalk ground laid under a painting. Sits with
Impasto and Parfait in spreadsheet, and it means what the unit does.

## What the Airwindows catalogue actually has

Surveyed `~/repos/airwindows/Airwindopedia.txt` (6946 lines) and the LinuxVST
sources. The pedia's own Dynamics list runs to 35 plugins, "roughly in order of
goodness."

**There is no SSL G clone.** One plugin is SSL-voiced and three are described as
glue.

**Logical4** is the only SSL-tied one: "when we're talking about 'tone' and
something called 'Logical' you can see that it's going to be in the SSL style:
there's a sparkliness which requires some extra coding attention." But the
provenance is thinner than it looks - the Rock SSL impulse convolution the pedia
mentions (line 743, in the BussColors4 entry) lived in the old paid *Logical*;
Logical4's source has no convolution. What survives is voicing, not measurement.
And it is expensive: three cascaded ButterComp stages interpolated by Ratio, each
stage bipolar x interleaved = 4 detectors per channel per stage, so up to 24
detector states per channel; three 999-sample Power Sag buffers; up to 36 `pow()`
per sample frame at max ratio. ~845 lines of DSP.

**Pressure6** is the one Chris built for this job: "my target was '2-buss
Compressor'. This isn't the same as 'limiter' or 'loudenator'. It's more like
'glue' compressor... it's still a variation on Vari-Mu." ~65 lines, two state
variables, no buffers.

**ButterComp2** is the third: "perhaps best used as a particular kind of 'glue'
compressor, at which it is exceptional" (of ButterComp), with genuine feedback
release modulation - `divisor = compfactor / (1.0 + fabs(lastOutputL))`,
recomputed every sample from the previous output. Softer, "holographic," less
SSL and more Fairchild-adjacent. ~30 ops/sample once `pow(x,2)` becomes `x*x`.

Also noted and not taken: Thunder (glue via subsonic-modulated sense circuit),
Pop3 (the ConsoleX comp+gate, "designed to be more a 'glue' type"), Podcast
(five curve comps in series), Recurve/curve (one line of code, no threshold, no
controls at all), Surge, PurestSquish, Dynamics3, BeziComp.

**License: MIT**, Chris Johnson, `~/repos/airwindows/LICENSE`. Every per-file
header is MIT; no plugin differs. He asks for visible credit. Follow the existing
convention from `readme-airwindows-attribution`.

## Reading the Pressure6 source

`~/repos/airwindows/plugins/LinuxVST/src/Pressure6/Pressure6Proc.cpp`. The whole
algorithm, read directly rather than taken on description:

```
threshold = 1.0 - pow(A*0.9, 3.0)           // per block
adjSpd    = ((A*92.0)+92.0) * overallscale  // per block

inputSample = max(|L|, |R|)                 // stereo-linked detector
inputSample *= muComp/threshold             // detector sees the COMPRESSED signal
L,R         *= muComp/threshold
if (|inputSample| > threshold) { muComp *= muSpd;   ...;  muComp /= muSpd; }
else                           { muComp *= muSpd^2; ...;  muComp /= muSpd^2; }
muComp = clamp(muComp, threshold, 1.0)
L,R *= muComp*muComp
muSpd = clamp( ((muSpd*(muSpd-1)) + |inputSample*adjSpd|) / muSpd, adjSpd, 2*adjSpd )
out = dry*(1 - wet*1.1) + sin(clamp(x*wet, +-pi/2))*wet
```

Four things matter here.

1. **It is a feedback compressor.** The input is pre-multiplied by `muComp`
   before the detector reads it, so the detector sees the already-compressed
   signal. Same topology class as the SSL G bus comp, arrived at independently.
2. **The time constant is program-dependent.** `muSpd` chases the signal and is
   clamped to `[adjSpd, 2*adjSpd]`. This is a crude auto-release, and auto-release
   is the single mechanism most responsible for what people call SSL glue.
3. **Stereo is linked at the detector**, one gain coefficient for both channels.
4. **Cost is trivial**: one `sin()` per channel, ~5 divides, `fmin/fmax/fabs`.
   `pow()` appears only in per-block setup. No exp/log/tanh/sqrt in the loop.

Two constraints the code imposes that the design has to break:

- **`adjSpd` is derived from `A`**, the same knob as threshold. There is no
  independent speed control at all. Decoupling it is required for any
  attack/release surface.
- **The `[adjSpd, 2*adjSpd]` clamp is only a 2:1 span.** That is a narrow
  program dependence - enough to sound alive, not enough to be a real dual-stage
  auto release. This is the main place the design departs from the source.

Also: `B` is not a dry/wet. At B=1 the dry coefficient is `1 - 0.99 = 0.01` and
the wet path is a sine soft-clip - Chris says as much ("a modified dry/wet that
also pads output into a soft saturation"). Keep that behavior but do not label
it Mix, because it isn't one.

## The decision

Not a straight port, and not a from-scratch SSL reimplementation.

**Pressure6 as the engine, SSL G as the control surface, Logical4's Power Sag as
the one color knob.**

A straight Pressure6 port gives the right sound with the wrong ergonomics: two
knobs, no way to dial the 4:1 / 30 ms / auto setting that everyone reaches for on
a bus comp. A from-scratch SSL G reimplementation gives the right ergonomics with
an engine we would have to write, tune and defend, when an MIT-licensed one with
the correct topology is sitting right there. The hybrid takes the engine that is
already feedback-topology and already program-dependent, and puts the familiar
detented surface on it.

Considered and rejected: **ButterComp2** as the engine (softer and lovelier, but
its character is not what "SSL G" means to anyone, and its 4-detectors-per-channel
interleave costs more for a sound further from the brief); **Logical4** entire
(the SSL claim does not survive reading the source, and three ButterComp stages
plus three sag buffers is a lot of A8 for voicing).

## What to take from Logical4

Only the Power Sag, which is cheap and is where its distinctive give comes from:

```
d[n] = |in| * (intensity - (sumOfControlValues * 0.0033002236853241))
control += d[gcount]/offsetA;  control -= d[gcount+offsetA]/offsetA;
```

A 499-sample boxcar moving average of the rectified input, scaled down by the
summed detector state - a power-supply droop that deepens as the compressor
works harder. Two adds and a multiply per sample once `1/offsetA` is precomputed,
plus one float ring buffer per channel (499 floats, not the source's 1000
doubles). That is affordable, and it is the part of Logical4 worth having.

## Control surface

| control | range | sub-params |
|---|---|---|
| **Thresh** | dB, CV | - |
| **Ratio** | 2 / 4 / 10 (option) | - |
| **Attack** | 0.1 / 0.3 / 1 / 3 / 10 / 30 ms (option) | - |
| **Release** | 0.1 / 0.3 / 0.6 / 1.2 s / **Auto** (option) | - |
| **Makeup** | dB, CV | - |
| **Sag** | 0..1 | - |
| **Mix** | 0..1 | HPF (sidechain highpass) |

Detents rather than continuous knobs for Ratio/Attack/Release, because that is
the SSL workflow and because discrete options serialize cleanly - see
`discrete-control-standard-inventory` and `control-step-standards-inventory` for
the house pattern. Auto is the default Release and the reason the unit exists.

The engine's own sine soft-clip stage stays, driven by how hard the unit is
working rather than exposed as a knob. Sag at 0 must be an exact bypass of the
sag stage.

Per `feedback_ui_labels`, labels/buttons/descriptions all move together when a
parameter changes.

## Building the Auto release

This is the one place real design work is owed, because Pressure6's 2:1 span is
not enough and the SSL auto-release is the whole point.

The classic behavior is a **dual time constant**: a fast stage (~50-100 ms) and a
slow stage (~1-2 s), with the effective release crossfading toward the slow one
as sustained gain reduction accumulates. Transients recover fast; sustained
material recovers slowly; the result is that the compressor stops breathing on
the program and starts holding it together.

Concretely: keep `muSpd` as the fast state, add a second slow-integrating state
over the gain reduction, and let the release coefficient interpolate between the
two under the slow state's control. When Release is set to a fixed detent, the
interpolation is pinned and `adjSpd` comes from the detent instead of from
Thresh. This also resolves the `adjSpd`-tied-to-`A` constraint noted above.

The fixed detents are the easy half. Auto needs a listening pass against
material with both transients and sustain - a drum bus is the honest test.

## Porting cautions

- **Doubles.** Every Airwindows plugin is double-precision throughout. Cortex-A8
  VFP is slow at doubles. Use the **hybrid-float** pattern already established by
  `fabula-am335x`: float for the audio path, double only where the recursion
  genuinely needs the headroom (the `muComp`/`muSpd` accumulators are the
  candidates, and they should be measured, not assumed).
- **Delete the dither block.** Every `*Proc.cpp` ends with a 32-bit float dither
  using `frexpf` + `pow(2, expon+62)`. That is VST output housekeeping. Removing
  it takes a `pow` and a `frexpf` per sample per channel out of the loop before
  any other optimization. Keep the `1.18e-23` denormal guard, which is real.
- **Do not port `processDoubleReplacing`.** Every file contains the identical
  algorithm twice; half the line count is a second copy.
- **`pow(x,2)` -> `x*x`** everywhere in the Logical4-derived sag code.
- **Stereo link.** The engine links at the detector with shared `muComp`/`muSpd`
  state. On the 301 a stereo unit is two instances, so the link has to be
  explicit - a shared detector object or a link path. Getting this wrong is
  exactly `biome-discont-mix-left-only`. Decide it up front and test it.
- **`feedback_runtime_branched_dsp_dispatch`** - Ratio, Attack and Release are
  options; resolve them to coefficients at block rate, never switch on them
  inside the sample loop.
- **No libm in the sample loop.** The `sin()` output stage goes to `sine_poly`
  from `util/neon_math.h`. Audit the objdump before shipping, not after - Breccia
  found three libm calls that way, after the fact.
- **`drywet-crossfade-audit`** - Mix here is a correlated wet path, so linear
  crossfade is correct and equal-power would be wrong. Note it so the audit does
  not "fix" it later.
- The am335x build check must use exit codes, not `grep ' error'` - gcc emits
  colourized `error:` and a failing ARM build has reported 0 errors before.

## Attribution

MIT, and Chris asks for visible credit: "all you have to do is credit that
you're using Airwindows code." Gesso is Pressure6-derived with a Logical4-derived
sag stage. Both get named in the unit description, the package README and the
release note, per `readme-airwindows-attribution`. The SSL G reference is
described as a *control surface* influence, not an emulation claim - the engine
is Chris's vari-mu, not a model of anyone's hardware.

## Phases

1. **Engine port.** Pressure6 to a spreadsheet atom, mono first, floats, dither
   block gone, `sine_poly` for the output stage. A/B against the plugin offline
   at matched settings to prove the port before anything is added.
2. **Decouple speed from threshold.** Independent `adjSpd`, fixed Attack/Release
   detents, Thresh and Makeup as real controls. Ratio detents.
3. **Auto release.** Dual time constant, slow state over accumulated GR,
   interpolated coefficient. Listening pass on a drum bus.
4. **Sag.** Logical4's boxcar droop, float ring, exact bypass at 0.
5. **Sidechain HPF** and Mix.
6. **Stereo link** decided, implemented and tested on both instances.
7. **Hardware.** A8 CPU, insert/delete, serialization round-trip of every option
   control, listening pass.
