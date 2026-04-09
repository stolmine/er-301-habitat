# ER-301 Habitat Development Knowledge Base

Consolidated from Claude Code session memories. Portable reference for development conventions, gotchas, and patterns.

---

## Build & Toolchain

When adding new source files (especially .c files like pffft.c) to a mod.mk, the existing am335x build won't pick them up even though the objects compile. The link step uses cached dependency info and skips the new objects.
**Why:** The `make` incremental build doesn't re-evaluate the OBJECTS variable when source files are added. The .so gets linked from the old object list.
**How to apply:** After adding/removing source files from mod.mk:
```bash
rm -rf testing/am335x/mods/<package>
make <package> ARCH=am335x PROFILE=testing
```
Always verify with `arm-none-eabi-nm` that expected symbols appear in the .so.

stmlib/dsp/dsp.h has `#ifdef TEST` guards around Clip16, ClipU16, and Sqrt. Without TEST, it uses ARM inline assembly (ssat, usat, vsqrt.f32) designed for the STM32 Cortex-M4. These instructions exist on the ER-301's Cortex-A8 but cause crashes during engine switching in Plaits — likely due to floating-point edge cases (NaN/denormal propagation through filters, especially the noise engine).
**Why:** The crash manifested as a UI freeze at engine 9 (noise) on hardware, while working perfectly on the emulator (x86). The emulator always defines TEST.
**How to apply:** Any module porting Mutable Instruments code that includes stmlib must define TEST for the am335x build, not just for emulator/linux/darwin. This ensures portable C implementations are used instead of STM32-specific inline assembly.

SWIG must see class declarations (at minimum constructor/destructor) to generate Lua bindings. Putting class declarations entirely inside `#ifndef SWIGLUA` makes them invisible to SWIG, causing "attempt to call nil value" errors at runtime.
**Why:** The ER-301 SWIG pattern uses `#undef SWIGLUA` in the `%{ %}` block (for C++ compilation) and `%include` sees SWIGLUA defined (for Lua binding generation). Classes inside `#ifndef SWIGLUA` are skipped by the `%include` pass.
**How to apply:** When declaring ER-301 unit classes:
- Constructor and destructor must be outside `#ifndef SWIGLUA`
- `process()`, Inlets, Outlets, Parameters go inside `#ifndef SWIGLUA`
- Private members (Internal struct pointer) can be outside
- If using macros to generate multiple classes, use two macro definitions: one for `#ifdef SWIGLUA` (minimal shell) and one for `#else` (full class)
- Cannot embed `#ifndef SWIGLUA` inside a macro body — preprocessor conditionals don't work inside macro expansions

When adding/removing/reordering members in a C++ header (e.g., Etcher.h), the SWIG-generated wrapper (.cpp) must be regenerated. The build system does not track header dependencies for the SWIG wrapper compilation -- only changes to the .swig file itself trigger regeneration.
**Why:** Header-only graphics (like SegmentListGraphic.h, TransferCurveGraphic.h) are compiled through the SWIG wrapper. If the class layout changes but the wrapper isn't recompiled, the graphics access member fields at wrong memory offsets, causing garbage reads (e.g., segment count reading as 1 instead of 16).
**How to apply:** After any change to a C++ header that modifies class layout (new members, removed members, reordered members), run `touch mods/<pkg>/<pkg>.cpp.swig` before building to force SWIG regeneration. This is not needed for changes that only affect method implementations in .cpp files.

SWIG cannot convert Lua tables to `std::vector<std::string>` even with `%template(StringVector)`. The `app.StringVector` binding doesn't register in the Lua namespace. Passing a raw table to a C++ method expecting `const std::vector<std::string>&` throws a type error.
**Why:** SWIG's `std_vector.i` template instantiation for strings doesn't produce a working Lua constructor in this SDK's build. The `%apply` and `%template` directives compile but the runtime binding is absent.
**How to apply:** When exposing C++ methods that need a list of strings from Lua, provide a per-element API instead. Use `addName(const std::string &name)` + `clearNames()` rather than `setNameTable(const std::vector<std::string>&)`. SWIG handles single `std::string` args natively. Guard with `if readout.addName then` for vanilla firmware compatibility.

Graphic header files (FilterResponseGraphic.h, BandListGraphic.h, etc.) are only `#include`d by the SWIG wrapper (stolmine_swig.cpp), NOT by the main .cpp files. The Makefile doesn't track header dependencies for SWIG.
**Why:** Spent an entire debugging session iterating on FilterResponseGraphic.h (Catmull-Rom, Bezier, Gaussian, simple polygon) while `touch Filterbank.cpp` only recompiled the .cpp. The .so never changed. Every test showed the same old Catmull-Rom graphic from the first compile.
**How to apply:** After ANY change to a graphic header (or any header included only by the SWIG wrapper), ALWAYS run:
```
touch mods/stolmine/stolmine.cpp.swig
```
before building. This forces SWIG regeneration and recompilation of the wrapper, which is the only translation unit that includes the graphic headers.
This compounds with the existing feedback_swig_regen memory -- that one covers class layout changes, this covers ANY header-only change to graphics.

