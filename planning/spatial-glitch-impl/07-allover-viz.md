# 07 — All-over viz: "Pond of Recollection" (Phase 5b) — DESIGN

Status: **concept LOCKED 2026-06-27; model REVISED 2026-06-27 (all-effects-global).**
Supersedes the per-ply viz idea in `06-ui-surface.md`. Implementation detail +
tuning constants live in **`08-viz-implementation.md`**; this doc is the *what/why*.

## The concept
One continuous **streamline flow-field** — a pond / sea of memory — painted across
Anamnesis's whole main-display ply strip (256×64). It reads left→right as signal
flow. Bold black/white line-art (refs `~/Downloads/vizref/anamnesis`: flow
streamlines #1, pond ripples #5, field-warped-by-objects #7). The **Looper rains
drops** into the pond; rings expand, **bend and illuminate** the flow lines.

## Model decision: effects are GLOBAL, on orthogonal channels (revised)
The original plan localized one feature per ply (vortex on Size ply, crystal on
Freeze ply, …). **Revised:** because the pond is ONE surface, every parameter's
visual pervades the WHOLE field. Plies differ only by *which control is focused*
(sub-display + active-ply emphasis), not by owning a feature. This stays legible
only if **each effect rides a distinct, separable visual channel** (the legibility
contract — never let two effects fight over the same channel):

| Channel | Effect |
|---|---|
| line **bending** (geometry) | rain ripples (Looper) |
| line **brightness / glow** | Mix (base level) + droplet ring illumination |
| **motion** (flow advance) | Clock tempo; **Freeze halts it** |
| **bubbles + z-depth** (floating shapes woven through line-bands) | Density |
| line **softness / blur** | Diffusion |
| slow **wander** | Mod |
| **persistence** (ripple/flow linger) | Decay |
| **stroke break-up** (dashes) | Grit |
| **horizontal width** | Spread |

## Param → lever map (the full intent)

**LOOPER → the rain** (geometry/bending channel)
- **Length** → rain rate + drop size (loop period). **Speed** → impact energy +
  ripple drift direction (sign), 0 = drops hang. **Regen** → feedback turbulence
  (ripples breed ripples → churn near self-osc). **Mode** → drop character
  (Tape clean / Stretch smeared / Env transient-triggered). **Sense** → Env trigger
  readiness.

**FIELD → the water body** (aggregate surface character; each subparam shapes it)
- **Size** → flow feature scale (swell wavelength/amplitude). **Decay** → persistence
  (ripple τ + flow linger). **Diffusion** → fuzz/scatter (crisp ↔ hazy). **Density**
  → **bubbles** (outlined shapes float up through the field; count ∝ Density) woven
  in front of / behind the streamline **bands** via randomized **z-levels** (real
  depth). [Dendrite/branch & line-subdivision ideas tried + abandoned 2026-06-27.]
  **Mod** → slow organic wander of the lines.

**CLOCK → global time + lo-fi**
- **Clock** → global tempo (flow + ripple expansion speed; flow ∝ 1/R). **Grit** →
  stroke break-up (continuous → dashed → pixel-quantized). **ClockMode** → tempo
  quantization (Steps/Smooth).

**ROUTER / OUTPUT → framing** (global modulations)
- **Mix** → brightness/contrast (dry faint ↔ wet vivid; base to ~12/15). **Spread**
  → horizontal width/symmetry. **Source** → excitation character (live-input chop ↔
  discrete loop drops). **DirectLoop** → crisp dry overlay layer.

**FREEZE → state freeze**
- Stops the **flow motion only** (lines hold their shape); droplet instancing +
  influence (bend/glow) continue → a frozen pond still being rained on. NOT a
  global crystallize (that competed with everything).

### Active-ply emphasis (editing feedback)
Global effects risk feeling disconnected from the knob. Mitigation (pending): the
focused ply **brightens / its local region shows the strongest version** of that
effect, so you get a "you are here" anchor while the effect still pervades the pond.
(Reborn `project_bias_indication`.)

## Why it's feasible (verified 2026-06-27)
- ViewControls are a horizontal `SpottedStrip` of **42px columns + 1px gap**,
  panning at **55 FPS**; graphics aren't clipped.
- **Seams align by construction:** each ply has a fixed `canvasIndex`; it renders
  content-X of one shared field f(X,y,t,state). Control points sit on a GLOBAL grid
  (multiples of `kCtrlStep`) so neighbours sample identical points at the seam.
- **All 6 plies share one field renderer** (incl. the Freeze gate) — required for an
  unbroken image. `AnamFieldControl` = `AnamSubControl` + field main; `AnamFieldGate`
  = `Gate` + field main (stock ComparatorView orphaned; freeze via the sub-display).
- **"All-over" = the main 256-wide strip only.** The sub-display (128×64) holds the
  readouts.

## Build status (2026-06-27)
- **A Foundation — DONE.** Flowing field across all plies, seams aligned.
- **B Consolidate to 6 plies — DONE.** Looper(Speed+Length default subs s1/s3),
  Freeze(field-Gate), Size, Density, Clock, Mix; Clock→ply-1 reorder still pending.
- **C Effects — IN PROGRESS:**
  - **Rain ripples DONE** (dev 0.2.0.37): physical drop model — impact transient
    (crater→jet) + dispersive fanning train + knock-on; pond-wide bending. See 08.
  - **Brightness + Freeze + lifetime DONE** (dev 0.2.0.40): Mix→base brightness,
    droplet rings illuminate the lines, Freeze stops flow only, longer drop life.
  - **NEXT: Density → branch/absorb** (spec in 08), then Diffusion, Mod, Decay,
    Clock-tempo polish, Grit, then Spread/Source/DirectLoop (weak — make subtle).
- **D Polish — pending:** active-ply emphasis, CPU profile on CM4 (Phase 6).

## Open questions / future
- **Literal droplet ring lines at high Mix** (FUTURE, Bram-requested): at high Mix,
  draw actual expanding ring arcs/lines from each drop (on top of the bend+glow) to
  further differentiate ripples from line-bending. Deferred — current bend+glow ok.
- Clock→ply-1 reorder (target 6-ply order) not yet applied.
- Whole-width rain keeps the per-loop spawn RATE, so each region is sparser — raise
  rate/pool if it reads too sparse.
- Spread/Source/DirectLoop visual channels are weak — may stay subtle or defer.
