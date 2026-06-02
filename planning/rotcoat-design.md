# RotCoat — original-design reverb concept

Status: design exploration. No code. Concept distilled from
`planning/refs/airwindows-port-handoff.md` §3.2. References
`planning/reverb-design-philosophy.md` for the combination
mechanics this engine recombines.

## What it is

Fusion of two AW mechanics:

1. **Quantized-divisor verb** (CreamCoat taken all the way) —
   the entire reverb core runs at a reduced internal rate,
   reconstructed via Bezier at the host-rate boundary.
2. **Tape-rot per delay line** (ChromeOxide mechanic applied
   inside each FDN line) — per-line band-split where the low
   band gets head-bump saturation and the high band gets
   noise-FM warble + bias delay.

The two interact at the reconstruction boundary in a way neither
does alone. The handoff calls this **"the richest emergent
corner explored"**.

> **Per the handoff §4.2**: RotCoat is **the AM335x-friendly
> design** — structurally, not incidentally. The reduced-rate
> domain cuts buffer length AND read rate by the divisor
> simultaneously, attacking the exact memory bottleneck that
> dominates AM335x reverb cost.

## Macro topology

```
in
  → Predelay
  → [ reduced-rate domain (clocked by World):
        Decimate (host → reduced)
      → Householder FDN core (with Regen feedback)
      → Bezier Reconstruct (reduced → host) ]
  → Output (wet/dry mix)
```

A dry path bypasses the reduced-rate domain entirely. The Bezier
reconstruction is the boundary that interpolates back to host
rate — serves **double duty as both character and cost
reduction** (the CreamCoat insight).

## Per-delay-line tape-rot (inside each FDN line)

Each FDN line's tap output gets band-split processing:

```
tap_out
  → band-split (LP / HP, crossover at e.g. 1.5–3 kHz)
  → low band  → head-bump saturation
  → high band → noise-FM warble + bias delay (depth = Mulch)
  → recombine → into the Householder matrix
```

A **band-recirc flip** selects which band the matrix feedback
carries forward (lows-feedback vs highs-feedback). Two different
tail personalities from the same DSP.

## Parameters

| Knob | Range | What it does |
|---|---|---|
| **World** | Stepped: ÷1, ÷2, ÷3, ÷4, ÷6, ÷8 | Discontinuous headline knob. Internal-rate divisor. **Not a size sweep — a switch between quantized worlds**, each a distinct clean room with its own characteristic Bezier cutoff / ceiling. |
| **Regen** | 0..1 | Feedback / decay. |
| **Predelay** | 0..(host buffer max) | Pre-delay. |
| **Mulch** | 0..1 | Tape-rot depth (high-band noise-FM depth + bias). |
| **Lock / Drift** | Toggle | Lock = hold a world (rack of pristine quantized rooms). Drift = slew the divisor between worlds → **CrunchCoat-style pitch-swoop glitch instrument.** |
| **band-recirc flip** | Toggle (lows / highs) | Tail personality (see below). |

Six controls total. World is the headline (steppy on purpose;
each stop is a different world). Drift is the wild-card.

## Emergent payoff (the boundary interaction)

At low World divisors the Bezier reconstruction is already
drawing coarse, near-straight segments between sparse points;
the high band's noise-FM warble modulates a delay read *against
that coarse grid*, so reconstruction quantization and warble
swim **intermodulate**. Each divisor rots with a different
flavor:

- **÷2** = subtle tape haze
- **÷3 / ÷4** = audible color, room feels bleached
- **÷6 / ÷8** = full cursed-cassette wow where warble and
  reconstruction stairsteps beat against each other

## Tail personality via band-recirc flip

- **Recirculate the lows** → saturated low-mid bloom that
  thickens as it decays.
- **Recirculate the highs** → swimming, detuning top band
  persists → tail wanders pitchward.

Same DSP, two characters. Cheap macro.

## Why it's cheap

**Cheaper than the FDN alone at host rate.** The reduced-rate
domain cuts per-sample loop cost AND buffer length by the
divisor simultaneously. Warble is just a modulated buffer read
(no extra filtering). Band-split crossover is pure FLOPs.

At ÷4: hold ¼ the samples for the same decay AND touch them ¼
as often → ~16× memory-traffic reduction in the FDN core. The
band-split crossover and warble re-read are paid at the reduced
rate too.

**This is the AM335x-friendly design by construction.** ÷2 and
below is comfortable; ÷1 (unity) is the tight corner at full
freight — **honest constraint: ÷1 is a budget decision, not a
free sound.**

## Watch items (per handoff §3.2)