Always pass a pre-allocated work buffer to `pffft_transform_ordered`, never NULL. On the ER-301 ARM build, passing NULL causes pffft to call `malloc` internally during the transform. The `malloc` stub in `pffft_stubs.c` is a bump allocator that resets its heap on small allocations, corrupting memory and causing a hang.
**Why:** Parfait hung on hardware insert. The fix was matching Clouds' pattern: allocate a work buffer with `pffft_aligned_malloc` and pass it to every transform call.
**How to apply:** For any unit using pffft:
```cpp
// Init
fftWork = (float *)pffft_aligned_malloc(N * sizeof(float));
// Transform -- always pass work buffer
pffft_transform_ordered(setup, in, out, fftWork, PFFFT_FORWARD);
```
Also requires `pffft_stubs.c` (malloc/free/sinf/cosf shims) copied from `mods/clouds/`. Use `__attribute__((weak))` on sinf/cosf stubs so firmware's real versions take priority when available. Lazy-init the FFT setup in first `process()` call, not in constructor.

---

## UI Patterns & Controls

## Sub-display lifecycle
Framework calls `addSubGraphic(self.subGraphic)` on cursor enter and `removeSubGraphic(self.subGraphic)` on cursor leave. Never swap `self.subGraphic` at runtime. Use a single container and show/hide children.
## Gate sub-display pattern (from SDK Gate.lua)
- DrawingInstructions flow diagram (arrows: branch->thresh->or->title, fire->or)
- MiniScope watching branch input (col1, 40x45, with border/corners)
- Threshold Readout bound to comparator:getParameter("Threshold") (col2)
- Description label (between col2 and col3, with border/corners)
- SubButtons: input(1), thresh(2), fire(3)
- Sub-button 1 opens branch for patching: `self:unfocus(); branch:show()`
- Fire: subPressed = simulateRisingEdge, subReleased = simulateFallingEdge
- Subscribe to branch "contentChanged" for scope outlet and modButton text updates
- Main graphic: `app.ComparatorView(0, 0, ply, 64, comparator)`, not a plain label
## Shift-toggled dual mode sub-display
- Single self.subGraphic with ALL elements from both modes
- show()/hide() elements per mode
- Grab "shiftPressed"/"shiftReleased" in onCursorEnter, release in onCursorLeave
- shiftPressed toggles mode, shiftReleased returns true (consume, no-op)
## Reference
- xroot/Unit/ViewControl/Gate.lua
- mods/stolmine/assets/TransformGateControl.lua
- mods/stolmine/assets/NRCircle.lua

