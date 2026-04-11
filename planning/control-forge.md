# Control Forge-alike (working title TBD)

Clock-driven multistage contour generator. Spreadsheet paradigm, based on Etcher architecture. Somewhere between envelope and sequencer — respects the clock, not arbitrary durations.

## Core Concept

Clock-driven sequence of steps. Each step consumes N clock ticks. Curves stretch to fit their tick duration. No notion of absolute time — the clock is the only timebase.

## Step Types (4)

Offset steps define the level skeleton. Transitional steps (ramp/sinus/noise) describe movement between or around anchors.

- **Offset** — static voltage for duration of step. Shape = level (bipolar).
- **Ramp** — travels from previous anchor to next anchor. Shape = curvature (bipolar: linear at center, expo one direction, log the other).
- **Sinus** — oscillates between previous and next anchor levels. Shape = amplitude/number of cycles within step duration.
- **Noise** — random wander within range. Shape = range and/or density. Presents as a random offset at step boundaries so adjacent steps have a concrete target.

## Sub-Params (3 per step)

1. **Type** — offset / ramp / sinus / noise
2. **Shape** — meaning varies per type (see above)
3. **Ticks** — clock duration for this step (integer, >= 1)

## Boundary Rules

- Implicit zero on both sides, unless the opposite boundary has an anchor.
- Start of sequence: inherits from 0 if first step is non-offset. 
- End of sequence: a trailing ramp targets the first step's value if that step is an offset (or noise's pinned random value). Falls to 0 if first step has no anchor.
- Ramp between two non-offset steps: inherits destination from whatever the next offset anchor is. If none exists, targets 0.
- Noise steps pin to a random offset value at their boundaries — adjacent ramps connect to that concrete value.

## Playback

- Loops continuously. End wraps to start — trailing ramp targeting first step's value gives seamless cycling.
- Reset input snaps to 0 and resets playhead to step 1.
- One-shot variant can be derived later if needed.

## Plies (7)

1. **Step list** — navigator for up to 8 or 16 steps, sub-param editing on focus
2. **Overview** — 2-ply graphic showing segment contour (like Etcher)
3. **Clock** — input with sub-display menu containing division parameter (first unit to do this). Division lengthens/shortens sequence against incoming clock.
4. **Reset** — gate input
5. **Skew** — redistributes step tick durations across the sequence (not a per-step weight curve like Etcher — operates on the tick distribution globally)
6. **Xform** — gate-triggered transforms, Etcher-style
7. **Level** — TBD if needed, may not be necessary given shape=level for offset steps

## Skew (TBD)

Redistributes tick weights across the full step sequence. Details to be worked out — key question is whether total tick count is preserved (redistribute) or scaled (stretch).

## Open Questions

- Step count: 8 or 16 max?
- Sinus shape param: cycles vs fold vs start/end phase?
- Noise shape param: range vs density vs both?
- Skew behavior: redistribute fixed total vs proportional scaling?
- Xform selection: which transforms make sense here? Etcher's set (random/rotate/invert/reverse/smooth/quantize/spread/fold) is a good starting point.
- Loop points: simple full-loop, or configurable loop-start/loop-end for sustain-then-release patterns?
- What to call it
