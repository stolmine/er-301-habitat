# 00 — Codebase prior art (the reusable spine)

Distilled from three read-only code-finder sweeps (2026-06-25). Everything here already
exists in-repo and is liftable/adaptable. Organized by subsystem. File:line refs are
starting points, not exact (verify on read).

---

## A. Micro-looper / variable-speed playback

### Buffers & interpolation
- **Clouds `AudioBuffer`** — `eurorack/clouds/dsp/audio_buffer.h`. Circular record buffer,
  16/8-bit/8-bit-µlaw resolutions, `write_head_` wrap, an 8-sample interpolation tail
  mirrored at the wrap so reads never straddle the seam. `Write()` (single-sample),
  `WriteFade()` with a `kCrossFadeSize=256` tail + crossfade counter → **click-free
  record start/stop**. Three read methods template-specialized: ZOH / Linear / Hermite.
- **Hermite (Laurent de Soras 4-point)** — `audio_buffer.h` ReadHermite + `stmlib/dsp/delay_line.h`
  ReadHermite. The repitch-quality interpolator. Linear is `a+(b-a)*frac`.
- **Clouds `LoopingSamplePlayer`** — `eurorack/clouds/dsp/looping_sample_player.h`. Naive
  looped playback with fractional read pointer (`phase_`, `phase_increment_ =
  SemitonesToRatio(pitch)`), sync-to-tap mode, and a **freeze** crossfade envelope. This
  is the closest single-file looper template.

### Variable-speed + reverse + grains
- **stolmine `MultitapDelay`** — `mods/stolmine/MultitapDelay.{h,cpp}`. Per-grain struct
  `{phase, readPos, phaseDelta, speed, active, reverse}`. Variable speed:
  `gr.speed = powf(2, vOct + pitch/12)`; reverse: `readPos += reverse ? -speed : speed`;
  linear interp on read. Per-tap SVF (LP/BP/HP/Notch) + feedback damping + Q-comp. This
  is the **discrete-speed-step + reverse + stalled (speed→0)** reference.

### Time-stretch (Stretch mode) — already in-tree
- **Clouds `GranularSamplePlayer`** — `eurorack/clouds/dsp/granular_sample_player.h` +
  `grain.h`. 64 grains, LOW/MED/HIGH quality, probabilistic+deterministic scheduling
  (≤4 spawns/block), 16.16 fixed-point phase, per-grain stereo gain, triangular/Hann
  window (fold at phase=1), overlap-add by summation, `1/sqrt(N-1)` gain norm.
- **Clouds `WSOLA` player** — `eurorack/clouds/dsp/wsola_sample_player.h` + `window.h`.
  Two overlapping windows, correlation-based seam finding, Hermite read + triangular
  envelope. **This is the realistic Stretch engine** (pitch-independent time-stretch,
  cheaper/cleaner than phase-vocoder).

### Reusable for the looper
Circular buffer + crossfade-write (Clouds), Hermite/linear interp (stmlib), per-grain
variable-speed+reverse (MultitapDelay), WSOLA for Stretch, granular for Env-mode slices.
Stalled = speed 0. Freeze = hold pointer + decay envelope. Overdub = feedback multiplier
on write-back + optional tone filter.

---

## B. Spatial field — and the Network postmortem (READ FIRST)

### Network (the lineage)
- `mods/spreadsheet/Network.{h}` (header-only, ~2226 lines) + `network/geometry.h`
  (phyllotaxis golden-angle reflector field) + `network/trig_lut.h` (Bhaskara sin/cos).
- 64 taps reading ONE shared int16 circular buffer, **parallel star** (not cascade).
  Per-tap from geometry each block: `delayTarget` (listener→reflector distance × size),
  `gainL/gainR` (azimuth pan, no atan2), density-comp gain `2.5·activeTaps^-0.4`.
- **3-pass NEON** (the workhorse pattern, ~line 1437+): Pass A dual-read advance
  (forward/back idx, branchless NEON wrap); Pass B scalar gather w/ 8-sample prefetch;
  Pass C NEON 4-wide FMA (old→new crossfade, per-tap 1-pole LP, optional bitcrush,
  gain FMA into wetL/wetR/fbSum). **Dual-read crossfade** (`w=1−i/(FRAMELEN−1)`) is the
  Doppler-free delay-modulation trick → no close-pass impulse.
