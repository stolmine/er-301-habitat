# Plaits per-engine NEON viability recon + 6-op FM noise notes

**Date: 2026-05-15.** Triage doc, not a full work plan — produces
the engine matrix the user requested + flags the 6-op FM noise as
a separate (non-NEON) follow-up.

## Engine dispatch (verified from `eurorack/plaits/dsp/voice.cc`)

stolmine's Plaits build registers engines in this order (verified
against `voice.cc:41-66`, not MI's documented numbering):

| Slot | Engine | File | Notes |
|---|---|---|---|
| 0 | Virtual Analog VCF | `engine2/virtual_analog_vcf_engine.cc` | Modern Plaits |
| 1 | Phase Distortion | `engine2/phase_distortion_engine.cc` | Modern Plaits |
| 2,3,4 | **Six-Op FM (×3)** | `engine2/six_op_engine.cc` | Modern Plaits — 3 separate slots, 3 patch banks |
| 5 | Wave Terrain | `engine2/wave_terrain_engine.cc` | Modern Plaits |
| 6 | String Machine | `engine2/string_machine_engine.cc` | Modern Plaits |
| 7 | Chiptune | `engine2/chiptune_engine.cc` | Modern Plaits |
| 8 | Virtual Analog | `engine/virtual_analog_engine.cc` | Classic |
| 9 | Waveshaping | `engine/waveshaping_engine.cc` | Classic |
| 10 | FM (2-op) | `engine/fm_engine.cc` | Classic, 4× oversampled |
| 11 | Grain | `engine/grain_engine.cc` | Classic |
| 12 | Additive | `engine/additive_engine.cc` | Classic, 36 partials in 3×12 batches |
| 13 | Wavetable | `engine/wavetable_engine.cc` | Classic |
| 14 | Chord | `engine/chord_engine.cc` | Classic |
| 15 | Speech | `engine/speech_engine.cc` | Classic |
| 16 | Swarm | `engine/swarm_engine.cc` | Classic, 8 voices |
| 17 | Noise | `engine/noise_engine.cc` | Classic |
| 18 | Particle | `engine/particle_engine.cc` | Classic |
| 19 | String | `engine/string_engine.cc` | Classic, KS-style |
| 20 | Modal | `engine/modal_engine.cc` | Classic |
| 21 | Bass Drum | `engine/bass_drum_engine.cc` | Classic |
| 22 | Snare Drum | `engine/snare_drum_engine.cc` | Classic |
| 23 | Hi-Hat | `engine/hi_hat_engine.cc` | Classic |

## NEON viability summary

(Sorted by win-per-effort. **HIGH** = clean cross-partial/voice NEON
axis, polynomial DSP only. **MED** = mixed — some hot work
vectorizes, some doesn't. **LOW** = LUT-dominated or fundamentally
serial. **N/A** = no SIMD axis exists.)

