# Fabula — implementation-ready design

Status: **ready to implement**. No code. No other file changes.
Phase 1 of the Zaum roadmap. Architecture context: `planning/zaum-design.md`.
Phased build context: `planning/zaum-roadmap.md`.

---

## 1. Intent and scope

Fabula is the believable-room substrate: the Dattorro/Griesinger allpass tank
that makes lush, smooth, Valhalla-adjacent reverb tails. It fills the gap in
the current catalog — every shipped reverb (Galactic, Verbity, WoodenBox,
RotCoat) has visible character; none occupies the smooth long-decay niche.

Fabula ships as a standalone unit in a new package `mods/zaum/`. The package is
CM4-targeted (Cortex-A72). Fabula itself is cheap enough to run on am335x, but
ships CM4-only to keep the Zaum family together and avoid setting am335x
expectations for the units that follow (Sujet, Phase 3 Portals, the north-star
Zaum). This is an explicit positioning decision: Zaum is a CM4 package.

The DSP atom `mods/zaum/atoms/APFTank.h` is the Phase 1 north-star primitive.
It encapsulates the full Dattorro/Gardner tank as a self-contained `od::Object`
subclass. Zaum (Phase 5) reuses it verbatim.

Cross-references: `planning/zaum-design.md` §"SUBSTRATE — modulated APF tank";
`planning/zaum-roadmap.md` §"Phase 1 — Fabula".

---

## 2. DSP architecture

### Topology

Dattorro 1997 figure-8 recirculating allpass tank, extended with Gardner nested
allpasses for denser early diffusion and decorrelated Brownian modulation per
delay line for lushness.

```
IN L ──┬──────────────────────────────────────────────────────┐
IN R ──┘                                                      │
        ↓                                                     │
   [Predelay ring buffer, 0..340 ms]                         │
        ↓                                                     │
   [Input diffusion: 4 series allpasses, fixed coefficients] │
        ↓                                                     │
   ┌─── inputDiff ───────────────────────────────────────┐   │
   │                                                     │   │
   │  loop_L:                                            │   │
   │    AP1_L (g=0.70, d=672) ──────────────────────┐   │   │
   │    nested_AP1_L (g=0.50, d_inner=224)          │   │   │
   │    → D1_L (4453 smp, mod ±42 smp, Brownian)   │   │   │
   │    → HF damp (one-pole LP, coeff from Damp)   │   │   │
   │    → AP2_L (g=0.50, d=908) ─────────────────┐ │   │   │
   │    nested_AP2_L (g=0.50, d_inner=303)        │ │   │   │
   │    → D2_L (3163 smp, mod ±38 smp, Brownian)  │ │   │   │
   │    × decay_gain ────────────────────────────→ cross_L→R │
   │                                                     │   │
   │  loop_R (symmetric, cross-fed from loop_L):         │   │
   │    AP1_R (g=0.70, d=672) → nested_AP1_R             │   │
   │    → D1_R (4453, mod ±42) → HF damp                 │   │
   │    → AP2_R (g=0.50, d=908) → nested_AP2_R           │   │
   │    → D2_R (3163, mod ±38) × decay_gain ──→ cross_R→L │  │
   │                                                     │   │
   └─────────────────────────────────────────────────────┘   │
        ↓ taps from D1_L, D2_L, D1_R, D2_R                  │
   [Wet/dry mix]                                             │
        ↓                                                     │
OUT L / OUT R ────────────────────────────────────────────────┘
```

The figure-8 cross-couple: loop_L's D2 output feeds into loop_R's input
accumulator, and vice versa. This is the stereo-decorrelation mechanism per
Dattorro; same structural pattern as Galactic's L↔R cross-feedback
(`mods/house/atoms/Galactic.h` lines 36-42).

### Delay-length table at 48 kHz

Dattorro's published values are at 29761 Hz. Scale factor to 48000 Hz: 1.6128.
All lengths rounded to nearest prime (anti-comb discipline).

**Input diffusion allpasses (4 series, fixed):**

