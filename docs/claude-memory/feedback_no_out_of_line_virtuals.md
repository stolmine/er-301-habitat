---
name: Out-of-line virtuals on framework subclasses crash on hardware
description: Any package C++ class that inherits from a framework type (od::Graphic, od::Object, od::Followable, od::ReferenceCounted, etc.) and defines a virtual function override out-of-line in a .cpp triggers GCC's "key function" rule. Vtable + typeinfo emit in non-COMDAT linkage in a single .o, mis-resolve across firmware rebuilds, and hard-fault on cross-binary virtual dispatch (firmware code reading the package-allocated instance via base-class pointer). Empirically pinned on Cortex-A8 / gcc 4.9.3 / am335x. Rule applies to ALL framework subclasses, not just graphics.
type: feedback
originSessionId: bd738562-37e5-4b2a-9bdc-85a9feb561af
---
## The rule

Any package C++ class that inherits from a framework type (anything declared in `od::*`) MUST define ALL its virtual function overrides **inline in the header**. Including:

- The destructor (`virtual ~MyClass() { ... }`).
- Any virtual override (`virtual void draw(...) override { ... }`, `virtual void process() { ... }`, etc.).
- The constructor — defined inline with member initializer list.
- Any non-virtual helper called from inline virtual bodies, also inline.

Practical consequence: **the class should have no `.cpp` file at all.** Build artifact: no `<ClassName>.o` in `testing/<arch>/mods/<pkg>/`. All symbols (`vtable for ...`, `typeinfo for ...`, virtual method bodies, helpers) emit with `W` (weak) / `V` (vague-linkage) in `nm` of the SWIG wrapper's `.o`.

## Why

GCC's "key function" rule: if a class has any non-inline virtual function definition, GCC picks the **first one** as the class's key function and emits the vtable + typeinfo **only in that one TU**. The resulting symbols are emitted with normal (strong) linkage in a single `.o`.

When all virtuals are inline, no key function exists. Vtable + typeinfo are emitted in **COMDAT (vague-linkage)** sections in every TU that uses the class. Linker dedups across the boundary cleanly.

The firmware emits its own base-class vtables (`od::Graphic`, `od::Object`, etc.) with vague linkage. When a package class subclasses one of those base types and emits its derived vtable with normal linkage in a single `.o`, the cross-binary symbol resolution path differs. On Cortex-A8 / gcc 4.9.3, this layout combination hard-faults on virtual dispatch from firmware code into the package-allocated instance — most commonly during `addChild` (for Graphic subclasses), `process()` from a chain (for Object subclasses), or any other path where firmware code reads/writes inherited fields or invokes virtuals.

The exact transition mechanism remains hand-wavy (likely related to how the dynamic linker resolves vtable addresses for classes whose vtables aren't COMDAT'd between the kernel and the package `.so`), but the empirical rule is universal and easy to enforce.

## Scope

This rule applies to **any** framework-derived class in package code, not just graphics:

- `od::Graphic` subclasses (custom viz, indicators, controls)
- `od::Object` subclasses (DSP units — though these are usually OK because they tend to be all-inline already)
- `od::Followable`, `od::Parameter`, `od::Option` subclasses (rare but possible)
- `od::Task` subclasses
- Any other `od::*` base with virtual functions

If the base class has virtual functions and the package extends it, the package's class must keep ALL virtuals inline.

## How to verify

```bash
# After build, check that the class has no .o file and its vtable is in
# the wrapper TU with weak linkage:
arm-none-eabi-nm testing/am335x/mods/<pkg>/<pkg>_swig.o | grep -E "<YourClassName>|vtable|typeinfo"

# Lint the whole repo for the anti-pattern:
tools/check-graphic-virtual-defs.sh
```

The lint script flags any `<type> Class::virtualMethodName(` definition in package `.cpp` files. The set of flagged virtual method names matches the framework's known vtables (draw, notifyVisible, notifyHidden, setSize, setPosition, notifyContentsChanged, etc.). Extend the list when new framework virtual surfaces are added.

## Counter-example (the trap)

```cpp
// MyGraphic.h
class MyGraphic : public od::Graphic {
public:
  MyGraphic(int l, int b, int w, int h);
  virtual ~MyGraphic();
  virtual void draw(od::FrameBuffer &fb);     // declared, not defined — TRAP
};
```

```cpp
// MyGraphic.cpp
MyGraphic::MyGraphic(int l, int b, int w, int h) : od::Graphic(l, b, w, h) {}
MyGraphic::~MyGraphic() {}
void MyGraphic::draw(od::FrameBuffer &fb) { ... }   // out-of-line — KEY FUNCTION
```

Result: vtable emits in `MyGraphic.o` only. **Hard-faults on insert** under firmware rebuilds.

## Correct pattern

