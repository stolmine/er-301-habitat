# 06 — UI surface (Phase 5): LOCKED layout

Status: **LOCKED 2026-06-26; UI build IN PROGRESS (dev 0.2.0.24).** Folds the flat
dev controls into a 6-ply surface + a config menu. Build order: **organization first
(plies + subs + menu), animated visualization after.**

## BUILD PROGRESS / RESUME HERE (Phase 5)
- **Step 1 DONE (0.2.0.21):** config MENU via `onShowMenu` -- Mode, Sense (3-level
  Low/Med/High), ClockMode, Grit (3-choice Clean/Normal/Broken) are unit-menu
  Options (Sense+Grit discretized from knobs in the atom). **Trig removed** (atom
  inlet + manual fire gone; re-trigger is Env-implicit only).
- **Step 2 DONE (0.2.0.22-24):** the shift-row SUB-CONTROL. Built reusable
  **`mods/anamnesis/assets/AnamSubControl.lua`** (canonical Pattern A per
  `planning/shift-handling.md`, modeled on spreadsheet `DriveControl`; tap-shift
  toggles main<->param sub-display, sub-buttons focus readouts, shift+sub = keyboard)
  + self-contained **`mods/anamnesis/assets/ShiftHelpers.lua`** (no cross-pkg require).
  Applied to: **Size** (main) + Decay/Mod/Regen subs; **Density** (main) + Diffuse sub;
  **Clock/overview** (main) + Source/DirLoop/Spread subs. Titles trimmed to 1-2 words.
- **Current surface = 7 plies:** Length, Speed, Freeze, Size, Density, Clock, Mix.
- **NEXT / PENDING:**
  1. **Looper ply** (Speed/Length) -- the locked spec wants NO main (viz-as-main +
     Speed/Length subs), which needs an EncoderControl-with-graphic base, not the
     GainBias-based AnamSubControl. Left Speed+Length FLAT for now. OPEN DECISION:
     (a) keep flat until the viz, or (b) stopgap Speed-main + Length-sub now (-> 6 plies).
  2. **Animated VIZ phase** (all-viz/animated): overview/plexus visualizer (Clock ply),
     looper playhead (Looper ply), animated mains. project_bias_indication: dotted
     bias-line overlay on viz plies. feedback_clock_control (LaretClockControl) if a
     clock viz is wanted.
- **Subs lose dedicated CV** (buried params via readout); the mains keep their CV
  branch. Acceptable per the design (subs = less-performed). Atom unchanged since
  0.2.0.21 -- this phase is Lua/UI only.

## Final 6-ply live surface

| Ply | Main | Shift-row subs |
|---|---|---|
| **1 Overview** | **Clock** (GainBias, under the field/plexus viz) | Source · DirectLoop · Spread |
| **2 Looper** | *(no main — looper playhead/buffer viz)* | Speed · Length |
| **3 Freeze** | **Freeze** (gate) | — |
| **4 Field** | **Size** | Decay · Mod · Regen |
| **5 Density** | **Density** | Diffusion |
| **6 Mix** | **Mix** | — |

Diffusion pairs under Density (both = "how diffuse the field is"). Freeze is its
own gate ply.

## Config menu (Options — set-once)
- **Mode** — Tape / Stretch / Env (3)
- **ClockMode** — Steps / Smooth (2)
- **Sense** — Low / Med / High (3 discrete Env thresholds)
- **Grit** — Clean / Normal / Broken = 0 / 0.5 / 1 (3 discrete)

## Removed from the surface
- **Trig** — re-trigger is now **Env-implicit only** (no manual/CV control). This
  drops clocked/CV rhythmic stutter; the Env follower must carry the slicing role.
  **Prerequisite: an Env-tuning pass** so auto-slicing feels good without Trig.
- **LpByp / FldByp** — no dedicated engine bypasses (unit-level bypass suffices).

## Build order
1. **Consolidate plies** — main + shift-row SUBS per the table (custom ViewControl
   pattern; mine habitat prior art: LaretClockControl etc.).
2. **Config MENU** — move Mode/ClockMode/Sense/Grit to Option/MenuControl; Sense +
   Grit as discrete-level Options.
3. **Env tuning** — make auto-slicing carry the (now-removed) Trig role well.
4. **Animated viz** (LAST) — overview/plexus visualizer (Ply 1), looper playhead
   (Ply 2), animated mains (Size/Density/Mix/Freeze). The unit is "all viz/animated."

## Tally (everything accounted for)
- **Live (13):** Clock, Source, DirectLoop, Spread · Speed, Length · Freeze · Size,
  Decay, Mod, Regen · Density, Diffusion · Mix
- **Config (4):** Mode, ClockMode, Sense, Grit
- **Removed:** Trig, (bypass gates never built)

## Notes
- Speed/Length labels reshade per Mode (set in config).
- The atom keeps all DSP params; this phase is UI/Lua reorganization + the Sense/Grit
  discretization + the Trig removal + Env tuning. No core DSP changes expected.