1. **Clamp warble depth + bias** so the modulated high-band read
   can never go negative — indexing behind the write pointer
   produces a click. Same bug shape on AM335x as on Pi 4, just
   less headroom to mask it.
2. **Denormal floor in the feedback path** — low divisors mean
   fewer samples flush the lines between hits, so denormals
   accumulate faster than at host rate. Already a Cabinet
   discipline, applies everywhere here.

## AM335x verdict (per handoff §4.2)

> RotCoat is the AM335x-friendly design (structurally, not
> incidentally). [...] ÷2 and below is comfortable; ÷1 (unity)
> is the tight corner at full freight. Tape-rot cuts slightly
> the other way (warble read + bias delay = more taps/line),
> but paid at the reduced rate; band-split crossover is cheap
> FLOPs. **Net: fits, and World is the headroom knob.**

This is the cleanest fit-by-design among the originals. XYZ's
Coupled regime has to *inherit* the reduced-rate trick or it
overruns budget; RotCoat is built on the trick natively.

## How this maps to the combination mechanics

(References `planning/reverb-design-philosophy.md` mechanic
numbers.)

- Whole macro topology: mechanic 2 (undersample/Bezier as
  character axis), taken to its extreme. World stops at ÷1 are
  the lush regime, ÷6–8 are the cursed regime.
- Per-line tape-rot: not one of the catalogued mechanics — it's
  *inside* a single line, not a topology recombination. It's a
  per-line character stage.
- Band-recirc flip: lightweight version of mechanic 4 (shared
  buffer, two read strategies — same FDN feeds back through one
  band or the other).
- Lock / Drift toggle on World: the "scope of variability"
  switch — Lock = preset-discoverable, Drift = performance
  instrument. Not strictly a DSP mechanic; it's a UX move
  worth lifting elsewhere.

## Open implementation questions

1. **FDN size** — handoff doesn't specify. 4×4 keeps per-sample
   cost low and matches CloudCoat / Chamber lineage; 6×6 matches
   Cabinet. Recommend starting 4×4 for the lower per-sample
   cost (since World is already the headroom knob, the FDN
   should be lean too).
2. **Crossover frequency** — fixed (e.g. 1.8 kHz) or
   user-controlled? Fixed is simpler and matches AW style;
   tunable would add an axis but risks Mulch-shape parameter
   redundancy.
3. **World as discrete stops vs continuous-with-snap**? Handoff
   says stepped. Stepped feels right for the "switch between
   worlds" framing, but Drift mode wants continuous interpolation
   of the divisor — needs to think about how those two coexist
   in the parameter surface.
4. **Drift rate** — when in Drift mode, how fast does the
   divisor slew? Constant rate or modulatable? Probably gets
   its own sub-control under World.
5. **Output stage** — handoff says "wet/dry mix" at the output.
   Standard. No saturation post-mix.
6. **Habitat-native name** — RotCoat is the design codename
   (continues AW's *Coat naming). Candidates for unit name:
   **Lath** (wooden lath = thin slat, room-construction; goes
   with House package theme), **Cure** (the chemical sense of
   wood curing → tape rot), **Sediment**, **Patina**, or your
   preference. Defer naming until aesthetic is validated.

## Implementation phases (sketch — not committed)

Downstream of the active AW port pipeline. Sketch only:

- **Phase A**: prototype the reduced-rate FDN core at one fixed
  World setting (e.g. ÷4). No tape-rot, no band-recirc. Validate
  the divisor + Bezier reconstruct math against Cabinet's
  proven outer Bezier loop. Get the CPU profile on hardware.
- **Phase B**: add per-line tape-rot (band-split → head-bump on
  low / noise-FM warble on high → recombine). Listen for the
  emergent quantization-warble intermodulation.
- **Phase C**: wire World as a stepped selector + band-recirc
  flip. Six fixed worlds (÷1, ÷2, ÷3, ÷4, ÷6, ÷8).
- **Phase D**: add Drift mode (slew divisor between worlds).
  This is the wildcard — UX needs care so it doesn't disrupt
  the Lock-mode workflow.
- **Phase E**: parameter polish, naming, package home (`house`),
  release.

## Where this fits in the pipeline

- Active: Cabinet (kWoodRoom) port. Phase 0 (Smoketest harness)
  on hardware now.
- Next: WoodenBox → CreamCoat → BrightAmbience3 → Galactic →
  Verbity (per `planning/airwindows-reverb-research.md`
  addendum).
- After the AW pipeline is established, RotCoat is the
  **stronger pick** between the two originals for first
  implementation (AM335x-friendly by construction). XYZ engine
  is the more cryptic / signature payoff and comes after.

## Source

`planning/refs/airwindows-port-handoff.md` §3.2 + §4.2.
