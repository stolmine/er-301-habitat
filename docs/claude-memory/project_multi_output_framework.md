---
name: Multi-output unit framework (shipped in stolmine; author guide at docs/multi-output-units-author-guide.md)
description: Multi-output unit framework for habitat. Rationale in planning/redesign/07-multi-output-units.md; shipped in er-301-stolmine fork with QuadLFO reference at er-301-stolmine/mods/multiout/; author-facing how-to at docs/multi-output-units-author-guide.md. Use to gate whether a proposed unit qualifies and to shape its UI, vanilla fallback, and Lua contract.
type: project
originSessionId: 0063d5be-4d30-4c1e-a7b3-e687f2cae19d
---

The framework covers units that expose >1 output where the relationship between outputs is the unit's contribution.

**Status:** framework has shipped in the stolmine firmware fork. QuadLFO reference impl at `er-301-stolmine/mods/multiout/` (4 sine phases at 0/90/180/270). No habitat-side unit ships on it yet; build a qualifying unit against the author guide.

**Why:** this moved from "specified but deferred" to "implemented" — the author guide (`docs/multi-output-units-author-guide.md`, committed 45f933e) documents the concrete v1 contract.

**How to apply:** when proposing a multi-out habitat unit, consult the author guide for the Lua contract, vanilla fallback, and testing checklist. Run the derivability test first and record the verdict in the todo entry.

**Gating test:** "Can the relationship between the outputs be reconstructed downstream from one output?"
- Yes (two osc with tunable phase offset -> delay reconstructs): decompose to parallel chains.
- No (quadrature LFO locked at 0/90/180/270, Geode taps, coupled CV+gate, multichannel seq with shared step pointer, chaos x/y/z): qualifies as multi-out.
- Author declares, not user.

**Core principles (unchanged):**
- Coupling belongs to producers, never consumers. Never bind inputs.
- Primary output is author-declared and not user-reassignable. Sub-outs never occupy chain positions.
- Sub-outs reached only via local input picker on the consuming chain.
- Controls live only at top level, macro-style. No per-sub-out control depth.
- Sub-outs require meaningful author labels (no generic "out 1 / out 2").

**v1 decisions now locked (were open in earlier charter):**
- **Sub-out 1 is the primary by convention.** Vanilla's `Unit:getOutputSource(i)` is hardcoded stereo (only i==1, i==2 resolve); subs ≥3 silently inaccessible on vanilla, no crash. Pick sub-out 1's wiring carefully — it's what vanilla users get by default.
- **Local picker navigation is M6 cycling**, not axis-distinct sub-view drill-down. Edge indicator overlays the scope ply showing `label[i] + "i/N"`; M6 advances with wraparound.
- **Labels:** ≤6 chars (42px indicator ply at 10pt), descriptive not numeric, no unit-name prefix. Non-ASCII (degree symbols etc.) test on hardware first — font glyph coverage varies.
- **Sub-out ordering:** author-declared semantic, not recency.

**v1 explicitly out of scope:**
- Stereo-paired sub-outs (each sub-out is a single mono channel).
- Per-sub-out controls.
- Sub-views / drill-down navigation (M6 cycle replaces it).
- ≥10 sub-outs (the X/Y indicator assumes single digits; reconsider as sub-chain).

**Vanilla compatibility — the load-bearing principle:**
1. Package `.so` calls only upstream APIs: `od::Object`, `od::Inlet`, `od::Outlet`, `od::Parameter`. No stolmine-introduced symbols. **Do not add new C++ virtuals on `od::Object`-derived classes** — vanilla won't have them.
2. Lua sets `args.subOutLabels = {...}` (vanilla ignores unknown args fields).
3. On vanilla: package loads, insert works, sub-out 1 auto-wires, sub-out 2 also resolves on stereo chains, subs ≥3 silently inaccessible. A stolmine-saved preset wiring sub-out ≥3 will drop that connection on vanilla (standard log warning); can't be fixed from habitat side.

**Lua contract:**
- `args.channelCount = N` (force N-channel construction).
- `args.subOutLabels = {"main", "aux", "cv", "gate"}` — length defines fan-out, order is semantic.
- `onLoadGraph`: wire all N DSP outputs to `self, "Out1"` … `self, "OutN"`. `unitOutputNames` in `xroot/boot/globals-setup.lua` maps Out1-Out4; >4 requires extending that table in stolmine.

**Hardware trig bug reminder:** quadrature / phase-shifted multi-out DSP almost always uses `sinf`/`cosf`; these miscompute in the package `.so` at the package→firmware boundary on am335x with TI 4.9.3. Use a precomputed LUT (reference: `mods/spreadsheet/FilterResponseGraphic.h` `kLutCos`/`kLutSin`, 72-entry, bias-then-cast-then-modulo). Emu is unaffected — validate on hardware before declaring done. (Same underlying issue covered in the trig-LUT feedback memory; the author guide flags it specifically for quadrature/panner/indicator DSP.)

**Candidate queue** (see todo ## Multi-Output Units):
- Just Friends Geode clone — polyphonic clocked burst generator with busy-state voice distribution.
- Quadrature LFO — proof-of-concept (reference impl lives in stolmine, not yet in habitat).
- Coupled CV/gate envelope.
- Multichannel sequencer with shared step pointer.
