# Catchall (`catchall`) v0.4.1

> **This package is EXPERIMENTAL.** Everything here is work-in-progress: control
> surfaces are unstable between versions, several parameters are wired but not
> reachable from the screen, and at least one unit does not currently load (see
> Flakes). Nothing in this package is covered by the compatibility promises that
> apply to the other packages. Use it for exploration, not for a set you have to
> reproduce next week.

Five units, all in the **Experimental** category: a z-plane morphing filter, a
seeded procedural voice, a freeze looper, a self-organizing-map voice, and a
sample-trained phase-modulation voice. All original work: none of these are ports
of third-party algorithms, though Flakes began life as a C++ rewrite of a
community ER-301 Lua preset ("Joe's Shards") and the state-variable filter core
used by several of them follows Andrew Simper's (Cytomic) TPT topology.

---

## Sfera

*mnemonic: Sf* · Category: Experimental

A z-plane morphing filter. A cascade of up to seven state-variable filter sections
sits inside a "cube" of four corner configurations; Morph X and Morph Y are the
position inside that cube, and every coefficient is bilinearly interpolated between
the corners as you move. The 128 selectable cubes span Butterworth low/high/band/
notch shapes, Moog-ladder colorations, formant vowels, combs, phasers, resonators
and EQ curves, so a single X/Y sweep can walk from a clean 6-pole lowpass into a
vowel or a comb. A ferrofluid-sphere visualizer reacts to the pole/zero layout and
the signal envelope.

**Controls** (expanded order): `config`, visualizer, `X`, `Y`, `cutoff`, `spin`, `level`.

| Control | Type | Range / Options | Default | What it does |
|---|---|---|---|---|
| `Config` (`config`) | stepped selector (GainBias + label) | 0 … 127, integer | 0 | Chooses the morph cube. 0-31 are hand-authored and carry short names on the fader (`BW LP>HP`, `BW LP>BP`, `BW 2>6p`, `BP>Ntch`, `BW Quad`, `BW Fade`, `BW Ring`, `BW Deep`, `Moog Q`, `Moog>BW`, `Moog Sw`, `Moog Rng`, `AEIO`, `AUOI`, `EIUA`, `Sop>Bas`, `B>S AE`, `B>S IO`, `Vox Dia`, `Vox Rev`, `Comb`, `Cmb>Flt`, `Cmb Shf`, `Cmb>Res`, `Phase`, `Phs>BW`, `Phs>Res`, `Phs Osc`, `Reson`, `Res>Flt`, `Res>Vox`, `Res>Cmb`). 32-127 are procedurally generated and labelled `g32` … `g127`. |
| n/a (visualizer) | display ply | n/a | n/a | The ferrofluid sphere. Not editable. |
| `Morph X` (`X`) | GainBias | 0 … 1 | 0.5 | Horizontal position in the cube. Crossfades the two left corner configs against the two right. |
| `Morph Y` (`Y`) | GainBias | 0 … 1 | 0.5 | Vertical position in the cube, same idea on the other axis. |
| `Cutoff` (`cutoff`) | GainBias (custom control with sub-display) | 20 Hz … 20000 Hz | 1000 Hz | Scales the whole cascade's frequency. 1000 Hz leaves each config at its authored frequency; the value is a multiplier, not an absolute corner frequency. |
| `Spin` (`spin`) | GainBias | −2 … 2 | 0 | **Visual only.** Rotation velocity and pole spread of the sphere graphic. It has no effect on the audio at all. |
| `Level` (`level`) | GainBias | 0 … 2 | 1.0 | Output gain, multiplied with the per-config makeup gain. |

**Sub-display / expanded**: on the `cutoff` ply only, tap SHIFT (press and release
without turning the encoder) to swap the sub-display for a **Q Scale** page. It
holds one readout under sub-button `Q`, range 0.25 … 4.0, default 1.0, which
divides the resonance damping of every section: below 1 the filter is tamer, above
1 it sharpens toward self-oscillation. Tap SHIFT again, or leave the ply, to go
back. Q Scale is not available anywhere else.

**Menu**: none.

**I/O**: mono or stereo; on a stereo chain a second filter instance handles the
right channel with all parameters shared, so both channels track identically.
`In1`/`In2` → `Out1`/`Out2`. No gate or trigger inputs. A `V/Oct` inlet exists in
the DSP graph and does exponentially transpose the cutoff, but **no V/Oct control
is placed on any ply**, so it is not reachable from the screen in this version.

