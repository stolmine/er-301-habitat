# house package architecture: atoms, harnesses, chains

Status: design lock. Answers "can the AW tone-shaping work
structurally like the firmware does for composing units?" Yes,
and this doc codifies how.

## The firmware's pattern (what we're matching)

The ER-301 firmware ships ~150 small DSP nodes as `od::Object`
subclasses (Constants, Filters, Comparators, Mixers, Adapters,
Comparators, Multipliers, etc.). Each is a thin C++ class with
inlets / outlets / parameters / options + a `process()` method.

Units are then **Lua compositions** of these objects:
`addObject(name, app.Filter())`, `connect(self, "In1", filter,
"In")`, `tie(filter, "Cutoff", adapter, "Out")`. The graph
compiler schedules them based on inlet/outlet dependencies. The
unit's identity is the **graph topology + UI surface**, not any
single class.

## The house package mirrors this

`house` becomes a **library of small DSP atoms** (each an
`od::Object` subclass) plus **Lua templates** that compose them
into units. Same pattern, package-scoped.

### Directory layout

```
mods/house/
├── atoms/              # Reusable DSP nodes (od::Object subclasses)
│   ├── Density.h       # AW saturator atoms
│   ├── Spiral.h
│   ├── Slew.h
│   ├── Acceleration.h
│   ├── Capacitor2.h
│   ├── Console0Channel.h    # Console containment atoms
│   ├── Console0Buss.h
│   ├── DeRez2.h
│   ├── ChromeOxide.h
│   ├── ...
│   └── KWoodRoomCore.h      # Larger DSP cores (the existing
│                            # KWoodRoomDSP refactored as an atom)
├── harnesses/          # C++ composite patterns (when Lua-level
│   │                   # composition isn't enough — e.g. patterns
│   │                   # that need internal feedback or shared state)
│   ├── ChannelBussPair.h    # Wraps channel→inner_chain→buss
│   └── ReducedRateDomain.h  # Decimate→core→Bezier-reconstruct
├── assets/             # The actual user-facing units
│   ├── KWoodRoom.lua   # Wires atoms into the kWoodRoom unit
│   ├── Density.lua     # Maybe individual atoms as units too
│   ├── Smoketest.lua   # Phase 0 harness (existing)
│   ├── toc.lua
│   └── init.lua
├── house.cpp.swig      # %include every atom + harness header
└── mod.mk
```

### What an atom looks like

