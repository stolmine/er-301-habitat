# Graphics Authoring Guide

How to write custom graphics (3D viz, custom indicators, parameter overlays, etc.) for habitat units that won't break on hardware after firmware rebuilds.

This guide is written from incidents — every rule below was learned the hard way after a crash. If you're tempted to skip a rule, read its "Why" section first.

## TL;DR — The one rule that prevents most pain

**Custom graphics that subclass `od::Graphic` (or any other framework type with virtual functions) must define ALL their methods inline in the header. No `.cpp` file.**

If you remember nothing else, remember that. The rest of this doc explains why and how.

## Why custom graphics are fragile

The ER-301 plugin model has a structural fragility you can't avoid by being careful: the firmware (kernel ELF, `app.bin`) and your package (`libyourmod.so`) are **two separate binaries** built at potentially different times, possibly with subtly different toolchain state, that have to share C++ class layouts and vtables across the boundary.

When a Lua wrapper does:
```lua
local cube = libspreadsheet.DrumCubeGraphic(...)
local container = app.Graphic(0, 0, ply, 64)
container:addChild(cube)
```

the call to `addChild` is a **firmware-compiled** function that takes a `Graphic*` pointer to your **package-allocated** instance. The firmware reads the cube's `mpParent` field, calls `attach()` on it, walks its vtable. Every one of those operations depends on the firmware and the package agreeing exactly on the cube's class layout AND vtable resolution semantics.

That agreement is not free. It depends on:

1. The cube's class definition matching what the firmware expects of any `od::Graphic` subclass.
2. The cube's vtable being emitted with **vague linkage** (COMDAT) — the same linkage flavour that the firmware uses for its own base-class vtables.
3. The cube's class size matching the firmware's idea of the same class size.
4. None of the cube's members being subject to compiler-conditional layout (e.g. `#ifdef`-guarded fields).

Most of these line up automatically if you follow the conventions. One does not: **vtable linkage is determined by GCC's "key function" rule**, and getting it wrong causes crashes that are hard to diagnose and tend to come back across firmware rebuilds.

## The key function rule

GCC's rule for emitting a class's vtable + typeinfo:

- If the class has any virtual function defined **out-of-line** (in a `.cpp`, not in the header), GCC picks the **first such method** as the class's "key function" and emits the vtable + typeinfo in **only that one TU**, with normal (strong) linkage.
- If all virtual functions are defined **inline** (in the header), no key function exists. The vtable + typeinfo emit with **COMDAT (vague) linkage** in every TU that uses the class. The linker dedups across TUs and across binaries.

The firmware emits its own base-class vtables (`od::Graphic`, `od::Object`, etc.) with vague linkage. When your package subclass does the same (all-inline → COMDAT), symbol resolution lines up cleanly across the firmware/package boundary.

When your subclass uses the key function pattern, its vtable lives in the package's binary at an address resolved by the package's own linker run, not visible to the firmware via the same vague-linkage mechanism. **On Cortex-A8 + gcc 4.9.3, this layout combination hard-faults** when firmware-compiled code dispatches through your subclass's vtable or accesses inherited fields. We've reproduced this specifically:

- DrumCubeGraphic crashed on `addChild` for years (the `.175` Ngoma codex `xform-removal` fix was treating the symptom). Root cause was `void DrumCubeGraphic::draw(...)` defined out-of-line in `DrumCubeGraphic.cpp`. Moving the body inline into the header fixed it permanently.
- AlembicSphereGraphic has the same shape (`mods/catchall/AlembicSphereGraphic.cpp:257` defines `draw()` out-of-line). Currently works by luck. The lint script `tools/check-graphic-virtual-defs.sh` flags it. **Refactor before next release.**

## The correct pattern

