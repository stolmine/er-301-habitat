# Complex Oscillator (working title TBD)

2-operator FM oscillator inspired by YMF262 (OPL3) architecture. Carrier + modulator with OPL3-derived waveforms, discontinuity wavefolder, and optional lo-fi character.

## Signal Flow

```
Modulator (OPL3 waveform set) → FM → Carrier (primary shape) → Discontinuity Folder → Output
         ↑___feedback___|
```

## Plies

1. **V/Oct** — pitch input
2. **Fundamental** — base frequency
3. **Overview** (phase space viz, rotating like Rauschen)
   - Modulator mix (FM wet/dry)
   - Lin/Expo FM switch
   - Primary carrier shape
4. **Shaping** (live transfer function viz, perturbed by modulator state)
   - Mod index (FM depth)
   - Discontinuity index (folder depth)
   - Discontinuity type (folder shape — morphs through OPL3-derived transfer functions)
5. **Modulator**
   - Ratio
   - Feedback (modulator self-feedback depth)
   - Shape (hard-switched through OPL3 wavetable: sine, half-sine, abs-sine, quarter-sine, alternating-sine, camel-sine, square, log-saw)
   - Fine (expansion-only — set-once detune, no fader slot needed)
6. **Sync** — hard sync input
7. **Level** — output level

## OPL3 Waveform Set

Used for both modulator shape (hard-switched) and as the vocabulary for the discontinuity folder's transfer functions (morphable on carrier).

| # | Name | Operation on sine |
|---|------|-------------------|
| 0 | Sine | identity |
| 1 | Half-sine | zero negative half |
| 2 | Abs-sine | full-wave rectify |
| 3 | Quarter-sine | zero 2nd and 4th quarters |
| 4 | Alternating sine | zero every other period |
| 5 | Camel sine | abs-sine, zero every other period |
| 6 | Square | sign only |
| 7 | Log saw | derived ramp |

## Discontinuity Folder

Transfer function applied to carrier output, independent of FM. Shape vocabulary derived from OPL3 operations (rectify, zero, abs, sign). Discontinuity type morphs through these transfer functions, discontinuity index controls depth (wet/dry). Shaping ply viz shows the live transfer function.

## Lo-Fi Character (Config Menu)

- **OPL bit-depth mode** — quantizes internal oscillator phase/amplitude tables to approximate YMF262's 10-bit phase, log-encoded amplitude
- **Alias mode** — naive phase accumulator (no anti-aliasing). The aliasing foldback IS the OPL sound.
- **Mod shape switching** — hard switch (default, faithful) vs smooth morph through OPL wavetable

## Feedback

Modulator self-feedback — modulator output feeds back into its own phase. Turns sine modulator from warm sine to harsh saw-like wave. Key part of OPL3 character.

Lives on modulator ply (ratio, feedback, shape). Fine tuning is expansion-only.

## Visualizations

Each ply with sub-params gets a distinct visual language.

### Overview Ply — Circular Waveform / 3D Phase Plot
Two options under consideration:
- **3D rotating phase plot** — similar to Rauschen's phase space viz, showing carrier output trajectory in 3D (x[n], x[n-1], x[n-2]) with rotation and phosphor decay.
- **Circular waveform** — single cycle of output waveform wrapped around a circle, viewed at isometric angle. Updates live either as a continuous rotation or a stationary ring with a wipe-around redraw of the polyline. Gives an organic, oscilloscope-rose quality that changes shape dramatically with FM depth and carrier shape.

### Shaping Ply — Transfer Function Curve
X-axis = input (carrier signal), Y-axis = output (post-folder). At zero discontinuity: straight diagonal. As discontinuity index increases, OPL3 operations visibly deform the curve — rectification flattens the bottom, abs folds it up, zeroing cuts sections. A live dot traces the carrier's current position on the curve, showing where in the transfer function the signal sits at any moment. Most informative viz — you watch the folder work in real-time.

### Modulator Ply — Harmonic Ratio Orbital
Circular X/Y display: X = carrier phase, Y = modulator output. Ratio determines the orbital pattern — simple ratios (1:1, 2:1) produce stable geometric figures (circle, figure-8), complex ratios produce dense Lissajous-like webs. Feedback visibly distorts the orbit from smooth to jagged. Shape changes the waveform drawn along the path (sine = smooth, square = angular, etc.). Shows the FM relationship as a living geometric pattern.

## Open Questions
- Carrier primary shape — same OPL3 set, or a different/wider selection?
- Phase space viz: carrier-only or carrier+modulator interaction?
- Name
