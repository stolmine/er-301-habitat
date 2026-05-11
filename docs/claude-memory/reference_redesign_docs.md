---
name: Standalone redesign docs and rpidev branch
description: Where the ER-301 standalone-redesign work lives — branch, docs dir, scope of completed work
type: reference
originSessionId: e013ff61-8ec8-4f50-999d-39598e390fe6
---
The standalone ER-301 redesign (CM4/CM5 successor, desktop + Eurorack variants on shared mainboard) is a substantial in-progress effort — read these before brainstorming hardware/firmware-architecture topics with the user.

**Branch:** `rpidev` in the `er-301` submodule (the firmware fork at `~/repos/er-301-stolmine`). 462 commits ahead of vanilla. Diff `develop..rpidev` is ~3k LOC across emu HAL, AudioThread, BufferPool, TaskScheduler, UnitChain, plus a new `WorkerPool` primitive.

**Redesign docs:** `er-301/docs/planning/redesign/` — 19 numbered files. Reading order:
- `00-overview-and-roadmap.md` — phase plan (24–36 mo), framing, primary/secondary audience.
- `01–05` — compute/power, dual i2c, CV/gate outs, audio/USB, MIDI scoping decisions.
- `06–07` — IO unit model, multi-output framework.
- `08–09` — mainboard/daughterboard split, form factor.
- `10` — controller section.
- `11` — BOM/cost.
- `12` — latency test jig.
- `13` — stolmine core package.
- `14` — multi-output CPU cost.
- `15` — analog I/O hardware.
- `16` — RT audio stack.
- `17` — RPi bench bringup.
- `18` — parallelization (revised 2026-04-26 with 2-thread MVP).
- `19` — unit authoring under parallelism.

**Locked headlines from doc 00:** CM4 or RPi5; sub-3ms @ 96kHz; dual i2c on rear 3.5mm TRS; 8 onboard CV/gate outs (TXo-derived, ±10V); USB-C PD with onboard ±12V; USB UAC2 4-in/4-out async device-master; MIDI 1-in/1-out clock+CC only.

**Parallel-DSP MVP status:** Phases 1–9 landed on rpidev. WorkerPool with cacheline-aligned atomics + hybrid spin-then-park, TaskScheduler dispatch, `BUILDOPT_PARALLEL_DSP` flag, within-chain v2 priority-batched dispatch + branch parallelism, multi-chain xrun root cause documented and resolved. Per-chain CPU% via `UnitChain::getLoad`. emu links libmvec for auto-vectorized package binaries. Dev rig runs at real-time latency.

**How to apply:** before recommending any hardware option, build-system change, RT-audio approach, or parallelization idea, check whether it's already decided/implemented in the redesign docs or rpidev branch. Don't re-suggest things the user has spent months working through.
