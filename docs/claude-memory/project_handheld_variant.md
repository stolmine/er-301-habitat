---
name: Handheld variant sketch (decimated I/O)
description: Brainstormed third hardware variant — minimal I/O handheld on Pi Zero 2W class, dpad in place of encoder
type: project
originSessionId: e013ff61-8ec8-4f50-999d-39598e390fe6
---
A third hardware variant beyond the desktop + Eurorack pair in the standalone redesign — a handheld with deliberately decimated I/O. Sketched in conversation 2026-04-28; not yet documented in `er-301/docs/planning/redesign/`. Flagged as "very interesting" but not a primary v1 target.

**I/O surface:** 4 audio output jacks + headphone (pinned to ch1 mono or ch1+2 stereo depending on whether they're ganged). No CV/gate I/O, no i2c, no MIDI, no USB UAC2 host duties. Patches are single stereo chain + globals; chain hierarchy (containers, sub-chains, mixer chains) preserved because complex composition matters even more without external patch cables.

**Compute:** Pi Zero 2W (quad A53 @ 1GHz, ~0.7W idle, fits a small LiPo). Still ~5–10× am335x. Same 256×64 main + 128×64 sub screens.

**Control surface:** dpad replaces encoder. Tradeoff: encoder is continuous (smooth scrub, fine/coarse parameter edits), dpad is discrete — fine for option toggles and small ranges, painful for 0–10s slew or 64-step pattern scrubbing without accel/repeat. Compromise paths if pursued: tiny clickable encoder + dpad combo (TE-style), capacitive strip/wheel substitute, or accept that handheld is preset-driven with fewer per-step edits.

**Why:** the user is keeping hardware options open. The minimal-I/O variant is a natural decimation of the same shared mainboard idea — single stereo chain + globals is enough because all current habitat units make sense without external input (every signal generated internally).

**How to apply:** if the user revisits handheld design, this is the working sketch. Decision points still open: (1) whether handheld shares firmware with desktop/Eurorack or forks the UI, (2) sample rate (48k or 96k like the primary variants), (3) storage (SD vs eMMC), (4) speaker on board or headphone-only. Desktop and Eurorack remain primary; handheld is a "v interesting" follow-on, not on the v1 roadmap.
