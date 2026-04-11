# Automata Sequencer (Chess)

Grid-based generative sequencer where game-like piece interactions drive musical events. Adversarial — player spawns pieces from one side, AI opponent from the other. Collisions and interactions between pieces generate gates, CV, and parameter changes across 4 networked voices.

Aesthetic reference: clicks + cuts (Mille Plateaux). Fragmented micro-events with internal logic. Everything networked — no voice is independent, every collision ripples through the system.

## Board

4 rows x 32 cells. 128 total cells.

- Pieces enter from edges: player from left, opponent from right
- Display: 128px wide / 32 cells = 4px per cell, rows 6-8px tall
- Pieces as bright pixels or 2x2 blocks, collisions as flashes, movement as fading trails
- Edge behavior (global mode): wrap / bounce / die

## Voices

4 voices, one per row. Stereo summed output with per-voice pan. Voices are coupled — events on one voice affect others based on coupling parameter.

Synthesis model TBD: micro-sample player (very Mille Plateaux), noise burst + resonant filter (percussive/abstract), or pure CV/gate output for external patching.

## Piece Types

| Piece | Movement | Musical role |
|---|---|---|
| **Pawn** | forward only, 1 cell/clock | steady pulse, predictable |
| **Rook** | straight lines, N cells/clock | rhythmic bursts across a row |
| **Bishop** | diagonal, crosses rows | creates inter-voice connections |
| **Knight** | L-shape, jumps | syncopation, unexpected placement |
| **Assassin** | invisible until adjacent, then strikes | silence then sudden event |
| **Bomber** | explodes on collision, area effect | cascade trigger, splash to neighbors |

### Meta-param per type

- Pawn: stubbornness (capture resistance, collision energy)
- Rook: speed (cells per clock tick)
- Bishop: angle steepness (row-crossing rate)
- Knight: reach (L-shape size)
- Assassin: patience (ticks before revealing)
- Bomber: blast radius

## Interaction Model

The network quality is central. Collisions are primary events, but every collision sends ripples. Nothing happens in isolation.

### Collision Events

| Event | Trigger | Effect |
|---|---|---|
| **Collision** | same cell | both pieces interact, primary gate event |
| **Capture** | collision + one piece stronger | winner absorbs loser's energy/params |
| **Elimination** | collision + equal strength | both destroyed, double gate, energy disperses to neighbors |
| **Contamination** | collision + survival | pieces exchange partial params (pitch bleeds, timbre shifts) |
| **Betrayal** | piece flips allegiance on capture | voice assignment changes, parameter inversion |
| **En passant** | pieces pass through same cell on same tick | ghost trigger — quieter event, brief parameter blip |
| **Ranged fire** | line of sight, no obstruction | CV modulation at distance, intensity = 1/distance |
| **Dodge** | target piece has high agility | near-miss, tension accumulates instead of resolving |
| **Cross-row capture** | bishop/knight lands on occupied cell in different row | inter-voice gate, collision sounds on BOTH voices |

### Network Effects

- **Energy transfer** — collision energy propagates to adjacent cells, can trigger secondary collisions, cascading into flurries across all 4 voices then decaying back to sparse
- **Parameter contamination** — interacting pieces exchange properties. Over time the board develops timbral drift neither player programmed. Voices become correlated in emergent ways
- **Tension accumulation** — near-misses and proximity build tension per voice. Threshold crossing triggers burst of activity, parameter jump, timbre shift. Long avoidance produces sudden violent changes
- **Decay/entropy** — pieces lose energy over time, slow down, die. Without new spawns the system winds down. Spawn rate = macro density control

### Voice Coupling

Voices are networked, not independent:
- Collision on voice 1 can steal amplitude from voice 3
- Capture on voice 2 shifts pitch of voice 4
- Proximity between voices 1 and 2 crossfades their timbres
- Single "coupling" parameter: isolated (4 independent clicks) to entangled (everything affects everything)

## UI Layout

### Spawner Sheet (player control)

Two-level spreadsheet. Top level: 4 spawners as rows, sub-display shows 3 params. Enter on a spawner expands to full detail view with 12 GainBias faders, all CV-controllable.

**Top level sub-display (per spawner):**

| Column | Parameter | Notes |
|---|---|---|
| Type | piece type | pawn/rook/bishop/knight/assassin/bomber |
| Division | spawn rate | clock divisions (1/1, 1/2, 1/4, 1/8...) |
| Meta | character | meaning depends on type |

**Expanded detail view (enter on spawner):**

| Fader | Notes |
|---|---|
| Type | GainBias, CV-controllable piece selection |
| Division | GainBias, CV-controllable spawn rate |
| Meta | GainBias, CV-controllable character |
| Energy | initial HP/strength of spawned pieces |
| Pitch range | octave range for this voice |
| Timbre | base timbre for this voice's events |
| Pan | stereo placement |
| Coupling | how much this voice affects/is affected by others |
| Aggression | AI tendency for this row's opponent pieces |
| Density cap | max pieces alive on this row |
| Decay | how fast pieces lose energy over time |
| Sensitivity | how easily this row's pieces dodge |

### Opponent Control

v1: single global opponent personality (aggression/density/variety). Could expand to a second spawner sheet (8 spawners total) later.

### Overview Ply

Board visualization: piece positions, movement trails, collision flashes. Header with per-voice activity indicators.

## Timing

At 120bpm, 1/16 division = ~125ms per clock tick.
- Pawn crosses full board: 32 ticks = 4 seconds
- Rook at speed 4: 8 ticks = 1 second
- Events every few hundred ms with bursts on collisions

Density cap of 6-8 pieces per row = 24-32 active pieces at saturation. Constant interaction without becoming noise.

## Game Inspirations

- **Chess** — piece movement archetypes, capture mechanics
- **Mancala** — sowing/scattering energy on capture
- **Othello** — flanking flips allegiance (betrayal mechanic)
- **Stratego** — hidden information, piece rank revealed on collision
- **Conway's Game of Life** — cellular automata, neighbor-count rules
- **Folktek Matter / Plumbutter** — networked nodes, freaky sequencing
- **Orca** — grid-based generative sequencing
