# Design note: Facet - a normalled macro controller

Status: design note / not started. Ledger item `facet-macro-controller`.

User request 2026-08-14: a multi-out macro controller, in the manner of Ladik
J-011 Joystick Math or Mannequins Cold Mac.

Named **Facet**: one stone, many faces, all cut from the same block, each
showing the same light differently.

Package: **biome**, with the CV utilities.

## The two references

**Ladik J-011 Joystick Math** (4 HP) is the simple one: two inputs named X and Y,
and outputs for `X+Y`, `-X-Y`, `X-Y`, `Y-X`, plus positive-only and negative-only
rectification of Y. Works at audio rate; usable as a standalone inverter or
rectifier. It is a bag of two-input arithmetic, and that is all it claims to be.

**Mannequins Cold Mac** is the one worth studying, and its design idea is not the
maths. Its blocks are ordinary - a crossfader, an analogue max (`OR`), an
analogue min (`AND`), a full-wave rectifier (`SLOPE`), a wrap-around wavefolder
(`CREASE`, "+5 if ≤ 0, −5 if > 0"), a slew-based envelope follower (`FOLLOW`),
an integrator (`LOCATION`, "rate of change proportional to the input voltage"),
and an AC-coupled summing VCA (`MAC`).

What makes it a *macro controller* is the **normalling**. From the technical map:
the summed SURVEY value "is then normalled into these jacks: LEFT/RIGHT
Crossfader Block: FADE Input; OR/AND: OR2 Input (which is normalled to AND2
Input); SLOPE/CREASE: SLOPE Input (which is normalled to CREASE Input)." Plus
−5 V into LEFT, +5 V into RIGHT, OR1 into AND1, SLOPE into CREASE. Inserting any
cable breaks that normal.

So one knob drives eight outputs at once, each through a different transfer
function - and patching any input peels that block off the macro and makes it an
independent utility. The panel graphs are literally the transfer functions with
SURVEY on the X axis.

**That is the thing to build.** A bag of math functions is the part that is
easy and the part that matters least.

## The derivability question, answered honestly

The multi-out author guide is explicit: "If outputs are independent and could
just as well be parallel mono units, don't make them multi-out."

Facet has to answer that, and the answer is not automatic:

- The **combinatorial** outputs mostly *are* derivable downstream. `Fold` and
  `Rectify` already exist as core units. `Offset`, `Slew Limiter` and biome's
  `Integrator` exist. Only min and max have no existing unit anywhere.
- The **stateful** outputs - the follower and the integrator - are not derivable
  from the others, but they are derivable from the *input* if you have those
  units, which you do.

So the justification is neither of those. It is that **all eight derivations
share one control source**. Reconstructing that downstream means patching one CV
into eight separate units and keeping them in sync - which is exactly the
guide's own quadrature-LFO reasoning ("reconstructing exact phase lock requires
the same internal state") in a different key. The macro relationship is the
product; the maths is the packaging.

This is worth writing down because if the normalling gets dropped during
implementation, the unit loses its reason to exist and becomes eight utilities in
a trenchcoat.

## Shape

**Inputs**

- **Survey** - the macro. Knob plus CV branch. Bipolar.
- **X** - the chain input, **normalled to Survey** when nothing is patched.
- **Y** - a mono branch, **normalled to X** when nothing is patched.

That cascade is Cold Mac's scheme reduced to what the ER-301 can express: a unit
has one chain input, so the second operand arrives as a branch, following
`Warps.lua:49-53`. Presence detection uses the guide's cascade mask
(`getInputSource(1) ~= nil`, pushed to C++ as a parameter, DSP falls through to
the neighbour) - **not** source rebinding, which the guide forbids. Same mechanism
as `sill-window-comparator`, and a second consumer for it.

**Eight sub-outs**, all labels within the 6-char limit:

| # | label | function |
|---|---|---|
| 1 | `sum` | X + Y — primary, auto-wires on insert |
| 2 | `diff` | X − Y |
| 3 | `max` | max(X, Y) — Cold Mac's OR |
| 4 | `min` | min(X, Y) — Cold Mac's AND |
| 5 | `abs` | \|X\| — SLOPE |
| 6 | `fold` | wrap fold — CREASE |
| 7 | `slew` | slews toward \|X\| — FOLLOW |
| 8 | `intg` | integrates X, rate ∝ X, clipped — LOCATION |

Eight sits just under the guide's "reconsider at ≥10." Gate the two stateful
outputs and `fold` on `Outlet::isConnected()`; leave the four cheap
combinatorial ones ungated, where the branch would cost as much as the work.

**Controls**: Survey (CV), Y (branch), Time (slew and integrate rate, split into
two sub-params), Fold (threshold), Offset, and **Reset** (gate) for the
integrator.

The integrator reset is not optional. An integrator with no reset rails and stays
there, and the unit then looks broken. biome's existing `Integrator` has one;
match it.

## Deferred

**MAC itself** - the AC-coupled summing VCA across all blocks, gain
`(SURVEY+5)/10`. It is Cold Mac's namesake and what makes it an audio mixer as
well as a CV utility. Left out because it would be a ninth output and because
`sum` covers part of it. Revisit once the unit exists; if it goes in, it should
replace an output rather than extend the count.

## Cautions

- **Voltage convention.** Cold Mac's constants are in volts (±5 V rails, CREASE
  wrapping by ±5). Pin the 301's scaling against the core `Offset` and
  `Rectify` units before transcribing any number, exactly as
  `sill-window-comparator` requires.
- **Audio rate is a feature, not an accident.** Ladik notes J-011 "works in the
  audio range," and min/max/abs/fold at audio rate are waveshapers -
  rectification, folding, and a min/max pair that behaves like a crude ring
  modulator. Their hard corners will alias. Unlike Sill I would *not* add a BLEP
  mode: these are shaping functions rather than gate edges, the aliasing is part
  of the character, and the cost is real. Document it rather than fixing it.
- **Vanilla**: sub-outs 3+ are invisible on stock firmware, so six of eight
  outputs are stolmine-only. This is the **fourth** stolmine-first unit in two
  days (`diptych-mid-side`, `sill-window-comparator`, `assay-audio-listener`,
  this). biome has been vanilla-clean until now and this is no longer drift - it
  needs a decision.
- **`feedback_runtime_branched_dsp_dispatch`**: the cascade mask and the
  `isConnected` gates are block-rate decisions.

## Phases

1. **Cascade normalling first**, not last. Survey → X → Y fall-through with the
   presence mask, proven with two dummy outputs. If the normalling is not right
   the unit has no point, so it should not be the last thing built.
2. **The six combinatorial outputs.** Trivial maths; the work is the voltage
   convention and the transfer-function documentation.
3. **Slew and integrator**, with Reset and clipping.
4. **Hardware**: multi-out picker checklist, serialization of the branch
   contents and every control, CPU with all eight patched and with only the
   primary patched.
