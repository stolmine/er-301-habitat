# Sujet — status & resume point

Last updated: 2026-06-24. Branch: `planning/zaum-woven-reverb` (pushed to origin).
This is the "where things are headed" handoff so work can resume on another machine.

---

## Where it stands — dev 0.2.0.11

Sujet is **Zaum Phase 2**, the spectral *fiction* reverb (the сюжет counterpart to
Fabula's believable room). Package `mods/zaum/`, atom `mods/zaum/atoms/STFTSpectral.h`,
unit `mods/zaum/assets/Sujet.lua`. Built and emu-auditioned incrementally (0.2.0.1 →
0.2.0.11), every sub-phase committed.

**Engine (Spectral Magnitude Decay):** STFT (ShyFFT, N=1024, hop 256, 4×/75% sine-window,
COLA-verified; inherent latency ~26.7 ms presented as predelay) → per-bin magnitude
accumulator `magAcc[k] = inGain*|X| + magAcc[k]*g(k)`, `g(k)=10^(-3R/(RT60·fs))` →
temporal Blur/Bloom (`blurState`/`bloomState`) → synth `m*e^{jφ}`, `φ=phase+V*ξ`
(Diffuse) → **Spiral wet-output governor** (clip/heat-death safety) → Mix.

**Live params (7):**
- **Decay** → per-bin RT60 / g(k) (~0.3–120 s).
- **Damp** → HF decay tilt.
- **Diffuse (V)** → per-bin phase randomization. THE headline voicing: coherent/mechanical
  → lush diffuse noise → whisperization. The believable↔mangler axis (Vickers SMD).
- **Freeze** → decay-extension (gEff→1) + floored input (kFreezeInFloor) → true infinite
  hold; governed. (NB: an earlier snapshot/hold rewrite was rejected — this is the keeper.)
- **Smear** → bipolar consolidation of Blur+Bloom: `<0.5` → Bloom (asymmetric slow-rise
  swell + HF freq-stagger), `>0.5` → Blur (cross-time IIR magnitude smear), `0.5` = off.
- **Predelay** → inert stub (inherent latency only; wire later).
- **Mix** → dry/wet (dry latency-aligned by 1280 smp).

Research basis is solid and verified: `planning/sujet-design.md` (impl-ready design),
`sujet-blur-research.md`, `sujet-bloom-research.md`, `sujet-fiction-research.md`.

---

## What's NEXT — the fictions (the сюжет identity)

The current ops are all *believable* spectral-reverb moves. Sujet doesn't yet do the
*impossible* things that earn its "fictional reverb" name. The user chose three fiction
ops (Shimmer deliberately skipped as too expected). **Build order, lowest-risk first
(`planning/sujet-fiction-research.md` has exact equations + state + stability for each):**

1. **Spray** (BUILD FIRST) — per-bin **magnitude** noise injection in the synth stage
   (Loris/SMS bandwidth-enhanced model). `m = bloomState[k] + ε·bloomState[k]·rand`,
   `ε = Spray·kSprayMax (~0.3)`. Apply INSIDE the synth pass (never to bloomState) →
   zero accumulator feedback → unconditionally safe. Zero new state, uses existing PRNG.
   Distinct from Diffuse: Diffuse = phase jitter; Spray = added noise *energy*. ~15 lines.

2. **Warp** — power-law inharmonic frequency-axis remap. `warpedBin = (i/Nyq)^α · Nyq`,
   `α = exp(2·(Warp−0.5))` → bipolar around 0.5 (identity): compress↓ / stretch→
   bell-like inharmonic. Read via interp from a `warpScratch[513]` copy of bloomState;
   energy-conserving (no runaway); phase mismatch absorbed by Diffuse for v1.
   **Freeze+Warp = a frozen inharmonic bell drone** — the headline impossible sound.
   Lineage: Clouds `WarpMagnitudes` (vendored) + Risset stretched partials. ~30 lines.

3. **Scramble** — stochastic band-swap: width `W = Nyq·Scramble·0.3`, swap prob
   `P = Scramble²`, plus Clouds "hold-and-blow" (case 0 of `AddGlitch`) at the extreme.
   `scrambleScratch[513]`; energy-conserving; the заумь edge. Trickiest param voicing
   (subtle→chaotic curve). ~50 lines.

Each: research (done) → implement → emu-audition → commit. **Param surface** grows to
~10 (Decay, Damp, Diffuse, Freeze, Smear, **Warp, Scramble, Spray**, Predelay, Mix) —
workable in the expanded view; consider grouping or a "Fiction" menu at the polish pass.

---

## Remaining after the fictions

- **Transient handling** (deferred): onset detect → reset transient-bin phase + suspend
  accumulation across attacks (kill pre-echo / musical noise; Röbel DAFx-03).
- **UI / param-surface polish**, labels, defaults review, first user-facing version.
- **CM4 hardware audition** — everything is emu-only (darwin) so far.
- Then **Phase 3 — Portals**: couple Fabula + Sujet (per `zaum-roadmap.md`).

---

## Build / install / resume (darwin emu on this Mac)

- **Build:** `make zaum` from repo root → `testing/darwin/zaum-<ver>.pkg`.
- **Install for the darwin emu** (NOT `make zaum-install` — its `cp → ~/.od/rear/` is the
  am335x-hardware path, wrong for the emu): copy the pkg to
  `~/.od/front/ER-301/packages/` AND extract it into `~/.od/rear/v0.7/libs/zaum/`
  (`unzip -o`), then **restart the emu** and re-add Sujet. See the
  `project_zaum_darwin_install` memory and `docs/dev-rig-procedures.md`.
- am335x cross-compile is unavailable on this Mac (SWIG binary missing); Zaum is CM4-only.
- Offline validation rigs live in the session scratchpad (throwaway, not committed):
  the Fabula density rig and the Sujet STFT transparency rig.

---

## Context

- **Fabula (Phase 1)** is complete through dev 0.1.0.12 (`mods/zaum/atoms/APFTank.h`) —
  the believable Dattorro/Gardner allpass-tank room, fully voiced (room↔hall via Size/
  Decay/Early, governed). The package `mods/zaum/` ships both Fabula and Sujet.
- North star + roadmap: `planning/zaum-design.md`, `planning/zaum-roadmap.md`.
- The Zaum flagship (Phase 5) reuses both atoms (APFTank, STFTSpectral) verbatim via the
  procedural field; Sujet's per-bin arrays are the field's spectral-element hook.
