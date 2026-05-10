# Network — gltch overview ply (3D phase-space viz)

## Context

Network's `gltch` ply currently presents a standard `GainBias` bar
fader. The unit's spatial richness (64 reflectors in unit disk +
orbiting listener + 6-mode glitch mutex) is invisible to the user.

This plan replaces the gltch fader display with a 3D phase-space
visualization on the gltch ply itself, and moves the gltch ply to
the leftmost position so it acts as the unit's overview/identity.

**Visual concept**: 2D unit-disk reflector field rendered as a
slowly rotating disc in 3D. Each tap's Z displacement is driven by
its glitch-mode character — `NORMAL` taps stay in-plane, glitch
modes push the tap "out of plane" in mode-specific signatures. At
glitch=0 the field is a flat rotating disc; at high glitch it's a
writhing 3D cloud whose structure is revealed by the rotation.

This makes the geometric / glitch state of the unit legible at a
glance, and the gltch macro's effect immediately visible — tap
displacement is the literal visualization of "how much glitch
character is active."

The encoder still drives the underlying `Glitch` parameter
(standard `GainBias.encoder` behavior). No fader bar — the 3D viz
*is* the control surface visual.

## Reference patterns

- **`mods/spreadsheet/Rauschen.h:43` `PhaseSpaceGraphic`** —
  header-only `od::Graphic` subclass with `follow()` + persistence
  buffer + slow tumble rotation. Closest template.
- **`mods/spreadsheet/LaretOverviewGraphic.h`** — `follow()`
  pattern, `mUpdateCounter` to throttle SWIG-overhead snapshot
  reads to every 3 frames.
- **`mods/spreadsheet/assets/LaretOverviewControl.lua`** — canonical
  GainBias subclass that replaces the bar fader with a custom
  graphic via `setControlGraphic` + `setMainCursorController`. With
  shift-toggle `paramMode` for supplementary readouts (skip the
  paramMode for first ship; can add later).
- **`er-301/mods/core/objects/granular/Grain.cpp:65`
  `snapToZeroCrossing`** — not relevant to this plan but referenced
  for completeness.

## Architecture

### Visual layers (back-to-front render order)

1. **Persistence buffer** — Rauschen-style 64×64 `uint8_t mPixels`
   array. Each frame: fade all non-zero pixels by 1. Used for ghost
   trails. Cleared on glitch=0 transition (clean fade-out).
2. **Connectivity mesh** — thin 1-pixel dim lines between
   feedback-selected taps (where `mFbWeight[t] != 0`). Each
   selected tap drawn-to its nearest neighbor in selected set
   (closest by 3D distance after projection). At conn=0 no lines;
   at conn=1 dense mesh.
3. **Tap dots** — 64 reflector positions, projected to 2D after Z
   displacement + rotation. Per-mode visual:
   - NORMAL: solid 1-pixel bright dot
   - MUTE: 1-pixel dim dot (faint outline)
   - STUTTER: solid bright dot + ghost trail (see persistence)
   - CRUSH: dithered (hashed flicker per frame)
   - SCRUB: dot at scrub-offset-jittered position
   - REVERSE: small hollow square outline (2x2 pixels)
4. **Ricochet lightning** — on G4 events, brief 1-pixel lines from
   listener position to each affected tap, fading rapidly over ~6
   frames. Read from per-tap `mTapRicochetFlash` counter exposed by
   Network.
5. **Listener marker** — small cross/chevron at orbit position
   (`listenerX, listenerY, 0`), oriented to face inward (forward
   toward origin). Shows where the listener is in the field.

### 3D coordinate system + Z displacement

Reflectors are in `[-1, +1]²` (unit disk). Listener orbits at
radius 1.3 in z=0 plane. Per-tap Z is computed each frame from
glitch state:

```cpp
float tapZ(int t) {
  switch (mTapEffectMode[t]) {
    case NORMAL:   return 0.0f;
    case MUTE:     return -1.0f;                 // sinks below
    case STUTTER:  return 0.5f * sin(2π × posInLoop[t] / loopSamples[t]);  // orbits in Z
    case CRUSH:    return 0.3f * (hash(t, frame) - 0.5f);                  // noise jitter
    case SCRUB:    return 0.4f * scrub_offset_normalized;                  // jitters per block
    case REVERSE:  return -0.4f;                 // settles below
  }
}
```

`scrub_offset_normalized` = sign-preserving normalized scrub offset
exposed by Network as a getter (computed from
`mTapNewReadIdx[t] - geometric_position`).

### Rotation + projection

Per Rauschen pattern (line 159):
```cpp
mRotAngle += 0.01f;  // ~1 rev / 10s at 60fps
if (mRotAngle > 2π) mRotAngle -= 2π;
float cosA = cosf(mRotAngle), sinA = sinf(mRotAngle);
const float kTilt = 0.3f;
const float costilt = 0.9553f, sintilt = 0.2955f;
```