```cpp
// MyGraphic.h — the entire class lives here.
#pragma once

#include <od/graphics/Graphic.h>
#include "FilterResponseGraphic.h"  // for lutSin / lutCos if you need trig

namespace mypkg
{
  class MyGraphic : public od::Graphic
  {
  public:
    MyGraphic(int left, int bottom, int width, int height)
        : od::Graphic(left, bottom, width, height),
          mpSomething(0),
          mAccumulator(0.0f)
    {}

    virtual ~MyGraphic()
    {
      if (mpSomething)
        mpSomething->release();
    }

    void follow(MyDataSource *p)
    {
      if (mpSomething) mpSomething->release();
      mpSomething = p;
      if (mpSomething) mpSomething->attach();
    }

#ifndef SWIGLUA
    virtual void draw(od::FrameBuffer &fb)
    {
      // Entire body — however long. All inline.
      fb.fill(BLACK, mWorldLeft, mWorldBottom,
              mWorldLeft + mWidth - 1, mWorldBottom + mHeight - 1);

      // Use lutSin/lutCos, not sinf/cosf — see "trig" section below.
      float c = lutCos(mAccumulator);
      // ...
    }

  private:
    MyDataSource *mpSomething;
    float mAccumulator;

    // Helpers called from draw must also be inline.
    void renderHelper(od::FrameBuffer &fb, ...) { ... }
#endif
  };
}
```

No `.cpp` file. Build artifact: no `MyGraphic.o`. Vtable: COMDAT, weak linkage. Verify:

```bash
arm-none-eabi-nm testing/am335x/mods/<pkg>/<pkg>_swig.o | grep -E "MyGraphic|vtable|typeinfo"
# Expect "W" (weak) or "V" (vague) symbols. NOT "T" or "R".
```

## Anti-patterns to avoid

### 1. Virtual function bodies in a `.cpp`

The cardinal sin. Documented at length above. Any of these is a bug:

```cpp
// MyGraphic.cpp
void MyGraphic::draw(od::FrameBuffer &fb) { ... }  // KEY FUNCTION → trap
MyGraphic::~MyGraphic() { ... }                    // also virtual → also trap
void MyGraphic::notifyVisible() { ... }            // ditto
void MyGraphic::setSize(int w, int h, bool s) { ... }
void MyGraphic::setPosition(int l, int b) { ... }
```

If any of those appear in a package `.cpp`, the lint script will flag it.

### 2. Stack-local NEON arrays in `draw()`

```cpp
virtual void draw(od::FrameBuffer &fb) {
  alignas(16) float buf[4] = {1.0f, 2.0f, 3.0f, 4.0f};   // BAD
  float32x4_t v = vld1q_f32(buf);                        // emits :64 hint, traps
  ...
}
```

Stack-local NEON-loaded arrays trigger GCC `:64` alignment hints that trap on Cortex-A8. See `feedback_neon_intrinsics_drumvoice.md` for details. Fix: declare the working memory as class members (heap-allocated), not stack locals.

### 3. `sinf` / `cosf` in `draw()`

```cpp
virtual void draw(od::FrameBuffer &fb) {
  float c = cosf(mAngle);   // BAD: package .so libm trig miscomputes on am335x
  float s = sinf(mAngle);
  ...
}
```

The package-side `.so` libm trig miscomputes silently on am335x. The result is usually visual-only (graphic looks malformed), but in the cube case it can interact with `(int)NaN` cast undefined behavior to produce out-of-bounds writes. Use `lutSin` / `lutCos` from `mods/spreadsheet/FilterResponseGraphic.h` (a 72-entry LUT). Reference `feedback_package_trig_lut.md`.

### 4. Auto-vectorized init code in member-init lists

```cpp
class MyClass : public od::Object {
  od::Parameter *mP1 = nullptr;   // multiple of these → BAD
  od::Parameter *mP2 = nullptr;
  od::Parameter *mP3 = nullptr;
  od::Parameter *mP4 = nullptr;
  // GCC auto-vec'd this into vst1.64 :64 stores → trap
};
```

The Ngoma `.165` lesson: many contiguous in-class default initializers get auto-vectorized into `:64`-hinted NEON stores. Fix: drop the in-class defaults, do a single `memset` in the ctor body. See `feedback_neon_intrinsics_drumvoice.md`.

### 5. Custom `od::Object` subclasses with out-of-line virtual `process()`

The same key-function rule applies to `od::Object` subclasses, not just `od::Graphic`. This anti-pattern is rarer because most package units have `process()` inline already, but if you write:

```cpp
// MyOscillator.h
class MyOscillator : public od::Object {
  virtual void process();  // declared
};
// MyOscillator.cpp
void MyOscillator::process() { ... }   // out-of-line → KEY FUNCTION → at risk
```

