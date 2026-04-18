# SOM + Stuber Voice: Full Architecture

## Voice Engine: Stuber-Inspired Coupled Filter Network

### Signal Flow

```
Audio In ──┬──> Filter A (TPT SVF) ──> Squarewaveifier A ──> Binary Divider A ──> Staircase DAC A
           │         ▲  ▲                                                              │
           │         │  │  ┌─── Sh'mance Mux A (8 routing slots) ◄────────────────────┘
           │         │  │  │         selects source→dest per divider state
           │         │  └──┘
           │         │
           │    [cross-coupling paths via mux routing]
           │         │
           │         ▼
           └──> Filter B (TPT SVF) ──> Squarewaveifier B ──> Binary Divider B ──> Staircase DAC B
                      ▲  ▲                                                              │
                      │  │  ┌─── Sh'mance Mux B (8 routing slots) ◄────────────────────┘
                      │  │  │
                      │  └──┘
                      │
                 [output mix] ──> SOM feedback path ──> SOM input blend
```

### Filter Implementation

2x TPT (topology-preserving transform) SVFs using Cytomic/Andrew Simper formulation. Stable at self-oscillation — produces pure sine at Q=infinity, matches analog OTA SVF behavior. Do NOT use Chamberlin/Huovilainen — they diverge near self-oscillation.

Each filter outputs LP, BP, HP simultaneously. BP feeds the squarewaveifier.

### Binary Divider Chain

Per filter: comparator on BP output → 3-stage T flip-flop cascade (/2, /4, /8). Produces 3 square wave sub-octaves. Staircase DAC sums weighted bits: `staircase = (bit2 * 4 + bit1 * 2 + bit0) / 7.0`.

Divider clock source: zero crossings of the squarewaveifier output. Digital implementation needs 1-2 sample jitter in the feedback path to prevent perfect periodic locking.

### Sh'mance Multiplexer

Per filter pair: the 3-bit divider state selects among 8 routing configurations. Each routing config has 8 `(source, destination, attenuation)` triples.

**Sources (~12):**
- Filter A: LP, BP, HP
- Filter B: LP, BP, HP  
- Divider A: /2, /4, /8, staircase
- Divider B: /2, /4, /8, staircase
- Input audio

**Destinations (~8):**
- Filter A freq, Filter A Q
- Filter B freq, Filter B Q
- Coupling A→B depth, Coupling B→A depth
- Divider A rate bias, Divider B rate bias

### 64 Routing States

One unique routing table per SOM node (64 total). Generated procedurally from Fibonacci sphere coordinates:

- Polar nodes (near poles of sphere): sparse, clean routings — few active paths, low attenuation. Predictable, resonant behavior.
- Equatorial nodes (middle band): dense, chaotic routings — many active paths, higher attenuation variance. Edge-of-chaos behavior.

Geography of the sphere = geography of circuit complexity.

Memory: 64 states × 8 triples × 3 floats = 1536 floats = 6 KB.

### 6 SOM Dimensions → Voice Parameters

1. Filter A frequency
2. Filter B frequency
3. Resonance (shared Q, near self-oscillation sweet spot)
4. Cross-coupling depth (master scaling on all coupling paths)
5. Divider rate bias (shifts active divider stages)
6. Input/self-oscillation balance

### Global Feedback

Voice output → mixer → SOM input. Feedback control (0-1) blends external audio with voice output as the SOM's training source.

- Feedback 0: SOM learns from external audio only
- Feedback 0.5: half external, half self-referential
- Feedback 1: fully self-organizing — map learns from its own output, reorganizes, shifts voice params, changes output, learns again

### Anti-Periodicity Jitter

Digital dividers lock into perfect cycles. Countermeasures:
- 1-2 sample random delay in squarewaveifier→divider path
- Prime-offset the two channels' divider clocks (e.g., channel B delayed by 3 samples)
- Small Perlin noise on filter cutoff coefficients (~0.1% deviation) for temperature-drift character

### CPU Estimate

| Component | Cost |
|-----------|------|
| 2x TPT SVF | ~20 FLOPs/sample × 2 = 40 |
| 2x comparator + 6 flip-flops | negligible |
| 2x staircase DAC | negligible |
| Mux routing lookup + apply | ~16 muls/sample |
| SOM (existing) | ~5 MFLOPS total |

