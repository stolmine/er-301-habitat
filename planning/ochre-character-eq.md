# Design note: Ochre - a characterful four-band EQ

Status: design note / not started. Ledger item `ochre-character-eq`.

User request 2026-08-14: characterful EQ, with a stated interest in the SSL 611.

Named **Ochre**: an earth pigment. The two circuits this draws on are known by
the colour of their knob caps, so a pigment name is the right register, and it
sits with Gesso, Impasto and Parfait.

Package: **house** - see the open question at the end.

## What the 611 actually is

The SSL 611EQ is the equaliser section of the SL 4000 E console channel strip
(the 611E module), reissued in 500 series. Four bands:

| band | range | shape |
|---|---|---|
| LF | 30 - 450 Hz | shelf, switchable to bell |
| LMF | 200 Hz - 2.5 kHz | parametric, with Q |
| HMF | 600 Hz - 7 kHz | parametric, with Q |
| HF | 1.5 - 16 kHz | shelf, switchable to bell |

**Two circuits, switchable, known by knob colour.**

- **Brown (02)** - the original, standard on all E-series consoles before summer
  1985. **±15 dB**. Described as good-natured; a desk tech quoted in Tape Op's
  review puts it on drums for "girth and harmonic structure that sounds great."
- **Black (242)** - developed in 1983 for the first SSL installed at AIR Studios,
  in conjunction with George Martin. **±18 dB**. More powerful and precise, and
  said to tend toward harshness. The same tech puts it "on vocals and guitars
  when you need them to punch through a mix."

Note on attribution: the 242 is frequently credited to George Massenburg in
forum lore. The sources found here consistently credit **George Martin / AIR**.
Do not repeat the Massenburg claim in any user-facing text without better
evidence.

Two published behavioural facts matter more than the ranges:

1. **The mid bands are logarithmically symmetric.** SSL's own wording: the two
   parametric mid sections ensure "that the ±3 dB up/down points retain the same
   musical interval from the centre frequency regardless of frequency and
   amplitude settings." That is constant bandwidth *in octaves*, held across gain
   - not the proportional-Q behaviour often attributed to this EQ. Getting this
   right is most of what makes it feel like an SSL rather than a generic
   parametric.
2. **The shelves are 6 dB/octave**, with a fixed-Q bell as the alternative. Gentle
   first-order-ish shelves, not the steeper shelves most digital EQs default to.
   Cheap to implement and a large part of the sound.

## What this can honestly be

There are no public schematics. This is a **behavioural design informed by
published specification**, not a circuit emulation, and nothing user-facing
should claim otherwise - the same posture as `dross-trash-compressor` and
`gesso-glue-compressor`.

What can be done faithfully, because it is all published or measurable:

- the band layout, ranges and shape switching,
- 6 dB/octave shelves,
- the log-symmetric Q law,
- the ±15 vs ±18 dB difference, which is a real behavioural difference and not
  just a number: the Black's extra range changes how hard people push it,
- a per-band level-dependent nonlinearity, which is where "characterful" actually
  lives.

## The character, concretely

Three things separate a characterful EQ from a set of biquads, and none of them
is expensive:

**The Q law.** Log-symmetric constant-octave bandwidth for Brown; a
proportional-Q law for Black, where Q tightens as gain increases. This is the
single best candidate for what the colour switch should *do* in our version, and
it is a coefficient decision, not new DSP.

**Band interaction.** Analogue EQ bands are not independent - they load each
other, so the composite curve is not the sum of the individual curves. Cascaded
digital biquads are independent by construction and that is exactly why they
sound "clean" in the pejorative sense. Deliberately introducing interaction (a
shared feedback path, or band coefficients that read neighbouring band gains) is
the least obvious and most valuable move available here.

**Level-dependent nonlinearity.** Airwindows' **BiquadStack** is the ready-made
answer, and it was written for precisely this purpose - Chris's own note says he
built it "when I started really trying to work out what was so special about SSL
channel strips, so I could use similar parametric bands in ConsoleX." Its design
is a Butterworth Q ladder (1.93185165 / 0.70710678 / 0.51763809) used as
*bandpasses* rather than for a steep rolloff, giving "not a narrowing spike as a
normal resonant filter would be, instead a little region of intensity" with
"moats" at the edges from phase interference. The nonlinearity is
`dis = fabs(a0 * (1.0 + out*nonlin))` clamped to 1.0 - b0 modulated by
instantaneous level, saturating by clamping. **No tanh, no transcendentals in the
sample loop at all.** MIT, and the single-band kernel is the file to lift.