| AP | Dattorro (29761 Hz) | ×1.6128 → scaled | Used (nearest prime) |
|----|---------------------|-----------------|----------------------|
| ID1 | 142 | 229 | 229 |
| ID2 | 107 | 173 | 173 |
| ID3 | 379 | 611 | 613 |
| ID4 | 277 | 447 | 449 |

Coefficients: ID1/ID2 = 0.75, ID3/ID4 = 0.625.

**Tank allpasses (2 per loop, Gardner nested):**

| AP | Dattorro (29761 Hz) | ×1.6128 → scaled | Used (prime) | Nested inner (÷3, prime) |
|----|---------------------|-----------------|--------------|--------------------------|
| TA1 | 672 | 1084 | 1087 | 367 |
| TA2 | 908 | 1465 | 1471 | 491 |

Coefficients: TA1 g = 0.70, TA2 g = 0.50. Inner nest g = 0.50 (both).
Gardner nested allpass multiplies effective diffusion order for ~1.5× the
allpass depth at the same delay budget. Implementation uses
`house::allpassNestedStep()` from `mods/house/atoms/AllpassMono.h`.

**Tank delay lines (2 per loop, mutually prime):**

| Line | Dattorro (29761 Hz) | ×1.6128 → scaled | Used (prime) | Max mod excursion |
|------|---------------------|-----------------|--------------|-------------------|
| D1_L | 4453 | 7182 | 7187 | ±68 smp (1.4 ms) |
| D2_L | 3163 | 5101 | 5101 | ±58 smp (1.2 ms) |
| D1_R | 4217 | 6802 | 6803 | ±64 smp (1.3 ms) |
| D2_R | 3931 | 6342 | 6343 | ±60 smp (1.25 ms) |

L and R loops use asymmetric delay lengths to ensure per-channel decorrelation.
All four lengths are mutually prime — the exact discipline that the Network
multitap failed: equal-spaced taps share divisors, producing modal clustering
(flutter / metallic ring) at any shared divisor frequency.

**Mutual-prime rule:** For delay lengths p₁, p₂, p₃, p₄, require
gcd(pᵢ, pⱼ) = 1 for all i ≠ j. This suppresses shared-frequency reinforcement
in the feedback loop's eigen-structure. Modal distribution becomes incoherent
→ smooth, dense tail rather than audible resonant frequencies.

**Decay gain → RT60:**

The per-loop decay_gain `g_d` is applied once per round trip. Round-trip length
at Size=1.0 is approximately D1 + D2 + 2×AP overhead ≈ 7187 + 5101 + ~3200 =
~15500 samples (0.323 s at 48k). RT60 relates to g_d by:

```
RT60 = -3 × round_trip_time / log10(g_d)
```

At g_d = 0.90 and RTT = 0.323 s: RT60 ≈ 8.8 s. Practical range:
g_d = 0.60 (RT60 ≈ 2.0 s) to g_d = 0.97 (RT60 ≈ 32 s). The Decay parameter
maps linearly in log space. Keep g_d < 1.0 unconditionally for passive
stability; the Spiral governor is the fallback for user error.

### Allpass helper

`house::allpassNestedStep(xNow, vDelayed, g, vNew, yOut)` from
`mods/house/atoms/AllpassMono.h`:

```
vNew = xNow + g * vDelayed;
yOut = -g * vNew + vDelayed;
```

`vNew` is written back to the delay buffer at the current write head. `yOut`
is the APF output routed forward. At g=0 this degenerates to a pure delay
(identity). State is in the caller's delay buffer; `allpassNestedStep` is
stateless. For the nested form, call the function twice on the same buffer
using inner/outer delay-length offsets.

---

## 3. Modulation — the lushness lever

### Mechanism

Each of the four tank delay lines (D1_L, D2_L, D1_R, D2_R) is read at an
integer offset that drifts slowly according to a per-line Brownian LFO:

```
xorshift64(&seed_n);
noise_n = (double)(seed_n & 0xFFFF) / 65535.0 - 0.5;   // [-0.5, 0.5]
walk_n += noise_n * step_size;
walk_n  = clamp(walk_n, -excursion, +excursion);
read_offset = base_delay + (int)walk_n;
```

