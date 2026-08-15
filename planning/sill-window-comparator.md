# Design note: Window Comparator - dual window comparator on the multi-out framework

Status: design note / not started. Ledger item `sill-window-comparator`.

User request 2026-08-14, after the mid/side unit: a dual window comparator in the
manner of Joranalogue Compare 2, built on the multi-out framework, with custom
controls if they earn their place.

Fills the logic/comparator gap - no gate logic, no comparator, no window
detector exists in the collection *or* in the firmware, so a patch that needs one
currently cannot get one.

Name: **Window Comparator**. Units outside spreadsheet take descriptive, factual names (user direction 2026-08-14), matching how biome already names things (Fade Mixer, Tilt EQ, Gated Slew, Spectral Follower). The poetic working title *Sill* is retired; evocative names stay in spreadsheet.

Package: **biome**, with the CV utilities (Gridlock, Integrator, Quantoffset,
PSR). Biome's naming is otherwise functional, so "Window Comparator" is the
fallback if Window Comparator reads as too cute next to Tilt EQ.

## The reference, from the manual

Compare 2 user manual, rev C, 2018-06-18 (joranalogue.com). 8 HP, two identical
window comparators plus a logic section. A window comparator differs from a plain
comparator in that it fires when the input is *between* two levels rather than
above one.

**Per comparator**

- **Shift** knob offsets the window's centre, -5 V to +5 V, 0 V centred.
- **Size** knob sets the distance between the window edges, "from very small (a
  few mV) to 10 V maximum."
- **Shift and Size CV inputs**, added to the knob settings to produce the final
  window. Both accept negative CV. Critically: "In the case of the size
  parameter, this can cause the window to become negative, which simply means the
  comparator output will never become active." That is a feature, not a
  degenerate case - it is what lets the CV inputs act as extra gate inputs.
- **Signal input**, CV or audio.
- **OUT** is +5 V when the input is inside the window, 0 V otherwise. **NOT** is
  its logical inverse.

**Normalisation**: signal inputs and CV inputs are both normalled left to right,
"so by default the same signal is sent to both comparators" and "both comparators
can be voltage controlled together." The two halves can still be used completely
independently by patching the right side.

**Logic section**, fed from both comparators: **AND** active only when both are
active, **OR** when one or both, **XOR** only when one is. Rising edges of XOR
toggle a flip-flop, **FF**. All +5 V active, 0 V inactive.

**LEDs**, which the manual calls "Compare 2's most striking feature": blue when
the input is below the window, red when above, white when inside. If the window
size is negative and the signal is within that negative window, the LED turns
off rather than white.

**Patch ideas given**, worth reading as a spec for what the unit must be able to
do:

- *Complex rhythm generator* - LFO into the input, different gates emerge on the
  logic outputs at different points in the cycle.
- *Dual pulse width modulator* - audio rate. A triangle in, and the gate output
  is a pulse whose width is set by shift and size; "unlike a regular pulse, the
  frequency can transition between fundamental and +1 octave." Phase modulation
  by modulating shift with a narrow window.
- *Frequency multiplier/divider* - same signal to both comparators, XOR output
  transitions between fundamental, +1 and +2 octaves; FF gives -1/0/+1.
- *Digital ring modulator* - two different frequencies in, XOR as audio out,
  pulse-width-modulatable.
- *Dynamic depth oscillator sync* - XOR into a VCO's FM input.
- *Chaotic envelope looper* - envelope output into the input, FF back to the
  EG's gate; the envelope oscillates between two boundary levels, and modulating
  a window parameter makes it chaotic.
- *Logic function array* - shift at maximum and size centred puts the window
  edges at +2.5 V and +7.5 V, which reliably passes +5 V gates. Because the shift
  and size CV inputs also behave as gate inputs, "a total of 6 gate input signals
  can be processed for complex logic functions."

Four of those seven are audio-rate. That matters below.

## Why this is a multi-out unit

Eight outputs - out A, not A, out B, not B, AND, OR, XOR, FF - all derived from
shared internal state. It passes the author guide's "derivable at destination"
test comfortably: you cannot reconstruct AND from out A alone downstream, and the
whole point of the logic section is the relationship between the two comparators.

Against `docs/multi-output-units-author-guide.md`:

- **Sub-out labels** (≤6 chars, descriptive, no unit name):
  `{"outA", "notA", "outB", "notB", "and", "or", "xor", "ff"}`.
- **Sub-out 1 is primary** and auto-wires on insertion, so it must be the most
  useful default. `outA` is the obvious choice.
- **Eight is near the practical ceiling.** The guide caps at 99 graphically but
  says "if your design calls for ≥10 sub-outs, look hard at whether the unit
  should instead be a sub-chain." Eight is inside that, but the M6 cycler will be
  a little tedious. It is the right count anyway - it is the module's actual
  output set, and dropping any one of them loses a documented patch.
- **CPU is Category A/B throughout**: comparisons and boolean ops on state we
  already have. The guide says gate Category B on `Outlet::isConnected()`; here
  the compute is so cheap the branch may cost as much as the work, so gate the
  four logic outputs and leave the four comparator outputs ungated, then measure.
- **No `sinf`/`cosf` anywhere in the DSP**, so the am335x package-trig bug does
  not apply. If the visualizer needs trig, use the `kLutCos`/`kLutSin` pattern
  from `mods/spreadsheet/FilterResponseGraphic.h`.
- **Vanilla**: sub-outs 3+ are invisible on stock firmware, so six of the eight
  outputs are stolmine-only. The unit still loads and `outA` still works. This is
  a stolmine-first unit and should be described as one.

## Normalisation - use the cascade mask, not source rebinding

