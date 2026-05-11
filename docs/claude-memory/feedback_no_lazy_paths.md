---
name: Do not surrender rigor for expedience
description: When a hard bug is isolated to a specific component, the answer is to diagnose it, not to rip the component out. Removing functionality to avoid the work of understanding the root cause leaves a landmine for next time and degrades the unit. Stay rigorous, stay curious, fix the actual problem.
type: feedback
originSessionId: bd738562-37e5-4b2a-9bdc-85a9feb561af
---
When a crash bisects to a specific feature, code path, or component:

**Do this:**
- Pin down the exact mechanism. What instruction trapped? What memory operation failed? What ABI mismatch, what alignment, what vtable slot, what register?
- Read the source. Inspect the disassembly. Compare working and broken builds at the binary level.
- Reproduce minimally — strip *into* the failing component, not *around* it.
- Document what you found, even if the fix takes longer.

**Do not do this:**
- "It crashes when this graphic is loaded → strip the graphic."
- "It crashes on this code path → comment out the code path."
- "It crashes during init → defer init."
- "Pragmatic call: remove the surface entirely for this release." — this exact phrase shows up in the Ngoma `.175` commit, and the user has now hit the same class of bug **twice** because the actual root cause was never pinned. The second time around it cost another half-day to re-bisect.

**Why this matters:**
Ripping out functionality to dodge a bug means:
- The bug is still there, latent, ready to bite the next unit that exercises the same surface.
- The unit ships degraded. Users feel it.
- Future-you spends hours re-chasing the same crash signature with less context.
- The codebase accumulates unexplained holes that other contributors have to read around.

**The user's exact words (2026-05-01):**

> "this is lazy and bogus. we want to diagnose what is actually happening and retain the graphic. even if we strip it out we run the risk of doing this again and wasting hours chasing the same crash. write to memory: DO NOT BE LAZY, DO NOT GO FOR THE EASIEST PATH FORWARD, DO NOT SURRENDER RIGOR FOR EXPEDIENCE"

This was after I proposed permanently removing `DrumCubeGraphic` because it was the trigger surface for an insert-crash that we hadn't fully root-caused. The cube was successfully isolated as the trigger via a Lua-side stub test, but the *mechanism* — what specifically inside `DrumCubeGraphic` construction or `od::Graphic`/`od::FrameBuffer` interaction was crashing — remained unknown. Stripping the cube would have shipped a known-degraded unit and left the underlying ABI / vtable / construction bug to recur on the next graphic-using unit.

**The right move when you hit a wall:**
- Add more instrumentation (printf-debug into the constructor, into draw, into the vtable lookup).
- Compare disassembly between a known-working build and the broken build.
- Read the SDK source for the suspect class to understand its expected ABI surface.
- Build a *minimal* repro — a stub Graphic subclass that just does the same thing the cube does, see if it also crashes.
- Ask the user about state changes (toolchain, fw, build environment) that might be invisible from source diff.

The right answer to "this crashes" is almost never "remove what crashes." It's "understand why."