Per-point project (point = reflector 3D position OR ghost OR
listener):
```cpp
// Center to [-0.5, 0.5]³
float nx = px;     // already in [-1, 1] for reflectors
float ny = py;
float nz = pz;
// Rotate around Y axis (turntable tumble)
float rx = nx * cosA + nz * sinA;
float ry = ny;
float rzNew = -nx * sinA + nz * cosA;
// Tilt around X axis for 2.5D depth
float fy = ry * costilt - rzNew * sintilt;
float fx = rx;
// Project to ply (64x64) — center origin, scale slightly
int sx = (int)((fx + 0.0f) * 28.0f) + 32;   // scale 28 = ~conservative, leaves margin
int sy = (int)((fy + 0.0f) * 28.0f) + 32;
// Clamp to 0..63
```

Depth cue (optional first-pass): brightness scaled by depth
(`rzNew` value). Taps "behind" appear dimmer than taps "in front."

## Network public getters (added to mods/spreadsheet/Network.h)

The graphic needs read-only access to internal state. Add public
inline getters (header-only, follows existing pattern):

```cpp
public:
  // Graphic accessors (read-only views of internal state).
  // All are header-only inline so no out-of-line virtual concerns.
  int    getActiveTapCount() const { return mLastActiveTaps; }
  void   getReflector(int t, float *x, float *y) const;
  uint8_t getTapMode(int t) const { return mTapEffectMode[t]; }
  uint8_t getTapStutterIter(int t) const { return mTapStutterIterations[t]; }
  float  getTapStutterPosNorm(int t) const;     // posInLoop / loopSamples
  float  getTapCrushMask(int t) const { return mTapCrushMask[t]; }
  float  getTapDecimFactor(int t) const { return mTapDecimFactorF[t]; }
  int    getTapReadIdx(int t) const { return mTapNewReadIdx[t]; }
  int    getTapGeomDelay(int t) const;          // expected delay from listener,
                                                // for scrub-offset normalization
  uint8_t getTapRicochetFlash(int t) const { return mTapRicochetFlash[t]; }
  float  getListenerPhase() const { return mWalkerPos; }
  float  getFbWeight(int t) const { return mFbWeight[t]; }
```

State adds (Network.h):
- `int mLastActiveTaps` — copy of `activeTaps` at end of `process()`
  for graphic to read without re-deriving from density.
- `uint8_t mTapRicochetFlash[kMaxNetworkTaps]` — per-tap flash
  counter set on G4 events, decremented per graphic-frame (or per
  block — block-rate is fine since flash lasts ~6 frames at 60fps =
  ~30 blocks at 187/s, plenty of resolution).

G4 dispatch updated to set `mTapRicochetFlash[t] = kRicochetFlashMaxFrames`
(e.g., 8) on each affected tap per event. Decrement at top of
`process()`. Graphic reads value as visual flash level.

### Geometric tap delay accessor

For scrub offset normalization, the graphic needs to know what
`mTapNewReadIdx[t]` would have been *without* SCRUB applied. We
can't easily reconstruct that post-hoc, so simpler:

- Network stores `mTapGeomReadIdx[kMaxNetworkTaps]` — the
  geometry-derived read index *before* G5 scrub offset is applied.
- In the dual-read shift loop, capture into `mTapGeomReadIdx[t]`
  immediately after computing `idx = mWriteIndex - newDelay`,
  before G5 scrub modifies `mTapNewReadIdx[t]`.
- Graphic computes:
  ```cpp
  scrub_offset = (mTapNewReadIdx[t] - mTapGeomReadIdx[t] + maxDelay) % maxDelay;
  if (scrub_offset > maxDelay/2) scrub_offset -= maxDelay;
  scrub_offset_normalized = scrub_offset / (float)scrubMaxSamples;
  ```

(Or expose `getTapScrubOffsetNorm(t)` directly that does this math
internally.)

## Files to modify / create

### Create

- `mods/spreadsheet/NetworkOverviewGraphic.h` — header-only
  `od::Graphic` subclass. Inline draw() handles persistence buffer
  fade, connectivity mesh, projected tap dots, ricochet lightning,
  listener marker. Throttled snapshot reads (every 3 frames per
  LaretOverviewGraphic pattern).
- `mods/spreadsheet/assets/NetworkOverviewControl.lua` — `GainBias`
  subclass per LaretOverviewControl pattern. Replaces bar fader
  with `NetworkOverviewGraphic` via `setControlGraphic`. No
  `paramMode` toggle for first ship (simpler; supplementary
  readouts can be added later via shift-toggle if wanted).

### Modify

- `mods/spreadsheet/Network.h`:
  - Add the public getters listed above.
  - Add `mLastActiveTaps` and `mTapRicochetFlash[64]` and
    `mTapGeomReadIdx[64]` class members.
  - Constructor inits.
  - `process()`: capture `activeTaps` to `mLastActiveTaps` at end;
    decrement ricochet flash counters at top; capture geom
    read-idx in dual-read shift before G5 scrub.
  - G4 dispatch: set `mTapRicochetFlash[t] =
    kRicochetFlashMaxFrames` on affected taps.
