---
name: Spreadsheet unit serialize/deserialize implementation pattern
description: Concrete shape of per-unit serialize/deserialize in the ER-301 habitat spreadsheet package — adapter Bias round-trip, option enableSerialization placement, post-deserialize label refresh, and special cases for ConstantOffset / Comparator.
type: feedback
originSessionId: 6486de73-452f-466c-9c43-03152c66fce2
---
This is the standard shape of `serialize()` / `deserialize()` every spreadsheet unit in `er-301-habitat` should follow. Applied and verified across Excel, Ballot, Etcher, Tomograph, Petrichor, Parfait, Rauschen, Impasto, Helicase, Larets during the 2026-04-13 audit.

Companion memory: `feedback_option_vs_parameter.md` covers which widget type (Option vs ParameterAdapter) to pick in the first place.

## Shape of the Lua override

```lua
-- List every user-facing ParameterAdapter object name in self.objects.
-- Loop instead of hand-enumerating target/hardSet lines to keep it readable.
local adapterBiases = {
  "adapter1", "adapter2", "bandFoo0", "bandFoo1", "bandFoo2", ...
}

function MyUnit:serialize()
  local t = Unit.serialize(self)
  -- Any per-step / per-band state the unit owns (not yet a Parameter) goes here.
  -- e.g. loop over step/tap/segment indices calling the op's getters.
  -- ...
  for _, name in ipairs(adapterBiases) do
    local obj = self.objects[name]
    if obj then
      t[name] = obj:getParameter("Bias"):target()
    end
  end
  -- Special cases: different parameter names on non-adapter objects.
  if self.objects.tune then
    t.tuneOffset = self.objects.tune:getParameter("Offset"):target()
  end
  if self.objects.xformGate then
    t.xformGateThreshold = self.objects.xformGate:getParameter("Threshold"):target()
  end
  return t
end

function MyUnit:deserialize(t)
  Unit.deserialize(self, t)           -- restores anything with enableSerialization
  -- Per-step/per-band restore mirrors serialize.
  -- ...
  for _, name in ipairs(adapterBiases) do
    if t[name] ~= nil and self.objects[name] then
      self.objects[name]:hardSet("Bias", t[name])
    end
  end
  if t.tuneOffset ~= nil and self.objects.tune then
    self.objects.tune:hardSet("Offset", t.tuneOffset)
  end
  if t.xformGateThreshold ~= nil and self.objects.xformGate then
    self.objects.xformGate:hardSet("Threshold", t.xformGateThreshold)
  end
  -- ALWAYS refresh any label that derives from restored state.
  -- See "Stale label refresh" section below.
end
```

## Param-name map by object type

- **ParameterAdapter** — `Bias` (and `Gain`, but Gain is almost never user-facing).
- **ConstantOffset** — `Offset`.
- **Comparator** — `Threshold`.
- **MinMax** — no user state to serialize.
- **ConstantGain** — `Gain`, but typically hardcoded, not user-facing.
- Bare `od::Parameter` on the op — either call `enableSerialization()` in the C++ constructor (preferred), or treat it like an adapter and round-trip via `op:getParameter("Name"):target()` / `op:hardSet("Name", v)`. The former is cleaner because the framework handles it automatically.

## Options (`od::Option`)

Always call `enableSerialization()` in the **C++ constructor**, next to `addOption(...)`. Do NOT rely on Lua-side `enableSerialization()` in the control `init` — that runs later in the unit lifecycle and has masked bugs in the past (Ballot ratchet was functional only after moving the enable to C++).

```cpp
// In the unit's C++ constructor
addOption(mAutoMakeup);
mAutoMakeup.enableSerialization();
```

Value convention: use **1 (on) / 2 (off)** per `od/constants.h` (CHOICE_YES / CHOICE_NO). Never 0 — that's CHOICE_UNKNOWN and causes silent non-persistence in some code paths.

Once `enableSerialization()` is set in C++, do NOT also manually write `t.myOption = opt:value()` / `opt:set(t.myOption)` in Lua serialize/deserialize. Redundant and can mask misconfiguration.

## Stale label refresh

