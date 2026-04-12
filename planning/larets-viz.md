# Larets Overview Visualization

## Concept

Replace bar graph with metaball-based step visualization. Each step is a small metaball arranged in a row. A larger metaball slides into position over the active step, merging with it. The large ball's surface, lighting, and shell are perturbed differently per effect type.

Step ticks removed from step list (already represented by overview). Step list shows: step number, effect type abbreviation.

## Layout

Small metaballs in a row, evenly spaced or proportional to tick count. Large active metaball slides between positions on step transitions. Merging/separation gives visual step transition for free.

Small balls: subtle undulation via noise LUT, dimmed when inactive. Active small ball consumed by large ball overlay.

## Per-Effect Visual Character (Large Metaball)

| Effect | Visual |
|--------|--------|
| Off | Calm, minimal surface movement |
| Stutter | Large ball divides into smaller ones or particles |
| Reverse | Ball fades into darkness with a gradient |
| Bitcrush | Faceted/angular surface, low-poly metaball |
| Downsample | Regress to wireframe representation using precomputed noise LUT for uneven fade — higher param = more wireframe until fully framed at max |
| Filter | Edges blur for LPF, sharpen for HPF |
| Pitch shift | Brightness sucked from center leaving only edge ring at low pitch, radiates halo at high pitch |
| Tape stop | Gradually flattening/deflating |
| Gate | Opacity pulsing |
| Distortion | Jagged/spiky surface perturbation |
| Delay | Ghosted afterimage trailing behind |
| Comb | Concentric rings/ripples on surface |

## Implementation Phases

1. Basic layout: small circles per step, active step highlighted, tick count removed from step list
2. Metaball rendering: implicit surface with threshold, distance field per ball
3. Large ball sliding between positions with merge animation
4. Per-effect surface perturbation (start with 2-3 effects, iterate)
5. Noise LUT for surface texture (reuse Raindrop/Sfera patterns)
6. Voronoi shell option for some effects

## References

- Rauschen PhaseSpaceGraphic: 3D rotating phase space with depth shading
- Sfera: ferrofluid metaball with pole protrusions, Wyvill kernel
- Helicase HelicasePhaseGraphic: k-means clustered metaballs, Voronoi edges
- RaindropGraphic: precomputed Perlin noise LUT, bilinear sampling
