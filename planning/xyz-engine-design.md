# XYZ engine — original-design reverb concept

Status: design exploration. No code. Concept distilled from
`planning/refs/airwindows-port-handoff.md` §3.1. References
`planning/reverb-design-philosophy.md` for the combination
mechanics this engine recombines.

## What it is

A cryptic, emergent reverb with three parameters X / Y / Z where
**Z reframes what X and Y mean**. That reframing is the source
of the emergence and also the thing that breaks simple recall —
so the design intentionally keeps one Z regime (Nested) fully
predictable as solid ground.

Single-engine-core design. Range comes from connective processing
(Z's topology reroutes), not from bolted-on parallel verbs.

## Parameters

### X — Size + texture morph

Sweeps a single coherent perceptual axis:
- **Low**: small room + allpass-heavy → soft diffuse bloom /
  halo, no distinct echoes.
- **High**: large room + pure-delay → long modal ringing cavern
  with discrete repeats and resonant pitch in the tail.

Mechanism: delay ↔ APF morph in the FDN inner stage, emphasizing
smearing at low X and tails at high X. Single knob covers a
range that's typically two different units.

### Y — Saturate / desaturate + character axis (coupled on one knob)

- **Low**: pristine.
- **High**: saturated *and* undersampled simultaneously
  (vintage-gear-in-the-red + bandwidth-starved at once).
  Saturation adds harmonics up top; undersampling aliases them
  away. **The interaction IS the feature.**

**Curve requirement**: skew the coupling so saturation leads and
undersampling follows, giving the knob an arc instead of a
washy mud-zone at the midpoint. **A linear coupling sounds bad
in the middle.** Open question: exact taper. Sketch is something
like sat = `Y^1.4` and undersample = `Y^2.2` (or whatever
empirically gives the arc — needs tuning).

### Z — Meta-routing (the headline knob)

Changes topology / reroutes the discrete algorithms through
each other. Three regimes:

**Z low — Nested** (clean serial chain): `saturate → FDN →
Bezier → desaturate`. Resting state, the only fully predictable
zone. X and Y behave as described above, independently. Sounds
like an excellent characterful digital verb with a lo-fi switch.

> **Ship this as the predictable home base.**

**Z mid — Folded** (Y processing moves *inside* the feedback
path): saturation and undersampling now compound per pass →
**tails evolve**: clean transient in, tail that darkens,
thickens, and detunes as it decays ("the room rots as the sound
dies"). With Y high, each pass aliases the previous pass's
saturation harmonics → tail drifts **inharmonic**, a clean hit
blooming into an alien bell.

> **This per-pass aliasing of generated harmonics is the
> emergent payoff; neither mechanic alone produces it. This is
> the signature sound — make it the default.**

**Z high — Coupled** (split into two slightly-detuned sub-FDNs
cross-modulating, saturation on the cross-link): semi-chaotic.
- Low decay = two networks beating → lush chorused double-room.
- High decay = self-oscillation; stops being a reverb →
  evolving pad, inharmonic clangs, two rooms fighting.

Dramatic ceiling. Least predictable. A texture / drone generator
wearing a reverb's clothes.

> **In Coupled, Y is not optional** — the in-loop saturation +
> desaturate wrap is the leash that keeps runaway musical rather
> than a screech.

## Cryptic hot-spots (presets to remember)

- **X-low / Y-mid, any Z** — warm saturated bloom, shoegaze /
  ambient wash. Reliably musical.
- **X-high / Y-high / Folded** — haunted-cistern microtonal
  wavering, modal tails pitch-smeared.
- **Coupled / high decay** — emergent instrument, not an effect.
- **Folded / low-mid Y / mid X** — the evolving room. Novel but
  usable. **Suggested default position.**

## Three things to get right

1. **Y's curve** — see above. Linear coupling is bad. The arc
   has to be empirically tuned by listening.
2. **Keep Nested genuinely predictable** so there's somewhere to
   stand. Easy to over-engineer; the resting state is the
   strongest aesthetic of the design.
3. **Stability in Coupled** — self-oscillation must die into a
   denormal floor or stay musical via the Y leash. Add denormal
   handling regardless.

## AM335x feasibility (per handoff §4.2)

| Z regime | AM335x verdict |
|---|---|
| Nested | **Fits.** Standard FDN + Bezier shell. |
| Folded | **Fits, and is *helped* by its own undersampling** (same lever as RotCoat's World knob). |
| Coupled | **The problem child.** Two cross-modulating sub-FDNs ≈ double the core *and* double the memory traffic. **Not expected to fit at host rate on the A8.** |

**Coupled-on-AM335x decision**: Coupled mode must inherit the
reduced-rate trick (the cross-mod is per-sample math,
rate-indifferent, so this is consistent with the design). Without
that, Coupled pushes over budget. Spec it from the start as a
reduced-rate mode; don't try to bolt it on later.

## How this maps to the combination mechanics

(References `planning/reverb-design-philosophy.md` mechanic
numbers.)

- X axis uses mechanic 3 (topology morph between allpass and
  pure-delay characters).
- Y axis uses mechanics 1 + 2 (saturation-as-governor coupled
  with undersample-as-character on a single knob — the coupling
  IS the curve question).
- Z's Nested regime: clean #1 + #2 in series (saturation outside,
  undersample inside the FDN, but not coupled with the
  feedback).
- Z's Folded regime: #1 + #2 move into the feedback path;
  per-pass interaction is the emergent feature.
- Z's Coupled regime: adds mechanic 5 (cross-modulated feedback
  between two engines). Mechanic 1 is mandatory here as the
  runaway governor.

## Open implementation questions

1. **FDN size** — 6×6 (matches kWoodRoom) or smaller? Folded mode
   makes the FDN do extra work per pass; smaller FDN keeps the
   per-pass cost down at the price of less density.
2. **Y curve exact taper** — `Y^a` for sat and `Y^b` for
   undersample with a > b? Listening-tuned. Sketch needs
   prototyping in a Lua/emu harness before committing.
3. **Coupled regime memory budget** — at the reduced rate, what
   divisor keeps Coupled under the AM335x ceiling? `÷2` likely
   tight, `÷4` comfortable per the RotCoat analogy.
4. **Parameter automation** — Z is a topology switch, so its
   transitions can pop. Either crossfade between modes (memory
   cost), step at a zero-crossing of the tail (UX cost), or just
   accept the artifact (aesthetic cost — might be on-brand
   for a "cryptic" reverb).
5. **Mode indication on the device** — the user needs to know
   which Z regime they're in without staring at a number. Plies
   could relabel Y dynamically based on Z. Worth a viz?

## Implementation phases (sketch — not committed)

This is far downstream of the active AW port pipeline. Sketch
only:

- **Phase A**: prototype Folded mode (the default / signature
  sound) in isolation. No Z switch yet, just the per-pass-
  aliasing tail. Validate the aesthetic exists before building
  the rest.
- **Phase B**: add Nested and Coupled modes; wire the Z switch.
- **Phase C**: Y curve tuning by listening test.
- **Phase D**: package home and naming. (Habitat-native name TBD
  — XYZ is the design codename, not a unit name.)

## Where this fits in the pipeline

- Active: kWoodRoom port. Phase 0 (Smoketest harness)
  on hardware now.
- Next: WoodenBox → CreamCoat → BrightAmbience3 → Galactic →
  Verbity (per `planning/airwindows-reverb-research.md`
  addendum).
- Then: pick between XYZ and RotCoat (`planning/rotcoat-design.md`)
  as the first original-design unit. RotCoat is structurally
  more AM335x-friendly; XYZ has the more cryptic / signature
  payoff. Could ship both eventually.

## Source

`planning/refs/airwindows-port-handoff.md` §3.1 + §4.2.