Four independent `uint64_t` seeds (one per line), initialized from distinct
compile-time constants to ensure decorrelation from sample 0. Rate is controlled
by `step_size` (proportional to Mod rate parameter). Excursion is the Mod depth
parameter, mapped to ±9..72 samples (±0.2..1.5 ms at 48k).

### Why Brownian, not sine

Galactic (`mods/house/atoms/Galactic.h`) uses a single shared sine LFO (`vibM`,
`oldfpd` state) that drives the predelay modulation. This produces a predictable
periodic wobble — characteristic of Galactic, but not the mechanism for smooth
density. The Valhalla discipline: decorrelated aperiodic modulation per delay
line breaks the tank's modal eigen-structure. No two lines drift in phase. The
result is a randomized eigen-frequency smear that prevents metallic resonances
from reinforcing. This is the primary mechanism separating "smooth lush room"
from "ringing comb".

### Integer vs fractional modulation

Phase 1 uses integer-sample reads (no interpolation). This is the simplest path
and avoids the inter-sample-pitch-shift artifacts that linear interpolation can
introduce when walk_n changes quickly. Risk: at shallow Mod depth, integer steps
produce audible clicking (zipper noise) as the read pointer jumps one sample.
Mitigation: step_size is smoothed with a one-pole filter per line (α ≈ 0.9995
per sample) so walk_n rarely steps by more than 0.1 sample per sample, making
jumps infrequent.

Upgrade path: if hardware audition at 0.1.0.4 reveals zipper noise, swap to
linear interpolation:
```
frac  = walk_n - floor(walk_n);
out   = (1 - frac) * buf[i0] + frac * buf[i1];
```
Cost: 2 muls + 1 add + 1 floor per line. Allpass interpolation (Thiran) is a
further upgrade if linear interp introduces high-frequency droop audibly. Defer
both until audition reveals the need.

**Modulation calibration is the primary Phase 1 risk** (per zaum-roadmap.md
§"Phase 1 risks"). Depth too high → pitch wobble. Depth too low → sounds like
Verbity. Calibrate at audition gate 0.1.0.4.

---

## 4. Feedback governor

`house::spiralFastSaturate(x, densityA)` from `mods/house/atoms/Spiral.h`:

```cpp
static inline double spiralFastSaturate(double x, double densityA)
{
    double absX = fabs(x) * densityA;
    if (absX > 1.5707963267948966) absX = 1.5707963267948966;
    double x2 = absX * absX;
    double s = absX * (1.0 + x2 * (-0.16666666666666666
                                   + x2 * 0.008333333333333333));
    return (x > 0.0) ? (s / densityA) : -(s / densityA);
}
```

Applied once per loop iteration to the feedback accumulator before it re-enters
the tank. With `densityA = 1.0`, output is bounded to `[-π/2, π/2]`; for
normalized signals this is effectively linear below ~0.8 and compresses
smoothly above. Output range is `[-1/densityA, 1/densityA]`.

For a clean room with `g_d < 1.0`, the saturator is never activated under normal
use. It becomes the safety net at Decay→max where g_d approaches 1.0: without
it, a hot input transient that briefly drives the tank above unity would take
many seconds to decay. With it, the tank wraps to a bounded wall instead of
diverging. This mirrors the RotCoat pattern: Spiral governor on each Householder
output (rotcoat-port-plan.md §"Feedback governor").

`spiralFastSaturate` is preferred over `spiralSaturate` here: it's called once
per loop iteration (not per sample in the diffusion chain), and the Taylor
approximation error (0.45% max at π/2) is inaudible in a saturator role.

---

## 5. Stereo strategy

Internal-stereo throughout. One `APFTank` object owns both L and R state arrays.
The Lua wrapper maps `In1 → In L`, `In2 → In R`, `Out L → Out1`, `Out R → Out2`
(identical to the KWoodRoom and Galactic patterns).

Figure-8 cross-coupling: loop_L's decay output feeds loop_R's input accumulator
and vice versa. Coupling coefficient is fixed at 1.0 (full cross-feed), matching
Dattorro's published topology. This produces the stereo decorrelation that makes
the tail wide without explicit matrix tricks.

