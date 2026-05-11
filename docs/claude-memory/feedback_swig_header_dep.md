---
name: SWIG wrapper must be regenerated when a %include'd header changes
description: Editing a header exposed through spreadsheet.cpp.swig (e.g. Blanda.h) does NOT trigger a SWIG regen — the Makefile deps only the .swig source. A stale wrapper compiled with the old sizeof() allocates too-small objects, corrupts the heap, and surfaces as seemingly unrelated malloc/free crashes later (on delete, quicksave, or next alloc).
type: feedback
originSessionId: 6486de73-452f-466c-9c43-03152c66fce2
---
## The rule

Whenever a header that is `%include`d by `mods/spreadsheet/spreadsheet.cpp.swig` (currently: `Blanda.h`, `BlandaInputGraphic.h`, and everything else listed in the `%include` block at the top of that file) is modified — especially when adding/removing fields like a new `od::Parameter` — the SWIG wrapper must be force-regenerated before building:

```
rm testing/linux/mods/spreadsheet/spreadsheet_swig.{cpp,o}
rm testing/am335x/mods/spreadsheet/spreadsheet_swig.{cpp,o}
make spreadsheet ARCH=linux
make spreadsheet ARCH=am335x
```

Then for the emu, also overwrite the already-extracted copy if same-version reload won't re-extract:

```
cp testing/linux/libspreadsheet.so ~/.od/rear/v0.7/libs/spreadsheet/libspreadsheet.so
```

## Why this bites

`mods/spreadsheet/mod.mk` defines the SWIG wrapper build rule as:

```
$(SWIG_WRAPPER): $(SWIG_SOURCE)
```

Only the `.cpp.swig` file itself is a dependency. The headers that `.cpp.swig` `%include`s are *not* in the dep list, so touching `Blanda.h` does not retrigger SWIG. The wrapper keeps an out-of-date view of the class — missing new getter method bindings, wrong method offsets for anything SWIG needs to reference by name, etc. Force-regenerating the wrapper on any `%include`d header edit is the safe default.

**Note on `sizeof` and `#ifndef SWIGLUA`**: the SWIG-generated wrapper does `#undef SWIGLUA` right before including user headers, then `#define SWIGLUA` back afterward. So the user class is declared with the full layout visible even though the wrapper TU has `SWIGLUA` defined at file scope. `sizeof(Class)` is therefore consistent across the wrapper TU and the `.cpp` TU — *not* a source of heap corruption, contrary to early speculation.

## Symptom signature

- `malloc(): invalid size (unsorted)` or similar glibc abort, often surfacing during a fresh construction path (e.g. `Blanda::Blanda() -> addInput -> vector push_back -> operator new -> abort`) with ghost pointer values like `__new_start = 0x6` in the vector-realloc frame.
- Crashes at destruction-adjacent paths: unit delete, entering quicksave menu, loading quicksave. The unit works fine while operating.
- *If* the heap-corruption symptom is actually caused by stale SWIG, a fresh wrapper regen clears it. If a regen doesn't clear it, look elsewhere — the failure mode is shared with several other UB flavours (Lua/C++ reference lifetime, double-release, etc.).

## Reference

- First noticed during the Blanda Skew addition session, 2026-04-14. Added `mSkew` to `Blanda.h`, rebuilt; the SWIG wrapper was indeed stale (timestamps proved it) and was force-regenerated. In that specific session the stale wrapper was **not** the cause of the observed delete/quicksave crash — so proving the wrapper is fresh doesn't rule out other layout-related heap issues. The rule still stands: header edits to `%include`d files must trigger a wrapper regen, since a truly stale wrapper would produce exactly this failure mode.
- **2026-04-23 (Ngoma 2.5.4 → 2.5.5 hang):** added 7 new private `od::Parameter*` fields to `DrumVoice.h` between 2.5.3 (working) and 2.5.4 (hangs hardware on load). Class size grew by 28 bytes. Wrapper had been cached since 2.5.0 build; intervening 2.5.1–2.5.3 added state only inside the PIMPL `Internal` struct so the SWIG-visible class layout was unchanged and the cached wrapper stayed valid. The 2.5.4 field additions broke that invariant — the wrapper allocated DrumVoice using the old size, corrupting heap on load. Force-regen of the wrapper + version bump to 2.5.5 produced a clean build. Pending hardware confirmation but symptoms match exactly. **Lesson:** changes inside `struct Internal` (PIMPL state) do NOT need wrapper regen, but adding/removing direct member fields on the SWIG-visible class always does.
