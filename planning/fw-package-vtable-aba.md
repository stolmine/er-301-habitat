# Package/firmware vtable ABI fragility — solution sketches

**Audience:** ER-301 SDK / firmware maintainers.
**Source:** habitat repo, accumulated incidents 2026-04-28 → 2026-05-03.
**Drop location suggestion:** `er-301/docs/planning/package-abi-stability.md` or similar.

## Problem statement

ER-301's plugin model loads packages as ARM ELF `.so` files into a running firmware ELF. Both binaries share C++ class hierarchies rooted in `od::*` (notably `od::Object`, `od::Graphic`, `od::Followable`). When a package class subclasses one of these and overrides virtuals, the package's vtable layout depends on the firmware's vtable layout at the time the package was compiled.

GCC's "key function" rule emits the vtable in a single TU when the class has any non-inline virtual. With the current SDK:

- Firmware-side `od::Object`/`od::Graphic` vtables emit COMDAT (vague-linkage) per their inlined-virtual style.
- Package-side derived classes that have **any** out-of-line virtual emit their derived vtable in normal (strong) linkage in a single `.o`.

When the firmware's vtable layout shifts between the package's compile time and runtime — e.g. SDK adds a new virtual, reorders, deletes one — the package's compiled-in offsets to inherited slots no longer match, and cross-binary virtual dispatch hard-faults on Cortex-A8 / am335x. Symptom: hard-fault on insert, on first `process()` call, or on first virtual call from firmware code into a package-allocated instance.

### Empirical evidence (habitat repo)

- **DrumCubeGraphic** (Ngoma): out-of-line `draw()` in `DrumCubeGraphic.cpp` → hard-fault on insert after firmware rebuild. Fixed by moving to header-only inline; vtable becomes COMDAT.
- **AlembicSphereGraphic** (Alembic): same pattern; pre-emptively migrated before crash surfaced.
- **Visadhara**: `od::Object` subclass. Phase 1 had out-of-line `process()` and worked; Phase 2 added new code in `process()` and **crashed on hardware insert** with no lint hits and no apparent surface change. Empirically confirms the rule applies to `od::Object`, not just graphics.
- Other Object subclasses (DrumVoice, Helicase, JF, Pecto, etc.) currently work but are silently at risk — they rely on the firmware's vtable layout not shifting.

The rule "all virtuals must be inline in the header" works as a workaround because COMDAT vtables are emitted per-TU by every consumer; the linker dedups within each binary; and cross-binary virtual dispatch has no shared vtable address to be stale against.

## Solutions to consider

Listed roughly cheapest → most invasive.

### 1. Documentation + lint (current habitat workaround)

**What:** Document "all virtuals inline" as the canonical pattern. Provide a lint script that flags `<type> Class::virtualMethod(` patterns in package `.cpp` files. Migrate existing units one-by-one as risk is identified.

**Cost:** Zero firmware change. Per-package audit and refactor effort. Compile-time grows in package SWIG TUs.

**Pro:**
- No SDK breakage.
- Provably effective (Visadhara confirmed).
- Each package author can apply unilaterally.

**Con:**
- Treats symptom, not cause.
- Easy to forget; a single regression silently re-introduces the trap.
- Doesn't help legacy packages built against older SDK headers.
- Compiler version sensitive — a future GCC could surface different layouts even with all-inline.

**Verdict:** Adequate near-term ceiling for habitat; not a permanent SDK answer.

### 2. Frozen/versioned vtable contract

**What:** Declare `od::Object`/`od::Graphic` vtable layouts **stable**. Rules:
- New virtuals only ever appended at the end of the class.
- Existing virtuals never reordered, deleted, or changed in signature.
- Bump a `kFrameworkVtableVersion` constant on any incompatible shift; refuse to load packages compiled against a different version.

**Cost:** Discipline on every SDK change. A version-check at package load time (`dlopen` + symbol probe).

**Pro:**
- Packages can keep current ABI without per-class header acrobatics.
- Mismatched packages get a clean error rather than hard-fault.
- Backward-compatible with all existing code patterns (out-of-line virtuals OK as long as ABI is preserved).

**Con:**
- Constrains future SDK evolution.
- Version-bump becomes a release-notes liability — every package must rebuild.
- Doesn't help during the version window where mismatches happen; just turns crash into refusal.

**Verdict:** Good middle ground; usable alongside option 4 below.

### 3. Pure-virtual interface ABI

**What:** Refactor `od::Object`/`od::Graphic` so the parts the firmware reaches across binary boundary are an **abstract interface** (all-pure-virtual class with no data members). Concrete behavior moves into the firmware-side default impl. Packages implement the interface; no inheritance from a class with concrete data + virtual mix.

**Cost:** Significant SDK refactor. Affects every existing package.

**Pro:**
- Pure-virtual classes don't have key-function vtables — they're forced COMDAT.
- Makes the cross-binary contract explicit and minimal.
- Aligns with COM-style ABI patterns proven in long-lived plugin systems.

**Con:**
- Massive breaking change.
- Loses convenience of inheriting concrete behavior from firmware base classes.
- Packages may need to compose rather than inherit, which feels foreign in this codebase.

**Verdict:** Architecturally cleanest; too disruptive for the maturity stage.