Modulation is decorrelated per channel: L lines (D1_L, D2_L) and R lines
(D1_R, D2_R) use different xorshift seeds. This ensures L and R LFO walks are
independent — the R tail's spectral smear does not phase-lock to L.

Mono input (channelCount == 1): In1 feeds both L and R at the Lua wiring level.
The figure-8 cross-couple naturally spreads a mono source to stereo within a
few loop cycles.

---

## 6. Atom plan

### APFTank.h — the new atom

`mods/zaum/atoms/APFTank.h` — header-only `od::Object` subclass per
`planning/house-atom-architecture.md`. Contains:

- Input diffusion state: 4 delay buffers (float, sizes 229/173/613/449)
- Tank AP state: 4 delay buffers (float, outer+inner per loop side, sizes per
  table in §2)
- Tank delay lines: 4 delay buffers (float, sizes 7187/5101/6803/6343 +
  modulation headroom ÷2 on each end)
- Predelay buffer: float, 16384 samples (~341 ms max)
- Brownian LFO state: 4 `uint64_t` seeds + 4 `double` walk accumulators —
  declared inline as member state; no separate atom (mirrors Galactic's inline
  vibM/oldfpd precedent)
- One-pole HF damper state: 4 doubles (one per D1/D2 per side)
- `od::Inlet mInL, mInR`, `od::Outlet mOutL, mOutR` (public, outside
  `#ifndef SWIGLUA`)
- `od::Parameter mSize, mDecay, mDamp, mDiffusion, mMod, mModRate, mPredelay,
  mMix` (public, outside `#ifndef SWIGLUA`)
- Constructor: `addInput`, `addOutput`, `addParameter` for all, `memset` all
  buffers to 0, initialize seeds to compile-time constants
- `process()` inline, inside `#ifndef SWIGLUA`

SWIG rule: constructor, destructor, and all `addInput`/`addOutput`/
`addParameter` calls are OUTSIDE `#ifndef SWIGLUA`. The `process()` body,
inlet/outlet/parameter member declarations, and delay buffer members are INSIDE
`#ifndef SWIGLUA`. This is the pattern throughout the house atom inventory
(KWoodRoom.h, Galactic.h, etc.).

### Atom sharing — house atoms in the zaum package

Recommendation: use an `-I` include path in `mods/zaum/mod.mk` to point at
`mods/house/atoms/`:

```make
INCLUDES += mods/house/atoms
```

This lets `mods/zaum/atoms/APFTank.h` include `AllpassMono.h` and `Spiral.h`
directly without copying them. The house atoms are header-only and have no link
dependencies; they compile cleanly into whichever package includes them.

Tradeoff: APFTank now has a build dependency on house atoms. If a house atom is
modified (e.g. `spiralFastSaturate` signature changes), zaum needs a rebuild.
Given both packages live in the same repo and are maintained together, this
coupling is acceptable. The alternative — copying `AllpassMono.h` and `Spiral.h`
into `mods/zaum/atoms/` — avoids the dependency at the cost of divergence risk.
Recommendation stands: shared include path, no copies.

---

## 7. Parameter and UI surface

### DSP parameter map

| Control | Range | Default | DSP lever |
|---------|-------|---------|-----------|
| **Size** | 0..1 | 0.5 | Scales all tank delay lengths proportionally; mutual-prime relationships preserved by rounding to nearest prime in the pool |
| **Decay** | 0..1 | 0.5 | Maps to decay_gain g_d via RT60 formula; 0 → g_d≈0.60 (~2s RT60), 1 → g_d≈0.97 (~32s RT60) |
| **Damp** | 0..1 | 0.25 | One-pole LP feedback coefficient in each loop's HF path; 0 = open (no damp), 1 = maximum damp (dark) |
| **Diffusion** | 0..1 | 0.6 | Scales input-diffusion AP coefficients 0.50..0.85; tank AP coefficients 0.40..0.70 |
| **Mod** | 0..1 | 0.3 | Brownian excursion depth ±0.2..1.5 ms; all four tank lines scale together |
| **ModRate** | 0..1 | 0.2 | Brownian step_size; maps to LFO walk rate 0.05..2.0 Hz equivalent |
| **Predelay** | 0..1 | 0.0 | Predelay ring buffer tap 0..340 ms |
| **Mix** | 0..1 | 0.5 | Dry/wet crossfade |

