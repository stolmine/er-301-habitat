---
name: Preserve custom graphics in expansion views
description: Expansion view layouts must reference the original control (with its custom graphic) as the first element, not a separate plain GainBias fader. Using a duplicate fader replaces the custom graphic with a stock fader on expansion.
type: feedback
originSessionId: b5f87646-88b8-4a6d-8557-2224c6b447ce
---
When a GainBias-based control replaces the default fader with a custom C++ graphic (via `setMainCursorController` + `setControlGraphic`), the expansion view layout must list the **same control key** as the first element, not a separate `xxxFader` GainBias.

**Wrong** (shows stock fader on expansion):
```lua
block     = { "blockFader", "phraseMin", "phraseMax", "blockMaxFader" },
```

**Right** (preserves custom graphic on expansion):
```lua
block     = { "block", "phraseMin", "phraseMax", "blockMaxFader" },
```

**Why:** The expansion layout `views.block = { ... }` lists which controls to show when the user expands the ply. If the first element is a different control object (a plain GainBias `blockFader`), its stock fader graphic renders instead of the custom one. Using the original control key (`"block"`) reuses the same object, so the custom graphic persists.

**How to apply:** For any control that has a custom graphic (HelicaseOverviewControl, CompBandControl, BandControl, ColmatageBlockControl, etc.), ensure the expansion layout references the control's own key, not a duplicate fader. Additional expansion controls (sub-param faders) follow after.

**Reference:** Helicase uses `views.overview = { "overview", "overModMix", ... }` — the overview control with its phase-space graphic is the first element and persists in expansion. Colmatage's block ply was incorrectly using `"blockFader"` and showed a stock fader on expansion until fixed.