---

## Lambda

*mnemonic: Lm* · Category: Experimental

A seeded procedural synth voice. A single integer Seed (0-999) deterministically
generates an entire instrument: a 24-frame additive wavetable built from sixteen
randomized harmonics, plus a matching bank of randomized pole/zero filter sections.
Scan sweeps one position through both the wavetable and the filter bank at once, so
timbre and filter character morph together. The same seed always gives the same
instrument, so a seed number is a patch you can write down.

**Controls** (expanded order): `seed`, visualizer, `V/Oct`, `scan`, `f0`, `cutoff`, `level`.

| Control | Type | Range / Options | Default | What it does |
|---|---|---|---|---|
| `Seed` (`seed`) | stepped selector (GainBias + label) | 0 … 999, integer | 0 | Regenerates the whole instrument: wavetable, harmonic content, and a bank of 1-4 filter sections per frame with randomized pole angle, pole radius and zero placement. Changing it is not click-free: the regeneration runs inline in the audio path. |
| n/a (visualizer) | display ply | n/a | n/a | Draws the currently morphed 256-sample wavetable frame; brightness follows output level. |
| `V/Oct` (`V/Oct`) | Pitch | standard pitch control | 0 | Exponential pitch tracking on the oscillator. |
| `Scan` (`scan`) | GainBias | 0 … 1 | 0.0 | Position across the 24 wavetable frames **and** the matching 24 filter-bank entries simultaneously. The two are locked together. |
| `Fundamental` (`f0`) | GainBias | 0.1 Hz … 2000 Hz | 110 Hz | Base pitch, before V/Oct. Internally clamped just under Nyquist. |
| `Cutoff` (`cutoff`) | GainBias | 20 Hz … 20000 Hz | 1000 Hz | Multiplier on the generated filter bank's frequencies (1000 Hz = as generated). Does not track V/Oct. |
| `Level` (`level`) | GainBias | 0 … 1 | 0.5 | Output gain. |

**Sub-display / expanded**: none beyond the stock GainBias sub-displays.

**Menu**: none.

**I/O**: a **generator**, with no audio input. The chain input is connected to a
dead-end and discarded, so Lambda can sit anywhere in a chain but will not pass
anything through. Output is mono, duplicated to both channels on a stereo chain.
`V/Oct` is the only signal inlet. No gate, trigger, or reseed input; Seed can only
be changed by the encoder or by CV on the `seed` ply.

---

## Flakes

*mnemonic: Fk* · Category: Experimental

A feedback looper and freeze effect on a ten-second buffer. Delay sets the loop
length from a millisecond up to the full ten seconds; Depth sets both how much
feedback there is and how dark and unstable the loop becomes; Warble wobbles the
delay time; Noise injects filtered noise that accumulates through the loop. A
self-modulation engine watches the loop's own level and, each time it crosses a
threshold, fires short random modulations of delay time, feedback and filtering,
so loud material makes the loop shimmer and drift on its own. Holding Freeze stops
writing to the buffer, locking whatever is in there for infinite repeats.

**Controls** (expanded order): `freeze`, `depth`, `delay`, `warble`, `noise`, `d/w`.

| Control | Type | Range / Options | Default | What it does |
|---|---|---|---|---|
| `Freeze` (`freeze`) | Gate | gate | off | While high, the buffer stops recording. Input and feedback no longer enter it, so the current loop repeats unchanged. This is a level gate, not a trigger. |
| `Depth` (`depth`) | GainBias | 0 … 1 | 0.5 | Master intensity. Sets feedback amount, darkens the feedback path (a lowpass sweeping from about 12 kHz down to 840 Hz), scales all the self-modulation, and lowers the threshold at which self-modulation fires. |
| `Delay` (`delay`) | GainBias | 0 … 1 | 0.25 | Loop length, mapped from 1 ms to the full buffer (about 10 s). The default sits near 2.5 s. |
| `Warble` (`warble`) | GainBias | 0 … 1 | 0.24 | One sine LFO on the delay time; sets both its rate (0.1-2 Hz) and its depth (up to about ±2% of the loop length). |
| `Noise` (`noise`) | GainBias | 0 … 1 | 0.1 | Amount of lowpassed white noise written into the buffer alongside the input. It builds up through the feedback path rather than sitting on top. |
| `Dry/Wet` (`d/w`) | GainBias | 0 … 1 | 0.5 | Linear crossfade between input and loop. Note the button reads `d/w`, the label reads `Dry/Wet`. |

