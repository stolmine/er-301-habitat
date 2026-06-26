# Chase Bliss MOOD MKII: architecture map + spatial-glitch design directions

> Deep-research synthesis, 2026-06-25 (97-agent run, adversarially verified:
> 22 claims confirmed, 3 killed). Primary sources are the two official
> manuals plus the product pages, cross-checked against reputable press.
> Purpose: an architecture-level reference to design an ER-301 unit that
> takes the spatial-glitch paradigm farther than Network. Not a clone target.

## 1. Verified architecture

### Two engines, independently designed, mutually aware

- **Micro-Looper Channel** (by Drolo / Benjamin Drolez): an always-listening,
  continuously-recording short-tape looper with NO stop command. It records
  whenever bypassed and is "never really off." Three modes: Env, Tape,
  Stretch. Own MODIFY knob + 3-way mode switch.
- **Wet Channel** (by Old Blood Noise Endeavors): real-time spatial effects.
  Three modes: Reverb, Delay, Slip. Own MODIFY knob + 3-way mode switch.

### CLOCK = shared variable sample rate (the soul of the device)

The global CLOCK knob sets MOOD's sample rate, and that single variable
governs BOTH engines at once, in musical harmonized steps:
- Wet Channel: the quality AND time of the effects.
- Micro-Looper: the length AND resolution of the loops.
- Example from the manual: lowering 64k to 32k half-speeds the micro-loop
  AND the wet effect together.
- Low CLOCK introduces heavy aliasing and downsampling (grit) and longer
  times; high CLOCK is hi-fi. "CLOCK is tone, length, and quality, all in one."
- MkII SMOOTH dip switch removes the harmonized stepping for fluid sweeps.

This shared-sample-rate axis is the single most distinctive and portable
idea for re-implementation.

### Routing: a Wet-Channel source selector (NOT a chain reorder)

Middle ROUTING toggle, active only when both channels are on, selects what
the Wet Channel processes:
- Input Only
- Input + Micro-Looper Channel
- Micro-Looper Channel Only

The coupling is bidirectional and "aware": the looper captures Wet output
into its loops, and loops can be sent back through the Wet channel for
further processing. A hidden DIRECT MICRO-LOOP option blends clean
micro-loop in when routed through the wet side. This is a signal-SOURCE
selector plus both-on state, not a series/parallel reorder.

### Control semantics per mode (worth copying)

Looper:

| Knob          | Env                    | Tape                       | Stretch                      |
|---------------|------------------------|----------------------------|------------------------------|
| LENGTH        | slice size             | loop length                | slice size                   |
| MODIFY        | sensitivity (dynamics) | playback speed + direction | stretch amount + direction   |

Wet:

| Knob   | Reverb        | Delay      | Slip                        |
|--------|---------------|------------|-----------------------------|
| TIME   | decay / size  | delay time | refresh rate                |
| MODIFY | smear         | feedback   | playback speed + direction  |

Env mode is dynamics/audio-controlled (MODIFY = sensitivity), which is how
input level governs capture in that mode.

Loop length is set by CLOCK position, not a manual length like a normal
looper; the LENGTH knob can further shrink it, and a SYNC switch can reroute
length to TIME.

### The glitch mechanism (concrete)

The speed control (MIDI CC19) sets discrete playback speed AND direction
over the recorded buffer. Variable and negative playback rate over a short
captured buffer is the mechanical source of pitch, reverse, and tape-style
artifacts.
- Tape SPEED steps: 4x Rev / 2x Rev / 1x Rev / 0.5x Rev / 0.5x Fwd / 1x Fwd
  / 2x Fwd / 4x Fwd.
- Stretch SPEED steps: No-Stretch-Rev / 1.5x Rev / 2x Rev / 4x Rev / Stalled
  / 4x Fwd / 2x Fwd / 1.5x Fwd / No-Stretch-Fwd. The "Stalled" state freezes
  playback for stutter textures.

### CLASSIC: glitch as opt-in per-subsystem degradation

The MkII cleans everything up by default and exposes a per-subsystem CLASSIC
dip switch that restores the original Mood's idiosyncrasies. This is the
precise design map for "broken textures as a toggle":

| Subsystem  | MkII default            | CLASSIC                                  |
|------------|-------------------------|------------------------------------------|
| Clock      | noise-free              | noise-full (turn CLOCK down -> noise)    |
| Micro-loops| stable, infinite        | gradual deterioration / distortion       |
| Reverb     | pure, modulated         | resonant, ringy (mild feedback)          |
| Slip       | clean, consistent       | aliased, crunchy digital artifacts       |
| Delay TIME | crisp, controlled       | bendy, rubbery pitch-bends               |
| Tape LENGTH| controls loop length    | controls chopping rate                   |

### MkII additions over the original mono Mood