```cpp
// MyGraphic.h — entire class inline
class MyGraphic : public od::Graphic {
public:
  MyGraphic(int l, int b, int w, int h)
      : od::Graphic(l, b, w, h),
        mpSomething(0)
  {}

  virtual ~MyGraphic() {
    if (mpSomething) mpSomething->release();
  }

#ifndef SWIGLUA
  virtual void draw(od::FrameBuffer &fb) {
    // Entire body inline, however long.
  }

private:
  Something *mpSomething;
  void helperMethod(...) { ... }   // Inline too.
#endif
};
```

No `.cpp` file. No `MyGraphic.o`. Vtable: COMDAT.

## Reference incidents

- **DrumCubeGraphic insert-crash** (2026-04-28 → 2026-05-01): Ngoma's rotating-cube viz on the Character ply was the only spreadsheet graphic with `draw()` defined out-of-line. Crashed on hardware insert after firmware rebuilds. Bisect path: `spreadsheet-2.6.0.18..22`. Code-pattern bisect via Lua-side step-by-step pinpointed `container:addChild(cube)` as the trigger. Tried matching Helicase's pattern in every other respect (inline ctor + dtor + member init list) — still crashed at `.21`. Final fix at `.22`: move entire DrumCubeGraphic into header (no .cpp), making it COMDAT-vtable like every other working graphic. **Loaded cleanly on hardware.**
- **Earlier `.175` Ngoma "fix"**: removed xform/randomize functionality to dodge the same crash class. That was treating the symptom; the root cause was the cube graphic's out-of-line virtual all along. See also `feedback_no_lazy_paths.md`.
- **AlembicSphereGraphic** (`mods/catchall/AlembicSphereGraphic.cpp:257` defines `draw()` out-of-line): same anti-pattern. Currently working on the user's hardware (linker / fw rebuild luck), but at risk on any future rebuild. Lint flags it. Refactor to header-only before next release.
- **Visadhara crash on Phase 2** (2026-05-03): empirically confirmed the rule applies to `od::Object` subclasses, not just `od::Graphic`. Phase 1 (skeleton) had `process()` and dtor out-of-line and worked. Phase 2 added new params + new code in process() body and **crashed on hardware insert**. Lint was clean (only graphic virtuals listed). Fix: moved Internal struct, ctor, dtor, and process() body all inline into `Visadhara.h`; deleted `Visadhara.cpp`. Loaded cleanly. Confirms the working theory: out-of-line virtuals on **any** od::* subclass are at risk; the units that "work" (DrumVoice, Helicase, JF) are silently relying on linker/fw-rebuild luck and could break on the next firmware vtable shift. Visadhara is now the **canonical pattern** for new package C++ classes. Existing out-of-line units should be migrated pre-emptively before the next firmware ABI shift surfaces them.

## Pre-emptive migration list (units silently at risk)

These units have process() (or other virtuals) defined out-of-line in `.cpp` files. They currently load on the user's hardware but could break on the next firmware rebuild that shifts an `od::Object`/`od::Graphic` vtable slot:

- `mods/spreadsheet/DrumVoice.{h,cpp}` (Ngoma)
- `mods/spreadsheet/Helicase.{h,cpp}`
- `mods/spreadsheet/JF.{h,cpp}`
- `mods/spreadsheet/Pecto.{h,cpp}`
- `mods/spreadsheet/MultitapDelay.{h,cpp}` (Petrichor)
- `mods/spreadsheet/Etcher.{h,cpp}`, `MultibandSaturator.{h,cpp}` (Parfait), `MultibandCompressor.{h,cpp}` (Impasto), `Filterbank.{h,cpp}` (Tomograph), `TrackerSeq.{h,cpp}` (Excel), `GateSeq.{h,cpp}` (Ballot), `Larets.{h,cpp}`, `Blanda.{h,cpp}`, `Colmatage.{h,cpp}`, `Rauschen.{h,cpp}`
- `mods/catchall/Alembic.{h,cpp}`
- `mods/biome/*` units with .cpp/.h splits

Migration cost per unit: typically straightforward (move Internal/ctor/dtor/process into the header) but large for units with multi-hundred-line process() bodies. Assess on a per-unit basis. The lint script needs an extension to flag all od::Object virtuals (`process`, `populateGraph`, `serialize`, `deserialize`, etc.) in addition to the current od::Graphic set.

## Lint script

`tools/check-graphic-virtual-defs.sh` — scans `mods/` for `<type> Class::virtualMethod(` patterns where the method name matches a known framework virtual. Exits non-zero on hit. Add to pre-release / CI checks. Extend the method list as the framework adds new virtual surfaces.

## Why a memory rather than a workaround

This isn't a toolchain bug we're working around — it's how GCC has always emitted vtables under the key function rule. It interacts badly with the dual-binary plugin model (firmware ELF + package `.so`) that ER-301 uses. The rule is universal: **no out-of-line virtuals in package code that subclasses framework types**. Easy to enforce, no exceptions needed, no performance cost (inline functions get COMDAT-deduplicated by the linker).