**Sub-display / expanded**: none.

**Menu**: none.

**I/O**: mono or stereo; a second independent instance with its own 10 s buffer
handles the right channel, driven by the same controls and the same Freeze
comparator. `In1`/`In2` → `Out1`/`Out2`. No V/Oct; Freeze is a gate, not a trigger.

> **Known defect:** as shipped in v0.4.1 the unit's Lua asset requires
> `biome.libcatchall`, a module that does not exist. The package's SWIG module is
> `catchall_libcatchall`. Flakes is expected to fail when you try to insert it.
> This is a leftover from when Flakes lived in the `biome` package.

---

## Som

*mnemonic: Sm* · Category: Experimental

Som listens to whatever you feed it and continuously trains a 64-node
self-organizing map: a small neural network that arranges itself on a sphere so
that similar-sounding input lands on neighboring nodes. Each node holds six numbers
that *are* a patch: filter frequency, resonance, series/parallel topology, a filter
blend, a divider-staircase blend, and drive, all driving a pair of coupled
state-variable filters with a square-wave divider and a chaotic routing matrix that
gets wilder toward the sphere's equator. Scan walks a fixed tour of all 64 nodes and
morphs between the patches it finds. Plasticity decides how hard incoming audio
rewrites the map, Parallax lets it train one region while you listen to another, and
Feedback closes the loop so it trains on its own output and reorganizes indefinitely.

**Controls** (expanded order): `scan`, `plst`, `prlx`, `mod`, `fdbk`, `mix`.

| Control | Type | Range / Options | Default | What it does |
|---|---|---|---|---|
| `Scan` (`scan`) | custom GainBias with sphere graphic | 0 … 1 | 0.0 | Position along a fixed tour of all 64 map nodes; the six voice parameters are interpolated between adjacent nodes. The ply shows the sphere instead of a fader. Press the ply button again to swap the ply row into the `scan` sub-view (see below). |
| `Plasticity` (`plst`) | GainBias | 0 … 1 | 0.0 | How hard the incoming audio rewrites the map. At 0 training is off entirely and the map is a fixed instrument; raise it and the map deforms toward what it hears. |
| `Parallax` (`prlx`) | GainBias | −1 … 1 | 0.0 | Offsets the training write head from the listening head along the tour, so training lands somewhere other than where you are scanning. Only does anything with Plasticity above zero. |
| `Mod Amount` (`mod`) | custom GainBias | 0 … 1 | 0.0 | Depth of an internal LFO added to the scan position; at full depth it sweeps half the tour. Press the ply button again for the `mod` sub-view. |
| `Feedback` (`fdbk`) | GainBias | 0 … 1 | 0.0 | Blends the voice's own output into what the feature analyzer listens to. At 1 the map trains purely on itself. This is an analysis-path feedback, not an audio feedback loop through the filters. |
| `Mix` (`mix`) | GainBias | 0 … 1 | 0.5 | Dry/wet crossfade between the chain input and the voice. |

Reachable through the two context sub-views (press the button of an
already-focused `scan` or `mod` ply):

| Control | Type | Range / Options | Default | What it does |
|---|---|---|---|---|
| `Neighborhood Radius` (`nbr`) | GainBias | 0.05 … 0.5 | 0.06 | How far around the write node the training spreads. Small keeps learning local and detailed; large smears one sound across a whole region of the map. |
| `Learning Rate` (`rate`) | GainBias | 0.01 … 1 | 0.1 | How large each training step is. |
| `Decay` (`decay`) | GainBias | 0.9 … 1.0 | 0.995 | **Visual only.** How fast heavily-trained cells sink back into the sphere. It does not make the map forget what it learned. |
| `Mod Rate` (`rate`) | GainBias | 0.001 Hz … 20 Hz | 0.1 Hz | LFO frequency. |
| `Mod Shape` (`shape`) | GainBias | 0 … 1 | 0.0 | Continuous morph across nine LFO shapes: sine, triangle, ramp, square, sample-and-hold, dual sine, Hénon chaos, three-sine inharmonic, and a smoothed random walk. |
| `Mod Feedback` (`fb`) | GainBias | 0 … 0.95 | 0.0 | The LFO modulates its own rate. |

`Output Level` (`lvl`, 0 … 2, default 1.0) exists but is on **no view** in this
version; it is only reachable via the unit's control editor.