Pattern for adding a second sub-display mode to a GainBias control, toggled by shift key. Used in Filterbank's MixControl.
**Core mechanism:**
1. `onCursorEnter` calls `self:grabFocus("shiftPressed", "shiftReleased")` after GainBias.onCursorEnter
2. `onCursorLeave` restores normal mode, calls `self:releaseFocus(...)`, then GainBias.onCursorLeave
3. `shiftPressed()` sets `shiftHeld=true, shiftUsed=false` (does NOT toggle yet)
4. `encoder()` checks `if shifted and self.shiftHeld then self.shiftUsed = true end` before delegating
5. `shiftReleased()` toggles param mode ONLY if `not self.shiftUsed` (shift wasn't used for encoder fine/coarse)
6. `setParamMode()` swaps between two separate subGraphic objects
**Shift tap vs hold:**
- Shift tap (press + release, no encoder turn): toggles param sub-display
- Shift hold + encoder: passes through to GainBias for superfine/supercoarse stepping, no toggle
- This preserves GainBias fine/coarse encoder behavior while adding the param toggle
**Sub-display swap (the critical part):**
```lua
function MyControl:setParamMode(enabled)
  self:removeSubGraphic(self.subGraphic)  -- detach current from display
  self.subGraphic = enabled and self.paramSubGraphic or self.normalSubGraphic
  self:addSubGraphic(self.subGraphic)     -- attach new to display
end
```
**DO NOT** call `self:notifyControls()` -- that method exists only on Section, not on controls. Calling it on a control causes infinite metamethod lookup and hangs the device.
**DO NOT** try to show/hide individual GainBias sub-display children -- the flow diagram Drawing and "gain"/"bias" SubButtons are added as unnamed locals in GainBias.init and cannot be referenced. Instead, keep TWO completely separate subGraphic objects and swap them.
**Implementation steps:**
1. Extend GainBias. In init, after `GainBias.init(self, args)`:
   - Save `self.normalSubGraphic = self.subGraphic`
   - Build `self.paramSubGraphic = app.Graphic(0, 0, 128, 64)` with your readouts/labels/SubButtons
2. `setParamMode()`: use removeSubGraphic/addSubGraphic to swap (see above)
3. `onCursorLeave`: restore normal mode BEFORE calling GainBias.onCursorLeave
4. Route `subReleased` and `encoder` based on `self.paramMode` flag
**Reference implementation:** `mods/stolmine/assets/MixControl.lua` (extends GainBias, adds inputLevel/outputLevel/tanhAmt readouts on shift). Also see `mods/stolmine/assets/TransformGateControl.lua` for the simpler same-subGraphic show/hide variant (works when you own all children from EncoderControl base).

Sub-display shift-toggle must use value-comparison to detect whether shift was used for zeroing (shift+home) vs. toggling menus. Simply blocking the toggle when a readout is focused is wrong -- the user may want to switch menus while a readout is focused.
**Why:** Two patterns exist (toggle-on-release in GainBias controls, toggle-on-press in EncoderControl controls) and both break shift+home zeroing. The user may also legitimately want to toggle menus while a param is focused, so we can't just block.
**How to apply:** Snapshot the focused readout's value on shiftPressed (`shiftSnapshot = readout:getValueInUnits()`). On shiftReleased, compare: if the value changed, shift was used for zeroing -- skip toggle. If unchanged, proceed with toggle. For press-to-toggle controls (EncoderControl-based), defer the toggle to release when a readout is focused (`shiftDeferred` flag). Affected files as of 2026-04-03: FeedbackControl.lua, TimeControl.lua, MixControl.lua, TransformGateControl.lua, RatchetControl.lua.

If a parameter is only accessible through a focus/expansion view (e.g., feedback tone inside master time's expansion), the parent control must also show it as a sub-display readout. The user should be able to see and tweak expansion params without entering the full expansion view.
**Pattern:** Parent control gets a shift-toggle sub-display (like MixControl) showing readouts for its expansion children. Or at minimum, the parent's normal sub-display should show the key expansion params.
**Example:** Master time has feedback and feedback tone in its expansion view. The master time control's sub-display should show feedback and tone as readouts (on shift or directly), so the user can see/adjust them without entering the expansion.
**Why:** Users shouldn't have to navigate into expansion views for frequently-adjusted params. The sub-display is always visible when the control is focused, making it the quick-access layer.
**How to apply:** When adding expansion controls, always wire matching readouts into the parent's sub-display graphic. Use the MixControl shift-toggle pattern (removeSubGraphic/addSubGraphic) if the parent is a GainBias that already has its own sub-display.

Mode toggle faders should be the first control in the expanded view list (far left position), before other controls like V/Oct, frequency, etc.
**Why:** User preference for consistent UI layout across units.
**How to apply:** In Lua `onLoadViews`, put mode selectors first in the `expanded` array: `expanded = { "mode", "tune", "freq", ... }`. Applies to any unit with a mode/topology toggle (Canals, Discont, LatchFilter, etc).

OptionControl in the ER-301 config menu has a maximum of 3 visible choices. A 4th choice will not be displayed or selectable.
**Why:** The menu layout uses button positions (descWidth + choice index) and there are only 3 available button slots after the description.
**How to apply:** When defining OptionControl choices, limit to 3 options max. If more modes are needed, split into multiple controls or use a different UI pattern.

The ER-301 encoder is rotate-only, it has no press/click action. Do not suggest encoder press as an interaction for any unit design.
**Why:** Hardware limitation of the ER-301.
**How to apply:** When designing UI interactions, only use encoder rotation, sub-buttons, shift modifier, and menu items. Never propose "press encoder" or "click encoder" as an input.

All spreadsheet list graphics (StepListGraphic, BandListGraphic, TapListGraphic, SegmentListGraphic) must have a `mFocused` bool and `setFocused(bool)` method. The selection box color should be `mFocused ? WHITE : GRAY5`.
**Why:** Without this, the selected row looks identical whether the user is actively editing or has moved the cursor elsewhere. Hard to tell focused from unfocused at a glance.
**How to apply:** In the C++ graphic header, add `bool mFocused = false;` and `void setFocused(bool focused) { mFocused = focused; }`. Use `mFocused ? WHITE : GRAY5` for the `fb.box()` selection highlight. In the Lua control, override `onCursorEnter`/`onCursorLeave` to call `self.pDisplay:setFocused(true/false)`, calling `Base.onCursorEnter`/`Base.onCursorLeave` as appropriate. Any new list graphic + control pair must follow this pattern.

`Readout:encoder(change, shifted, fine)` uses the third arg to select step size. `fine=true` uses the fine step from the DialMap, `fine=false` uses coarse.
**Why:** 15 controls had `self.encoderState == Encoder.Coarse` as the fine arg, which is backwards -- Coarse mode triggered fine steps. Fixed in commit 9023048.
**How to apply:** Any control that forwards encoder input to a Readout must pass `self.encoderState == Encoder.Fine` as the third argument. This includes sub-display readouts, spreadsheet list controls, and any custom control with focusable Readout widgets. The two controls that had it correct (TransformGateControl, RatchetControl) can be used as reference.

The GainBias fader IS the controlGraphic (not a child of it). Setting colors or calling hide() doesn't work because the Fader draws its own track/cap regardless. Adding children to it draws them behind the fader.
To replace the fader with a custom graphic while keeping GainBias encoder behavior:
```lua
function MyControl:init(args)
  GainBias.init(self, args)
  -- Create custom graphic and wrap in container
  local customGraphic = libspreadsheet.MyGraphic(0, 0, ply, 64)
  local container = app.Graphic(0, 0, ply, 64)
  container:addChild(customGraphic)
  self:setMainCursorController(customGraphic)
  self:setControlGraphic(container)
end
```
Key points:
- Call `GainBias.init` first (creates fader, wires encoder/bias)
- Create a new `app.Graphic` container
- Add custom graphic as child of container
- Call `setMainCursorController` with the custom graphic
- Call `setControlGraphic` with the container (replaces fader entirely)
- Encoder still works because GainBias routes through `self.bias` readout, not the fader graphic
Used in: Parfait BandControl (SpectrumGraphic replaces Fader).

`mods/spreadsheet/assets/ThresholdFader.lua` -- thin GainBias wrapper that applies `addThresholdLabel` entries from an `args.thresholdLabels` table. Graceful fallback on vanilla firmware (checks `self.fader.addThresholdLabel` existence).
Usage:
```lua
local ThresholdFader = require "spreadsheet.ThresholdFader"
controls.morph = ThresholdFader {
  -- standard GainBias args...
  thresholdLabels = {
    {0.0, "off"}, {0.1, "LP"}, {0.33, "BP"}, {0.66, "HP"}
  }
}
```
For shift sub-display Readouts, apply directly:
```lua
if readout.addThresholdLabel then
  readout:addThresholdLabel(0.0, "off")
  readout:addThresholdLabel(0.1, "LP")
end
```
## Adoption list
Units that need ThresholdFader or addThresholdLabel applied:
- **Parfait BandControl**: filter morph sub-display readout (off/LP/L>B/BP/B>H/HP/H>N/ntch) -- currently numeric. Also expansion morph fader.
- **Parfait ParfaitMixControl**: could label comp/tanh ranges if desired
- **Filterbank MixControl**: similar morph-style params
- **Petrichor FeedbackControl**: tone parameter (-1 dark to +1 bright) could show "dark"/"neutral"/"bright"
- **Petrichor TapListControl**: filter type readout in sub-display
- **Any future SVF morph controls**: reuse the same threshold table
First confirmed working: Rauschen CutoffControl (sub-display) + expansion morph fader (ThresholdFader).

Spreadsheet units (Filterbank, Excel, Ballot, Etcher) use an edit buffer pattern: loadX() copies array data into edit parameters (mEditFreq, mEditOffset, etc.) bound to sub-display readouts. storeX() copies back.
**Bug:** Any bulk operation that modifies the underlying arrays without calling loadX() leaves the selected step's readout stale. The user sees old values.
**Where it happens:**
- C++ bulk operations (distributeFrequencies, applyTransform) -- must call loadX(mLastLoadedIndex) at the end
- Lua menu tasks (randomize, clear, "Set All" batch operations) -- must call `op:loadStep(self.controls.steps.currentStep or 0)` at the end
**Fix pattern:**
1. C++ side: add `int mLastLoadedX = 0` field, set it in loadX(), call `loadX(mLastLoadedX)` after any bulk array modification
2. Lua side: after any loop that calls setStepX/setBandX on multiple indices, add:
```lua
if self.controls and self.controls.steps then
  op:loadStep(self.controls.steps.currentStep or 0)
end
```
**How to apply:** Every new spreadsheet unit needs this. When adding menu tasks or C++ transforms that modify the data arrays, always reload at the end. Check for this in code review.

---

## DSP & Architecture

Do NOT allocate large arrays on the stack in process(). The ER-301 audio thread has a small stack. 4 × FRAMELENGTH float arrays on the stack caused a system freeze.
**Why:** Canals crashed with 4 stack-allocated float[FRAMELENGTH] buffers. Moving them to the heap-allocated Internal struct fixed it.
**How to apply:** Put work buffers in the Internal struct with a fixed max size (e.g. `float buf[256]`). Use `kMaxFrameLength = 256` as the allocation size. This applies to any unit that needs temporary buffers in process().

For units with multiple outlets (e.g. main/aux), crossfade between them using a C++ od::Parameter read in process(), NOT SDK graph objects like app.CrossFade().
**Why:** Unconnected outlets may alias a shared zero buffer in the SDK, discarding writes from Process(). And app.CrossFade() fed by app.GainBias() didn't respond to bias changes — the Fade input appeared stuck. The app.ParameterAdapter() + od::Parameter pattern (same as Canals' Output control) works reliably.
**How to apply:**
- Add an od::Parameter (e.g. mMix) to the C++ object
- Use an internal heap buffer (in the Internal struct) for the secondary output — don't rely on the outlet buffer for intermediate processing
- Compute the crossfade in process() after the DSP writes both outputs
- In Lua: use app.ParameterAdapter() with tie(), not app.GainBias() with connect()
- For adaptive labels on the fader, subclass GainBias as a view control (ModeSelector/MixControl pattern)

ParameterAdapter output is `Bias + Gain * In`. Gain does NOT multiply the Bias -- it only scales the CV input. With no CV connected, output equals Bias regardless of Gain.
For params only changed from UI (no CV input), the tied ParameterAdapter may not be scheduled by the graph compiler, so `mParam.value()` returns the initial value forever. Workaround: read directly from the Bias parameter pointer via `setTopLevelBias()` / `mBiasX->value()` instead of the tied parameter.
**Why:** Discovered on Pecto -- pattern/slope/resonator params never updated live because their adapters had no graph connections and weren't scheduled.
**How to apply:** Any integer-selector param (pattern, mode, type) that's only set from UI readouts (not CV-modulatable in practice) should be read from Bias refs. Continuous params with CV branches (combSize, feedback, mix) work fine through ties because the branch connection forces scheduling.

Do not use ParameterAdapter Gain to convert units (e.g. BPM to Hz). Since Gain only scales the CV input, setting `Gain = 1/60` does not convert a BPM Bias to Hz -- C++ receives the raw Bias value.
**Why:** Transport clock was running at 120 Hz instead of 2 Hz because Gain=1/60 had no effect on Bias=120.
**How to apply:** When a fader displays one unit (BPM) but C++ needs another (Hz), do the conversion in C++ `process()` (e.g. `rate = bpm / 60.0f`). Keep the Bias in user-facing units.

When a C++ Object needs to modify top-level parameters that are tied to Lua ParameterAdapters (e.g., on gate trigger), hardSet on the C++ parameter gets overwritten by the tie on the next frame. The solution: store pointers to the adapter's Bias parameters in C++ and hardSet those directly.
**Pattern:**
1. C++ class stores `od::Parameter*` pointers (outside `#ifndef SWIGLUA` for correct SWIG sizing):
```cpp
od::Parameter *mBiasMasterTime = 0;
od::Parameter *mBiasFeedback = 0;
// etc
```
2. SWIG-visible setter:
```cpp
void setTopLevelBias(int which, od::Parameter *param);
```
3. Lua passes refs during onLoadGraph after adapters are created:
```lua
op:setTopLevelBias(0, masterTime:getParameter("Bias"))
op:setTopLevelBias(1, feedback:getParameter("Bias"))
```
4. C++ uses them in process() or applyRandomize():
```cpp
if (mBiasMasterTime) mBiasMasterTime->hardSet(newValue);
```
**Why it works:** The ParameterAdapter's output is computed from `Bias + Gain * Input`. By hardSetting the Bias parameter directly, you're setting the source that the tie reads from. The tie propagates the new value on the next frame.
**Used in:** Petrichor xform gate (gate-triggered randomization of all params including tied top-level ones).
**Key lesson:** Don't compromise the design by splitting C++/Lua responsibility when a clean bridge exists. The Bias ref pattern lets C++ reach any adapter-backed parameter.

Do not randomize params with `cur + random_offset` clamped to range. Values near edges can only move inward on one side and get clamped on the other, causing repeated randomizations to pin everything to min/max.
Correct approach: `cur + (randomTarget - cur) * depth` where `randomTarget` is uniform random in `[min, max]`. This is a convex combination -- always in range, no clamping needed, no drift bias.
**Why:** Pecto xform gate was pushing all params to max after a few fires.
**How to apply:** Use this pattern in any unit with randomization (Pecto, Petrichor). Petrichor still has the old `randomizeValue` -- should be updated.

## Pattern: Recording buffer with waveform display
For units that record into an `od::Sample` buffer and need waveform visualization. Based on the FeedbackLooper's RecordingView.
### C++ Display class
Subclass `od::TapeHeadDisplay`. Override `draw()` with:
1. `mSampleView.setSample(pSample)` -- returns true on new sample
2. `invalidateInterval(lastPos, pos)` -- critical for waveform cache updates during recording. Handle wrap-around with two calls.
3. `mSampleView.setCenterPosition(pos)` -- tracks playhead (zoomable)
4. Standard draw calls: `mSampleView.draw(fb)`, `drawPositionOverview`, `drawPosition`
5. Zoom gadget support via `mZoomGadgetState` switch
Without `invalidateInterval`, the waveform cache never redraws even if sample data changes. `setDirty()` alone is not enough.
### Lua WaveView control
Extend `Zoomable` (not plain ViewControl). Key elements:
- `mainDisplay` = your C++ display class (GestureHeadDisplay, etc.)
- `subDisplay` = `app.HeadSubDisplay(head)` -- shows head position and buffer length
- SubButtons: `|<<` (reset, calls `head:reset()`), `> / ||` (play/pause, calls `head:toggle()`)
- Spots: one per ply across the width
- `setSample(sample)` callback to update sub-display name
- Floating menu with "collapse" option to return to expanded view
### Views table pattern (VariSpeed/FeedbackLooper)
Controls don't show the waveform at top level. Instead, pressing Enter on a control opens a context view pairing the waveform with that control:
```lua
local views = {
  expanded = { "run", "reset", "offset", "slew", "erase", "write" },
  collapsed = {},
  offset = { "wave", "offset" },
  erase  = { "wave", "erase" }
}
```
### C++ DSP class
Must inherit from `od::TapeHead` (gives `mpSample`, `mCurrentIndex`, `mEndIndex`). Buffer managed via `od::Sample` through `Sample.Pool.create` in Lua, passed to C++ via inherited `setSample()`.
Mark `mpSample->setDirty()` after writes so the display knows data changed.
### Reference files
- `er-301/mods/core/assets/Looper/RecordingView.lua` -- canonical Lua pattern
- `er-301/mods/core/graphics/RecordHeadDisplay.h` -- canonical C++ display (source not available, but pattern is clear)
- `er-301-custom-units/mods/sloop/SloopHeadMainDisplay.h` -- full source showing invalidateInterval with wrap handling
- `mods/stolmine/GestureHeadDisplay.h` -- our implementation
- `mods/stolmine/assets/GestureSeq.lua` -- our WaveView implementation

RaindropGraphic.h (mods/spreadsheet/) contains a working noise LUT implementation:
- 64x64 float texture (16KB, L1 cache friendly on Cortex-A8) baked at init from Perlin
- Bilinear sampling at runtime with wrapping (~10x cheaper than live Perlin per cell)
- FBM via same texture at 1x/2x/4x UV scales, domain warp via offset UV sampling
- Tileable by using 4 Perlin periods across the LUT
The firmware repo (er-301-stolmine) has Voronoi and Perlin screensaver implementations that could adopt this LUT pattern for lower CPU usage. The current screensavers compute noise per-pixel per-frame; a pre-baked LUT with UV scrolling would dramatically reduce their cost.
**How to apply:** When working on firmware screensavers or any real-time noise visualization, reference RaindropGraphic.h for the bake/sample pattern rather than computing Perlin per frame.

---

## Conventions & Style

Never use Émilie Gillet's deadname anywhere in code, comments, or attribution. When copying MI source files that contain the old name, always replace with "Émilie Gillet" and "emilie.o.gillet@gmail.com".
**Why:** User explicitly requested this — respect for the original author's identity.
**How to apply:** When copying any Mutable Instruments source files into the project, run a find-and-replace on the copied files. Check before committing.

Do not use copyrighted product names anywhere -- not in source code, comments, planning docs, commit messages, or any material, even if it's not visible to end users.
**Why:** User wants to avoid any association with trademarked names throughout the entire codebase, not just in user-facing text.
**How to apply:** Use original names for units (e.g., "Pecto" not "Rainmaker Comb", "Petrichor" not "Rainmaker Delay"). When referencing inspiration, use generic descriptions ("multiband saturation", "comb resonator") rather than product names. This applies to toc.lua entries, Lua titles, C++ comments, planning docs, commit messages, and README descriptions.

Do not use em dashes (—) in public-facing material such as READMEs, release notes, changelogs, or commit messages visible to others.
**Why:** User preference for cleaner punctuation in published text.
**How to apply:** Use regular dashes (-), colons, commas, or restructure the sentence instead. Applies to any text that will be read by others outside the conversation.

The `category` field on unit entries in toc.lua must use the standard ER-301 unit picker categories. Custom category names (like "Mutable Instruments") create separate orphaned sections instead of grouping with existing units.
**Standard categories:** Essentials, Synthesizers, Oscillators, Audio Effects, Filtering, Timing, Envelopes, Mapping and Control, Measurement and Conversion, Containers, Experimental
**Why:** Custom names don't integrate with the built-in picker — units end up in their own isolated categories. Accents uses standard categories throughout (confirmed by reading its toc.lua).
**How to apply:** Pick the best-fit standard category per unit type: oscillators/synths → "Synthesizers", clock/gate generators → "Timing", effects → "Audio Effects", etc.

spreadsheet = decorated/multimode units (custom graphics, algorithm switching, list plies). biome = simpler utility/effect units.
**Why:** The package split is about UI complexity. Spreadsheet units have custom graphics (SpectrumGraphic, PhaseSpaceGraphic, list controls) or multimode algorithm switching. Biome units are more straightforward.
**How to apply:** When creating a new unit, check: does it have custom C++ graphics, algorithm/mode switching with decorated display, or scrollable lists? If yes -> spreadsheet. Simple effects/utilities/oscillators -> biome. Example: Rauschen (10 algo modes + phase space viz) goes in spreadsheet. Varishape Osc (simple 4-ply oscillator) goes in biome.

Aim for best fit to the user's intention, not the path of least resistance. Do not be lazy.
**Why:** The xform gate was initially implemented with fire button only (no gate for top-level params), then with split C++/Lua responsibility, then with spread removed to make room for fire button. Each was a compromise that degraded the design. The right answer was to pass Bias parameter references from Lua to C++ so the gate trigger can reach everything.
**How to apply:** When a feature requires bridging Lua and C++ (or any cross-boundary problem), find the solution that delivers the full design. Don't settle for partial implementations that compromise the user experience, even if they're quicker to build.

---

## Environment & References

User works on ER-301 sound computer custom firmware and module development. Runs Arch Linux with the TI ARM toolchain at ~/ti for cross-compiling to am335x (Cortex-A8). The custom firmware fork is at ~/repos/er-301-stolmine. Comfortable with embedded systems, make build systems, and hardware testing. Prefers concise instructions and direct commands over explanations.

User uses zellij as their terminal multiplexer. ctrl+g is zellij's default prefix key, so Claude Code's external editor shortcut was rebound to alt+g.
**Why:** ctrl+g conflict makes external editor inaccessible in zellij sessions.
**How to apply:** Don't suggest ctrl+g for anything. If keybindings need resetting, preserve the alt+g editor binding.

The ER-301 front SD card is mounted at `/mnt`. Hardware packages are copied with:
```bash
sudo cp testing/am335x/<package>.pkg /mnt/ER-301/packages/
```

## Build
```bash
cd ~/repos/er-301-habitat
make clean ARCH=am335x          # wipes ALL am335x builds
make ARCH=am335x PROFILE=testing  # rebuild all 13 packages
```
Output: `testing/am335x/*.pkg` (13 packages: plaits, clouds, rings, grids, stratos, kryos, warps, commotio, peaks, scope, marbles, biome, spreadsheet).
## Install scripts
**`./install-packages.sh`** -- install dev builds to SD card (TXo firmware only)
- Copies `testing/am335x/*.pkg` to `/mnt/ER-301/packages/` with `-stolmine` suffix
- Also copies to emulator at `~/.od/front/ER-301/packages/`
**`./install-packages.sh --release`** -- install v1.3.2 release builds (vanilla compatible)
- Copies from `testing/third-party/stolmine-1.3.0.pkg` + `release-*.pkg`
- v1.3.2 has no separate biome/spreadsheet; all original units in stolmine pkg
- New units (Petrichor, Etcher, Filterbank, Bletchley Park, Station X) are TXo-only
**`./install-packages.sh --third-party`** -- install Accents + tomf packages
- Accents-0.6.16, sloop-1.0.3, lojik-1.2.0, strike-2.0.0, polygon-1.0.0
- These are vanilla release builds from GitHub; also work on TXo firmware
**`./install-third-party.sh`** -- older script, copies all `testing/third-party/*.pkg` to SD
## Key details
- `er-301` symlink points to `~/repos/er-301-stolmine` (TXo firmware fork)
- Every firmware recompile invalidates ALL packages; must rebuild all
- `make clean ARCH=am335x` wipes ALL builds; always follow with full `make`
- Packages get `-stolmine` suffix on SD card to distinguish from other builds
- Third-party release packages are in `testing/third-party/`

Do NOT run `git submodule deinit` before handling the working tree. Deinit clears the directory contents, losing any uncommitted modifications and detached HEAD commits.
**Why:** When converting eurorack from submodule to tracked directory, `deinit` wiped the directory including our local NEON/FFT modifications. Had to re-clone and manually re-apply all changes.
**How to apply:** When converting a submodule to a tracked directory:
1. First remove the `.git` file/directory inside the submodule (NOT deinit)
2. Remove the submodule entry from `.gitmodules` and `.git/config`
3. `git rm --cached <submodule>` (removes from index only, preserves working tree)
4. `git add <submodule>/` to re-add as regular files
5. This preserves all local modifications in place

---

## Project State

## Package Split (2026-04-04)
stolmine package split into two:
- **spreadsheet** (v1.0.0): Excel, Ballot, Etcher, Tomograph, Petrichor + all their graphics/controls
- **biome** (v1.0.0): NR, 94 Discont, Latch Filter, Canals, Gesture, Gated Slew, Tilt EQ, DJ Filter, Gridlock, Integrator, Spectral Follower, Quantoffset, PSR, Bletchley Park, Station X, Fade Mixer
Both build for linux and am335x. Emulator tested and working. Hardware untested due to SD card issues.
## Hardware ELF Loading Issue
The combined stolmine .so (804KB / 4148 sections) fails with "Failed to load ELF file" on ER-301 hardware. The v1.3.2 release (352KB / 1839 sections) loads fine. Rebuilding the exact same v1.3.2 source produces a larger binary (511KB / 2502 sections) that also fails.
**Root cause identified:** Firmware SDK headers changed (Readout.h added std::vector<std::string> members). SWIG processes current SDK headers even when building old source, generating a larger wrapper. A SWIG guard was added to Readout.h (commit 96c65c6) but the combined stolmine package still failed.
After splitting, spreadsheet is 565KB / 2781 sections and biome is 318KB / 1811 sections. Biome's size is close to the working release. Need to test both on hardware.
## SD Card Mount Issues
Multiple phantom devices mount to /mnt simultaneously (sdc1, sdd1, sde1, sdf1). Must unmount all except the ER-301 card before copying packages. The card occasionally mounts read-only (filesystem errors) -- needs `fsck.vfat -a` before remounting rw.
**How to apply:** Always run `mount | grep mnt` before any SD card operation. Unmount phantom devices first. If read-only, unmount + fsck + remount.
## Next Steps
1. Fix SD card (fsck), install spreadsheet + biome packages
2. Test both on hardware
3. If both load: commit the split, remove stolmine package
4. If spreadsheet still fails (2781 sections): may need further splitting or investigating the actual ELF loader limit
5. Update install-packages.sh for new package names
6. Codescan units (Bletchley Park, Station X) produce silence on hardware -- need firmware-compatible file API for loadData

**Top level (7 plies):**
V/Oct | taps | overview | time | fdbk | xform | mix
**Overview sub-display (always visible, no shift toggle):**
- grain / taps / stack (addName readout for stack power-of-2 display)
**Shift sub-displays (tap to toggle, hold+encoder for fine/coarse):**
- time -> grid / rev / skew (addName readout for grid power-of-2 display)
- fdbk -> tone
- mix -> input / output / tanh
**Focus/expansion views:**
- taps -> taps + filters + tapCount + volMacro + panMacro + cutoffMacro + qMacro + typeMacro
- overview -> overview + grainSize + tapCount + stack
- masterTime -> time + grid + drift + reverse + skew
- feedback -> feedback + feedbackTone
- mix -> mix + inputLevel + outputLevel + tanhAmt
**Key params added 2026-04-03:**
- Grid (1/2/4/8/16): taps-per-beat, Rainmaker-style spacing. masterTime is beat period, not ceiling.
- Stack (1/2/4/8/16): groups taps at same time position
- Drift (0-1): per-tap sinusoidal time jitter
- Reverse (0-1): per-grain reverse playback probability
- 20s int16 buffer, mono mixdown on mono chains

Etcher is a new unit in the stolmine package. CV-addressed piecewise transfer function -- maps input voltage to output voltage through user-defined segments with offset, curve type, and weight.
**Why:** Inspired by MI Frames but fundamentally different: continuous CV addressing (voltage-to-voltage mapping) instead of clock stepping. Functions as waveshaper, LFO sculptor, response curve designer, or quantizer.
**How to apply:** Files are in mods/stolmine/: Etcher.h, Etcher.cpp, SegmentListGraphic.h, TransferCurveGraphic.h, and assets/Etcher.lua, SegmentListControl.lua, TransferCurveControl.lua. Input is GainBias branch (no inlet). Deviation snapshots on segment transition with scope selector (offset/curve/weight/all). Unit is registered in toc.lua as "Etcher" in category "stolmine".

## Completed 2026-04-06/07 (16 commits)
### Pecto (comb resonator) -- functionally complete
- 16 tap patterns (uniform/fibonacci/early/late/middle/ess/flat/rev-fib + 8 randomized)
- 4 resonator types (raw/guitar/clarinet/sitar with amplitude-dependent delay mod)
- Dual-instance stereo, 2s buffer (down from 20s)
- Adaptive ModeSelector labels on expansion faders
- Xform randomization fixed (lerp, not perturb+clamp)
- Live param fix: pattern/slope/resonator read from Bias refs
### Transport (gated clock) -- complete
- Toggle run/stop, BPM fader (1-300), 4 ppqn output
- Phase resets on start/stop, BPM-to-Hz conversion in C++
### Parfait (multiband saturator) -- functional, needs polish
- 3-band crossover (weight/skew, 12dB/oct one-pole)
- Drive + tilt EQ, per-band level/mute
- 7 upgraded shapers: tube/diode/tri-fold/half-rect/crush/sine/fractal
- Anti-alias LP at 18kHz per band
- SVF morph filter (off/LP/BP/HP/notch)
- Single-knob compressor with SC HPF
- DC blocker after band sum
- FFT spectrum display (256-point pffft, Catmull-Rom spline, per-pixel gradient)
- SpectrumGraphic replaces fader via setControlGraphic pattern
- BandControl with cycling shift sub-display, DriveControl, ParfaitMixControl
- Skew fader: -1 to +1, mapped via pow(4, skew) internally
### Parfait remaining
- Safety limiter after band sum (sine/fractal blow up output)
- Per-shaper gain normalization
- Expansion/focus views for all plies
- Filter morph adaptive labels (needs FW Readout API)
- Steeper crossover slopes (LR4)
- CPU profiling on hardware
## Package versions
- biome: v1.0.1 (Pecto + Transport added)
- spreadsheet: v1.0.1 (Parfait added)
## Hardware status
- Parfait loads and runs on am335x with FFT viz
- pffft requires explicit work buffer (NULL causes hang via bump allocator)
- Old spreadsheet-1.0.0 pkg must be removed from SD to avoid conflicts

Community packages live in separate repos at ~/repos:
- `er-301-custom-units` (tomf): sloop, lojik, strike, polygon
- `Accents` (SuperNiCd/Joe Filbrun)
Both symlink `er-301` → `../er-301-stolmine` for the SDK.
**Fixes applied (not upstreamed):**
- er-301-custom-units `scripts/env.mk`: changed `TI_INSTALL_DIR := /root/ti` → `$(HOME)/ti`
- er-301-custom-units `scripts/mod-builder.mk`: darwin -march=native → arm64-aware, linker flags fixed
- Accents `Makefile`: added darwin arch detection, fixed SDK path, added darwin build section, fixed hardcoded `/home/joe/` include paths in Bitwise.cpp, DXEG.cpp, PointsEG.cpp
**Why:** These repos assume a specific build environment (root user, x86 Linux, or pre-Apple-Silicon macOS). Our setup is Arch Linux with TI SDK at ~/ti and macOS Apple Silicon for emulator builds.
**How to apply:** If re-cloning these repos, these fixes must be reapplied. The edits are documented in er-301-habitat's README.md.