### 4. C ABI shim layer

**What:** Package-firmware boundary exposed as a C function-pointer table, not a C++ class hierarchy. Each `od::Object`-equivalent operation gets a C entry point; firmware translates inside its own walls. Package authors write C++ classes that implement a `static const ObjectVTable kVTable = {...}` table or similar.

**Cost:** Wholesale plugin-API redesign. Probably a v2-of-the-SDK effort.

**Pro:**
- C ABI is the most portable, most stable cross-binary contract that exists.
- Removes GCC-specific key-function fragility entirely.
- Survives compiler version changes, target rebuilds, even cross-compiler scenarios.

**Con:**
- Big architectural commitment.
- Loses C++ ergonomics at the API boundary (or papers them with macros).
- Existing packages need full rewrites, not just per-class fixes.

**Verdict:** Right answer for v3 / clean-slate redesign; not a near-term option.

### 5. Header-only base classes (firmware-side prevention)

**What:** Make `od::Object`/`od::Graphic`/etc. themselves have all-inline virtuals (or use `=default` for trivial dtors). Forces COMDAT vtables on the **base** classes. Packages still must follow the all-inline rule for their own derived classes, but the firmware side stops being a fixed-position vtable target.

**Cost:** Inline currently-out-of-line firmware virtuals. May bloat firmware compile time and binary size modestly.

**Pro:**
- Reduces the asymmetry between firmware and package vtable linkage.
- Compatible with current package-author patterns.
- Useful adjunct to option 1 — improves the workaround's reliability.

**Con:**
- Doesn't fix package-side out-of-line virtuals (still need lint or rule).
- Firmware base classes may have legitimately large virtual bodies that shouldn't be inline (compile time).
- Some ABI fragility remains if firmware adds/reorders virtuals on the base classes.

**Verdict:** Worth doing in tandem with option 2; cheap insurance.

### 6. Runtime vtable validation at package load

**What:** Each package exports a `kBuiltAgainstVtables` blob (offsets/hashes of expected `od::Object` and `od::Graphic` vtable layouts). Firmware computes the actual layout at load time, compares, refuses to load on mismatch with a clear error.

**Cost:** A few hundred lines in firmware loader; build-time tooling to extract the package's expected layout.

**Pro:**
- Hard-fault → diagnostic refuse-to-load.
- Works orthogonally to options 1, 2, 5 — any of those can fail the check.
- Gives package authors a clean signal that they need to rebuild.

**Con:**
- Doesn't fix anything; just changes the failure mode.
- Adds load-time complexity.
- Packages still don't load on mismatch — user-visible breakage just with better error.

**Verdict:** Good safety net; minimal effort relative to the other options.

## Recommended path

Combine the cheapest options for immediate relief, plan the structural work for later:

1. **Now (habitat-side):** keep enforcing the all-inline rule via lint + memory + authoring guide. Migrate existing at-risk units as bandwidth permits, prioritizing units that touch user-facing paths (graphics first).
2. **Next SDK release:** adopt option 2 (frozen vtable contract) + option 5 (firmware-side header-only base classes). Document the contract in the SDK README. This pairs the lightest enforceable rules with the strongest base-side guarantees.
3. **Defensive layer:** add option 6 (runtime vtable validation). One-time loader work; converts the entire class of failure from hard-fault to clean refusal.
4. **Long-term:** option 3 or 4 if the SDK ever undergoes a major redesign; not urgent.

## Empirical / diagnostic notes

The lint script `tools/check-graphic-virtual-defs.sh` in habitat currently flags `<type> Class::virtualMethod(` patterns in package `.cpp` files for a fixed list of `od::Graphic` virtuals (`draw`, `notifyVisible`, `notifyHidden`, `notifyContentsChanged`, `setSize`, `setPosition`). The Visadhara incident showed this list is incomplete — it should also flag `process` and probably `serialize`/`deserialize` for `od::Object`. SDK-side, the canonical list is whatever virtuals are actually declared in the `od::*` base classes; a script that introspects framework headers for `virtual` keywords would be more robust than a hard-coded list.

To verify a class has the safe linkage:
```bash
arm-none-eabi-nm testing/am335x/mods/<pkg>/<pkg>_swig.o | grep -E "<ClassName>|vtable|typeinfo"
# All vtable / typeinfo / virtual method symbols should show 'W' (weak) linkage.
# A 'T' (text/strong) on `vtable for ClassName` is the trap.
```

A `<ClassName>.o` file should not exist in `testing/<arch>/mods/<pkg>/` for any package class subclassing an `od::*` framework type. If it does, that class has out-of-line code generating a non-COMDAT vtable.

## Cross-references (in habitat repo)

- `feedback_no_out_of_line_virtuals.md` — the working memory on the rule.
- `tools/check-graphic-virtual-defs.sh` — the lint script.
- `docs/graphics-authoring-guide.md` — package-author guide (currently graphic-focused; should be generalized).
- Reference fixes: `mods/spreadsheet/DrumCubeGraphic.h`, `mods/catchall/AlembicSphereGraphic.h`, `mods/spreadsheet/Visadhara.h` — three working examples of the all-inline pattern, two graphics + one Object subclass.
