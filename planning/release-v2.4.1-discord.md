**er-301-habitat v2.4.1** — 2026-05-02 (hotfix)

Single-package hotfix: **spreadsheet 2.6.0 → 2.6.1**. All other packages unchanged.

**Fix**: Ngoma could hard-fault on insert. DrumCubeGraphic's `draw()` was defined out-of-line, which under GCC's key-function rule emits a non-COMDAT vtable. Any ABI drift in the framework vtable layout between firmware and package then drifts the offsets the package was compiled against, and the cube hits a bad slot on insert. Implementation moved inline into the header so the vtable is COMDAT-linked and immune to firmware-vs-package drift. DrumVoice ctor `sin` init swapped to a precomputed LUT alongside (per the known am335x package-trig miscompute).

Also bundled: a graphics authoring guide and a lint script flagging out-of-line virtual definitions in package `.cpp` files, to prevent the class of bug recurring across other packages.

No new units, no UI changes, no API changes.

Full notes + binaries: <github release URL>
