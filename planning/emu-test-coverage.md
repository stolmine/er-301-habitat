# Condensed emu test list: one test per SITUATION, not per unit

Status: **partially built** (2026-08-19). Situations 1 and 4 implemented and
green; the rest are specified and unbuilt.

## Why condense

94 units across 10 packages. The per-unit insert-smoke pattern had four tests
covering four units, so 90 units had no automated coverage at all, and every new
unit needed someone to remember to add one. A per-SITUATION list scales: a new
unit is covered the day it ships because the tests walk the toc.

Each situation below is justified by a defect that ACTUALLY happened here, not
by what might theoretically go wrong.

## The list

### S1. Every unit constructs and destructs — BUILT

`96-units-spreadsheet` .. `9C-units-house`, one per package. Walks the whole toc:
insert, run frames, remove, collecting failures by name rather than aborting.

Caught on its very first run:
- **every scope unit dead** (`undefined symbol: _ZGVbN4v_sinf`) - the libmvec
  bug, a third instance after kryos and biome
- **catchall Flakes** requiring `biome.libcatchall`, which does not exist -
  already filed as `catchall-flakes-wrong-module-require`, now fixed

Also covers: SWIG sizeof mismatch, constructor crash on a recycled heap block,
and the DELETE path, which runs on the 4096-byte busy stack and fails
differently from insert.

Split per package because the emu watchdog is a 60-second WHOLE-TEST deadline,
not per command. Each test runs in 2-3 s standalone.

Two traps found while building it, both recorded in the test comments:
- `!packages NAME` only COPIES an archive into the front repository. Installing
  is a separate Lua step. Breccia failed for a day because of this.
- The package match must be ANCHORED. `k:find('mi')` matches
  `core-0.7.0-stolmine.9.7.0`, because "stolmine" contains "mi", so a substring
  match installed core twice and never installed mi. The `_count` vacuity guard
  is what caught it; without that the test passes while walking nothing.

### S2. Cross-package dependency is installed — BUILT, folded into S1

Two units require a package other than their own: Breccia and biome Quantoffset
both need `core.libcore` at construction time. Every S1 test installs core
first. `make core ARCH=linux` must have been run.

### S3. Control expands on ENTER with the expected members — BUILT

`95-control-expansion-coverage`. Asserts `unit.views[key]` CONTENTS, not just
existence, because `Unit/init.lua:84-96` auto-generates a `{scope, control}`
view for any expanded control lacking a descriptor, so an existence check passes
on broken code. Proven to discriminate against the pre-change build.

### S4. An expanded fader reads out identically to its sub-readout — NOT BUILT

The gap that matters most right now. User-reported 2026-08-18; I verified it by
hand with a one-off script and it found faders with **entirely wrong ranges**:
GateSeq/TrackerSeq xform factor was `[0,1]` precision 2 where the readout is
`LinearDialMap(1,64)` precision 0, and Larets output level is 0..4 where the
fader said 0..1.

Fixed by exporting the maps from the control classes so both consumers share one
object. A test should assert that: for every `Fader` in an expansion, its `map`
and `precision` are the same VALUES the parent control's sub-readout uses. This
is a static/source check as much as a runtime one, and it currently exists only
as a throwaway script.

### S5. Option and preset round-trip — NOT BUILT

`od::Option` is NOT auto-serialized and needs `enableSerialization()`; forgetting
it is a documented trap that has bitten repeatedly. Test: set every option and
adapter bias to a non-default, serialize, deserialize into a fresh instance,
assert everything survived. Would also catch the double-serialization risk from
`Fader` calling `param:enableSerialization()` on params some units already
serialize by hand.

### S6. No-input and no-sample safety — PARTIALLY BUILT

Sample players with nothing attached must not crash or emit garbage. Breccia's
test does this deliberately; nothing else does. Generalize: for every unit, run
frames with silence and with no sample, assert output is finite.

### S7. Control writes the parameter it claims — NOT BUILT

The whole peaks-audit class. Every `tie(op, "X", ...)` must name a parameter the
C++ actually registers. This is a STATIC cross-check between Lua and the C++
`addParameter` calls, cheap to write, and would have caught
`spreadsheet-larets-compressamt-tie-mismatch` and probably several of the other
audit findings without needing the DMC comparison at all.

### S8. Multi-output routing — NOT BUILT

Every `Outlet` needs a matching `addOutput`, or the outlet is null and the unit
is silently mute. A grep-level count match would do.

## Known flakiness, pre-existing, NOT caused by these tests

The full suite intermittently fails with a watchdog "no reply" during bulk unit
loading. Confirmed pre-existing: with all seven new tests REMOVED, the suite
still failed the same way in `81-promote-subclass-classify`, which also loops
every unit in biome and spreadsheet loading each. Raising
`STOL_EMU_TEST_TIMEOUT` to 120 did not help, so it is not the deadline being
tight - the emu stops responding. Individually the new tests pass 6/6 and run in
2-3 s. Worth investigating on its own: an intermittent hang during bulk
load/unload is exactly the shape of a heap or resource bug.

## Current state

70 tests. Green individually; the suite shows the pre-existing bulk-load flake
roughly one run in two, in a varying test.
