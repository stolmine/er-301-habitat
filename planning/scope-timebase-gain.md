# Scope package — timebase, gain, voltmeter

Status: planning. Targets scope package `v1.1.0 → v1.2.0`.

## Goal

Add user-controllable timebase and gain to the three scope units
(Scope, Scope 2x, Scope Stereo). Keep the change inside the package
— no firmware modifications. Fold the voltmeter todo idea into the
same package update as a small dedicated readout ply rather than
shipping a separate voltmeter unit.

## Why a custom graphic (vs firmware patch)

The firmware MiniScope class wraps a `FifoProbe`, whose
`setDecimation(int)` is already public. The only thing missing is
a public accessor on MiniScope to forward to the probe. A firmware
patch to expose it would be ~5 lines, but it would invalidate every
package and force a coordinated firmware-bump release. The user
prefers to keep the work package-local.

We can subclass `od::Graphic`, allocate a `FifoProbe` through the
public `AudioThread::getFifoProbe()` API, and call
`setDecimation()` on it directly. Everything MiniScope uses is
public ABI. The custom-graphic version is roughly a copy of
`od/graphics/meters/MiniScope.cpp` plus a few accessors.

## Constraints to respect (memory-driven)

- **No out-of-line virtuals on framework subclasses.** All
  virtuals (`draw`, dtor, `notifyHidden`, `notifyVisible`) defined
  inline in the header. No `ScopeGraphic.cpp`. Verify with
  `tools/check-graphic-virtual-defs.sh` and `nm` check for COMDAT
  (weak) linkage. See `docs/graphics-authoring-guide.md`.
- **No stack-local NEON arrays in draw.** Not a concern here —
  draw uses scalar math and `std::vector<int>` (heap) only.
- **No package-side `sinf`/`cosf`.** Not a concern — no trig.
- **No large contiguous in-class default initializers.** Few
  members; safe.
- **SWIG header dep tracking.** `mod.mk` already globs
  `mods/scope/*.h` for `SWIG_HEADER_DEPS`. New `.h` picked up
  automatically.
- **Option values must be 1/2-indexed.** Never 0 (=`CHOICE_UNKNOWN`).
- **`enableSerialization()` in C++ constructor.** For any
  `od::Parameter` / `od::Option` we want to round-trip via
  quicksave.
- **am335x `-fno-tree-vectorize`.** Already applied package-wide
  in `mod.mk`.
- **Bump `PKGVERSION`** so ER-301 re-extracts to rear SD.
  `1.1.0 → 1.2.0`.
- **Build both arches every time.** linux + am335x; auto-cp
  linux build to `~/.od/rear/`.

## Decisions (locked with user 2026-05-27)

| # | Decision |
|---|----------|
| 1 | 7-step timebase, default 2x (matches firmware default decimation) |
| 2 | Gain ships in the same change as timebase |
| 3 | ScopeStereo: one shared control pair drives both L and R graphics |
| 4 | Time + Gain controls live in the scope graphic's sub-display only — no extra plies for them. Voltage readout gets its own ply to the right of the scope graphic. |

## UI surface

### Time selector (sub-display readout on scope graphic ViewControl)

| Choice | Decimation | Display window @ 48k |
|---|---|---|
| 1 = "1x" | 1 | 83 ms |
| 2 = "2x" (default) | 2 | 167 ms |
| 3 = "4x" | 4 | 333 ms |
| 4 = "8x" | 8 | 667 ms |
| 5 = "16x" | 16 | 1.33 s |
| 6 = "32x" | 32 | 2.67 s |
| 7 = "64x" | 64 | 5.33 s |

### Gain selector (sub-display readout on scope graphic ViewControl)

| Choice | Gain |
|---|---|
| 1 = "0.25x" | 0.25 |
| 2 = "0.5x" | 0.5 |
| 3 = "1x" (default) | 1.0 |
| 4 = "2x" | 2.0 |
| 5 = "4x" | 4.0 |

### Sub-display layout

Both readouts share the scope graphic's sub-display via the
spreadsheet paramMode convention (see
`feedback_parammode_convention.md`):
- Cursor on scope graphic (encoder focus) → sub-display shows
  Time (default) and Gain side by side, or shift toggles between
  them.
- Initial implementation: side-by-side multi-readout sub-display
  (simpler than shift-toggle). If sub-display real estate is
  cramped, fall back to shift-toggle.

### Voltmeter (Phase 2 — new ply right of scope graphic)

Separate `ViewControl` occupying 1 ply, positioned immediately
after the scope graphic. Shows numeric input voltage.

- Scope (was 1 ply wide) → 2 plies total (scope + voltmeter)
- Scope 2x (was 2 ply wide) → 3 plies total
- Scope Stereo (was 2 ply wide) → 3 plies total (single voltmeter
  reads L+R as two stacked readouts, or selects one — TBD in Phase 2)

