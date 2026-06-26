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

## 6. Complete control inventory (from the official MIDI manual, CC map)

Authoritative and exhaustive. Symbol key: water-drop = Wet channel, open
circle = Micro-Looper channel. CC numbers given for reference.

### A. Front panel (physical)

Knobs (7):
- TIME (CC14) - Wet: Reverb decay/size, Delay time, Slip refresh rate
- MIX (CC15) - dry/wet blend
- LENGTH (CC16) - Looper: Env slice size, Tape loop length, Stretch slice size
- MODIFY [wet] (CC17) - Reverb smear, Delay feedback, Slip speed+direction
- CLOCK (CC18) - global sample rate (tone+length+quality); stepped
  2k/3k/4k/6k/8k/12k/16k/24k/32k/48k/64k
- MODIFY [looper] (CC19) - Tape speed+direction, Stretch amount+direction
  (the glitch knob)
- RAMP SPEED (CC20) - rate of the movement/ramp LFO

Three-way toggles (3):
- Wet Channel mode (CC21): Reverb / Delay / Slip
- Routing (CC22): Input / Micro-Looper+Input / Micro-Looper only (what the
  Wet channel processes; only active when both channels on)
- Micro-Looper mode (CC23): Env / Tape / Stretch

Footswitches (2, tap + hold):
- Micro-Looper FS: tap = bypass/engage (CC102), hold = Overdub (CC106)
- Wet FS: tap = bypass/engage (CC103), hold = Freeze (CC105)
- Both: Tap Tempo (CC107/93); both-on-powerup = set MIDI channel; both + PC
  = save preset; a combo opens the Hidden Menu (CC104)

Preset toggle (2-way): preset slot 1 (right) / slot 2 (left).

### B. Dip switches (two banks of 8 = 16)

Left bank - Ramping / Expression (assigns the ramp LFO + expression pedal):
- TIME (61), MODIFY-wet (62), CLOCK (63), MODIFY-looper (64), LENGTH (65) -
  enable ramp/exp on that knob
- BOUNCE (66) - bounce/ping-pong movement
- SWEEP (67) - B vs T sweep shape (exact meaning not in the MIDI map)
- POLARITY (68) - F vs R (forward / reverse ramp polarity)

Right bank - Customize:
- CLASSIC (71) - restore original lo-fi character (per-subsystem degradation)
- MISO (72) - Mono In Stereo Out behavior (inferred from name)
- SPREAD (73) - stereo spread enable
- DRY KILL (74) - remove dry signal (100% wet)
- TRAILS (75) - bypass trails / spillover
- LATCH (76) - latch vs momentary (used for Freeze)
- NO DUB (77) - mute loop output during overdub (original hold-to-record)
- SMOOTH (78) - remove CLOCK stepping for continuous sweep

### C. Hidden menu options (reachable on pedal, also MIDI)

- STEREO WIDTH (CC24)
- RAMPING WAVEFORM (CC25) - 5 shapes (up ramp / down ramp / triangle /
  square / random)
- FADE (CC26) - loop fade-out speed
- TONE (CC27) - hi-cut / tone
- LEVEL BALANCE (CC28) - balance between channels
- DIRECT MICRO-LOOP (CC29) - blend clean micro-loop through the wet side
- SYNC (CC31) - sync routing: looper>wet / no sync / wet>looper
- SPREAD (CC32) - which channel gets the spread: wet only / both / looper only
- BUFFER LENGTH (CC33) - Half (like MkI) / Full (2x). This is the real 2x
  toggle; the named 5-mode buffer the research killed does not exist.

### D. MIDI-only (no front-panel control)

Global / misc: MIDI Clock Ignore (51), Stop Ramping (52), Clock Division wet
(53), Clock Division looper (54), True Bypass Mode (55), Factory Reset (56),
Expression Over MIDI (100), MIDI Reset (110), Preset Save (111); Program
Change preset recall/save across 122 slots, PC0 = Live mode.

Synth Mode (entirely MIDI): Enter (any MIDI note) / Exit (CC59 or move Clock);
Output Type (CC58: Open / On-Off / ADSR); Attack (80), Decay (81), Sustain
(82), Release (83); Octave Transpose (CC57: +1..+9 octaves); Portamento (84);
Pitch Bend (+/- 4 semitones); Mod Wheel (CC1). Transposes by shifting the
Clock in semitones; velocity-sensitive; saved globally.

Totals: 7 knobs, 3 toggles, 2 footswitches + preset toggle, 16 dip switches,
9 hidden-menu options, ~20 MIDI-only parameters (incl. full Synth Mode).

### Correction to Section 2

STEREO WIDTH (CC24) and SPREAD (CC32 / dip 73) DO exist as hidden options /
dip switches; the earlier refutation only killed a mis-framed claim, not the
controls themselves. BUFFER LENGTH (Half/Full) is likewise real; only the
"five named buffer modes" framing was false.