True stereo I/O via TRS (each mode generates its own unique stereo image,
mono-to-stereo); unlimited overdub layering (the loop itself stays short,
"no recording limit" = layers, not infinite buffer); binary 2x loop-length
toggle (with return to the shorter original length); loops preserved across
mode changes; freeze; adjustable fade (loops fade away at a chosen speed);
tone / hi-cut filtering; expanded routing; deep MIDI (note/CC/PC/clock sync,
control of every parameter); a monophonic Synth Mode (ADSR, portamento,
velocity; pitch transposes the wet output via Clock in semitones, not
arbitrary input repitch; MIDI clock ignored in Synth Mode); SMOOTH (remove
clock stepping); NO DUB (mute loop output during overdub, replicates the
original's hold-to-record); ping-pong stereo; switchable true/buffered
bypass (buffered default).

## 2. What the research killed (do not build on these myths)

- NO named buffer-length modes ("Half / Full / Tape / Stretch / Micro-Looper").
  The only documented buffer-length option is the binary 2x toggle plus the
  CLOCK-derived length. (0-3 refuted.)
- ROUTING is NOT a series-vs-parallel reorder of the two channels, and there
  are no STEREO WIDTH / SPREAD controls. It is a Wet-Channel source selector.
  (0-3 refuted.)
- "Granular" is loose marketing. Chase Bliss calls it a micro-looper; the
  grain-like texture comes from a very short buffer plus low sample rates,
  not a true granular engine.

## 3. Undocumented gaps (open questions)

- Absolute micro-loop buffer length (ms/samples) at full CLOCK, and the full
  sample-rate range and number of harmonized steps. Only 64k and 32k are ever
  named as example points.
- DSP platform / chip. No teardown-level hardware detail surfaced.
- Exact Env-mode capture mechanism (level-threshold arm vs envelope-gated
  slice trigger vs amplitude-windowed playback) beyond "MODIFY = sensitivity."
- How per-mode true-stereo imaging is implemented internally (dual mono,
  mid-side, decorrelated allpass/delay spreads, or per-mode algorithms).

## 4. Sources

Primary: MOOD MKII Pedal Manual and MIDI Manual (static1.squarespace.com),
chasebliss.com/mood-mkii and /mood. Secondary/press: guitar.com big review,
MusicRadar, GuitarPedalX, effectsdatabase, SynthAnatomy, delicious-audio.
Time-sensitivity: MkII shipped 2024; verify CC19 speed tables against the
current firmware before hard-coding.

## 5. Design directions for a 301 spatial-glitch unit

Network gave us the spatial graph. Mood's transferable soul is three ideas;
the interesting unit is their product, not a clone of either device.

1. **A global sample-rate / time-grit axis.** One control that warps length,
   pitch, time, and aliasing across the ENTIRE spatial field at once, in
   harmonized steps. Network has no unifying axis like this; a "CLOCK" that
   simultaneously stretches every delay/reflection and degrades the sample
   rate is the headline move. (We already own the relevant primitives:
   reduced-rate domain harness from RotCoat, the alias-synthesis paradigm
   from Mirror.)
2. **Nodes that are living micro-loopers, not fixed taps.** Instead of Mood's
   two engines, picture Network's graph where each node continuously captures
   recent input into a short buffer and replays it at its own speed/direction.
   The room is built from glitched fragments of what you just played, not
   clean reflections. This is genuinely past both devices.
3. **Bidirectional capture-feedback.** Mood's looper records the wet output
   and feeds loops back through it. Generalized: the spatial graph feeds the
   loopers and the loopers feed the graph - a controllable runaway. Pair with
   the Spiral feedback-governor pattern so it sings into saturation instead
   of blowing up.

Keep CLASSIC as the cleanliness axis: ship clean by default, expose
per-stage degradation (loop deterioration, aliased playback, ringy feedback)
as opt-in rather than baked-in lo-fi.

### Raw feature pool to distill next

The next step is sloughing off, distilling, and combining these into a
control set coherent on the 301 (limited knobs/plies, encoder + 6 sub
buttons, multi-out framework). Candidate axes drawn from Mood + Network:

- Global CLOCK / sample-rate-grit (the unifying axis).
- Micro-loop capture: length (CLOCK-derived + shrink), always-on vs armed,
  fade/decay, freeze, 2x length, overdub/layer.
- Playback: speed + direction (discrete steps incl. Stalled), Stretch.
- Env/dynamics capture (sensitivity-gated slice).
- Spatial field: the Network graph (size, decay, diffusion, the listener/
  plexus topology) as the "wet" side.
- Routing/coupling: input vs loop source, graph<->looper feedback amount.
- Character: CLASSIC-style per-stage degradation toggles.
- Stereo imaging (per-mode stereo generation) vs the package's stereo
  conventions (dual-instance vs internal-stereo, per feedback_stereo_pattern_selection).
- Modulation: CV over the high-leverage axes (CLOCK, speed, feedback).

Distillation goal: a control surface that expresses the spatial-glitch
identity in roughly 5-6 main plies + expansions, not a 1:1 port of Mood's 30
parameters. Decide which axes are macros, which are sub-params, which are
menu/dip-style toggles, and what the multi-out taps should be.
Cross-references: planning/network-* docs, project_alias_synthesis_paradigm,
the Mirror design docs, feedback_spiral_feedback_governor.