- Per-tap shimmer LFO (decorrelated rate/phase per tap), per-tap static pitch detune
  (`conn·24smp·±hash`), sparse feedback select (every-stride, ±sign hash, **1/√k norm**),
  4-stage Schroeder allpass diffusion (primes 167/263/419/677), fast tanh (Padé 3/3).
- Glitch suite (mutex modes by per-tap hash thresholds): MUTE/STUTTER/CRUSH/SCRUB/
  REVERSE/NORMAL, plus transient-ricochet peak follower. Directly reusable as the
  "addressable/glitchy" sparse end.

### ⚠ Network cascade-FDN postmortem — `planning/network-cascade-postmortem.md`
The attempt to retrofit a true FDN onto the multitap FAILED (20+ thrashing commits).
**The governing hazard for this unit's Field engine.** Causes:
1. **Per-sample vs per-round-trip Jot math** — Jot-1991 T60 assumes the matrix applies
   once per round-trip; applied per-sample to a varying-density state it collapsed T60
   by `D_g`. No single formula spanned the density range.
2. **Multitap output comb peaks** — tap-mean `|H(jω)| ≠ 1` → comb peaks inside the
   feedback loop → loop gain >1 at peaks → unstable even under tanh.
3. **Density × 1/√N normalization entanglement** — per-line gain scaled DOWN with N while
   the multitap wet bus scaled UP → lushness inverted (dense=thin, sparse=lush).
4. **Size × density × T60 cancellation** — Jot exponent went size-invariant; small-size
   ringing character lost.
5. **Regime crossing** — sparse and dense are different mathematical regimes; sweeping
   across them mid-session had no transition function.

**Lessons (bake into Field design):**
- **SEPARATE early-reflection multitap (coloration, |H|≠1) from the late tail (a fixed,
  unitary FDN).** Do NOT fuse them. This is the single most important rule.
- Choose a **fixed FDN structure (4 or 8 lines)**; decouple parameter math from matrix
  dimension — never tie matrix size to a user knob.
- Per-line frequency-dependent damping (true Jot: each line its own filter, same T60).
- **Build a numerical T60 rig** (impulse → −60 dB crossing) and gate by measurement, not
  ear (same discipline as Fabula's echo-density rig).
- When you start reverting every other commit, STOP and reach for a known-good reference.

### Dense/diffuse end — Fabula (proven)
- `mods/zaum/atoms/APFTank.h`. Dattorro figure-8 tank, **series-cascade** allpasses
  (provably unity-gain; in-feedback nesting ran away — avoid), Brownian per-line delay
  modulation (decorrelated LCG seeds), signed Dattorro multi-tap output, Moorer ER FIR
  (parallel, orthogonal to tank), Spiral governor on the feedback path. Reusable as the
  lush end and as the cross-coupled internal-stereo template.

### House FDNs (the unitary-tail recipe)
- `mods/house/atoms/{Galactic,Verbity,CreamCoat}.h`. **diff-Householder 4×4** mixing
  (`a' = a−(b+c+d)` etc.) — unitary, involutive, 4 sub + 4 add per stage, no matmul.
  Cascade stages → 4^stages lines (CreamCoat = 16). Galactic adds quadrature predelay
  LFO + full L↔R cross-coupling; Verbity adds per-line feedback smoother + thunder
  sub-bass chase (lighter, ~12–15% stereo); CreamCoat adds Bezier undersampling
  (cycle-rate FDN, audio-rate output). **Galactic ~25–30%, Verbity ~12–15%, CreamCoat
  ~18–22% stereo** — sizing anchors.

### Multitap infra
- `mods/spreadsheet/Pecto.{h,cpp}` — 3-pass NEON tap gather, Doppler base-delay one-pole
  smoother (`+= (target−s)·α`, α for ~25 ms), float-precision idx-wrap ulp guard.
- `mods/spreadsheet/Larets.cpp` — shared 96k buffer, write/fractional-read heads, 10
  per-step effects (stutter/reverse/bitcrush/downsample/filter/pitch/...), clock-edge
  detect + skew, 128-sample viz ring. Good clock-driven-trigger + step-sequencing model.

---

## C. Clock / sample-rate-grit (the CLOCK axis)

- **RotCoat** — `mods/house/atoms/RotCoat.h`. The reduced-rate harness: per-line
  `cyclePhase[i] += cycleStep[i]` (= `baseStep·spreadFactor[i]`); when `cyclePhase ≥ 1`
  the line FIRES (write in, read tap, update feedback); linear interp between cycle
  outputs (`prev + (out−prev)·phase`). `worldRate ∈ {1,2,3,4,6,8}` snapped; Mulch fans
  rates log-symmetrically. **A `while(cyclePhase≥1)` wrap lets a line fire multiple
  times/sample at high rate → intentional rate-mismatch aliasing, bounded.** Anti-alias
  guard: Spiral saturate + per-line 1-pole LP (~500 Hz·worldRate) restores bass and stops
  HF runaway. This is the closest existing thing to the MOOD CLOCK.
- **Mirror** — `mods/spreadsheet/Mirror.{h,cpp}` (`MirrorBlock`). Alias-as-SYNTHESIS:
  (1) tanh pre-sat generates >Nyquist harmonics; (2) divider-clocked S&H **without**
  anti-alias folds them back in; (3) bit-depth quantize (mid-riser); (4) reconstruction
  blend with **Nyquist-polarity flip** (`held + (held·sign−held)·flip`, sign alternates).
  The deliberate-grit / CLASSIC-broken reference.

---

## D. Fusion / governor / stereo

- **Spiral governor** — `mods/house/atoms/Spiral.h`. `spiralSaturate(x, d) =
  sign(x)·sin(min(|x|·d, π/2))/d` → output bounded to `±1/d`, soft cliff at ±1 when d=1.
  Fast Taylor variant ~20× cheaper (≤0.45% error). Used in RotCoat: Householder feedback
  → `spiralSaturate` → LP → `mFeedback[i]`. Inactive under normal gain; a hard transient
  wall. **The cross-feedback runaway guard.**
- **Internal-stereo "Pattern B"** — CreamCoat.h / APFTank.h: ONE `od::Object`, `addInput
  (mInL/mInR)`, `addOutput(mOutL/mOutR)`, parallel per-channel state arrays
  (`mTA1_L/mTA1_R`), cross-coupled feedback (`mFeedback_L` fed from R, vice-versa), a
  single `process()` loop. This is the unit's stereo template (shared coherent field, not
  dual mono).

