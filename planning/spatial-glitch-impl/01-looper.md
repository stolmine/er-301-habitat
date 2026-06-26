# 01 — Micro-looper engine (Env / Tape / Stretch)

Combines online research (dsp-research-expert, 2026-06-25) with our in-tree Clouds + MultitapDelay
code. The looper is always-listening, captures input into a short buffer, and replays it
glitched: variable speed incl. reverse + stalled, overdub/layering, fade, freeze. Three
modes (Mood conceit): **Env** (dynamics-gated slice), **Tape** (continuous loop), **Stretch**
(time-stretch). Loop length is CLOCK-derived (see `03-clock-grit.md`), not a manual length.

## Capture (always-on)
- Power-of-two **circular ring buffer**, **float32** (CM4 has RAM; skip Clouds' 16-bit/µ-law
  compaction). Modulo → bitwise AND. 4 s stereo = 1.5 MB; practical micro lengths 0.25–2 s.
- Write head advances unconditionally, overwrites oldest. Read head decoupled, behind write.
  Both are float/fixed phase accumulators (fractional positioning).
- Code to lift: `eurorack/clouds/dsp/looping_sample_player.h` + `audio_buffer.h` (MIT) — the
  whole looper template (Hermite read, crossfade, freeze) ports nearly verbatim.

## Variable speed + reverse + stall
- Signed fractional phase increment per sample: `read_phase += dr; wrap [0,N)`.
  Forward `dr>0`, reverse `dr<0` (decrement + underflow wrap), **stall/freeze `dr=0`**.
- Discrete musical steps = literal LUT `dr ∈ {±4, ±2, ±1, ±0.5}` (the glitch core; Stalled
  is a center detent). One-pole smoother on `dr` (~5 ms) to declick step changes.
- MultitapDelay (`mods/stolmine/MultitapDelay.cpp`) already does per-grain `speed=powf(2,…)`,
  `readPos += reverse?-speed:speed` + linear interp — reference for the per-voice playback.

## Interpolation (the key quality choice) → **4-point cubic Hermite (Catmull-Rom)**
- Linear: 6 dB Nyquist droop, alias floor only −13 dB → avoid for musical use.
- **Hermite: −40 dB first sideband, monotonic rolloff, ~10–12 ops/sample, NEON 4-wide,
  <1% core. Handles reverse (pointer just decrements) and all speed steps unchanged.** This
  is the recommended default. Code: Clouds `ReadHermite` / `stmlib/dsp/delay_line.h`.
- Allpass (Thiran-1): great for slowly-varying vibrato delay, bad for abrupt speed jumps
  (coeff retransients) → not for a step-jumping looper.
- Lagrange-4 / windowed-sinc: −50 to −transparent but ~8–64× cost for marginal gain at our
  modest ≤4× ratios → optional ultra-HQ upgrade only.
- **Antialiasing for fast/reverse reads (`|dr|>1` = decimation):** add a **dynamic 1-pole IIR
  LP on the read output, cutoff = 0.5/max(|dr|,1)** (normalized), bypassed at `|dr|≤1`.
  ~4–6 ops/sample, gives proper AA at 2×/4× fwd+rev for near-zero cost. (Hermite's rolloff
  alone is OK at 2×, marginal at 4×.)

## Overdub / fade / freeze (click-free)
- **Sound-on-sound:** `buf[w] = feedback*buf[w] + input_gain*input`. `feedback`∈(0,1]:
  1.0 = infinite sustain, <1 = tape-style fade per pass. Express decay as dB/s →
  `feedback_per_sample = 10^(decay_dB_per_s/(20·fs))`.
- **Freeze:** `input_gain=0`, `feedback=1.0`, halt write head (read-only) — simplest,
  predictable (Clouds gates `if(!freeze) write_head++`).
- **Loop-seam declick:** 64-sample (~1.3 ms) Hann **crossfade** between outgoing tail and
  loop-start (Clouds uses exactly 64). Optional zero-crossing snap for sparse material.
- **Record start/stop:** raised-cosine ramp `input_gain` 0↔target over 5–10 ms.
- All negligible CPU (per-event, not per-sample).

## Stretch mode → **synchronous granular time-stretch** (primary), **WSOLA** (HQ secondary)
The realistic, in-tree-supported choices:
- **Granular (recommended default):** read head + continuously spawned Hann grains (20–80 ms,
  50%+ overlap), each played at local rate 1.0 (pitch-invariant). Stretch = grain_emit_interval
  / grain_duration. **Reverse = negate per-grain local read increment.** Jitter knob (0–30%)
  destroys metallic comb periodicity. No FFT, no correlation, ~100–200 LOC, <2–4% core,
  deterministic load. In-tree: Clouds `granular_sample_player.h`; same family as ER-301 Grain
  Stretch. Its granular smearing is *aesthetically appropriate* for a glitch looper.
- **WSOLA (upgrade if clean transients matter):** OLA + per-grain cross-correlation seam-match
  (sign-bit, stride-2 → ~256 ops/grain). <3% core. Transient *doubling* is its known artifact
  (acceptable/musical in glitch context). Lift `eurorack/clouds/dsp/wsola_sample_player.h`
  (MIT) wholesale — handles correlator + overlap-add; reverse = negate analysis hop.
- **Phase vocoder:** reserve ONLY for a future spectral sub-mode (10–15% core, phasiness needs
  phase-locking). Not needed for basic stretch — and we already have Sujet for spectral fiction.

## Mode mapping (Mood conceit; controls relabel per mode — see UI pass)
- **Env** — dynamics-gated slice: arm on input level (sensitivity = MODIFY); short grain/slice
  capture. Use granular grains triggered by an envelope follower.
- **Tape** — continuous loop; LENGTH = loop length, MODIFY = speed+direction (the `dr` LUT).
- **Stretch** — granular/WSOLA; LENGTH = slice size, MODIFY = stretch amount+direction.
- Loops persist across mode changes; FREEZE, FADE, 2× length, overdub shared across modes.

## Well-established vs exotic
Everything here is WELL-ESTABLISHED. Granular vs WSOLA is an aesthetic/quality dial, not a
risk. No exotic dependencies.

## Codebase tie-ins
- `eurorack/clouds/dsp/{looping_sample_player,audio_buffer,wsola_sample_player,granular_sample_player}.h`
  — lift directly (MIT).
- `eurorack/stmlib/dsp/delay_line.h` — Hermite/linear read.
- `mods/stolmine/MultitapDelay.cpp` — per-voice variable-speed+reverse reference.