Implementation sketch:
- Add `od::Parameter mPeakL{"Peak L"}` (and `mPeakR`) on
  `scope_unit::Scope`, updated from `process()` as a slow peak
  follower (~50 ms attack / 250 ms release).
- Lua side: standard `Readout` bound to the parameter, displayed
  with `Volts` map (FULLSCALE_IN_VOLTS = 10).

## Files affected

| Path | Change |
|---|---|
| `mods/scope/ScopeGraphic.h` | NEW — header-only `scope_unit::ScopeGraphic : public od::Graphic` |
| `mods/scope/scope.cpp.swig` | Add `#include "ScopeGraphic.h"` in `%{` block and `%include` |
| `mods/scope/assets/Scope.lua` | Swap `app.MiniScope` → `libscope.ScopeGraphic`; add Time + Gain sub-display controls. Phase 2: add voltmeter ply. |
| `mods/scope/assets/ScopeWide.lua` | Same |
| `mods/scope/assets/ScopeStereo.lua` | Same; controls drive both L and R graphics |
| `mods/scope/Scope.h` / `.cpp` | Phase 1: unchanged. Phase 2: add `Peak L`/`Peak R` parameters + peak follower in `process()` |
| `mods/scope/mod.mk` | `PKGVERSION 1.1.0 → 1.2.0` |
| `todo.md` | Mark Scope timebase / gain entries done after ship; mark voltmeter (folded into scope package) done |

## ScopeGraphic.h sketch

```cpp
#pragma once

#include <od/graphics/Graphic.h>
#include <od/graphics/FrameBuffer.h>
#include <od/objects/measurement/FifoProbe.h>
#include <od/objects/Outlet.h>
#include <od/AudioThread.h>
#include <od/config.h>
#include <od/extras/FastEWMA.h>
#include <vector>

namespace scope_unit
{
  class ScopeGraphic : public od::Graphic
  {
  public:
    ScopeGraphic(int left, int bottom, int width, int height)
        : od::Graphic(left, bottom, width, height)
    {
      mMaximums.resize(mWidth + 1, 0);
      mMinimums.resize(mWidth + 1, 0);
      mEWMA.setInitialState(0.0f);
      mEWMA.setTimeConstant(globalConfig.sampleRate * 0.25f, 1.0f);
      mShowStatus = mWidth > 50;
    }

    virtual ~ScopeGraphic()
    {
      disconnectProbe();
      clearOutlet();
    }

    void watchOutlet(od::Outlet *outlet)
    {
      disconnectProbe();
      clearOutlet();
      mpWatchedOutlet = outlet;
      if (mpWatchedOutlet)
      {
        mpWatchedOutlet->attach();
        if (mVisibility == visibleState) connectProbe();
      }
    }

    void setDecimation(int d)
    {
      if (d < 1) d = 1;
      mDecimation = d;
      if (mpProbe) mpProbe->setDecimation(d);
    }

    void setGain(float g)   { mGain = g; }
    void setOffset(float o) { mOffset = o; }

#ifndef SWIGLUA
    virtual void draw(od::FrameBuffer &fb)
    {
      Graphic::draw(fb);
      // ... aped from MiniScope::draw with mGain applied in calculate()
    }

    virtual void notifyHidden()
    {
      disconnectProbe();
      Graphic::notifyHidden();
    }

    virtual void notifyVisible()
    {
      connectProbe();
      Graphic::notifyVisible();
    }

  private:
    od::FifoProbe *mpProbe = nullptr;
    od::Outlet    *mpWatchedOutlet = nullptr;
    od::FastEWMA   mEWMA;
    std::vector<int> mMaximums;
    std::vector<int> mMinimums;
    float mTriggerThreshold = 0.0f;
    float mGain = 1.0f;
    float mOffset = 0.0f;
    int   mHorizontalSync = 0;
    int   mDecimation = 2;
    int   mCalculateCount = 0;
    bool  mShowStatus = false;

    static const int WarmUpTime = 10;
    static const int RefreshTime = 2;

    void calculate()
    {
      // aped from MiniScope::calculate, with one change:
      //   y = (int)((mOffset + values[i] * mGain) * dy);
    }

    void connectProbe()
    {
      if (mpProbe) return;
      if (!mpWatchedOutlet) return;
      mpProbe = od::AudioThread::getFifoProbe();
      if (mpProbe)
      {
        mpProbe->setDecimation(mDecimation);
        od::AudioThread::connect(mpWatchedOutlet, &mpProbe->mInput);
      }
      mCalculateCount = -WarmUpTime;
    }

    void disconnectProbe()
    {
      if (mpProbe)
      {
        od::AudioThread::disconnect(&mpProbe->mInput);
        od::AudioThread::releaseFifoProbe(mpProbe);
        mpProbe = nullptr;
        mEWMA.setInitialState(0.0f);
      }
    }

    void clearOutlet()
    {
      if (mpWatchedOutlet)
      {
        mpWatchedOutlet->release();
        mpWatchedOutlet = nullptr;
      }
    }
#endif
  };
}
```

