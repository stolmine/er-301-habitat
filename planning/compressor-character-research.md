# Where compressor tone comes from, and a Character control for Gesso

Status: **research** (2026-08-21). User request: research compressor character
and propose a Character control for the house compressor, in the same spirit as
the one that worked on Parametric EQ.

Builds on `planning/gesso-glue-compressor.md`, which already settles the engine
(Pressure6) and the surface (SSL G detents). This changes one thing in that
note: **Sag should not be the colour control.**

## 1. The lesson being applied

Parametric EQ's first character control was a saturation ladder - one quantity,
scaled across four positions. Only one position read as different, because the
Q law changed exactly once and everything else was a percent of THD. The rebuild
made **each position change the curve LAW**, and it worked.

Gesso's note currently has **Sag, 0..1, as "the one color knob"**. That is the
same shape as the failed EQ Drive: a continuous, subtle amount with nothing to
A/B against. It should be a Character control where each position changes the
compressor's *behavioural laws*.

## 2. What the literature says actually differentiates compressors

Four interacting layers, in the sources' own ordering:

1. **The gain element's physics** - photocell lag, tube curve, FET nonlinearity,
   VCA linearity.
2. **Whether ratio and knee are fixed by that physics or dialled in.**
3. **Detector type and time constants** - peak vs RMS.
4. **Detector placement** - feedback vs feedforward.

And the observation that matters most for us, because it says the character is
cheap:

> modern VCAs can be extremely clean with short signal paths, such that the
> "sound" is controlled almost entirely by the sidechain - there are many
> compressors with nearly identical signal paths that differ wildly in sidechain
> topology, response, and character

**The character lives in the sidechain, not the gain element.** We do not need to
model a photocell or a tube. We need to model detector placement, detector type,
timing law and knee - all of which are arithmetic on the control signal.

## 3. The single biggest axis: feedback vs feedforward

Described as one architectural choice that "cascades into dramatically different
sonic behaviors":

| | feedforward | feedback |
|---|---|---|
| detector reads | the input, before gain reduction | the output, after gain reduction |
| transients | caught accurately, fast | more gets through before it responds |
| perceived | precise, controlled | smoother, more musical |
| time controls | independent of ratio | **attack and release become a function of ratio** |
| deep GR | easy | **hard - the output falls below threshold and the loop backs off** |
| threshold | fixed | **effectively adaptive; rides the signal** |

That last group is the interesting part. A feedback detector cannot be made to
squash hard, and its timing is not independent - so the *same knob settings* give
a materially different result. Sources note it is "harder to make feedback
compression sound bad".

Classic placements: 1176 and LA-2A are feedback; dbx 160 is feedforward; the SSL
bus is feedforward with a simulated feedback path inside the sidechain.

**Pop3Dynamics, which habitat already has, is FEEDFORWARD** - `observe()` reads
the raw input before `apply()` touches it. So we have one half of this axis
already built and measured, and the other half is a one-line change to what
`observe()` is fed.

## 4. Program-dependent timing, with published numbers

The dbx 160's spec is concrete enough to model directly:

| gain reduction | attack | release |
|---|---|---|
| 1 dB | - | 8 ms |
| 10 dB | 15 ms | 80 ms |
| 20 dB | 5 ms | - |
| 30 dB | 3 ms | - |
| 50 dB | - | 400 ms |

**The harder it is hit, the faster it moves** - attack falls 15 → 3 ms as GR
rises, while release *lengthens* 8 → 400 ms. That is a law, not a knob, and it
is why "program dependent" sounds different from any fixed setting.

The LA-2A's equivalent is the photocell's own recovery: a fast initial stage and
a much slower tail, neither of which the user sets.

## 5. Knee

Hard on the 1176 and the original dbx 160; the 165 added "Over Easy", which one
practitioner describes as essentially "just sticking a diode in the GR path".
Soft-by-nature on the LA-2A, because the photocell curve is soft. The SSL bus has
a fixed knee and is used gently.

Knee is trivially cheap: it is the shape of the threshold crossing.

## 6. Proposed Character control

**Three positions**, because `Unit.ViewControl.OptionControl` has exactly three
sub-buttons and a fourth choice is unreachable - the Parametric EQ shipped that
bug once already.

Each position changes **four laws at once**, which is what makes positions
audibly distinct rather than gradations of one thing:

| | **Glue** | **Peak** | **Opto** |
|---|---|---|---|
| detector placement | feedback | **feedforward** | feedback |
| detector type | RMS-ish | **peak** | RMS |
| timing law | program-dependent | **fixed, fast** | program-dependent, slow tail |
| knee | soft | **hard** | soft |
| deep GR | resists | **allows** | resists |
| reference behaviour | SSL bus | 1176 / dbx 160 | LA-2A |

- **Glue** is the default and the reason the unit exists: gentle, adaptive,
  hard to make sound bad.
- **Peak** is the one that will catch a snare transient and the one that can be
  pushed into obvious pumping. It is the only feedforward position, which is
  exactly why it behaves differently at identical knob settings.
- **Opto** is slow and forgiving, with a two-stage release that no fixed Release
  setting reproduces.

Attack and Release detents stay as the note specifies; Character changes what
those numbers *mean*, the same way the EQ's positions change what its Q number
means.

**Sag becomes secondary or is dropped.** If kept it should be a consequence of
position rather than its own ply, exactly as saturation became on the EQ.

## 7. What is cheap, and what is not

Cheap, all sidechain arithmetic:
- feedback vs feedforward: change what `observe()` is fed. One line.
- peak vs RMS: a squared-average detector alongside the existing absolute-value
  one. A few multiplies.
- program-dependent timing: scale the coefficients by current GR, per the dbx
  table. Block-rate.
- knee: a soft-knee interpolation around threshold. A few multiplies.

Not cheap and **not proposed**: modelling a photocell's actual physics, tube
transfer curves, or transformer saturation. The sources are consistent that
sidechain topology accounts for most of the audible difference, and habitat
already has saturation atoms if colour is wanted separately.

## 8. Acceptance tests, written before the build

The EQ's lesson was that the character control needed *measurable* distinctions,
not a THD ladder. For this one:

1. **Feedback resists deep GR.** At identical threshold and ratio, Peak must
   reach materially more gain reduction than Glue on the same signal. If it does
   not, the feedback path is not really closed.
2. **Feedback timing tracks ratio.** Changing ratio alone must change the
   measured attack time on Glue and must NOT on Peak.
3. **Program dependence is real.** On Glue and Opto, a 20 dB overshoot must
   produce a measurably faster attack than a 5 dB one. Fixed-timing Peak must
   not vary.
4. **Knee.** The transfer curve around threshold must show a measurable soft
   shoulder on Glue and Opto and a corner on Peak.
5. **Every position at ratio 1 (or amount 0) is a bit-identical bypass.**

## Sources

- https://sonicscoop.com/feedback-vs-feed-forward-compression-differences-need-know/
- https://mynewmicrophone.com/feedback-vs-feedforward-dynamic-range-compressors-in-audio/
- https://gearspace.com/board/geekzone/34147-please-explain-difference-these-types-comps-vca-fet-elop-vari-mu-opto.html
- https://www.masteringbox.com/learn/audio-compressors-vca-opto-fet-compression-circuit-types
- https://www.kvraudio.com/forum/viewtopic.php?t=384536
- https://www.soundonsound.com/techniques/classic-compressors
- https://www.justmastering.com/article-classiccompressorsguide.php