… you have a latent insert-crash on the next firmware rebuild. Move the body inline.

(Most unit `process()` bodies are large. Inline doesn't mean "one-liner" — it means "in the header." A 200-line `process()` in a header is fine. The linker dedups COMDAT bodies the same way it dedups COMDAT vtables.)

## How to write a custom graphic from scratch

1. Pick an existing all-inline graphic as your template. Good references:
   - `mods/spreadsheet/HelicaseOrbitalGraphic.h` — 3D-projected polyline orbital viz
   - `mods/spreadsheet/HelicasePhaseGraphic.h` — phase-space viz with state caching
   - `mods/spreadsheet/ColmatageOverviewGraphic.h` — BSP tile field
   - `mods/spreadsheet/DrumCubeGraphic.h` — 3D rotating cube (post-fix reference)
2. Copy the file. Rename the class. Strip the body to a stub.
3. Add your data members in the `#ifndef SWIGLUA` private section.
4. Add a `follow(...)` method (inline) if you need to track a DSP state.
5. Implement `draw()` inline.
6. **Do not create a `.cpp` file.** If you find yourself wanting one, reread "The key function rule" above.
7. Add `#include "MyGraphic.h"` to your package's `<pkg>.cpp.swig` in both the `%{` block and the `%include` block.
8. Use it from Lua: `local g = lib<pkg>.MyGraphic(0, 0, ply, 64)` etc.

## How to verify before shipping

Before any release that touches custom graphics:

```bash
# 1. Lint scan — fails on out-of-line virtuals.
tools/check-graphic-virtual-defs.sh

# 2. Build for am335x with force-clean SWIG (catches stale wrapper).
rm -f testing/am335x/mods/<pkg>/<pkg>_swig.{cpp,o}
make ARCH=am335x PROFILE=testing PROJECT=<pkg>

# 3. Hint check — flags :64 NEON traps from auto-vec / register spills.
tools/check-neon-hints.sh testing/am335x/mods/<pkg>/<unit>.o

# 4. Cross-check: your custom graphic's class should have no .o file.
ls testing/am335x/mods/<pkg>/MyGraphic.o   # expect: not found

# 5. Cross-check: vtable should be in the wrapper TU with weak linkage.
arm-none-eabi-nm testing/am335x/mods/<pkg>/<pkg>_swig.o | grep MyGraphic
# Expect "W" / "V" linkage entries; NOT "T" / "R".
```

## Reference patterns by use case

### "I want a custom data-driven 2D viz"
`HelicaseOrbitalGraphic.h` / `HelicasePhaseGraphic.h`. Polyline + circle math + state caching, all inline.

### "I want a parameter-driven 3D wireframe"
`DrumCubeGraphic.h` (post-fix). Vertex array + projection matrix + face culling, all inline.

### "I want a tile / field viz"
`ColmatageOverviewGraphic.h`. BSP tree of rectangles, state-cached frame-to-frame.

### "I want a spectrum / histogram"
`mods/spreadsheet/SpectrumGraphic.h`, `CompressorSpectrumGraphic.h`. Bin-array driven.

### "I want a list / step display"
`StepListGraphic.h`, `LaretStepListGraphic.h`, `BandListGraphic.h`. List of items + scroll cursor.

All header-only. All COMDAT vtables. All shipped to hardware without insert-crashes.

## Related memories

- `feedback_no_out_of_line_virtuals.md` — the rule, in detail, with the bisect history.
- `feedback_no_lazy_paths.md` — when a graphic crashes, diagnose the mechanism, do not strip the graphic. The cube was almost stripped permanently before the root cause was pinned.
- `feedback_neon_intrinsics_drumvoice.md` — stack-local NEON arrays / large in-class init lists trigger `:64` traps.
- `feedback_neon_hint_surfaces.md` — register-pressure spills + auto-vec init also emit trap-prone NEON hints.
- `feedback_package_trig_lut.md` — package-side `sinf`/`cosf` miscomputes; use the 72-entry LUT.
- `feedback_swig_header_dep.md` — when editing the header of a SWIG-exposed class, force-clean the wrapper.

## Adding to this guide

If you discover a new graphics anti-pattern (or a pattern that works), add it here. The pattern of incidents → memory → guide is what keeps future authors out of the same traps.