## Build it as an atom first

Habitat has **no EQ atom at all** - only biome's one-knob Tilt EQ and the
firmware's EQ3. Meanwhile `strata-channel-strip` needs an EQ section and
currently names SmoothEQ3 or BiquadStack as candidates.

So: build **one parametric-band atom**, and let both consume it. Ochre is the
standalone four-band unit; Strata's EQ section is the same atom instantiated
three or four times. That is the `house-atom-library` pattern - components by
default, with a composition that gives them standalone value - applied properly
rather than duplicating an EQ.

If only one gets built, build the atom and Ochre. Strata is a much larger job and
would inherit the EQ for free.

## Controls

Four bands, each: Freq, Gain, and (mids) Q. Plus:

| control | notes |
|---|---|
| **Colour** | option: Brown / Black - Q law, gain range, drive character |
| **LF shape** / **HF shape** | option: Shelf / Bell each |
| **Drive** | the nonlinearity amount, shared |
| **Mix** | linear |

That is a lot of controls, and it is the same problem Parfait solved with 34
controls across 8 views. Use the same structure: four top-level band controls,
Enter opening each band's expansion. If `strata-channel-strip`'s **SectionGate**
control lands first, use it here too - a band is a section, and shift-to-bypass
per band is exactly right for an EQ.

## Cautions

- **Coefficients in double, recursion in float.** The `aw-batch2-ports` finding
  and the Strata note both land here: float is safe except for the
  lowest-frequency biquads, and LF reaching 30 Hz is exactly that case.
- **Gain = 0 on every band must be a bit-identical bypass.** Subtractive EQ
  designs get this for free; check it rather than assuming.
- **Sample rate**: any constant lifted from Airwindows is 44.1 kHz-calibrated.
  See the 48 kHz findings in `strata-channel-strip` - Channel9's ultrasonic
  biquads bypass entirely at 48 kHz for two of five models, and that class of bug
  is easy to inherit.
- **Do not claim emulation.** Describe Ochre as SSL-informed, name the reference,
  and let the Colour labels be Brown and Black rather than borrowed model
  numbers.
- **`feedback_runtime_branched_dsp_dispatch`** for Colour and the two shape
  options - coefficient decisions at block rate.
- **Zipper noise** on Freq sweeps is the classic EQ failure. Smooth coefficients,
  not parameters, and check it on a sweep before shipping.

## Open question: which package

**house** is the recommendation: the AW atom bin lives there, BiquadStack would
be ported there, and `strata-channel-strip` - the other consumer - is a house
unit. Keeping an atom and its consumers in one package avoids the cross-package
sharing problem already flagged in `stft-frontend-atom`.

The tension is that `house-suppress-customs-optimize-ports` directs house to keep
and optimise the *ports* while suppressing the house *originals*. Ochre is
BiquadStack-derived rather than an original in the RotCoat sense, so this reads
as within the directive - but it is a judgement call and worth confirming rather
than assuming. The alternative is spreadsheet, next to Impasto, Parfait and
Gesso, at the cost of splitting the atom from Strata.

## Phases

1. **The parametric band atom.** One band: Freq, Gain, Q, both Q laws, the
   level-dependent nonlinearity. Verified against a swept measurement, with the
   log-symmetric claim actually checked - ±3 dB points at a constant octave
   interval across gain settings.
2. **Ochre four-band.** Band layout, ranges, shelf/bell switching, 6 dB/oct
   shelves. Null test at all gains zero.
3. **Colour.** Q law, gain range and drive character switched together.
4. **Band interaction.** The experimental part; A/B against independent cascaded
   bands and keep it only if it earns itself.
5. **Hardware**: A8 CPU with all four bands active, zipper check on sweeps,
   serialization of every option.
6. **Retrofit** Strata's EQ section onto the atom.
