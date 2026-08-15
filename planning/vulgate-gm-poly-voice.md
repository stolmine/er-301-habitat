# Design note: Vulgate - a polyphonic General MIDI voice

Status: design note / not started. Ledger item `vulgate-gm-poly-voice`.

User request 2026-08-14: "a general midi voice, preferably poly", referencing
**Expert Sleepers General CV**.

Named **Vulgate**: the common, standard, universally-available version of the
text. That is exactly what General MIDI is, cheesiness included. Sits with
Fabula, Sujet, Anamnesis and Palimpsest.

Package: **spreadsheet**, with the other voices (Ngoma, Visadhara, JF).

## The reference

General CV (Expert Sleepers, 12 HP) is "a combination of a powerful General MIDI
synthesizer, a multi-channel CV-to-MIDI converter, and a host processor capable
of generating huge amounts of MIDI data." 128 GM programs, five drum kits, 16
voices of polyphony, stereo reverb and chorus, four-band EQ.

The relevant mode is **CV To MIDI**, which "essentially takes a pitch CV and a
gate and generates a MIDI note to send to the synthesizer engine." Its parameter
list is the spec worth copying:

| # | parameter | range | note |
|---|---|---|---|
| 1 | Program | 0-127 | "A new sound choice only takes effect when a new note starts" |
| 2 | Unison Voices | 1-8 | |
| 3 | Unison Detune | 0-16 | |
| 4/5 | Reverb / Chorus | 0-127 | send amounts |
| 6 | Manual Gate | 0-1 | "Useful for creating drones" |
| 7 | Sustain | 0-1 | notes hold past gate-low |
| 8/9/10 | Quantizer Enable / Scale / Key | 16 scales | |
| 11 | Mod Wheel | 0-127 | |
| 12 | Transpose | ±36 st | applied *before* quantisation |
| 13/14/15 | Attack / Decay / Release Scale | 0-127, 64 = as programmed | |
| 16/17 | Cutoff / Resonance Scale | 0-127, 64 = as programmed | |
| 18 | Velocity | 1-127 | |

Two details worth keeping:

- **The unison detune spread is tabulated**, not computed: 2 voices are
  `-½d, +½d`; 3 are `0, -d, +d`; 4 are `-½d, +½d, -1½d, +1½d`; and so on to 8.
  Odd counts centre a voice, even counts straddle. Cheap to copy exactly.
- **Pitch is sampled only at gate-high** in CV To MIDI mode. **VCO mode** differs
  precisely: "The Pitch CV controls the sound's pitch continuously, not just when
  a note is triggered," changing Program switches immediately rather than at the
  next note, and there is no Sustain. That is a clean two-mode split worth
  reproducing.

The 16 quantiser scales are listed explicitly in the manual (Chromatic, Major,
Minor, Triad, Minor Triad, Root+Fifth, Triad+6, ... Harmonic Minor) with their
semitone sets. Transcribe rather than invent.

## The blocker: there is no polyphony anywhere

A code survey of habitat and the firmware found:

- **No voice allocator, no voice manager, no note-on/note-off routing, no
  stealing, no voice pool.** Nothing, in either tree.
- **No multisample support** - no keymaps, no velocity layers, no SF2 or DLS
  handling. Sample attachment is strictly one sample per unit.
- **No card streaming.** Every player loads the whole file into RAM via
  `SampleLoader`, which processes one load at a time. `SampleFifo` exists but is
  for recording, not playback streaming.
- The audio thread stack is **2048 bytes for the entire chain**.

The closest existing thing is Breccia, which runs up to 7 concurrent layers - but
they are harmonic offsets from a *single* read position over one buffer, not
independent voices.

So the deliverable is not really "a GM voice." **It is a voice-allocation layer,
with a GM voice as its first consumer.** That layer is reusable and is already
wanted by two other ledgered items (`polyphonic-sample-playback`,
`units-poly-sample-player`). Build it as an atom, exactly as
`ochre-character-eq` and `stft-frontend-atom` argue for their own front ends.

## Where does "poly" come from?

This deserves stating because it is not obvious. The ER-301 has **no MIDI input
at all**. A unit gets one chain input and whatever branches it declares. With a
single pitch CV and a single gate, polyphony has to come from somewhere:

1. **Release tails.** A monophonic gate sequence into a piano program still needs
   eight voices, because each note's release outlives the next note's onset.
   This is the least glamorous source of polyphony and the one that matters most
   - without it a sampled piano is unusable.