Notes:
- Inlet-watching variant from MiniScope is dropped; Scope.lua only
  ever calls `watchOutlet`. Reintroduce if needed later.
- `BUILDOPT_VERBOSE` telemetry block from MiniScope is dropped —
  firmware-debug-only.
- Status-text "No Signal" path kept (same as MiniScope).

## Lua wiring sketch (Scope.lua, single-channel example)

```lua
local app = app
local libscope = require "scope.libscope"
local Class = require "Base.Class"
local Unit = require "Unit"
local ViewControl = require "Unit.ViewControl"
local Option = require "Settings.Option"
local Readout = require "Unit.ViewControl.Readout"
local ply = app.SECTION_PLY

-- decimation lookup
local DECIMATION = {1, 2, 4, 8, 16, 32, 64}
local DECIMATION_LABELS = {"1x","2x","4x","8x","16x","32x","64x"}

-- gain lookup
local GAIN = {0.25, 0.5, 1.0, 2.0, 4.0}
local GAIN_LABELS = {"0.25x","0.5x","1x","2x","4x"}

-- ScopeViewControl: holds the ScopeGraphic + the two Option controls
-- driving its setDecimation/setGain.
-- Sub-display shows two Readouts (time, gain) side-by-side.
```

Persistence:
- Both `Option`s are auto-serialized by the `OptionControl`
  ViewControl pattern (CONTEXT.md, "Serialization" section). No
  C++-side `enableSerialization()` needed because the option
  state lives in Lua.
- On deserialize, re-apply the option values to the graphic via
  `graphic:setDecimation(...)` / `graphic:setGain(...)` —
  options restore their stored choice, but the graphic state is
  not part of the option, so we need an explicit refresh hook.
  Add `Scope:deserialize(t)` override that calls
  `Unit.deserialize(self, t)` then re-applies both options to
  the graphic.

## Build / verify checklist

1. `tools/check-graphic-virtual-defs.sh` — must pass clean.
2. `make scope ARCH=linux` and auto-cp to `~/.od/rear/`.
3. `make scope ARCH=am335x`.
4. `arm-none-eabi-nm testing/am335x/mods/scope/scope_swig.o | grep ScopeGraphic`
   — vtable entries should be `W` / `V` (weak), not `T` / `R`.
5. `ls testing/am335x/mods/scope/ScopeGraphic.o` — should NOT
   exist (header-only).
6. `tools/check-neon-hints.sh testing/am335x/mods/scope/scope_swig.o`
   — clean.
7. Emu smoke test: insert all three scope variants, verify signal
   draws, change Time across all 7 settings, change Gain across
   all 5 settings, verify auto-trigger still locks, quicksave +
   reload preserves both selections.
8. Hardware smoke test (required before ship per emu-vs-hw memory
   chain): install to front SD, insert each variant, walk all
   Time + Gain values, verify no insert/delete/quicksave crash.

## Phasing

- **Phase 1**: ScopeGraphic.h + Lua wiring for Time + Gain, all
  three units. Ship as `scope-1.2.0`.
- **Phase 2**: Voltmeter ply (peak-follower Parameter on
  `scope_unit::Scope`, dedicated readout ply). Ship as
  `scope-1.3.0`.

Reason for split: Phase 1 is pure Lua + header-only graphic, very
contained. Phase 2 modifies the C++ Object's `process()` and adds
a second ViewControl. Cleaner to land separately so the timebase /
gain change can be verified in isolation before the audio-thread
change goes in.

## Open follow-ups (post-ship)

- If users want continuous timebase / gain instead of stepped, swap
  Options for ParameterAdapter Bias mappings (CV-modulatable).
  Defer until requested.
- Spectrogram unit uses a different graphic path; not covered by
  this work.

## Related memories

- `feedback_no_out_of_line_virtuals.md` — the cardinal rule for
  this graphic.
- `feedback_neon_intrinsics_drumvoice.md` /
  `feedback_neon_hint_surfaces.md` — NEON traps to avoid (not
  applicable here, listed for completeness).
- `feedback_package_version_bump.md` — bump `PKGVERSION`.
- `feedback_always_build_both_arches.md` — every build is two
  commands.
- `feedback_linux_build_auto_install.md` — cp to `~/.od/rear/`
  after every linux build.
- `feedback_option_vs_parameter.md` — 1/2-indexed options.
- `feedback_parammode_convention.md` — sub-display shift toggle
  convention if we end up needing it.
- `docs/graphics-authoring-guide.md` — full reference.