Eight controls total. Mod and ModRate may be collapsed to a single Mod control
with a fixed rate ratio if two mod controls feel cluttered; defer the decision
to the 0.1.0.7 UI polish phase.

### Lua wrapper sketch

`mods/zaum/assets/Fabula.lua` — mirrors `KWoodRoom.lua` structure exactly.

```lua
local libzaum = require "zaum.libzaum"

function Fabula:onLoadGraph(channelCount)
  local op = self:addObject("op", libzaum.APFTank())

  connect(self, "In1", op, "In L")
  connect(op, "Out L", self, "Out1")
  if channelCount > 1 then
    connect(self, "In2", op, "In R")
    connect(op, "Out R", self, "Out2")
  end

  -- ParameterAdapter + branch per control (8 total)
  -- size, decay, damp, diffusion, mod, modRate, predelay, mix
  -- each: hardSet("Bias", default), tie(op, "ParamName", adapter, "Out")
  --       self:addMonoBranch(name, adapter, "In", adapter, "Out")
end

function Fabula:onLoadViews()
  -- GainBias plie per parameter, zeroOneMap (0..1)
  -- expanded = { "size", "decay", "damp", "diffusion",
  --              "mod", "modRate", "predelay", "mix" }
  -- collapsed = {}
end
```

Pattern is identical to KWoodRoom.lua: one `ParameterAdapter` per control,
one `GainBias` view per control, `biasMap = zeroOneMap`, `biasUnits = app.unitNone`.
No OptionControl — all parameters are continuous. An expanded view shows all
eight plies. Collapsed view is empty (no abbreviated view needed for v1).

Button labels: `"size"`, `"dcy"`, `"damp"`, `"diff"`, `"mod"`, `"rate"`,
`"pre"`, `"mix"`. Descriptions: full English per the KWoodRoom pattern.

---

## 8. File manifest for the new package

### Files to create

```
mods/zaum/mod.mk                    # package build rules (mirrors house/mod.mk)
mods/zaum/zaum.cpp.swig             # SWIG module (mirrors house/house.cpp.swig)
mods/zaum/assets/toc.lua            # package registry (mirrors house/assets/toc.lua)
mods/zaum/assets/init.lua           # package library init (mirrors house/assets/init.lua)
mods/zaum/assets/Fabula.lua         # unit Lua wrapper
mods/zaum/atoms/APFTank.h           # DSP atom (new)
```

### Scaffolding mirrors

**mod.mk:** Copy house/mod.mk verbatim; change `PKGNAME ?= house` →
`PKGNAME ?= zaum`, `PKGVERSION ?= 0.1.0.1`. Add `INCLUDES += mods/house/atoms`
for the shared-atom include path. Keep all CFLAGS, LFLAGS, SWIG rules, and the
`-fno-tree-vectorize` am335x override (even though Fabula doesn't target am335x,
keeping this rule is zero cost and matches house discipline).

**zaum.cpp.swig:** Same `%module zaum_libzaum` header pattern as
`house_libhouse`. `%{...%}` block: `#undef SWIGLUA`, `#include "atoms/APFTank.h"`,
`#define SWIGLUA`. `%include "atoms/APFTank.h"`. Minimal at v0.1.0.1; grows
as new atoms are added per phase.

**assets/toc.lua:** Same structure as house/assets/toc.lua:

```lua
return {
  title = "Zaum",
  author = "stolmine",
  name = "zaum",
  keyword = "reverb, room, lush, algorithmic, tank, allpass, zaum",
  units = {
    { title = "Fabula", moduleName = "Fabula", category = "Zaum",
      keywords = "reverb, room, lush, smooth, tank, allpass, dattorro, fabula, zaum" },
  }
}
```

