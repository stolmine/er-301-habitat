# 06 — UI surface (Phase 5): control layout & config relegation

Status: PROPOSAL (decisions pending — see "Open forks"). Drafted 2026-06-26 after
the full DSP + routing build (dev 0.2.0.20). Goal: fold the ~19 flat dev controls
into the designed 6-ply surface (`Looper · Field · Regen · Clock · Mix · Routing`)
with per-mode relabeling, moving set-once params to a config menu.

## Full inventory (19 live controls at 0.2.0.20)

| Control | Type | Function | Performed? |
|---|---|---|---|
| Mode | option | Tape / Stretch / Env | occasionally |
| Length | knob | loop / slice size | often |
| Speed | knob (±2) | pitch (Tape) / time (Stretch) | often |
| Sense | knob | Env transient threshold | ONLY in Env |
| Freeze | gate | hold the loop | often |
| Trig | gate | capture / re-trigger / stutter | often (clocked) |
| Size | knob | field extent / delay scale | often |
| Decay | knob | RT60 | often |
| Diffusion | knob | input smear | set-once-ish |
| Density | knob | plexus morph (taps↔FDN + α) | often (signature) |
| Mod | knob | FDN de-metallic chorus | set-once-ish |
| Regen | knob | cross-feedback | often |
| Clock | knob | internal rate (the soul) | often |
| ClockMode | option | Steps / Smooth | set-once |
| Grit | knob | clean↔broken | per-patch, sometimes performed |
| Mix | knob | dry/wet | often |
| Source | knob | field source input↔loop | per-patch |
| DirectLoop | knob | clean loop blend | sometimes |
| Spread | knob | stereo width | set-once-ish |

Types: 15 GainBias knobs, 2 Comparator gates (Freeze, Trig), 2 Options (Mode, ClockMode).

## Proposed 6-ply live surface (main + shift-row subs)

1. **LOOPER** — gate main **Trig** · subs **Length · Speed · Mode**
2. **FIELD**  — gate main **Freeze** · subs **Size · Decay · Density**
3. **REGEN**  — knob main **Regen** · subs **DirectLoop · Source · —**
   (coupling lives here: feedback + how the loop reaches the field/output)
4. **CLOCK**  — knob main **Clock** · subs **Grit · — · —**
5. **MIX**    — knob main **Mix** · subs **Spread · — · —**
6. *(spare / field-overview viz — the plexus/field visualization as the expanded
   view of FIELD, or a dedicated overview ply)*

Gates Trig & Freeze become the Looper/Field gate-mains (spec-faithful, CV-clockable).

## Config-menu candidates (set-once → unit menu)
- **ClockMode** (Steps/Smooth) — pure behavior toggle. STRONG config.
- **Mod** (FDN de-metallic) — voicing; set a depth and leave it. Lean config.
- **Diffusion** (input smear) — voicing; rarely swept live. Lean config.
- **Sense** (Env threshold) — only meaningful in Env → handle via adaptive relabel
  (see below) rather than a dedicated control.

Relegating these clears the surface enough for the clean 6-ply.

## Adaptive (mode-dependent) relabels (the Mood "knob changes per mode" idea)
- **Speed → Sense** in **Env** mode (Mood: MODIFY = sensitivity in Env) — removes the
  dedicated Sense control entirely.
- **Length / Speed** labels reshade per Mode (Tape: speed/length; Stretch: stretch/slice;
  Env: slice / sensitivity).

## Open forks (decisions pending)
1. **Gates as ply-mains** (Trig/Freeze on Looper/Field, spec-faithful) **vs dedicated gate
   plies** (simpler to build, costs 2 slots). Lean: gate-mains.
2. **Config aggressiveness:** minimum (just ClockMode) vs maximum (ClockMode + Mod +
   Diffusion + Sense-adaptive). Lean: maximum (gets to a clean 6).
3. **Mix sub-toggles** (Dry-kill / Trails from the spec — we don't have them). Add as menu
   toggles or skip? Dry-kill ≈ Mix at 1 already.
4. **Build cost:** the "main + 3-sub shift row" needs a custom ViewControl
   (LaretClockControl-style, see feedback_clock_control). Build it for the real surface,
   or ship a simpler grouped-flat + menu first and add sub-rows later?

Recommendation: gate-mains, maximum config relegation, build the custom sub-row control.

## Then
After the surface lands: first user-facing version (labels/descriptions/mnemonic polish),
multi-out taps (dry-loop / wet-field / per-stage), then Phase 6 CM4 hardware audition + CPU/
memory profile (worst case: Stretch + dense field + Regen + low Clock, stereo).