Voice adds ~56 FLOPs/sample = 2.7 MFLOPS at 48kHz. Total with SOM: ~8 MFLOPS. Well within budget even without NEON.

## User Interface

### Plies

1. **Scan** — icosphere viz (see below), scan position fader. Expansion: neighborhood, learning rate.
2. **Plasticity** — learning amount fader.
3. **Parallax** — read/write head offset.
4. **Mod** — internal LFO amount. Shift sub-display: rate, shape, feedback.
5. **Feedback** — global voice→SOM feedback amount.
6. **Mix** — dry/wet.

### Serialization

- All parameter adapter biases
- 384 weight floats (64×6) for trained map state
- Snapshot buffers: too large (32KB) — could optionally skip, map retrains from feedback

## Icosphere Visualization (Scan Ply)

### Voronoi Cells

Instead of dots, render filled Voronoi cells on the projected sphere surface. Each cell corresponds to one SOM node.

**Cell brightness:** Gradient mapped from training data richness — aggregate the node's weight vector variance across dimensions. Nodes trained on diverse material glow brighter. Untrained/decayed nodes are dark.

**Voronoi computation:** For each pixel in the viewport, find the nearest projected node (2D distance after projection). That pixel belongs to that node's cell. Color by the node's brightness. This naturally produces Voronoi tessellation on the projected sphere.

### 3D Cell Lifting

When scan/mod focuses a node, it lifts off the sphere surface. The lift amount is proportional to proximity to the scan position on the chain.

```
lift[n] = max(0, 1 - chainDistance(n, scanNode) / neighborhoodRadius)
projectedRadius[n] = baseRadius + lift[n] * liftAmount
```

Neighborhood control scales the falloff — wide neighborhood lifts many cells gently, tight neighborhood lifts only the focus node sharply. Nearby cells lift less, creating a terrain bump that follows the scan head.

The lift affects the Z coordinate before projection, so lifted cells appear larger (closer to viewer) and brighter (depth shading).

### Rotation: Scan-Following Tumble

Rotation follows aggregate scan position so the active region is always visible to the user.

**Target rotation:** Compute the 3D sphere coordinates of the current scan node. The target rotation orients that node toward the viewer (front-center of projection).

**Slew with integrator character (Cold Mac style):**
- Small scan jumps → slow, long rotation walks (integrator accumulates momentum)
- Large scan jumps → proportionally snappier response

Implementation:
```
// Compute target rotation to face scan node toward viewer
targetRotX = asin(scanNodeY);  // tilt to bring node to vertical center
targetRotY = atan2(scanNodeX, scanNodeZ);  // spin to bring node to horizontal center

// Integrator-style slew: rate proportional to distance
float distX = targetRotX - currentRotX;
float distY = targetRotY - currentRotY;
float dist = sqrt(distX*distX + distY*distY);

// Large distance = faster slew, small distance = slower (momentum)
float slewRate = 0.02 + dist * 0.15;
currentRotX += distX * slewRate;
currentRotY += distY * slewRate;
```

This produces tumbling behavior: the sphere lazily rolls to show the active region, with inertia that makes small movements feel organic and large jumps feel responsive.

**Third axis:** Add a slow constant drift on the Z rotation (roll) so the sphere never settles into a static orientation. Rate: ~0.003 rad/frame. This prevents the viz from looking "locked" even when scan is stationary.

### Depth Shading

Front of sphere bright (WHITE), back dim (GRAY3). Use post-rotation Z for screen-fixed lighting (same convention as Helicase orbital, which works correctly).

### Frame Caching

Voronoi per-pixel nearest-neighbor search across 64 nodes is expensive. Cache the cell assignment map — only recompute when rotation changes significantly (threshold on rotation delta). The brightness/lift values update every frame (cheap — just 64 lookups), but the Voronoi tessellation itself is cached.

## Open Questions

- Name for this unit (Som is placeholder)
- Should weight vectors be serialized? 384 floats is fine, but snapshot buffers (32KB) might be too much for quicksave
- Should the routing tables be fully procedural, or should there be a "seed" parameter that generates different instruments?
- Oversampling: 2x or 4x on the SVF stages? 2x is probably sufficient given we're not doing precise pitch tracking
