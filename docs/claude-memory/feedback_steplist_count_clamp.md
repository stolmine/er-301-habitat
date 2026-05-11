---
name: Step-list count reduction requires both C++ clamp and Lua reconcile
description: When a list-based graphic's count shrinks, clamp mSelected at draw AND reconcile the Lua-side currentX on cursor enter — one half alone leaves a stale edit buffer or a black viewport.
type: feedback
originSessionId: 6486de73-452f-466c-9c43-03152c66fce2
---
Every step-list graphic in the spreadsheet package (StepListGraphic, LaretStepListGraphic, SegmentListGraphic) has a `mSelected{Step,Segment}` that can fall out of range when its companion count parameter is reduced. Two symptoms:

- **Viewport goes black**: the `mScrollOffset` math derives from `mSelected`, so an out-of-range selection scrolls past the end, and the `for (step >= count) break;` guard kills every row in the loop.
- **Stale edit buffer**: the Lua control's `self.currentStep` / `self.currentSegment` drives `loadStep`/`storeStep`, so after a count reduction the edit readouts reflect and write a step that isn't on screen.

**Why:** These two sides live in separate translation units (C++ graphic vs Lua control) and neither watches the count parameter. The count is usually exposed as a sibling GainBias the user edits on a different ply, so there's no single event to hook for re-sync.

**How to apply:** When adding or reviewing any list-based graphic with a user-editable count:

1. In the graphic's `draw()`, right after reading `count` and before the scroll maintenance block, add `if (mSelected >= count) mSelected = count - 1;`. This self-heals the viewport on the very next frame.
2. In the Lua control, add a `reconcileSelection()` that re-reads `count`, clamps `self.currentX`, calls `loadStep/loadSegment(clamped)`, `pDisplay:setSelectedStep(clamped)`, and `updateTitle()`. Call it from `onCursorEnter` so the edit buffer re-syncs whenever the user returns to the ply.
3. Reference implementation: `mods/spreadsheet/{StepList,LaretStepList,SegmentList}Graphic.h` and the matching `{StepList,LaretStepList,SegmentList}Control.lua`. Same pattern for each.

Don't rely only on the C++ clamp — the viewport looks right but the edit readouts silently write to the wrong step. Don't rely only on the Lua reconcile — you get a black list until the user re-enters the ply.