Any label whose text is derived from a restorable value (the underlying parameter/option round-trips fine, but the on-screen text stays at the default) needs an **explicit refresh call at the end of deserialize**.

Known refresh methods to call:

- `ModeSelector:updateLabel()` — sets the fader label from a `modeNames` table indexed by rounded Bias.
- `MacroControl:updateLabel()` — inherits from ModeSelector (so same call).
- `HelicaseOverviewControl:updateLinExpo()` — "lin"/"exp" badge.
- `RatchetControl:updateToggleLabels()` — "len:ON" / "vel:ON" badges.
- `TransformGateControl` — no public method; Excel/Ballot refresh `funcLabel` inline by reading `funcNames[rounded bias]`.

Pattern at end of deserialize:

```lua
-- Any ModeSelector-based control derived from a restorable Bias
if self.controls and self.controls.scale and self.controls.scale.updateLabel then
  self.controls.scale:updateLabel()
end

-- Controls with custom label methods
if self.controls and self.controls.overview
    and self.controls.overview.updateLinExpo then
  self.controls.overview:updateLinExpo()
end

if self.controls and self.controls.ratchet then
  self.controls.ratchet:updateToggleLabels()
end

-- Loops are fine when you have many of the same kind (Petrichor has 6 MacroControls)
local macros = { "volMacro", "panMacro", ... }
for _, name in ipairs(macros) do
  local c = self.controls[name]
  if c and c.updateLabel then c:updateLabel() end
end
```

**Why this is needed:** the framework's deserialize path (`Persist.deserializeObjects`) only writes values back into the underlying Parameter/Option. It does not emit change notifications that would drive Lua-side label/display refresh. Labels are Lua-owned text set at specific interaction points (encoder, sub button, spot release) — not wired to parameter subscribers. If the control never receives an interaction after reload, the label reads the wrong name even though the audible value is correct.

## Per-step / per-band state (direct DSP buffers)

If the C++ stores state in a raw buffer (step arrays, tap arrays, band config, segment data) accessed via dedicated getters/setters — manual Lua round-trip is the only path. The framework has no visibility into those buffers.

```lua
-- serialize
local steps = {}
for i = 0, N - 1 do
  steps[tostring(i)] = { field = op:getStepField(i), ... }
end
t.steps = steps

-- deserialize
if t.steps then
  for i = 0, N - 1 do
    local s = t.steps[tostring(i)]
    if s then op:setStepField(i, s.field or default) end
  end
end
-- Often followed by op:loadStep(0) to re-prime the edit buffer.
```

## Checklist when adding or reviewing a new unit

1. Every `ParameterAdapter` in `onLoadGraph` is in the `adapterBiases` list.
2. Every `od::Option` in the `.h` has `enableSerialization()` in the C++ constructor, next to `addOption(...)`, and uses values 1/2 (never 0).
3. Every `ConstantOffset` / `Comparator` whose state the user edits has its `Offset` / `Threshold` round-tripped as a special case.
4. Every `ModeSelector`-based control (MacroControl, plus any direct ModeSelector usage) has `updateLabel()` called in deserialize.
5. Any custom label-refresh methods on domain-specific controls (`updateLinExpo`, `updateToggleLabels`) are called in deserialize.
6. Per-step / per-band state has explicit serialize/deserialize blocks.
7. `Unit.serialize(self)` / `Unit.deserialize(self, t)` is always the first call.
8. `op:loadStep(0)` / `op:loadSegment(0)` / `op:loadTap(0)` / `op:loadBand(0)` runs at the end of deserialize to re-prime the edit buffer so step-list controls don't show stale edit values.

## Reference implementations

- Excel (TrackerSeq.lua) — the canonical Excel pattern, compact.
- Petrichor (MultitapDelay.lua) — the largest unit, uses the `adapterBiases` list + loop, tune Offset + xformGate Threshold as special cases, plus macro label refresh loop.
- Impasto (MultibandCompressor.lua) — per-band adapters + option sync for legacy patches + stereo opR sync.
- Helicase (Helicase.lua) — options + adapters + single custom label refresh.
