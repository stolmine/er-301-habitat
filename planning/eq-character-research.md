# Where EQ tone actually comes from, and what to model

Status: **research** (2026-08-20). Prompted by the user observing that on
Parametric EQ's Character control "only black seems to have much effect".

That observation is correct and the cause is a design fault, not a tuning one.
It is diagnosed in section 1. Sections 2-4 are the research that should have
been done before the control was built.

## 1. Why only one position reads

| position | Q law | range | THD | changed from the one above |
|---|---|---|---|---|
| clean | constant | ±15 | 0.000% | - |
| brown | constant | ±15 | 0.579% | saturation |
| black | **proportional** | ±18 | 1.191% | **Q LAW**, range, saturation |
| hot | proportional | ±18 | 1.746% | saturation |

The Q law changes **exactly once**, at black. Everywhere else only saturation
moves, across a span of 0.58% to 1.75% THD. A Q-law change is a far larger
perceptual event than one percent of harmonic distortion, so black is the only
step that registers as a real change.

The ladder was built by calibrating a saturation amount and bolting the Q law
onto one step of it. It should have been built the other way round: **each
position should change the curve law**, with saturation as a secondary
consequence.

## 2. What the literature says actually differentiates EQs

Ordered by how much the sources weight them:

1. **Curve shape and Q law.** The same numbers on different designs give
   different apparent width. This is the dominant factor.
2. **Additive vs non-additive topology.** Called "one of the most underrated
   factors". Series-connected sections are additive: four bands at 1 kHz and
   +6 dB give +24 dB. Passive designs are not, and overlapping curves stay
   nearer the intended sweet spot instead of stacking.
3. **Passive plus makeup amp vs active.** Passive filtering loses gain, so a
   makeup amplifier follows, and that amplifier's character becomes part of the
   sound.
4. **Inductors.** They saturate: lows fatten, highs take large boosts without
   harshness, and past a point they behave like a frequency-dependent limiter.
   An LC node always resonates somewhere, unlike an RC node.
5. **Phase.** Inherent, not a flaw - an EQ cannot work without it. For
   minimum-phase designs phase is tied to the magnitude curve, so differing
   curve shapes already imply differing phase behaviour. **This means we get it
   for free and should not model it separately.**
6. **Harmonic and IM distortion.** Thickness and smoothness.

**We currently model only (1), and only two laws of it.** The unit is a series
of additive bands - the most "digital" topology available - with a saturation
amount on top.

## 3. MEASURED: how much topology matters

Four bands, all at 1 kHz, +6 dB each - the additivity stress test:

| topology | result |
|---|---|
| series, what we ship | **+24.00 dB** |
| parallel, non-additive | **+13.95 dB** |
| naive sum of the knob values | +24.00 dB |

Ten decibels apart, from nothing but band routing. This is the single largest
character axis available and it costs nothing to implement - sum the band taps
against the dry signal instead of chaining each band's output into the next.

## 4. The classics, and what is concretely modelable

### Pultec EQP-1A - passive LC, tube makeup

The famous low-end trick works because boost and cut are **not mirror images**:

- the boost shelf corner sits **below** the cut shelf corner,
- the boost curve is broad and soft; the cut is narrower and starts slightly
  **above** the selected frequency,
- the magnitudes differ: **+13.5 dB boost against -17.5 dB cut**.

Boosting and cutting the same frequency therefore yields a bump at that
frequency, a dip just above it, and tighter subs below. Measured example from
the sources: both controls at 30 Hz gives a boost near 80 Hz and a dip near
200 Hz.

Modelable exactly: it is two shelves with offset corners, different widths and
asymmetric gains. No new DSP is needed, only a band mode that instantiates a
boost/cut pair with those offsets. **This is the most distinctive and the
cheapest of the four.**

### API 550 - proportional Q, skirt-pinned

Q narrows as gain rises and widens back toward unity, **reciprocally in both
boost and cut**. The distinguishing measurable trait is that the skirt reaches
0 dB at essentially the same frequency regardless of gain: a 2 dB boost at
200 Hz spans roughly 20 Hz to 2 kHz, and increasing the gain narrows the shape
while leaving those endpoints where they are. A constant-Q design at 2 dB
spans only about 50 Hz to 800 Hz.