The hardware normals signal and CV left to right. The author guide already has
the blessed pattern for exactly this ("Just-Friends-style right-to-left
normalling"): each frame, Lua walks the sub-chains and computes a presence mask
with `getInputSource(1) ~= nil`, pushes the mask to C++ as a parameter, and the
DSP falls through to the neighbour for unpatched cells.

Two things the guide is emphatic about, both of which apply here:

- **Do not** rebind empty cells with `chain:setInputSource(j, ...)`. It mutates
  inlet ownership on the audio thread and makes the engine-visible source
  structure a lie.
- Use `getInputSource(1) ~= nil`, not `branch:length() > 0`. The latter is true
  when a unit has been added but nothing patched into it, which is not what
  presence means here.

So: comparator B's input, shift and size each get a branch, and each falls
through to A's when unpatched. No Link control is needed - the normalling *is*
the link, exactly as on the hardware.

## Controls

| control | notes |
|---|---|
| **Shift A** | bipolar, CV branch |
| **Size A** | bipolar - negative must stay reachable, see below |
| **Shift B** | bipolar, CV branch, normalled to A |
| **Size B** | bipolar, CV branch, normalled to A |
| **In B** | mono branch, normalled to the chain input |
| **Edge** | option: Hard / Smooth - see aliasing below |

Per the guide's control-polarity section, Shift and Size are naturally bipolar,
so declare them bipolar directly rather than unipolar-plus-toggle.

**Do not clamp Size at zero.** Negative size means the window never opens, and
the manual leans on that: it is how the CV inputs double as gate inputs, and it
is what the logic-function-array patch depends on. A well-meaning clamp would
silently delete a documented feature.

## The custom control: a window scope

This is where the custom control earns itself, and it is the direct translation
of the manual's "most striking feature."

One ply (two if it wants room), showing:

- the input trace, scope-style;
- **the two windows drawn as horizontal bands** across the trace, so shift and
  size are visible as geometry rather than as numbers;
- the trace coloured by state against window A - below / inside / above - which
  is the three-colour LED made continuous rather than instantaneous;
- a row of eight indicators along the bottom, one per sub-out, lit when active.

The bands answer the question the hardware's LED can only answer one sample at a
time: *where is my window relative to the signal, and how close is it to
catching*. That is the whole skill of using a window comparator, and it is
invisible on the hardware.

Related: `project_bias_indication` (dotted overlay showing a param's value during
encoder interaction) is the same instinct and the band edges should follow that
convention while the encoder is live. `docs/graphics-authoring-guide.md` for the
drawing primitives.

## Aliasing - the one genuine DSP problem

Compare 2 is analogue, so its gate edges land wherever they land. Ours are
digital and quantised to the sample grid, and **four of the seven documented
patches are audio-rate**: dual PWM, frequency multiplier, digital ring modulator,
oscillator sync. Hard edges at audio rate will alias badly, and the ring
modulator patch in particular is exactly the case where quantised edge timing
turns into inharmonic hash.

So the Edge option is not garnish:

- **Hard** - naive comparison, edges on the sample grid. Correct and cheapest for
  CV-rate use, which is most use.
- **Smooth** - BLEP-corrected edges, with the transition placed at the
  sub-sample crossing found by linear interpolation between the two straddling
  samples. This is the same problem `helicase-sync-polyblep` addresses at a
  carrier reset, and the same solution.

Hard is the default because the CV-rate patches are the common case and BLEP on a
gate that a downstream comparator will re-threshold is wasted work.

## DSP

Trivial - the entire per-sample core:

```
lowerA = shiftA - sizeA*0.5;  upperA = shiftA + sizeA*0.5
a = (in > lowerA) && (in < upperA)     // sizeA < 0 => never true, by construction
b = (inB > lowerB) && (inB < upperB)
outA = a;  notA = !a;  outB = b;  notB = !b
and = a && b;  or = a || b;  xor = a != b
if (xor && !xorPrev) ff = !ff;  xorPrev = xor
```

No libm, no branching on options inside the loop (resolve Edge to a function
choice at block rate per `feedback_runtime_branched_dsp_dispatch`), one bit of
state for the flip-flop and one for the XOR edge detector.

**Pin the voltage convention before writing any of it.** The manual's numbers are
in volts (-5..+5 shift, 10 V max size, +5 V gates, +2.5/+7.5 V window edges in the
logic-array patch) and the 301 has its own internal scaling. Read what the
firmware's `Offset` unit and `app.Comparator` actually use and match it, rather
than assuming 1.0 means 10 V. Getting this wrong makes every number in the manual
useless as a starting point.

## Phases

1. **DSP atom.** Eight outlets, hard edges, comparator B reading its own inlet.
   Verify against a written truth table before any UI exists, including the
   negative-size case and the FF toggle.
2. **Multi-out wrapper.** `subOutLabels`, `channelCount = 8`, primary auto-wire.
   Run the guide's full stolmine-emu checklist: picker cycling with M6, subscribe
   to a non-primary sub-out, preset save/restart/reload.
3. **Normalling.** Cascade mask per the guide, presence via `getInputSource(1)`.
   Test each of the three branches unpatched and patched.
4. **Smooth edge mode.** BLEP with interpolated crossing; A/B the ring-modulator
   patch against hard mode as the falsification.
5. **Window scope.** Bands, state colouring, eight indicators.
6. **Vanilla + hardware.** Both checklists from the guide, including the
   preset-drops-sub-out-3 case on vanilla.

## Attribution

Compare 2 is a hardware module; nothing is being ported. A window comparator is a
standard electronics building block, which the manual itself says ("common in
general electronics, but rarely found in modular synths"). The debt is to the
*arrangement* - two windows, complementary outputs, that specific logic set,
left-to-right normalling - and to the patch ideas, so credit Joranalogue's
Compare 2 as the design reference in the unit description and release note. No
clean-room concern; there is no code involved.