Header-only (per `feedback_no_out_of_line_virtuals` for any
framework subclass), inlet/outlet members public, `process()`
inline, parameters as `od::Parameter` / `od::Option`. Example
sketch for Density (AW's bipolar saturation workhorse):

```cpp
// mods/house/atoms/Density.h
#pragma once

#include <od/objects/Object.h>
#include <od/config.h>

namespace house
{
  class Density : public od::Object
  {
  public:
    Density()
    {
      addInput(mInL); addInput(mInR);
      addOutput(mOutL); addOutput(mOutR);
      addParameter(mDensity);
      addParameter(mHighpass);
    }

    virtual ~Density() {}

#ifndef SWIGLUA
    od::Inlet     mInL{"In L"};
    od::Inlet     mInR{"In R"};
    od::Outlet    mOutL{"Out L"};
    od::Outlet    mOutR{"Out R"};
    od::Parameter mDensity{"Density", 0.0f};   // bipolar -1..+1
    od::Parameter mHighpass{"Highpass", 0.0f}; // 0..1

    virtual void process()
    {
      // ... per-sample sin() transfer, multi-stage,
      //     optional pre-distortion highpass, remix ...
    }
#endif
  };
}
```

### What a harness looks like

When Lua-level composition isn't enough — e.g. the channel→buss
containment pattern needs to wrap an arbitrary inner chain — a
harness is a C++ Object that **owns or coordinates** atoms
internally. Two flavors:

1. **Pass-through container** — exposes inlets/outlets like an
   atom, but internally instantiates and chains other atoms. The
   inner topology is hidden from the Lua side. Useful when the
   pattern is fixed (e.g. "always saturate-process-desaturate").
2. **Pure coordinator** — exposes only parameters; the actual
   audio flow is wired in Lua via `connect()` to atoms the harness
   doesn't own, but the harness modulates them. The
   envelope-driven driver pattern (Swell → modulate inner-chain
   parameters) fits here.

Most patterns can be expressed as Lua templates rather than C++
harnesses. Reach for the C++ harness only when:
- The pattern needs internal feedback that has to compile-time-
  schedule together
- State has to be shared across atoms that the graph compiler
  would otherwise schedule independently
- Performance matters and Lua-level wiring would add per-block
  overhead

### What a unit looks like

Pure Lua composition, no different from any habitat unit. Wires
atoms together via `addObject` + `connect` + `tie`, exposes
parameters via `GainBias` / `OptionControl` / etc. Example
sketch for a `Saturate` unit built from Density + a Console pair:

```lua
-- mods/house/assets/Saturate.lua
function Saturate:onLoadGraph(channelCount)
  local channel = self:addObject("channel", libhouse.Console0Channel())
  local density = self:addObject("density", libhouse.Density())
  local buss    = self:addObject("buss",    libhouse.Console0Buss())

  connect(self, "In1", channel, "In L")
  if channelCount > 1 then connect(self, "In2", channel, "In R") end

  connect(channel, "Out L", density, "In L")
  connect(channel, "Out R", density, "In R")
  connect(density, "Out L", buss, "In L")
  connect(density, "Out R", buss, "In R")

  connect(buss, "Out L", self, "Out1")
  if channelCount > 1 then connect(buss, "Out R", self, "Out2") end

  -- expose Density's density param via standard GainBias adapter
  local d = self:addObject("d", app.ParameterAdapter())
  tie(density, "Density", d, "Out")
end
```

The unit IS the chain. Different chains expose as different
units. Same atoms, many products.

## Iteration workflow

The Lua composition layer is the iteration boundary. To try a
new chain:

1. Write a new `.lua` in `assets/`.
2. Register in `toc.lua`.
3. Rebuild — no C++ changes if all the atoms already exist.

To add a new atom:

1. Drop a header-only `od::Object` subclass in `atoms/`.
2. Add `%include "atoms/Foo.h"` to `house.cpp.swig`.
3. Rebuild.

To prototype a new harness pattern:

1. Try it in Lua first (template / mixin function shared across
   units).
2. If Lua's wiring overhead or scheduling-shape constraints
   matter, lift to C++ as a header-only harness in `harnesses/`.

## What this gets us

- **Reuse**: AW atoms are shared across reverb units, saturation
  units, lo-fi units, hybrid character units. Density's `process()`
  is one body of code regardless of which unit uses it.
- **Composability**: chains are Lua, so the iteration loop is
  edit-and-rebuild-lua, not edit-and-rebuild-C++.
- **Surface clarity**: each unit's UI is just a Lua file. Easy
  to copy a unit, swap atoms, see how the character shifts.
- **Bisect-ability**: a hung chain bisects to one atom by
  swapping `op` references between two simpler chains. The
  Smoketest harness pattern (currently used for KWoodRoomDSP)
  generalizes: any atom can be exercised in a Smoketest unit
  before being wired into a real chain.
- **CPU honest**: the graph compiler schedules per-Object, so
  the cost of a chain is the sum of its atoms' `process()` costs
  plus the wiring overhead (negligible). Easy to reason about.

## Constraints worth knowing

- **SWIG surface grows linearly with atom count.** Each atom
  needs a `%include`. Past ~30-40 atoms, the `house.cpp.swig`
  file gets long. Manage by grouping atoms into category headers
  (e.g. `atoms/saturators.h` includes all the saturator atoms).
  Defer until it actually matters.
- **No out-of-line virtuals on framework subclasses.** Atoms
  are header-only. `process()` lives in the header. For atoms
  with long process bodies, this is fine — the linker dedups
  COMDAT bodies.
- **NEON :64 alignment traps.** All atom state must be class
  members (heap-allocated when the Object is constructed via
  `addObject`), never stack-locals in `process()`. Standard
  rule per `feedback_neon_intrinsics_drumvoice`.
- **Audio-thread small stack.** Per CONTEXT.md, large temp
  buffers in `process()` should come from `od::AudioThread::getFrame()`
  rather than stack allocation.
- **Cross-atom state sharing.** If two atoms need to share
  state (e.g. a stereo-linked compressor where L's detector
  feeds R's gain), wrap them in a C++ harness. Pure-Lua
  composition can't share state across Object boundaries.
- **Feedback loops.** The graph compiler won't schedule a
  cyclic Lua-level graph. Any feedback (FDN, allpass loop,
  comb) lives inside a single atom's `process()`, not across
  atoms. KWoodRoomDSP already follows this — the entire 6×6
  trellis is one atom.

## What changes from the current house state

Current `mods/house/` has:
- `KWoodRoomDSP.{h,cpp}` — DSP class, but NOT yet an
  `od::Object` subclass. It's a plain helper that Smoketest
  wraps. Phase 1 will lift it into the architecture.
- `Smoketest.h` — Phase 0 throwaway harness.

Refactor plan for kWoodRoom Phase 1:

1. Promote `KWoodRoomDSP` to `mods/house/atoms/KWoodRoom.h` as a
   proper `od::Object` subclass with inlets/outlets/parameters.
   The existing per-sample DSP body is preserved verbatim;
   only the wrapper changes.
2. Wire `mods/house/assets/KWoodRoom.lua` as a thin unit that
   just instantiates the atom and exposes parameters via the
   standard GainBias ply pattern.
3. Smoketest unit stays for Phase 0 of future atoms (each new
   atom can be Smoketest-gated on hardware before going into a
   real chain).

## Where this fits in the broader plan

This architecture doc complements:
- `planning/airwindows-reverb-research.md` (which reverbs to
  port)
- `planning/airwindows-primitives-inventory.md` (which
  tone-shaping atoms to port)
- `planning/reverb-design-philosophy.md` (combination mechanics
  for composing chains)
- `planning/kwoodroom-port-plan.md` (Phase 0 → 8 walkthrough
  for the first port)

The atom architecture is the connective tissue that lets all of
that be implemented incrementally — one atom at a time, one
chain at a time, with the rebuild loop tight enough to iterate
on chains as a design exercise rather than a code project.
