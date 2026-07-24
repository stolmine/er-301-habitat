# Spatial-Glitch (CM4) — implementation research & build plan

Branch: `planning/spatial-glitch-cm4`. Started 2026-06-25.
This directory is the **serious implementation plan** for the fused spatial-glitch
unit — the research spine first, then per-subsystem build specs. Each file is kept
small enough for an agent to read in one pass.

## What the unit is (recap)

A CM4-only fused **spatial-glitch instrument** (Mood-MKII paradigm taken past Network):
a short-buffer **micro-looper** (captures input into living, glitched fragments) coupled
to a **spatial field** (builds a room from those fragments), both warped by a single
global **CLOCK** (variable internal sample-rate = tone+length+pitch+grit), with
**bidirectional cross-feedback** between them, Spiral-governed so it sings into
saturation instead of blowing up. Internal-stereo (one object, coherent L/R). Six
control plies × 3 subs: Looper · Field · Regen · Clock · Mix · Routing.
Design source: `../spatial-glitch-unit.md`, `../mood-mkii-architecture.md`.

## Doc map

- **`00-codebase-prior-art.md`** — what we can reuse from this repo (file:line refs). DONE.
- **`01-looper.md`** — micro-looper + Stretch: Hermite + dynamic AA LP; granular-primary /
  WSOLA-secondary stretch; Env/Tape/Stretch modes. DONE.
- **`02-field.md`** — spatial field morph: **Moorer two-stage + Erbe-Verb α-matrix**
  (resolves the Network postmortem) + stereo. DONE.
- **`03-clock-grit.md`** — variable-sample-rate CLOCK (RotCoat harness) + Mirror grit +
  Farrow smooth mode. DONE.
- **`04-fusion-governor.md`** — looper↔field cross-feedback, Spiral + full denormal/DC/NaN
  guard stack. DONE.
- **`05-scaffolding.md`** — package/atom/.swig/Lua + internal-stereo Pattern B + multi-out
  + new-package checklist. DONE.
- **`99-build-order.md`** — phased subsystem build sequence (one at a time). DONE.

## Research status — COMPLETE (2026-06-25)

- Codebase prior art: 3 read-only code-finder agents. → `00`.
- Online research: deep-research **workflow harness crashed** (StructuredOutput retry cap —
  infra failure, not the question). Fell back to **3 dsp-research-expert agents**
  (looper+stretch / clock-grit+feedback / field-topology+stereo), all completed and folded
  into `01`–`04`. Cited sources are inline in those docs.

## The two load-bearing findings
1. **Field = Moorer separation + Erbe-Verb α-morph.** Sparse "glitch" reflections come from
   a FEEDFORWARD multitap + the FDN's NEAR-IDENTITY feedback matrix (structural self-loops),
   never from multitap sums inside the loop. A single α∈[0,1] interpolates the (always
   orthonormal → always stable) feedback matrix I↔Hadamard for the sparse↔dense "plexus"
   axis. This avoids all four Network cascade-FDN failures. (`02-field.md`)
2. **Everything else is well-established + largely in-tree.** Looper/stretch lift from Clouds
   (MIT); CLOCK from RotCoat; grit from Mirror; governor from Spiral; FDN/Householder from
   the house reverbs; allpass tank + Brownian mod from Fabula. The build is integration, not
   invention — except the α-morph, the one published-but-rarely-coded novelty.

## Method

Per the user: research is the start; then **tackle each subsystem one at a time**
(implement → build → install to CM4/emu → audition → commit), the same incremental
rhythm used for Fabula and Sujet. No third-party branding — codename still TBD
(NOT "Mood"). CM4-only, unapologetic.
