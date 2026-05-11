---
name: Choose od::Option vs od::Parameter for persistable sub-controls
description: For toggle-style N=2 sub-controls use od::Option with values 1/2 and enableSerialization in the C++ constructor; for N>2 or CV-modulatable, use od::Parameter + ParameterAdapter + target/hardSet round-trip.
type: feedback
originSessionId: 6486de73-452f-466c-9c43-03152c66fce2
---
Two patterns for persistable sub-controls in ER-301 units. Pick the right one up-front — mixing them or picking wrong makes state loss look like "framework bug."

## Pattern A — 2-choice toggle: od::Option

When to use: binary toggles (on/off, len/vel, lin/expo, lofi/hifi) where CV modulation is not needed.

```cpp
// Header — MUST use values 1 (yes/on) and 2 (no/off), NOT 0/1
od::Option mMyToggle{"MyToggle", 2}; // 1=on, 2=off

// Constructor
addOption(mMyToggle);
mMyToggle.enableSerialization(); // do this here, not in Lua — earliest possible
```

```lua
-- Toggle logic in the control
local current = self.toggleOption:value()
self.toggleOption:set(current == 1 and 2 or 1) -- flip between 1 and 2

-- Read state
local isOn = self.toggleOption:value() == 1
```

**Why 1/2 and not 0/1:** `od/constants.h` defines `CHOICE_UNKNOWN=0`. Using 0 as a valid state puts "off" on the framework's sentinel for "never set." We hit this concretely on Ballot's ratchet toggles — options would not persist until we rebased them on 1=on / 2=off. Working reference: Impasto's `AutoMakeup` / `EnableSidechain`, Helicase's `LinExpo` / `HiFi`.

**Why `enableSerialization()` in C++ constructor:** Lua-side `enableSerialization()` runs inside `onLoadViews` which may run AFTER first `Unit.serialize()` call on certain code paths. Setting it at DSP-object construction time guarantees the flag is set before any save/load. Lua-side is fine as belt-and-suspenders but shouldn't be relied on alone.

**Do NOT** also manually serialize via `t.myToggle = opt:value()` / `opt:set(t.myToggle)` — framework `serializeObjects`/`deserializeObjects` auto-handles options with `enableSerialization()` set. Manual round-trip is redundant and can mask misconfiguration.

## Pattern B — multi-choice or CV-modulatable: od::Parameter + ParameterAdapter

When to use: mode selectors with N>2 choices, integer dials, or anything you want a CV inlet for.

```cpp
// Header — float valued, plain Parameter
od::Parameter mMyChoice{"MyChoice", 0.0f}; // rounded to int at use site

// Constructor — no enableSerialization() needed
addParameter(mMyChoice);
```

```lua
-- Graph wiring
local adapter = self:addObject("myChoice", app.ParameterAdapter())
adapter:hardSet("Bias", 0)                         -- initial value
tie(op, "MyChoice", adapter, "Out")                 -- adapter drives the param
self:addMonoBranch("myChoice", adapter, "In", adapter, "Out")  -- exposes CV inlet

-- Serialize
t.myChoice = self.objects.myChoice:getParameter("Bias"):target()

-- Deserialize
if t.myChoice ~= nil then
  self.objects.myChoice:hardSet("Bias", t.myChoice)
end
```

Label display (if discrete): keep a `local choiceNames = { [0] = "foo", "bar", ... }` table and update a label from the rounded Bias value. The Bias float remains authoritative — rounded int is display sugar only.

**Why this works for any N:** float values have no CHOICE_UNKNOWN sentinel; 0.0, 0.5, 1.0 are all valid data. ParameterAdapter's Bias is a regular Parameter, so `:target()` returns the authoritative float and `:hardSet("Bias", v)` writes back through the adapter into the tied DSP parameter. Reference: Excel's `xformFunc` (9 choices), `xformScope` (4 choices), `xformParamA/B` (integer dials up to 64).

## Quick decision rule

- Need CV inlet? → Pattern B.
- N > 2? → Pattern B.
- Plain on/off with just a screen toggle? → Pattern A with 1/2 convention.

Do not use `od::Option` with 0-based values. Do not mix manual `t.x = opt:value()` round-trips with `enableSerialization()` on the same option.