**assets/init.lua:** Identical to house/assets/init.lua with `House` → `Zaum`
and `require "Package.Library"` preserved.

**Makefile registration:** Add `zaum` to the `PROJECTS` list in the root
`Makefile`:

```make
PROJECTS = mi kryos peaks scope spreadsheet biome catchall porcelain house zaum
```

### SWIG rules (from CONTEXT.md)

Constructor and destructor: outside `#ifndef SWIGLUA`. All `addInput`,
`addOutput`, `addParameter`, `addOption` calls: outside `#ifndef SWIGLUA`.
Member declarations (`od::Inlet`, `od::Outlet`, `od::Parameter`) and
`process()`: inside `#ifndef SWIGLUA`. This is the invariant across all house
atoms; APFTank.h must follow it exactly.

---

## 9. Build sub-phases (dev-version cadence)

Each sub-phase ends at an audition gate before proceeding.

### 0.1.0.1 — Package scaffold + mono passthrough

- Create all files in §8 manifest.
- `APFTank.h` is a stub: constructor, destructor, 8 parameters declared, no
  audio processing (passthrough or silence).
- Register in toc.lua and Makefile.
- Build both arches (linux + am335x clean even though zaum is CM4-targeted;
  confirm the am335x cross-compile doesn't error).
- Install to `~/.od/rear/` via `make zaum-install`.
- **Gate:** Package installs and unit appears in emu browser. No audio yet.

### 0.1.0.2 — Input diffusion + mono tank (static, no modulation)

- Implement `APFTank.h` process() fully: predelay → 4-stage input diffusion →
  single-channel (L only) figure-8 tank with static delay reads (no LFO).
- Decay gain hard-coded to g_d = 0.85. HF damp disabled. No cross-couple yet.
- Build, install, run impulse in emu.
- **Gate:** Can hear a reverb tail. T60 roughly tracks expectation at g_d=0.85.
  No clicks, no silence, no crash.

### 0.1.0.3 — Stereo figure-8 cross-coupling

- Add R-loop state arrays. Implement the L↔R cross-feed. Both channels active.
- Input diffusion runs once shared (sums L+R before diffusion) or per-channel
  — decide by ear at this gate.
- **Gate:** Stereo output. Impulse L only produces audible R decay within ~100 ms.
  No phase cancellation at center.

### 0.1.0.4 — Brownian modulation (primary calibration risk)

- Implement the four per-line Brownian walks (xorshift seeds, walk accumulators).
- Wire Mod and ModRate parameters to excursion and step_size.
- Integer reads only at this stage.
- **Gate:** Sweep Mod 0→1; tail should shift from slightly metallic (0) to
  clearly smooth/dense (0.3–0.5) to mildly warbly (1.0). If zipper noise is
  audible at low Mod settings, add linear interpolation upgrade (see §3).
  No pitch drift audible below Mod=0.5.

### 0.1.0.5 — HF damping + Decay/RT60 calibration

- Implement the one-pole HF damper in each loop's feedback path.
- Wire Damp parameter to damper coefficient.
- Wire Decay parameter to g_d via RT60 formula.
- Wire Size parameter to delay-length scaling (floor to nearest prime in pool).
- **Gate:** Feed impulse; measure T60 at Decay=0.5, compare against formula
  prediction. Sweep Damp 0→1; tail darkens audibly. Sweep Size; tail length
  changes perceptibly.

### 0.1.0.6 — Spiral governor + high-regen safety

- Add `spiralFastSaturate` call on feedback accumulator per loop.
- Test at Decay→1.0 (g_d→0.97) with hot input.
- **Gate:** No runaway at any Decay setting. Long sustained tail at Decay=0.95
  decays to silence within 60s. No click on parameter change.

### 0.1.0.7 — Parameter surface and UI polish

- Review all 8 parameter default values against hardware listening impressions.
- Tighten button labels, descriptions, biasMap steps.
- Decide Mod vs Mod+ModRate vs single Mod parameter (see §7 note).
- Confirm expanded/collapsed view layout.
- Bump PKGVERSION, tag for first user-facing release.
- **Gate:** Unit description matches behavior. No stale label text. Parameter
  names are what a user reaching for a reverb would expect.

### 0.1.0.8 — Dual-scale frequency-routed tanks (stretch goal)

- Crossover filter (~800 Hz) splits input into HF/LF bands.
- HF band routes to shorter-delay configuration (pool B: smaller primes).
- LF band routes to longer-delay configuration (pool A: current primes).
- Outputs sum at wet stage.
- Attempt only if single-tank version lacks air at high Damp settings.
- This is not required for Phase 1 definition-of-done.

---

## 10. CPU and memory budget

### Per-sample operations (stereo, 48 kHz)

Breakdown per call to `process()` over a 64-sample block:

| Section | Operations per sample-pair |
|---------|---------------------------|
| Predelay read/write | ~6 |
| Input diffusion (4 allpasses) | 4 × 4 = 16 |
| Tank AP1 L + nested AP1 L | 2 × 4 = 8 |
| Tank delay D1_L (read + write + walk update) | ~8 |
| One-pole HF damp L | ~4 |
| Tank AP2 L + nested AP2 L | 8 |
| Tank delay D2_L (read + write + walk update) | ~8 |
| Symmetric R loop | ×2 above ≈ 52 |
| Cross-couple accumulators | ~4 |
| Spiral governor (×2 per loop, ×2 sides) | ~10 × 4 = 40 (Taylor, cheap) |
| Brownian LFO update (4 lines) | ~4 × 6 = 24 |
| Wet/dry mix | ~6 |
| **Total estimate** | **~180 MAC/sample-pair** |

Projected CPU at 48 kHz stereo on Cortex-A72 (CM4, ~3.6 GFLOPS scalar DP):
~180 × 48000 / 3.6e9 ≈ **0.24% CPU**. This is conservative (A72 is much
faster than A8; RotCoat runs at 6–12% on A8 at ÷4). Fabula fits easily within
a 5% CM4 budget.

On am335x (Cortex-A8, ~200 MFLOPS scalar DP): ~180 × 48000 / 2e8 ≈ **4.3%
stereo**. Feasible, but not targeted; no am335x hardware gate planned.

### Memory

| Buffer | Samples | Float bytes |
|--------|---------|-------------|
| Input diffusion (4 AP buffers × 2 ch) | ~2928 | ~11.7 KB |
| Tank AP outer (4 AP buffers) | ~6232 | ~24.9 KB |
| Tank AP inner (4 AP buffers) | ~2076 | ~8.3 KB |
| Tank delay lines (4 lines + mod headroom ×2) | ~27024 | ~108 KB |
| Predelay buffer (×2 ch) | 32768 | ~131 KB |
| Misc state (walks, dampers, iir) | — | ~0.5 KB |
| **Total per instance** | | **~285 KB** |

Fits comfortably in CM4's L2 cache (1 MB). The predelay buffer dominates at
max predelay; at typical use (Predelay < 50 ms, 2400 smp), effective working
set is ~155 KB. No dynamic allocation; all buffers are class members
(`memset` to 0 in constructor).

---

## 11. Audition gates and test method

The emu is updated; `mods/zaum` will build to `testing/darwin/` and install to
`~/.od/` (or rear/ as per house pattern). All gates run in emu first, then
hardware before phase advancement.

**Gate 0.1.0.2 — first sound:**
- Feed a click/impulse into Fabula with Decay=0.5, Size=0.5.
- Observe a decaying tail in emu output view.
- Measure approximate T60 visually; should be ~3–5 s at these settings.

**Gate 0.1.0.4 — modulation calibration:**
- Feed a sustained sine at 440 Hz. Sweep Mod 0→1.
- At Mod=0: observe clean but slightly resonant tail (eigentones faintly audible).
- At Mod=0.3: tail becomes smooth, spectrally diffuse. No metallic ringing.
- At Mod=0.8: mild pitch drift, acceptable.
- At Mod=1.0: visible pitch wobble, intentional extreme.
- If zipper clicks audible at any setting: implement linear interpolation before
  proceeding.

**Gate 0.1.0.5 — T60 calibration:**
- Feed impulse. Record output. Measure time for envelope to drop 60 dB.
- Compare against formula: `RT60 = -3 × RTT / log10(g_d)` where RTT is the
  measured per-loop round-trip time.
- Verify Decay=0 → RT60 ≈ 2 s, Decay=1 → RT60 ≈ 25–35 s.
- Sweep Damp; verify -6 dB/oct HF rolloff increase with Damp.

**Gate 0.1.0.6 — no runaway:**
- Set Decay=1.0 (maximum g_d). Feed a full-scale sine burst.
- Observe output; must not grow unboundedly. Saturator should kick in.
- After input stops, tail should decay to silence within bounded time.
- Repeat at Decay=0.99 with white noise input.

**Gate 0.1.0.7 — A/B comparison:**
- A/B Fabula against Galactic at comparable Size/Decay settings.
- Fabula should sound smoother/denser (no discrete Householder echoes), less
  distinctly bright, with a more neutral tonal character.
- A/B against Verbity: Fabula should have a longer, less immediately-saturated
  tail at same Decay settings.

These gates mirror the Phase 1 definition-of-done in zaum-roadmap.md:
smooth/dense/non-resonant tail, T60 tracking, no runaway, CPU within budget,
APFTank atom isolated and reusable.

---

## 12. Open questions and risks

**1. Modulation calibration (primary risk)**
The Brownian walk's step_size → perceptual lushness relationship is unknown
until hardware audition. The thresholds between "too dry / metallic", "sweet
spot", and "pitch wobble" are empirical. Reserve the entire 0.1.0.4 phase for
calibration iteration. Budget two or three build-audition cycles before settling
on default values.

**2. Integer vs fractional delay modulation**
Integer reads are simpler and avoid inter-sample pitch-shift artifacts from
interpolation errors. The zipper risk is mitigated by one-pole smoothing on
walk_n. If audition reveals zipper noise at low Mod settings, the upgrade to
linear interpolation is approximately 8 lines of code per delay line — a small
change with no structural impact. Do not pre-implement; wait for the audition
signal.

**3. Atom sharing mechanism**
The `-I mods/house/atoms` approach is untested in the zaum package context. A
build failure here (include path resolution issue in mod.mk) could stall
0.1.0.1. Fallback: copy `AllpassMono.h` and `Spiral.h` into
`mods/zaum/atoms/` and update their namespace to `zaum::`. Note the namespace
copy in the file header if this route is taken.

**4. New-package scaffolding correctness**
`mods/zaum/` is the second complete package after house. The `%module
zaum_libzaum` name in the SWIG file must match the `require "zaum.libzaum"`
call in the Lua wrapper. Getting this wrong produces a silent load failure (unit
visible in toc but crashes on instantiation). Confirm the module name convention
against house's `house_libhouse` / `require "house.libhouse"` pairing before
writing the SWIG file.

**5. Mono-summing vs true stereo input diffusion**
Two options for feeding L/R inputs into the figure-8 tank: (a) sum L+R before
input diffusion (mono-summed diffuser, as in most Dattorro implementations), or
(b) run separate L and R diffusion chains. Option (a) is cheaper and matches the
published Dattorro topology. Option (b) preserves stereo input character into
the tail. Decision: start with option (a) (mono sum, as per Dattorro). If
hardware audition reveals that stereo input material collapses too aggressively,
add option (b) as a parameter or fixed upgrade.

**6. Gardner nested allpass vs plain allpass for Phase 1**
The nested allpass (`allpassNestedStep` called twice per AP) adds density but
doubles the allpass buffer budget for the tank APs. If the 0.1.0.2 gate reveals
the tail is already dense enough with plain allpasses, skip nesting and reduce
the AP buffer memory. Flag this as a listen decision at 0.1.0.2.

**7. Makefile zaum-install target path**
Confirm whether `install` in mod.mk should copy to `~/.od/rear/` (the am335x
path, which house uses) or a darwin-specific path. For CM4-only packages the
correct install root may differ. Check the build_guide.md or emu config before
wiring the install rule.