- `mods/spreadsheet/spreadsheet.cpp.swig`: add
  `#include "NetworkOverviewGraphic.h"` and matching
  `%include "NetworkOverviewGraphic.h"`.
- `mods/spreadsheet/assets/Network.lua`:
  - `require "spreadsheet.NetworkOverviewControl"`.
  - Replace the `glitch = GainBias { ... }` view with
    `glitch = NetworkOverviewControl { ... op = objects.op ... }`.
  - **Reorder `expanded` table** to put `glitch` first:
    ```lua
    expanded = { "glitch", "size", "density", "motion",
                 "connectivity", "decay", "wet" }
    ```

### PKGVERSION bump

- `mods/spreadsheet/mod.mk`: `2.6.1.18` → `2.6.1.19`.

## Implementation order

Sequenced so each step is independently buildable:

1. **Persist this plan** to
   `planning/network-overview-viz.md` and commit (separate
   commit, before code edits).
2. **Network.h plumbing**: add state members
   (`mLastActiveTaps`, `mTapRicochetFlash`, `mTapGeomReadIdx`),
   constructor init, public getters. Modify `process()` to
   maintain them. Don't yet wire up G4 ricochet flash setter or
   geom read-idx capture (do that as part of step 3).
3. **Wire G4 + scrub into Network.h state**: G4 dispatch sets
   ricochet flash; dual-read shift captures geom read-idx before
   G5 scrub modification. Build, verify still no NEON hints,
   audible regression-free.
4. **Create NetworkOverviewGraphic.h** with the layered render.
   Add to `spreadsheet.cpp.swig`. Build (no Lua change yet —
   compile check only).
5. **Create NetworkOverviewControl.lua**, edit Network.lua to
   use it + reorder ply. Bump PKGVERSION. Force-clean SWIG.
   Build, install, audition.
6. **Single ship commit** for steps 2–5 (or split into 2–3
   commits if any phase warrants its own checkpoint —
   particularly step 4 if the graphic is large).

## Verification

1. **Build clean**:
   - `make ARCH=linux PKGNAME=spreadsheet` succeeds.
   - `make ARCH=am335x PKGNAME=spreadsheet` succeeds.
2. **NEON hint check**:
   `arm-none-eabi-objdump -d testing/am335x/mods/spreadsheet/spreadsheet_swig.o | grep -cE '\.32.*:(64|128)'`
   returns 0.
3. **Lint**:
   `tools/check-graphic-virtual-defs.sh` exits clean — critical
   here since `NetworkOverviewGraphic` is the first new graphic
   class added to spreadsheet via this work.
4. **vtable check**:
   `arm-none-eabi-nm -C testing/am335x/libspreadsheet.so | grep 'vtable for stolmine::NetworkOverviewGraphic'`
   shows `V` (COMDAT vague-linkage).
5. **Hardware audition**:
   - Insert Network from spreadsheet.
   - Confirm gltch is the LEFTMOST ply.
   - Confirm gltch ply shows the rotating disc viz instead of a
     bar fader.
   - At glitch=0: flat rotating disc, all taps in-plane.
   - Sweep glitch up: progressively more taps lift out of plane in
     mode-specific signatures (mute sinks, stutter oscillates Z,
     etc.).
   - Trigger transient input: ricochet lightning briefly visible
     from listener to affected taps.
   - Sweep conn: connectivity mesh density changes visibly.
   - Sweep motion: listener marker orbits around the field;
     glitch reseed events visible as pattern shifts.
   - Encoder on gltch ply: edits the underlying Glitch parameter
     as before (verify with another ply's readout).

## Risks / known limitations

- **Frame-rate vs block-rate state**: graphic runs at ~60fps, DSP
  at 187 blocks/sec. Ricochet flash counter set at block-rate,
  decremented at... block-rate is simpler than frame-rate (avoids
  graphic-driven mutation of DSP state). 8 flash blocks ≈ 43ms,
  perceptually right.
- **Connectivity mesh CPU**: nearest-neighbor pairing at K=64
  selected taps = O(K²) = 4096 distance comparisons. Tractable per
  frame (~ 240k ops/sec at 60fps). If too slow, fall back to
  fixed-stride pairing (each selected tap → next-selected by index).
- **Z-displacement at glitch=0**: must be exactly 0 for all NORMAL
  taps so the disc is genuinely flat. Verify in unit test or
  audition.
- **First ship simplicity**: skip paramMode shift-toggle. Can add
  in a follow-up if you want supplementary numeric readouts on
  shift-press.

## Memory references (rules to comply with)

- Header-only inline preserved (per
  `feedback_no_out_of_line_virtuals`). NetworkOverviewGraphic must
  have all virtuals defined inline in the .h.
- `feedback_swig_header_dep`: editing Network.h + adding
  NetworkOverviewGraphic.h triggers SWIG; force-clean wrapper
  before build.
- `feedback_package_version_bump`: bump spreadsheet.
- `feedback_persist_plans_to_repo`: this plan committed before
  first code edit.
- `feedback_linux_build_auto_install`: `cp testing/linux/spreadsheet-2.6.1.19.pkg ~/.od/rear/`.