| Rank | Slot | Engine | Viability | Cost share | Notes |
|---|---|---|---|---|---|
| 1 | 16 | **Swarm** | HIGH | high | 8 independent voices (saw + sine per voice), polynomial. 4-lane NEON → 2 quads per sample. ~3–4× kernel. Popular sound. |
| 2 | 12 | **Additive** | HIGH | high | 36 harmonic partials in 3 batches of 12. Already structured as SoA inside `HarmonicOscillator`. **Verify** whether MI upstream already NEON'd this before duplicating effort. |
| 3 | 8 | **Virtual Analog** | HIGH | high | 2–4 oscillators (variant-dependent), polynomial throughout (no LUT in hot path). Cross-osc NEON 4-lane. ~3–4× on the inner loop. The bread-and-butter VA engine. |
| 4 | 14 | **Chord** | MED | high | 5 voices. Divide-down voices are polynomial (HIGH); wavetable voices have LUT reads (LOW). Mode-dependent — vectorize the divide-down path, leave wavetable path scalar (or LUT-strip). |
| 5 | 7 | **Chiptune** | HIGH | med | 5 polynomial voices (variable-shape square/triangle). Clean SIMD. Less popular than VA so lower cost share. |
| 6 | 20 | **Modal** | MED | med | Multiple resonator modes — same shape as Rings modal (already NEON'd in `mods/mi/rings/dsp/resonator.cc`). Check if Plaits' modal_engine uses the same Resonator class or a separate impl. |
| 7 | 18 | **Particle** | MED | med | 6 grain generators, polynomial. Optional FDN diffuser is sequential. If diffuser is bypassed or block-rate-amortizable, HIGH. |
| 8 | 21,22 | **Bass / Snare Drum** | MED | high | 2-model engines (analog/synthetic). If analog model is a complex state machine, NEON-blocked; if it's a simple FM-or-noise + envelope, HIGH. Need a deeper read. |
| 9 | 6 | **String Machine** | MED | med | 5 chord voices through divide-down or wavetable, then 2-stage SVF + ensemble FX. SVF is the Filterbank-class pattern (proven HIGH); voices mixed. |
| 10 | 23 | **Hi-Hat** | HIGH | low | 2 noise sources + envelope. Trivial NEON but small CPU footprint. |
| 11 | 19 | **String (KS)** | MED | med | 3 Karplus-Strong voices. Same shape as Rings String — delay-line reads inherently scalar (no gather), filter portion vectorizable. Same payoff as Rings String at poly-1 (~25% filter portion). |
| 12 | 17 | **Noise** | MED | low | 2 noise + multimode filter. Niche use. |
| 13 | 5 | **Wave Terrain** | MED | low | 2× oversampled. Polynomial terrain modes vectorize (HIGH); LUT terrain modes blocked. Niche. |
| 14 | 0 | **Virtual Analog VCF** | MED | med | 2 osc + 2-stage SVF. Mixed — osc path HIGH, SVF chain MED. |
| 15 | 11 | **Grain** | MED | low | 2 grainlets + Z-osc + DC blockers. Polynomial windowing. |
| 16 | 13 | **Wavetable** | LOW | med | 2D LUT (bilinear interp per sample). No gather → LOW. The interpolation arithmetic is polynomial but blocked by the LUT step. |
| 17 | 9 | **Waveshaping** | LOW | med | LUT waveshaper in hot path. No gather. |
| 18 | 10 | **FM (2-op classic)** | LOW | med | 4× oversampled, Sine LUT per operator per sample. Same no-gather constraint as Rings FM. Polynomial sine substitution = tone audition gate (separate work). |
| 19 | 1 | **Phase Distortion** | LOW | low | LUT-heavy (Sine on each up-sampled phase). 2× oversampled. |
| 20 | 2,3,4 | **Six-Op FM (×3)** | LOW | high | LUT-per-operator-per-sample × 6 operators. **Also: noise bug — see below.** NEON is gated on polynomial sine substitution. |
| 21 | 15 | **Speech** | N/A | low | Inherently serial state machine (LPC / SAM / Naive formant filter). No SIMD axis. |

## Top NEON targets (recommended order)

1. **Swarm (16)** — best ratio of payoff to effort. 8 polynomial voices, clean SIMD axis, popular engine.
2. **Virtual Analog (8)** — second-strongest. 2–4 polynomial oscillators, very popular.
3. **Chord (14)** — divide-down path only; leave wavetable path alone. Decent payoff on a popular engine.
4. **Additive (12)** — but **verify whether MI upstream already SIMD'd `HarmonicOscillator`** before touching. If they did, the work is done; if not, this is the biggest mode-count win available.
5. **Modal (20)** — verify whether it shares the already-NEON'd Resonator class from Rings. If yes, may already benefit; if no, port the same pattern.
6. **Chiptune (7)** — clean win on a less-popular engine.

Each is a separate refactor — none is "the Plaits NEON pass." Pick targets individually based on what the user actually uses. Per-engine PKGVERSION bumps, individual hint audits, individual hardware auditions.

## Non-NEON optimization angles (visible at-a-glance)

Across most engines, a few patterns show up:

- **`Pow2Fast<2>(...)` calls per sample** (visible in FM voice's amplitude scaling at `voice.h:232-236`). These are polynomial approximations of `2^x`, already cheaper than libm `powf` but still hot — could be block-rate amortized when modulation is slow.
- **`ParameterInterpolator` per-sample `.Next()` calls** — already block-rate state, but every sample does a linear interpolation step. Vectorizable across multiple parameter-interpolators per sample if a future pass needs it.
- **Per-sample conditionals** for soft clip / saturation in the output stage of several engines. Some of these can be hoisted via branchless masking (per `feedback_runtime_branched_dsp_dispatch`).
- **`tanhf` is not used** in Plaits hot paths (good — Plaits uses `SoftClip` polynomial approximation everywhere, unlike Filterbank's per-sample `tanhf`).

## 6-op FM noise investigation

**User report**: 6-op FM engines (slots 2/3/4) "always sound quite
noisy" — suspected sample rate mismatch.

### Recon agent's claim — corrected

The recon agent flagged `fm/voice.h:74`:
```cpp
const float native_sr = 44100.0f;  // Legacy sample rate.
const float envelope_scale = native_sr * one_hz_;
```
…and concluded "envelopes decay ~8% faster than nominal."

**This is wrong.** The math:
- `one_hz_ = 1.0 / sample_rate` (line 71, where `sample_rate` is the
  ER-301 value passed in — `kCorrectedSampleRate = 47872.34`).
- `envelope_scale = 44100 / 47872.34 ≈ 0.9211`.
- The envelope generator multiplies its per-sample step rate by this
  factor. Over T seconds (= 47872·T samples at the new rate), the
  envelope advances by `rate · 0.9211 · 47872 · T ≈ rate · 44100 · T`
  — i.e. the same envelope advancement that would happen over T
  seconds at the native 44100 rate.
- **Net: envelopes complete in the same number of *seconds* (musical
  time) regardless of sample rate. This is the correct sample-rate
  compensation, not a bug.**

The "Legacy sample rate" comment refers to the DX7 tuning constants
that were calibrated at 44100 Hz — the scale converts them to the
current rate. Standard porting practice.

### Likely actual cause: no oversampling

Plaits' classic 2-op FM engine (slot 10, `fm_engine.cc`) uses **4×
oversampling** — operators run at 4× the output rate, then a FIR
downsampler brings them back down. This is the standard fix for FM
aliasing.

The **6-op engine has no oversampling** (`engine2/six_op_engine.cc`
`Render()` lines 159–173). The 6 operators all run at the
output sample rate (47872 Hz). With DX7-style patches that routinely
use modulation indices of 5–10, harmonics produced by the modulator
chain exceed Nyquist and fold back as aliasing.

**That aliasing is almost certainly what the user perceives as "noise."**
It would be most audible at:
- High carrier frequencies (more aliasing energy in-band)
- High modulation indices (more spectral spread = more energy
  above Nyquist)
- Patches with rich, evolving modulator chains (DX7 brass / bell
  presets — classic 6-op territory)

### Why this needs to be a separate work item

Fixing requires adding an oversampling layer to the 6-op render
path — either:
1. **2× or 4× oversampling shell** around `voice_[i].Render(...)` —
   render the voice at higher rate, then FIR-downsample. Plaits has
   `Downsampler<oversampling>` utility used in `fm_engine.cc`; the
   same class can wrap the 6-op render. Memory cost: `oversampling
   × kBlockSize` floats per voice for the upsampled buffer; CPU
   cost: oversampling-fold the operator work.
2. **Polynomial sine substitution** that's inherently band-limited
   (less aliasing at high modulation indices than the 4096-entry LUT
   sine, which has its own quantization-induced harmonics).
3. **Both** — oversampling + polynomial sine — the proper fix, but
   gated on tone audition for the polynomial sine.

This is structurally separate from the NEON work. The NEON pass on
6-op FM is blocked anyway by the `SineFm` LUT no-gather constraint
(`feedback_neon_no_gather_lut_dsp`). **Fix the noise first, then
revisit NEON** — and likely the polynomial-sine substitution covers
both at once.

**Recommended next move on 6-op FM**: read `engine/fm_engine.cc`
(classic 2-op) for the canonical Plaits oversampling-shell pattern,
adapt for 6-op. Estimated ~2–3 days of work; needs hardware audition
gate (oversampling adds CPU; need to confirm the noise reduction is
worth the cost).

## Verification notes

- Engine dispatch order confirmed by reading `eurorack/plaits/dsp/voice.cc:41-66` directly.
- 6-op staggered rendering verified by reading `engine2/six_op_engine.cc:103-173` directly.
- `envelope_scale` math verified by reading `fm/voice.h:65-91` directly. Agent's "8% too fast" claim does not survive the math.
- 6-op oversampling absence verified by inspection of the Render function — no upsample / FIR / Downsampler calls. Compare with `engine/fm_engine.cc` (classic 2-op) which has the canonical oversampling shell.

## Out of scope / follow-ups

- Polynomial sine substitution for FM and 6-op FM (tone audition gate, separate work).
- Verifying whether Plaits' `HarmonicOscillator` (engine 12, Additive) is already NEON'd upstream — needs a read before any work.
- Verifying whether Plaits' modal_engine uses the shared NEON'd Resonator class or a separate impl.
- Per-engine optimization opportunities outside NEON (block-rate hoisting, soft-clip branchless masking) — collect as a sweep item if the user wants a non-NEON pass too.