2. **Chord generation from one CV.** General CV's Chord mode: key, scale, chord
   shape, inversion. One pitch CV becomes three or four notes.
3. **Unison.** The tabulated detune spread above, 1-8 voices.
4. **Multiple gate/pitch branch pairs**, if real independent polyphony is wanted.
   Expensive in UI terms and probably a later phase.

Sources 1-3 all work from a single CV and gate and cover most of the musical
value. **Ship those; defer 4.**

## The fork: sampled or synthesised

This is a phase-0 decision that changes the entire project, and it should be
made deliberately rather than drifted into.

**Route A - sampled (a real ROMpler).** Preprocess a freely-licensed soundfont
offline into a habitat-native format via a new tool, load into RAM, play back
multisampled with keymaps.

- Faithful to what GM actually sounds like, which is most of the appeal.
- Requires: a new offline tool, a keymap/multisample format, a licence-clean
  sound set (GeneralUser GS, MuseScore_General and FluidR3 are the usual free
  candidates - **check each licence properly, do not assume**), and a RAM budget
  that has never been measured on this hardware.
- The no-streaming constraint is the real risk. A trimmed mono set at reduced
  sample rate is the only plausible shape, and even that needs measuring before
  anything is built.

**Route B - synthesised.** A small parametric engine plus a 128-entry program
table that sets oscillator type, filter, envelope and effects sends per program.

- Vastly cheaper, no licensing, no RAM question, no new tooling.
- Will not sound like GM. It will sound like a synth pretending, which is
  either charming or pointless depending on intent.

**Decided 2026-08-14: Route A.** The user's words: "we want a sample based
version, may not be possible." So Route B is a fallback only, taken if and only
if the phase-0 measurement rules Route A out - and "not possible" is an
acceptable thing to report rather than something to quietly substitute around.

Phase 0 still runs first and unchanged: a RAM and load-time measurement with a
trimmed soundfont on actual hardware. The voice-allocation layer is identical
either way, so it is safe to build before that measurement lands.

## Controls

Front: **Program**, **Voices** (polyphony count), **Chord** (shape), **Detune**,
**Mix**. The pitch CV is the chain input; **Gate** is a branch.

Expansion views, following Parfait's structure:

- Program: Transpose, Velocity, Mod Wheel
- Voices: unison count, stealing mode, Sustain, Manual Gate
- Chord: Key, Scale, Inversion, quantiser enable
- Envelope: Attack / Decay / Release scale, Cutoff / Resonance scale, all
  centred so that mid-position is "as programmed" per the manual's 64 convention

Reverb and chorus sends are **deliberately dropped** - the ER-301 has actual
reverbs, and a send to an internal one would be worse than routing to Fabula.

## Cautions

- **Scope.** This is the largest thing proposed in this batch by a wide margin,
  and it has a hard prerequisite that does not exist yet. It should not be
  started before the voice-allocation atom is real.
- **Sample licensing** is a genuine gate, not a formality. Whatever set is
  chosen, its licence goes in the package README and the release note, the same
  discipline as `readme-airwindows-attribution`.
- **"General MIDI" is a specification**, published by the MIDI Association;
  program numbering and the drum map can be implemented freely. What cannot be
  freely lifted is any *particular vendor's* sound set. Name the soundfont used.
- **Voice stealing must be audible-safe**: steal the oldest released voice first,
  then the quietest, and always fade rather than cut. A clicking stealer ruins
  the unit.
- **CPU** scales with voice count, so the polyphony control is also the CPU
  control. Make that legible to the user rather than hiding it.
- Audio thread stack is 2048 bytes total - per-voice state goes in an Internal
  struct on the heap, never on the stack (`CONTEXT.md`).

## Phases

0. **Decide the fork.** RAM and load-time measurement on hardware with a trimmed
   soundfont. Nothing else starts first.
1. **Voice-allocation atom.** N voices, allocation, stealing with fades,
   gate/pitch handling, release tails. Testable with the simplest possible
   sound source - a sine. This is the reusable deliverable.
2. **Voice engine** per the phase-0 decision.
3. **Chord and unison** from one CV, with the tabulated detune spread and the
   16 transcribed scales.
4. **Envelope and filter scaling**, centred at "as programmed".
5. **VCO mode** as the continuous-pitch variant.
6. **Hardware**: CPU at each voice count, serialization, and a listening pass
   on the thing GM is actually for - playing a multi-part arrangement.