**Sub-display / expanded**: the `scan` ply's sub-display shows, by default, a
"SOM Scan" page with three readouts: `decay`, `nbr`, `rate` (the same parameters as
the context-view plies). The `mod` ply's sub-display shows a "Rate / Shape / FB"
page with `rate`, `shape`, `fb`. On both, tap SHIFT to swap back to the ordinary
gain/bias sub-display, and tap again to return. Sub-buttons 1/2/3 pick which
readout the encoder edits.

The sphere on the `scan` ply is the map itself: one Voronoi cell per node, tumbling
to keep the currently scanned node facing you, a halo over the neighborhood around
the scan head, and cells that have absorbed a lot of training lifting off the
surface as shards. Note that this graphic is computationally heavy and is known to
make the encoder feel sluggish while it is on screen.

**Menu**: none. Control positions are saved with the preset; **the learned map is
not**. It re-initializes to a neutral gradient every time the unit loads.

**I/O**: **Mono input only** (`In1`); a stereo chain's right input is ignored.
Output follows the chain: mono, or stereo from the two internal filter pairs (on a
mono chain both pairs collapse to one). Every control has a CV-assignable branch.
No V/Oct, no gate, no trigger, no clock input; the LFO free-runs.

---

## Alembic

*mnemonic: Al* · Category: Experimental

A four-operator phase-modulation voice whose entire patch is trained from a sample.
Load a wav from the unit menu and Alembic analyzes it, picks 64 maximally-distinct
frames, and writes each one out as a complete synth state: operator ratios, levels
and detunes, a full 4×4 modulation matrix, a wavefolding transfer curve, a dual
filter pair with an audio-rate routing matrix, a multi-tap comb, and a read pointer
back into the sample itself. Instead of dialling operators, you scan that chain of
64 states. Reagent scans a second, independent position for the wavefolding curve,
Comb is a one-axis wet/position fader over a Pecto-style comb, and Ferment scales
all the chaos-bearing terms from clean and tonal up through as-trained to
over-driven. With no sample loaded it falls back to a built-in placeholder so it
still makes sound.

**Controls** (expanded order): `V/oct`, `f0`, `sync`, `scan`, `reagent`, `comb`, `ferment`, `level`.

| Control | Type | Range / Options | Default | What it does |
|---|---|---|---|---|
| `V/oct` (`V/oct`) | Pitch | standard pitch control | 0 | Exponential pitch tracking; multiplies all four operators together. |
| `Fundamental` (`f0`) | GainBias | Hz, `oscFreq` map | 27.5 Hz | Base frequency. Each operator runs at its trained ratio and detune off this. |
| `Sync` (`sync`) | Gate (trigger mode) | trigger | n/a | A rising edge hard-resets all four operator phases to zero. |
| `Scan Position` (`scan`) | custom GainBias with sphere graphic | 0 … 1 | 0.0 | Walks the 64 trained states. Ratios, levels, detunes, the modulation matrix, filters and the sample read pointer are blended smoothly across neighboring nodes; the routing topology instead hard-cuts at node boundaries, which is the source of the deliberate lurches and clicks as you sweep. |
| `Reagent Scan` (`reagent`) | custom GainBias | 0 … 1 | 0.0 | A second, independent scan position that selects only the wavefolding transfer curve, so you can borrow one part of the sample's character without moving the tonal state. |
| `Comb` (`comb`) | GainBias | 0 … 1 | 0.0 | One fader collapsing wet amount and comb position together. At 0 the comb is bypassed; raising it both fades it in and sweeps its density, pitch, feedback, pattern and resonator type. |
| `Ferment` (`ferment`) | GainBias | 0 … 1 on the dial (CV can reach 1.5) | 1.0 | Chaos scaling. Multiplies the whole modulation matrix and every routing depth. At 0 the routing collapses entirely and the voice is clean and tonal; 1.0 is exactly as trained; CV above 1 drives it past the trained point. |
| `Level` (`level`) | GainBias | −1 … 1 | 0.5 | Final output gain, before a soft limiter. |

**Sub-display / expanded**: two plies have a shift page. With the cursor on the
ply, tap SHIFT (press and release without turning the encoder) to toggle it, tap
again or leave the ply to exit.

- `scan` → sub-button `K` (*Path window K*, 2 … 6, integer, default 4) sets how many
  neighboring trained states are blended together at once: low is a sharp,
  state-to-state read of the sample, high is a smeared average. Sub-button `dpth`
  (*Sample pointer depth*, 0 … 1, default 0.5) scales how much of the raw sample
  read pointer is injected into the voice as an excitation signal.