Range ±12 dB. Note our current "proportional" law is not this: it narrows Q but
does not pin the skirt, which is the part that makes the API tolerate being
pushed.

### Neve 1073 - inductor, FIXED bandwidth

The mid is fixed-bandwidth: the shape stays the same and only amplitude scales.
Range ±16-18 dB, larger than the API. The result is a broad, gentle bell even
at extremes, suited to large "invisible" moves. **This is a third distinct law
we do not have** - constant Q in Hz rather than in octaves.

### SSL 4000 E - what we already have

Constant-bandwidth with the brown and black revisions differing in Q law and
range. Keep as the reference point; it is the one already implemented.

## 5. Proposed character set

**HARD LIMIT OF THREE.** `Unit.ViewControl.OptionControl` draws
`Drawings.Sub.ThreeColumns` and `subReleased(i)` maps sub-button `i` straight to
choice `i`. There are three sub-buttons, so a fourth choice is placed off-screen
and cannot be selected at all. The four-position set shipped with "hot"
unreachable on hardware - which may itself be part of why the control read as
inert. Checked across the catalog: no other unit has an OptionControl ply with
more than three choices.

So three positions, each changing the **curve law**, which is what the ear
actually tracks:

| position | Q law | topology | range | signature |
|---|---|---|---|---|
| **Console** | constant, SSL-style | series/additive | ±15 | transparent and surgical; the accurate position |
| **Punch** | proportional, **skirt-pinned** | series | ±12 | API - tightens as pushed, tolerates abuse |
| **Passive** | broad shelves, asymmetric | **parallel/non-additive** | +13.5/-17.5 | Pultec - boost and cut coexist |

Clean is dropped (user call). It costs nothing structurally: **exact bypass is a
property of GAIN, not of character** - a band contributes `gain*saturate(tap)`
and gain is exactly 0 at 0 dB - so flat gains bypass bit-identically at every
position. Console inherits the transparent role by carrying little or no
saturation while still differing from the other two by curve law.

Saturation becomes a consequence of position rather than the thing being
selected: little or none on Console, moderate on Punch, inductor-flavoured on
Passive.

**Do not model phase separately.** These are minimum-phase designs, so getting
the magnitude curve right delivers the phase behaviour automatically.

## 6. Cost

- **Parallel topology**: free, and a one-line change to the band loop.
- **Fixed-bandwidth law**: one line in the bake (Q in Hz rather than octaves).
- **Skirt-pinned proportional**: a different Q-vs-gain expression, still one
  line, but the pinning needs verifying by swept measurement.
- **Pultec pair**: no new DSP, a band mode that instantiates two offset shelves.
- **Inductor saturation**: the existing tap saturator, re-voiced.

All of it lands in `ParametricBand`'s bake plus the band loop. None of it needs
new per-sample work, so the CPU figure should not move materially.

## 7. Honest limits

The tube and transformer colour of a real Pultec, and the specific inductor
core material of a 1073, are not reproducible from published information. What
IS reproducible is every curve-shape and topology difference above, and the
sources are consistent that those account for most of the audible difference,
with nonlinearity supplying the rest.

Nothing user-facing should claim emulation. Position names should be functional
(Clean / Console / Punch / Passive), not model numbers.

## Sources

- https://www.sweetwater.com/insync/understanding-eq-curves-why-identical-eq-settings-can-sound-different/
- https://vladgsound.wordpress.com/2013/06/01/additive-and-not-additive-eq-designs/
- https://gearspace.com/board/geekzone/784895-what-makes-inductor-eqs-sound-different-4.html
- https://ethanwiner.com/EQPhase.html
- https://abbeyroadinstitute.nl/blog/demystifying-the-pultec/
- https://penny.cool/go-ahead-boost-and-attenuate-the-pultec-eqp-1a/
- https://www.waves.com/api-550-or-api-560-the-differences-explained
- https://ms-tas.com/api-550-equalizers/
- https://www.bhphotovideo.com/explora/pro-audio/tips-and-solutions/a-guide-to-classic-studio-gear-equalizers