---

## E. Scaffolding (new CM4 package)

- **mod.mk** (ref `mods/zaum/mod.mk`): `PKGNAME`, `PKGVERSION` (M.m.p.sub),
  `INCLUDES = $(MOD_DIR) mods mods/house/atoms $(SDKPATH) ...` (cross-package atom reuse),
  `SWIG_HEADER_DEPS := $(call rwildcard,$(MOD_DIR),*.h)`.
- **`.cpp.swig`** (ref `mods/zaum/zaum.cpp.swig`): `%module`, include `mod.cpp.swig`; in a
  `%{ #undef SWIGLUA … #define SWIGLUA %}` block include atom headers for C++; then
  `%include "atoms/Foo.h"` for SWIG/Lua binding. Component-only atoms (Spiral) stay
  wrapped in `#ifndef SWIGLUA`; od::Objects exposed.
- **Atom header shape** (ref `RotCoat.h`): `#pragma once`; ctor with
  `addInput/addOutput/addParameter` + state init OUTSIDE guards; member decls
  (`od::Inlet/Outlet/Parameter`) + `process()` INSIDE `#ifndef SWIGLUA`.
- **Lua unit** (ref `WoodenBox.lua`/`Sujet.lua`): `addObject` the atom, `connect` In/Out,
  per param `ParameterAdapter` + `hardSet("Bias",…)` + `tie(op,Param,adapter,Out)` +
  `addMonoBranch`; `onLoadViews` GainBias plies; expanded/collapsed lists.
- **Multi-out framework** (ref `Mirror.lua`): `args.channelCount=N`, `args.subOutLabels={…}`,
  write each outlet's `.buffer()` in `process()`, `connect(op,"OutX",self,"OutN")` last.
  Use for dry-loop / wet-field / per-stage taps.

---

## Headline takeaways for the build

1. **Field rule #1:** early-reflection multitap (coloration) and the late FDN tail
   (fixed 4/8-line unitary Householder) are SEPARATE structures. The sparse↔dense morph
   is a crossfade/weighting between them, not one network mutating its own order.
2. WSOLA (in-tree, Clouds) is the realistic Stretch engine; granular for Env slices.
3. RotCoat's per-line undersampling harness is the CLOCK prototype; Mirror is CLASSIC grit.
4. Spiral is the proven cross-feedback governor; CreamCoat/APFTank the internal-stereo shell.
5. Gate the Field with a numerical T60/echo-density rig before trusting ears (Fabula method).
