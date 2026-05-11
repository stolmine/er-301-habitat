---
name: Bump package version to force reinstall of extracted Lua
description: ER-301 only re-extracts a package to the rear SD card's `/v0.7/libs/<pkg>/` if the PKGVERSION differs from what's already extracted. Lua-only changes shipped under the same version number silently run with stale code.
type: feedback
originSessionId: 0063d5be-4d30-4c1e-a7b3-e687f2cae19d
---
**Headline rule (reaffirmed 2026-05-11):** the first three digits of `PKGVERSION` (MAJOR.MINOR.PATCH) are reserved for actual releases. Dev iteration moves the **4th digit only** — push it as high as needed during a build cycle (e.g. `2.6.1.99 → 2.6.1.100 → 2.6.1.250` is fine). Drop the 4th digit when the feature actually ships and the release-triggering digit advances.

When shipping any change (especially Lua-only changes, since they don't trigger a C++ rebuild that would visibly change `lib*.so`), **bump the package's `PKGVERSION` in `mods/<pkg>/mod.mk`**. Otherwise the device keeps running the extracted copy of the previous build.

**Observed 2026-04-21:** Built biome-2.1.0 with the new DensityControl that requires `spreadsheet.ShiftHelpers`. User reinstalled the .pkg but the rear SD still had the old extracted biome-2.1.0 so the new code never loaded. Both persistence (Decision 7) and shift+sub keyboard (Decision 5) tests appeared to fail until the version was bumped, forcing re-extraction. Moving Pecto to spreadsheet and bumping spreadsheet to 2.4.0 + biome to 2.2.0 resolved it.

**How to apply (dev iteration is 4th-digit ONLY; corrected 2026-04-24):**
- **Iterative dev rebuild on a single in-flight feature**: bump the **4th digit only**. Example: `2.5.5.98 → 2.5.5.99 → 2.5.5.100 → 2.5.5.101`. Never touch MAJOR/MINOR/PATCH during dev iteration — those segments are reserved for release-triggering events.
- **Bug fix (relative to public release, not dev)**: bump PATCH (3rd digit). E.g. `2.5.0` → `2.5.1` when shipping a fix to a released version.
- **New unit added (considered a subversion / MINOR)**: bump MINOR. E.g. `2.5.x` → `2.6.0` when a fresh unit lands.
- **Breaking change or package restructure**: bump MINOR or MAJOR, with release notes flagging that prior patches may not load.
- Don't rely on timestamp-based reinstall; version string is authoritative.

Earlier revisions of this note said PATCH could flow during dev; user corrected that: only the 4th digit moves during dev. Hold the line on this — drifting MINOR/PATCH in a single session inflates the nominal progress (as observed in the 2.4.0→2.5.5 Ngoma session pre-correction).

**Why the 4-digit dev convention.** Multi-build feature work that lands a stream of fixups (DSP tuning, hot-loop adjustments, follow-up to a hardware test) doesn't deserve a string of MINOR/PATCH bumps. Each rebuild needs the version bump for re-extraction, but if every iteration costs a MINOR/PATCH the version inflates with no user-visible delta. Reserve PATCH for "this is the bug-fix for the released MINOR" and use the 4th digit (build counter) for "I rebuilt and want the device to re-extract." Drop the 4th digit when the feature actually ships.

Concrete bad pattern observed 2026-04-23: spreadsheet went `2.4.0 → 2.4.1 → 2.4.2 → 2.5.0 → 2.5.1 → 2.5.2 → 2.5.3 → 2.5.4 → 2.5.5` in a single Ngoma-tuning session. That's 9 bumps, mostly iteration on the same feature plus one BLOCKER bisect. Should have been roughly `2.5.0 → 2.5.0.1 → 2.5.0.2 → ... → 2.5.0.9` and only flipped to `2.5.1` (or `2.6.0`) when the whole tuning pass settled.

**Anti-pattern:** reusing the same PKGVERSION across multiple builds during development "because nothing changed for the user". It changes for *us* when we need to debug on device -- the rear-SD extraction diverges from what we think is running. If in doubt, bump (the 4th digit).

Front-SD `.pkg` files under `/mnt/ER-301/packages/` are safe to overwrite; the version string is only used for the rear-SD extraction decision.

**Corollary (observed 2026-04-23):** This bites *within* a single dev session too, not just across releases. If you build a broken X.Y.Z, install it, then `make clean` and rebuild the SAME X.Y.Z, the emulator (and device) keep the **stale extracted .so** under `~/.od/rear/v0.7/libs/<pkg>/` because PKGVERSION is unchanged. Symptom: the user keeps reporting the same crash after every "fix" — because they're still running the broken extraction. Hours wasted on Ngoma 2.5.5.4 staring at code that was already fixed.

**How to apply.** When iterating on a build (especially after `make clean`):
- Either bump the 4th digit on every rebuild (preferred — cheap, automatic), OR
- `rm -rf ~/.od/rear/v0.7/libs/<pkg>` after copying the new .pkg to `~/.od/rear/`. This forces the emu to re-extract on next launch.

When the user reports "still crashing after the fix," check the timestamp on `~/.od/rear/v0.7/libs/<pkg>/libspreadsheet.so` against the freshly built one in `testing/<arch>/` BEFORE inspecting more code.