- `reagent` → sub-button `amt` (*Amount*, 0 … 1, default 0.0) is the crossfade into
  the wavefolded signal. At the default of 0 the wavefolder is inaudible, so
  Reagent Scan does nothing until you raise this.

On both pages, SHIFT + a sub-button opens a numeric keyboard for that value.

The sphere on the `scan` ply is the trained map: 64 Voronoi cells, one per state,
with the camera slewing to face whichever state you are scanning and the cells
inside the blend window glowing.

**Menu**: a **Sample Menu**, holding `Select from Card`, `Select from Pool`,
`Detach Buffer`, `Edit Buffer`. The sub-display shows the attached sample's name,
duration, channel count, rate and memory size, or "No sample attached." Attaching a
sample triggers an analysis pass (roughly 0.2-1.2 s) before the new map is live;
detaching reverts to the placeholder map.

**I/O**: a **mono voice with no audio input**. Signal inlets are `V/Oct` and
`Sync`; the single output is duplicated to both channels on a stereo chain (no
stereo image). Every control has a CV-assignable branch. There is **no envelope and
no note gate**: Alembic is a free-running drone source, so gate it externally if
you want notes. Control positions and the attached sample are saved with the preset.

---

<!-- VERIFICATION NOTES

README.md / release-note discrepancies:

- README.md:120-122 lists only Sfera, Lambda and Flakes for catchall. **Som and
  Alembic are entirely absent from README.md** (confirmed by grep: zero hits for
  either name). Neither has ever been announced in a RELEASE-2.*.md either; the
  only Alembic mentions are two internal notes in RELEASE-2.5.0.md / 2.5.1.md
  ("AlembicSphereGraphic refactor", "Alembic Phase 6 serialization still
  deferred"; the latter is now stale, serialization is implemented), and Som has
  never appeared in any release note.
- README.md:120 says Sfera has "32 configs". The Config selector exposes **128**
  (kMaxCubes = 128): 32 hand-authored + 96 procedurally generated. Documented as
  128 per source.
- README.md:122 (and the toc keywords) call Flakes "granular". There is no grain
  engine: no windowing, no grain scheduler. It is a single interpolated delay tap
  with feedback. Documented as a feedback looper/freeze.

Code-level findings surfaced in the doc (all verified in source):

- Sfera `Spin` is audio-inert: mSpin is never read in Sfera::process(), only by the
  graphic. Documented as visual-only.
- Sfera wires a V/Oct inlet and a "tune" mono branch but declares no Pitch
  ViewControl, so V/Oct cutoff tracking is unreachable from the UI.
- Sfera's qScale branch is likewise created by addMonoBranch but has no
  ViewControl, so Q Scale is knob-only via the Cutoff shift page.
- Flakes.lua:2 is `require "biome.libcatchall"`. Verified broken: biome's SWIG
  module is `biome_libbiome` and no libcatchall exists there; catchall's own is
  `catchall_libcatchall`, which every other asset in the package uses. This is
  load-blocking and is called out inline.
- Som `Decay` decays only the visualization's richness accumulator; weight
  forgetting is a separate hard-coded gravity term. Som `Output Level` is defined
  but present in no view list. Som's learned weights are not serialized. Som's
  input is mono-only despite stereo output.
- Alembic `Ferment`'s dial map is [0,1] while the C++ clamp allows up to 1.5, so
  only CV reaches the top of the range. `Level`'s map is [-1,1] with default 0.5.

Prose-vs-code contradictions in planning docs (planning docs are design, not
as-built; do not use them as reference):

- planning/som-stuber.md differs from shipped Som on divider width (3-bit vs the
  shipped 6-bit), the six node dimensions, mux dimensions, weight serialization,
  stereo input, and visualizer roll drift.
- planning/todo-archive.md still describes Flakes as living in the biome package
  and lists Lambda goals (reseed input, separate VCA, spreadsheet package) that
  were never implemented.
- planning/alembic-phase-8.md notes Phase 7 (per-region user-bias sub-params) is
  deferred; the eight plies documented here are the shipped v1 surface.

Could not verify:
- Sfera's 32 config-family names are read from the Lua label table and matched
  one-for-one against kCubes in SferaConfigs.h; the mapping was checked by index
  count, not by auditing every filter family's coefficients.
- Alembic's sample-analysis timing figure (~0.2-1.2 s) is taken from a source
  comment, not measured.
-->
