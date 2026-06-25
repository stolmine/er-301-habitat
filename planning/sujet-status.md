# Sujet — status & resume point

Last updated: 2026-06-25. Branch: `planning/zaum-woven-reverb`.
Resume entry point for work on Sujet (Zaum Phase 2, the spectral reverb).

---

## Where it stands — dev 0.2.0.15

Package `mods/zaum/`, atom `mods/zaum/atoms/STFTSpectral.h`, unit
`mods/zaum/assets/Sujet.lua`. Built/emu-auditioned incrementally (0.2.0.1 → 0.2.0.15),
each sub-phase committed. Darwin-emu only so far.

**Engine:** STFT Spectral-Magnitude-Decay (ShyFFT N=1024, hop 256, 4×/75% sine-COLA,
inherent latency 1280 smp ≈ 26.7 ms as predelay; ShyFFT vendored — no new FFT dep).
3-pass `smdProcess` per channel: (1) accumulate `magAcc[k]` (SMD + Freeze + Damp) +
`blurState[k]` (cross-time IIR) + store phase; (2) `bloomState[k]` (asymmetric IIR) +
`localRef[k]` (boxcar envelope for prominence); (3) synth `m·e^{jφ}` with Diffuse(V),
Spray, Space (prominence-weighted), Distance air-absorption. Spiral wet governor → ITDG
wet-delay → DRR wet-bias → Mix.

**Live params (9):**
- **Decay** — per-bin RT60 / g(k). **Damp** — HF decay tilt.
- **Diffuse (V)** — per-bin INTRA-channel phase randomization (line→noise; the
  believable↔mangler/diffuseness axis).
- **Freeze** — decay-extension + floored input → infinite hold (governed).
- **Smear** — bipolar Blur (cross-time magnitude IIR, >0.5) / Bloom (asymmetric swell +
  freq stagger, <0.5); 0.5 = off.
- **Spray** — per-bin synth-stage magnitude noise injection (breathy halo; ≠ Diffuse).
- **Space** — inter-channel decorrelation (width/envelopment): static freq-smoothed
  anti-symmetric (±δ L/R) per-bin phase offset, frequency-weighted (peak ~600 Hz, taper
  >3 kHz), NOW prominence-weighted (partials stay frontal, noise spreads — leap below).
  Mono fan-out (In1→both inlets) so mono sources spread. Default 0.4.
- **Distance** — depth/scale macro (repurposed the old inert Predelay slot): ITDG wet
  pre-delay (≤100 ms) + HF air-absorption (>4 kHz) + DRR wet-bias (≤+10 dB, separate from
  Mix → source recedes WITHOUT going quiet). Default 0.25.
- **Mix** — dry/wet (dry latency-aligned 1280 smp).

---

## The current arc — the SPATIAL pivot

User critique that drove the last several sub-phases: *"Sujet lays on top of its input
rather than enveloping it in space, and tends toward noise."* Diagnosis (research-backed):
the old Diffuse knob CONFLATED inter-channel decorrelation (space) with intra-channel
phase noise — so you couldn't get space without noise.

**Key framing established:** Diffuse & Freeze are "what the sound IS" transforms (they
keep perceptual DOMINANCE, by design). Space / Distance / the spatial leaps are "where it
SITS" — the frame, intentionally subtler. The goal is NOT to displace diff/frz, but to
give the fiction a believable enveloping space. User wants to keep pursuing the spatial
road (the content-interactive leaps haven't all landed) before deciding whether it can
reach co-equal footing. Distance felt "noticeable but subtle" → its maximums were bumped
(0.2.0.15). Diffuseness-by-prominence (0.2.0.15) is the first content-interactive spatial
leap (the real test of the road).

Research docs: `planning/sujet-spatial.md` (envelopment/decorrelation — Griesinger/Kendall/
Pulkki-DirAC), `planning/sujet-depth-research.md` (distance/size/depth — Zahorik/Bronkhorst/
Beranek ITDG), `planning/sujet-fiction-research.md` (Warp/Scramble/Spray),
`planning/sujet-blur-research.md`, `planning/sujet-bloom-research.md`, `planning/sujet-design.md`.

---

## What's NEXT (priority order)

**Spatial road (continue):**
1. **Coherence-collapses-over-decay** — per-bin `cohEnv[k]` (high at onset → falls over
   decay) scaling the Space decorrelation: reverb ARRIVES focused/frontal and OPENS into
   surrounding space as it rings out (early-ASW / late-LEV split + the ≥160 ms criterion,
   per bin). Builds the `cohEnv` infrastructure that the depth fusion needs.
2. **Per-bin / per-frequency DEPTH fusion** (novel; needs cohEnv): tie per-bin DRR/distance
   to cohEnv AND to the prominence map (strong partials near/frontal, noise far/surrounding)
   → depth that evolves over the decay. (`sujet-depth-research.md` §5 flagged these as the
   leaps a time-domain reverb can't do.)
3. **Per-bin spatial TRAJECTORIES** (exotic / "twist it around"): bins migrate
   frontal→lateral→rear by frequency over the decay (90°<700 Hz / medial>700 Hz /
   150°>2 kHz frequency-angle map). The most experimental spatial move.

**Deferred FICTIONS (the сюжет ops, build order from `sujet-fiction-research.md`):**
- **Warp** — power-law inharmonic frequency remap (α=exp(2·(Warp−0.5)); bipolar @0.5;
  Freeze+Warp = frozen inharmonic bell). **Scramble** — stochastic band-swap + Clouds
  hold-and-blow. (Shimmer deliberately skipped.) Spray already done (gap-fill variant was
  reverted; per-bin baseline kept).

**Then:** transient handling (onset → phase reset + suspend accumulation), UI/param-surface
polish + first user-facing version, **CM4 HARDWARE audition** (emu-only so far), then
Phase 3 Portals (Fabula+Sujet coupling).

**Param-surface note:** at 9 plies; Warp/Scramble would push to 11 — consider a "Fiction"
menu or grouping at the polish pass. The spatial leaps (coherence-over-decay, depth fusion,
trajectories) are INTRINSIC (no new params — they modulate Space/Distance per bin).

---

## Build / install / resume (darwin emu)
- Build: `make zaum` → `testing/darwin/zaum-<ver>.pkg`.
- Install (NOT `make zaum-install` — that's the am335x path): `cp` the pkg to
  `~/.od/front/ER-301/packages/` AND `unzip -o` it into `~/.od/rear/v0.7/libs/zaum/`,
  then restart the emu + re-add Sujet. (`project_zaum_darwin_install` memory; docs/dev-rig-procedures.md.)
- am335x cross-compile unavailable on this Mac (SWIG missing); Zaum is CM4-only.
- Most tuning constants are hoisted `static const` at the top of STFTSpectral.h
  (Space: kSpacePeakHz/kSpaceTaperHz/kSpaceFloor; Distance: kITDGMax/kDistanceWetExp/
  kAirAbsHz/kAirAbsMax; prominence: kPromSlope/kPromBlend/kPromBoxHalf; etc.).

## Context
- **Fabula (Phase 1)** complete through 0.1.0.12 (`atoms/APFTank.h`) — the believable
  Dattorro/Gardner room. Package ships both Fabula + Sujet.
- North star + roadmap: `planning/zaum-design.md`, `planning/zaum-roadmap.md`. Zaum (Phase
  5) reuses both atoms via the procedural field; Sujet's per-bin arrays are the spectral hook.
