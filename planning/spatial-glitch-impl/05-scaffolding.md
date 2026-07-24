# 05 — Package / atom / unit scaffolding (new CM4 package)

The mechanical checklist for standing up the new package. All patterns verified against
`mods/zaum/`, `mods/house/`, `mods/spreadsheet/` (code-finder sweep, 2026-06-25).
Codename TBD (NOT "Mood", no third-party branding) — call the package `<pkg>` below.

## Internal-stereo decision (locked)
One `od::Object` holding shared coherent L/R state ("Pattern B"), NOT dual mono instances —
the spatial field is one shared L/R field with cross-coupling. Template: `CreamCoat.h` /
`APFTank.h` (`addInput(mInL/mInR)`, `addOutput(mOutL/mOutR)`, parallel per-channel state,
`mFeedback_L` fed from R and vice-versa, single `process()` loop).

## Files to create
```
mods/<pkg>/mod.mk
mods/<pkg>/<pkg>.cpp.swig
mods/<pkg>/atoms/<Engine>.h          # the internal-stereo od::Object(s)
mods/<pkg>/atoms/...                  # reuse via INCLUDES: Spiral.h, etc.
mods/<pkg>/assets/<Unit>.lua          # the unit composition
mods/<pkg>/assets/...                 # toc.lua / package registration as per other pkgs
```

### mod.mk (ref `mods/zaum/mod.mk`)
- `PKGNAME ?= <pkg>`
- `PKGVERSION ?= 0.0.0.1` (Major.minor.patch.sub — bump sub per audition step)
- `INCLUDES += $(MOD_DIR) mods mods/house/atoms eurorack eurorack/stmlib $(SDKPATH)`
  (cross-package atom reuse: Spiral, Clouds DSP, stmlib delay/interp)
- `SWIG_HEADER_DEPS := $(call rwildcard,$(MOD_DIR),*.h)`

### <pkg>.cpp.swig (ref `mods/zaum/zaum.cpp.swig`)
```
%module <pkg>_lib<pkg>
%include <od/glue/mod.cpp.swig>
%{ #undef SWIGLUA
   #include "atoms/<Engine>.h"
   #define SWIGLUA %}
%include "atoms/<Engine>.h"
```
Component-only helpers (e.g. Spiral) stay wrapped in `#ifndef SWIGLUA` and are NOT
`%include`d; only od::Objects exposed to Lua are.

### Atom header shape (ref `RotCoat.h`, `CreamCoat.h`)
```cpp
#pragma once
#include <od/config.h>
#include <od/objects/Object.h>
namespace <pkg> {
class <Engine> : public od::Object {
public:
  <Engine>() {
    addInput(mInL); addInput(mInR);
    addOutput(mOutL); addOutput(mOutR);
    addParameter(mClock); addParameter(mRegen); /* ... */
    // state init (memset arrays, counters), FPCR FZ set on first process()
  }
  virtual ~<Engine>() {}
#ifndef SWIGLUA
  od::Inlet  mInL{"In L"};  od::Inlet  mInR{"In R"};
  od::Outlet mOutL{"Out L"}; od::Outlet mOutR{"Out R"};
  od::Parameter mClock{"Clock", 0.5f}; /* ... */
  virtual void process();             // FRAMELENGTH loop; all state = members
private:
  float mBuf[/*...*/]; double mFeedbackL, mFeedbackR; /* :64 NEON-aligned big arrays */
#endif
};
} // namespace <pkg>
```
Big buffers: BigHeap-allocate (ref Pecto/Network int16 circular buffers) rather than inline
if large. Keep `process()` inline header-only (no out-of-line virtuals).

### Lua unit (ref `Sujet.lua`, `WoodenBox.lua`)
- `onLoadGraph`: `addObject("op", lib<pkg>.<Engine>())`; `connect` In1/Out1 (+In2/Out2 if
  stereo, else mono fan-out In1→both inlets so the field spreads mono sources).
- Per param: `ParameterAdapter` + `hardSet("Bias",default)` + `tie(op,"Param",ad,"Out")` +
  `addMonoBranch`.
- `onLoadViews`: GainBias plies; `expanded`/`collapsed` lists.
- The unit is the 6-ply surface (Looper · Field · Regen · Clock · Mix · Routing) — but build
  the engine first with a flat param surface and add the adaptive per-mode UI later.

### Multi-out framework (ref `Mirror.lua`)
For dry-loop / wet-field / per-stage taps: `args.channelCount=N`,
`args.subOutLabels={...}`, write each outlet `.buffer()` in `process()`, `connect(op,"OutX",
self,"OutN")` AFTER params are set up.

## Build / install (darwin emu; CM4 is the real target)
Same procedure as Zaum (`project_zaum_darwin_install` memory, `docs/dev-rig-procedures.md`):
`make <pkg>` → copy pkg to `~/.od/front/ER-301/packages/` AND unzip into
`~/.od/rear/v0.7/libs/<pkg>/`, restart emu, re-add unit. am335x cross-compile unavailable on
this Mac (SWIG missing) — CM4-only by design anyway.

## First-light checklist (subsystem 0)
1. New package builds + loads (identity passthrough unit, Mix only).
2. Internal-stereo I/O wired (In L/R → Out L/R), mono fan-out works.
3. FPCR FZ set in `process()`.
4. Then start the first real subsystem per `99-build-order.md`.
