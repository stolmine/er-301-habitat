// zaum::APFTank
//
// Phase 1 north-star primitive for the Zaum woven-reverb package.
// A Dattorro/Griesinger figure-8 recirculating allpass tank extended
// with decorrelated Brownian delay-line modulation — the believable-room
// substrate that the standalone Fabula unit ships, and that the north-star
// Zaum (Phase 5) reuses verbatim. Internal-stereo: one Object owns both
// L and R tank state.
//
// Plan: planning/fabula-design.md (DSP architecture, delay tables,
// modulation, governor). Roadmap: planning/zaum-roadmap.md §"Phase 1".
//
// BUILD SUB-PHASE 0.1.0.12 — ER diffuser (slapback fix) + Early room-macro coupling.
//
//   PART A — ER diffuser: after summing the 9 ER taps into erSumL/erSumR, each
//   channel is passed through 2 short plain Schroeder allpasses (series) BEFORE
//   being scaled into the wet sum. This smears the discrete FIR taps into a
//   continuous early wash, eliminating discrete slapback echoes at high Early.
//
//   L diffuser delays: 211 smp (≈4.4 ms) and 317 smp (≈6.6 ms). Prime, decorrelated.
//   R diffuser delays: 241 smp (≈5.0 ms) and 359 smp (≈7.5 ms). Prime, decorrelated.
//   Coefficient g = 0.6 for all four stages (unity-gain, |g|<1 — provably stable).
//   Static (unmodulated). Four dedicated float buffers + write heads in constructor.
//
//   Stability proof: the ER diffuser is purely in the feedforward ER path (no
//   recirculating feedback anywhere). erSumL/erSumR are FIR tap sums from mER[];
//   passing them through short IIR allpasses cannot destabilize the tank, which
//   is a completely separate signal path. |H_allpass| = 1 — unity-gain, bounded.
//
//   PART B — Early room-macro coupling: at block rate, compute an EFFECTIVE
//   sizeFactor, g_d, and dampCoeff biased toward a smaller/shorter/warmer room
//   as the Early parameter rises. A macro value = kEarlyMacroDepth*earlyParam
//   is distributed across three targets via per-target weight constants.
//
//   sizeFactorEff = sizeFactor * (1 - macro*kMacroSizeAmt)
//     → smaller room as Early rises (fewer initial reflections on long delays)
//   g_d_eff = g_d - macro*kMacroDecayAmt*(g_d - kGdMin)
//     → shorter tail as Early rises (matches the denser early-reflection energy)
//   dampCoeff_eff applied to dampCoeff via effective Damp, pulled toward 1.0
//     (more HF absorption) as Early rises
//   sizeFactorEff one-pole smoothed per block (kSizeFactorSmooth) so Early
//   sweeps don't jump delay lengths abruptly.
//
//   Early=0 INVARIANT: macro=0 → effective values equal manual exactly, AND
//   erSumL/erSumR=0 (guarded), AND ER diffuser output=0 (driven by zero input).
//   Output is BIT-FOR-BIT identical to 0.1.0.11 at Early=0.
//
//   Residual click on Early sweep: sizeFactorEff changes delay lengths (same as
//   Size sweep). The per-block one-pole smoother (kSizeFactorSmooth=0.05) reduces
//   jump magnitude per block, but large fast Early moves may produce a brief zip.
//   This is the same deferred behavior as Size sweeps (known since 0.1.0.10).
//
// (Previous: 0.1.0.11 — Outer-allpass Brownian modulation for metallic reduction.)
//
//   Extends the Brownian walk modulation to the four OUTER tank allpasses
//   (AP1 outer 1087 L/R, AP2 outer 1471 L/R) to break up residual eigentone
//   ringing in the tail. Per Dattorro 1997 and Valhalla DSP (2011), modulating
//   the longer tank allpass delays reduces metallic modal coloration; short inner
//   APs (367/491) and input diffusers (229/173/613/449) remain UNMODULATED to
//   avoid "water sloshing" artifacts.
//
//   Four new independent xorshift64 PRNGs + walk accumulators (one per outer AP),
//   seeded with distinct constants DIFFERENT from the D1/D2 delay-line seeds.
//   Excursion: kAPModMin + Mod*(kAPModMax-kAPModMin) → ±0.5..6 samples.
//   At default Mod=0.40: apExcursion ≈ 2.7 samples — subtle, below audible chorus.
//   At Mod=0.0: apExcursion ≈ 0.5 samples — effectively static.
//   Outer AP buffers sized to base + 2*kAPHeadroom (16): 1087→1119, 1471→1503.
//   Inner AP buffers (367/491) UNCHANGED — unmodulated, exact size, no headroom.
//   AP excursion NOT size-scaled: outer AP delays are fixed (not Size-scaled),
//   so ±6 on 1087/1471 is ~0.4–0.55% — already a constant subtle ratio.
//
// (Previous: 0.1.0.10 — Size/Decay range extended to room scale; default sound preserved.)
//
//   Adds a discrete feedforward early-reflection (ER) FIR network that injects
//   room-character reflections in the 7–70 ms window (Griesinger/Moorer precedence
//   effect range) into the wet output — PARALLEL to the existing tank multi-tap,
//   summed as: wetL += kERLevel * earlyParam * erSumL (and R).
//
//   Source signal: the predelayed, pre-diffusion sample (diffIn captured before
//   the input-diffusion chain), written each sample into a dedicated 4096-sample
//   mono ER ring buffer (mER, power-of-two for cheap & wrap). L and R each read
//   9 taps with DIFFERENT delay offsets (decorrelated stereo) and mixed signs
//   (emulating surface-interaction phase inversions per Moorer 1979).
//
//   New parameter: Early (default 0.4). At Early=0 the ER term is zero —
//   the 0.1.0.8 output is reproduced exactly. At Early=1 the ER network is at
//   full level. With Size/Decay low + Early high → tight present room.
//   With Size/Decay high + Early low → the existing diffuse hall character.
//
//   Early=0 backward-compatibility guarantee: kERLevel * 0.0f * erSum = 0
//   unconditionally — no floating-point residual, no change to existing signal path.
//
// (Previous: 0.1.0.8 — Tier 1 present-room default retune, default-value changes only.)
//
//   Parameter defaults retuned for a "present room" voicing per fabula-character-tuning.md:
//     Size:      0.5f  → 0.35f   (smaller, more intimate room)
//     Decay:     0.5f  → 0.30f   (shorter tail, less cavernous)
//     Damp:      0.25f → 0.40f   (more HF rolloff, warmer default)
//     Diffusion: 0.6f  → 0.45f   (less onset smear, more transient clarity;
//                                  coefficients stay within clamped stable range —
//                                  lower g is always stable for Schroeder allpasses)
//     Mod:       0.3f  → 0.40f   (slightly more chorusing at default)
//     Predelay:  0.0f  → 0.041f  (≈14 ms; 0.041×16383 ≈ 672 smp at 48 kHz)
//     Mix:       0.5f  → 0.40f   (more dry presence at default)
//     ModRate:   0.2f  → 0.2f    (UNCHANGED)
//
//   No DSP logic, mappings, tuning constants, or structure were changed.
//
// (Previous: 0.1.0.7 — Series-cascade allpasses + Dattorro multi-tap output.)
//
//   PART 1: Each tank AP (AP1 and AP2, both L and R loops) is now a
//   SERIES CASCADE of two independent unity-gain Schroeder allpasses
//   (outer → inner). This is explicitly NOT in-feedback Gardner nesting
//   (which ran away in 0.1.0.2). The cascade transfer function is
//   H_outer(z)·H_inner(z), a product of two |H|=1 allpasses, so |H|=1
//   everywhere for |g|<1. Cannot run away — provably unity-gain.
//
//   Each inner AP has its OWN separate buffer (kTA1i=367, kTA2i=491),
//   unmodulated, exact size with no headroom, zeroed in constructor.
//   The outer reads use the existing outer buffers (mTA1_L etc.).
//   Inner coefficients fixed at gTA1_in = gTA2_in = 0.50 this sub-phase
//   (Diffusion scaling of inner coeffs deferred to polish pass).
//
//   PART 2: Multi-tap output replaces the old 0.5*(d1+d2) wet sum.
//   Dattorro-style: signed weighted sum of ap1Out, three intermediate
//   D1 taps, two intermediate D2 taps, and the full D1/D2 end reads.
//   Intermediate tap positions scale with Size (fractions of scaled length,
//   rounded to nearest odd). A 2.2× level match restores perceived wet
//   level (signed sum is ~7 dB quieter than the old 0.5*(d1+d2)).
//   This moves the 1000 echoes/sec threshold from ~160 ms to ~10 ms
//   (rig-validated). Loop stability is unaffected — this is output-tap
//   only; governors/DC blocker/cross-feed are all unchanged.
//
//   This sub-phase also inherits the 0.1.0.6 Diffusion wiring:
//
//   A. DAMP — one-pole HF low-pass in each loop's D1 feedback path. (unchanged)
//
//      Form: y += coeff * (x - y)   (one-pole LP, "leaky integrator")
//      State: mDampL, mDampR (double, init 0).
//
//      Damp=0  → coeff=1.0 → unity (open, no damping)
//      Damp=1  → coeff=kMinDampCoeff → maximum rolloff (dark)
//
//      Mapping (linear in coeff space, easy to retune):
//        coeff = 1.0 - Damp * (1.0 - kMinDampCoeff)
//        kMinDampCoeff = 0.08  (corner ~600 Hz at 48 kHz)
//        kMaxDampCoeff = 1.0   (open, Damp=0)
//
//      Perceptual calibration at 48 kHz:
//        Damp=0.00 (default 0.25 → coeff≈0.77): very mild 5+ kHz shelf
//        Damp=0.50 → coeff≈0.54: noticeable HF roll (~1.5 kHz corner)
//        Damp=1.00 → coeff=0.08: heavy (~600 Hz corner), very dark
//
//      The coeff is SR-independent by intention: the perceptual target is
//      a smooth tonal shift, not a specific Hz corner. SR-accurate mapping
//      (coeff = 2π·fc/sr) is an upgrade path if exact cutoffs matter.
//      PRIMARY TUNING KNOB: kMinDampCoeff (lower = darker maximum).
//
//      Placement: after D1 read (before AP2), inside the recirculating
//      path — the Schroeder/Jot HF-damping-in-feedback form per
//      fabula-design.md §2. Placing it on D1_out means every round trip
//      applies one LP, accumulating toward darker tails at high Decay.
//
//   B. DECAY — g_d via power-curve log-shaped map (replaces hard-coded 0.85).
//
//      0.1.0.10: floor lowered, shape re-solved to preserve default output.
//        kGdMin: 0.60 → 0.30  (Decay=0 now gives a short dry tail)
//        kDecayShape: 0.565 → 0.277  (re-solved to pin Decay=0.30 → g_d=0.780)
//
//      Mapping:  g_d = kGdMin + (kGdMax - kGdMin) * pow(decay, kDecayShape)
//
//      Calibration (0.1.0.10):
//        Decay=0.00 → g_d = 0.30   RT60 ≈ 0.4 s  (tight/dry room — NEW FLOOR)
//        Decay=0.30 → g_d = 0.780  RT60 ≈ 3.1 s  (default — UNCHANGED)
//        Decay=0.50 → g_d = 0.864  RT60 ≈ 5.0 s
//        Decay=1.00 → g_d = 0.970  RT60 ≈ 28 s   (UNCHANGED)
//        (RTT ≈ 0.330 s at Size=0.35 default; RT60 = -3*RTT/log10(g_d))
//
//      kDecayShape derivation (0.1.0.10):
//        Solve for k: 0.78 = 0.30 + 0.67*0.30^k → 0.30^k=0.71641 → k=0.277.
//
//      HARD SAFETY CAP: g_d = min(g_d, kGdCap = 0.985) regardless of Decay.
//      Lower g_d is strictly more stable; no new runaway path.
//
//   C. SIZE — delay-length scaling (block rate, careful bounds).
//
//      0.1.0.10: now a POWER CURVE so the floor reaches genuine small-room scale
//      while Size=0.35 (default knob) still maps to sizeFactor=0.850 (UNCHANGED).
//
//      sizeFactor = kSizeMin + (kSizeMax - kSizeMin) * pow(Size, kSizeShape)
//        kSizeMin = 0.18  → Size=0.0 (small room, D1_L≈1295 smp ≈27 ms)
//        kSizeMax = 1.50  → Size=1.0 (large hall — UNCHANGED)
//        kSizeShape = 0.647: pinned so Size=0.35 → sizeFactor=0.850 (UNCHANGED)
//        kSizeRef = 0.850: default sizeFactor, used for excursion scaling
//        Size=0.35 → sizeFactor=0.850 → current approved sound preserved.
//
//      Scaled lengths rounded to nearest ODD integer (not full prime-snap;
//      Brownian modulation smears eigentones so strict primeness is less
//      critical post-modulation). The four scaled lengths are checked for
//      shared divisors at audition; re-snap to prime pool in 0.1.0.7 if
//      needed. See note on prime rounding below.
//
//      BUFFER SIZING: buffers are allocated at MAXIMUM size (sizeFactor=1.5)
//      plus 128-sample headroom each side plus 1 interp neighbor:
//        D1_L: base=7187, max_base=10781 (7187*1.5 rounded to odd),
//              buffer = 10781 + 256 + 1 = 11038
//        D2_L: base=5101, max_base=7651  (5101*1.5 rounded to odd),
//              buffer = 7651  + 256 + 1 = 7908
//        D1_R: base=6803, max_base=10205 (6803*1.5 rounded to odd),
//              buffer = 10205 + 256 + 1 = 10462
//        D2_R: base=6343, max_base=9515  (6343*1.5 rounded to odd),
//              buffer = 9515  + 256 + 1 = 9772
//
//      BOUNDS PROOF (worst case: Size=1.0, walk=+72, interp neighbor):
//        scaledBase_max = 10781 (D1_L)
//        Buffer size    = 11038
//        Write head wraps at 11038.
//        Read = wrHead - scaledBase_max + walk
//             = wrHead - 10781 + 72 = wrHead - 10709
//        For i1 = i0+1: max integer offset from center = 72+1 = 73.
//        Needed headroom = 73 < 128 — safe with 55 samples margin.
//        At Size=0.0 (sizeFactor=0.5): scaledBase = round(7187*0.5)=3593(odd),
//        Buffer wraps at 11038; read is wrHead-3593±72 — all in [0,11038) mod.
//        The modular arithmetic handles ALL scaledBase values safely.
//
//      CLICK ON SIZE CHANGE: changing Size repoints the read length, which
//      may produce a click on large jumps (ACCEPTABLE v1; crossfading
//      deferred to a later sub-phase). Size changes at block rate only.
//
//      PRIME ROUNDING NOTE: to_odd() is applied at block rate. Full
//      prime-snapping from a precomputed prime pool is deferred to 0.1.0.7.
//
//   D. DC BLOCKER — per loop, on the tank input signal (before AP1).
//
//      Form: y[n] = x[n] - x[n-1] + R*y[n-1]   R = kDCBlockR = 0.9995
//      State per loop: mDCx1_L, mDCy1_L, mDCx1_R, mDCy1_R (doubles, init 0).
//
//      Corner frequency: fc ≈ (1-R)*sr/(2π) ≈ 0.0005*48000/6.283 ≈ 3.8 Hz.
//      This removes DC and sub-3 Hz content only — inaudible in reverb tail.
//
//      Placement: on tankIn_L / tankIn_R BEFORE AP1, after accumulating
//      diffIn + feedback. This prevents DC from entering the tank in the
//      first place; every round trip the DC blocker cleanly removes any
//      accumulated offset. Placing it before the wet taps (d1Read, d2Read)
//      means DC never reaches the output either.
//
//      Why here and not on d2Read×g_d (feedback signal): placing on tankIn
//      blocks DC from entering, whereas blocking on the feedback signal would
//      still allow DC from diffIn to accumulate for one round trip. Either
//      placement is stable; tankIn is cleaner.
//
// All 8 parameters are now wired.
// Wired parameters: Predelay, Mix, Mod, ModRate, Damp, Decay, Size, Diffusion.
//
// Tank allpass convention — series cascade (0.1.0.7), per Schroeder, provably unity-gain:
//   Each tank AP stage is: outer Schroeder AP → inner Schroeder AP (SERIES, not nested).
//   Outer AP: vO_new = x + g_out*vO_del; out = -g_out*vO_new + vO_del; write vO_new.
//   Inner AP: vI_new = out + g_in*vI_del; y   = -g_in*vI_new + vI_del; write vI_new.
//   |H_cascade| = |H_outer|·|H_inner| = 1·1 = 1. Cannot run away for |g| < 1.
//   Inner buffers (367, 491) are SEPARATE from the outer buffers — NOT in-feedback.
//
// Spiral feedback governor (fabula-design.md §4) applied once per
// round trip on each loop's recirculating feedback. With densityA=1.0
// the output is bounded to [-1, +1]. Under normal use with g_d<1.0
// the saturator is inactive; it acts only as a hard wall against
// transient overloads or parameter edge cases.
//
// NOTE — mono-input gain: the Lua wrapper only connects In2 when
// channelCount>1. For mono patches In R reads zeros, so monoIn=0.5*inL
// (~6 dB drop). Both loops still receive diffIn and cross-feed produces
// stereo spread. Mono gain compensation deferred post-audition.

#pragma once

#include <od/config.h>
#include <od/objects/Object.h>
#include <AllpassMono.h>
#include <Spiral.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

namespace zaum
{

  // Float (single-precision) variants of the house helpers, for the am335x
  // per-sample hot loop (Cortex-A8 has no double-precision NEON, so double math
  // falls to slow scalar VFPv3). Kept local so this does not touch shared house.
  // Same math as house::allpassNestedStep / house::spiralFastSaturate.
// always_inline is load-bearing: without it GCC leaves these as out-of-line
// calls in the per-sample loop. For spiralFastSaturateF that also blocks the
// densityA=1.0 constant-fold, so the out-of-line body keeps a real vdiv (s /
// densityA) - 2 calls + 2 vdiv/sample. Forcing inline removes both the call
// overhead and (with the literal 1.0f arg) folds the divide away entirely.
// SWIG's parser does not understand __attribute__, so hide it behind a macro.
#ifdef SWIG
#define ZAUM_ALWAYS_INLINE inline
#else
#define ZAUM_ALWAYS_INLINE inline __attribute__((always_inline))
#endif
  static ZAUM_ALWAYS_INLINE void allpassNestedStepF(
      float xNow, float vDelayed, float g, float &vNew, float &yOut)
  {
    vNew = xNow + g * vDelayed;
    yOut = -g * vNew + vDelayed;
  }
  static ZAUM_ALWAYS_INLINE float spiralFastSaturateF(float x, float densityA)
  {
    float absX = fabsf(x) * densityA;
    if (absX > 1.5707963f) absX = 1.5707963f;
    float x2 = absX * absX;
    float s = absX * (1.0f + x2 * (-0.16666667f + x2 * 0.008333333f));
    return (x > 0.0f) ? (s / densityA) : -(s / densityA);
  }
  // Character morph for the in-loop feedback nonlinearity: clean -> saturated ->
  // folded. densityA drives the spiral saturator harder (saturates peaks, unity
  // through-gain at low level so the decay time is preserved). foldAmt then blends
  // in a cubic single-fold f(u)=u-u^3/3 (slope 1 at 0 -> also unity through-gain,
  // decay preserved), driven by foldDrive and clamped to the fold region. Both
  // stages are unity at small signal, so Character colours the tail's harmonics
  // without changing loop gain / stability. Applied in the cross-feed path.
  static ZAUM_ALWAYS_INLINE float characterShapeF(float x, float densityA,
                                                  float foldAmt, float foldDrive)
  {
    float sat = spiralFastSaturateF(x, densityA);
    float fx  = sat * foldDrive;
    if (fx >  1.5f) fx =  1.5f;               // clamp to the fold region
    if (fx < -1.5f) fx = -1.5f;
    float folded = (fx - fx * fx * fx * 0.33333333f) / foldDrive;
    return sat + foldAmt * (folded - sat);
  }

  // ---------------------------------------------------------------------------
  // Buffer size constants
  // ---------------------------------------------------------------------------

  // Predelay: ~341 ms at 48 kHz. Power-of-two for cheap wrap.
  static const int kPD   = 16384;

  // Input diffusion allpass buffers (4 series, shared L+R mono path).
  // Sizes from fabula-design.md §2 (nearest-prime discipline).
  static const int kID1  = 229;
  static const int kID2  = 173;
  static const int kID3  = 613;
  static const int kID4  = 449;

  // Early-reflection ring buffer (dedicated mono, pre-diffusion source).
  // 4096 samples at 48 kHz = 85.3 ms — covers the full 7–70 ms ER window.
  // Power-of-two for cheap bitwise wrap: & (kER - 1).
  // Maximum tap delay used = 71 ms = 3408 samples < 4096 — all reads in-bounds.
  static const int kER = 4096;

  // ER diffuser allpass buffer sizes (0.1.0.12).
  // Two plain Schroeder allpasses per channel (series), STATIC, g=0.6.
  // Delays chosen prime and decorrelated between L and R.
  // At 48 kHz: 211 smp≈4.4 ms, 317 smp≈6.6 ms (L); 241 smp≈5.0 ms, 359 smp≈7.5 ms (R).
  // Buffers sized exactly to their delays — no modulation, no headroom needed.
  static const int kERD_L1 = 211;   // L ER diffuser stage 1 delay
  static const int kERD_L2 = 317;   // L ER diffuser stage 2 delay
  static const int kERD_R1 = 241;   // R ER diffuser stage 1 delay
  static const int kERD_R2 = 359;   // R ER diffuser stage 2 delay

  // ER diffuser allpass coefficient — shared across all 4 stages.
  // g=0.6: unity-gain (|g|<1), enough diffusion to smear 7–70 ms FIR taps
  // into a smooth wash within a few ms of smear. Primary tuning knob.
  static const float kERDiffG = 0.6;

  // Tank allpass buffers (series-cascade Schroeder APF, BOTH L and R loops).
  // Each tank AP is a SERIES CASCADE: outer (kTA1=1087, g=gTA1_out) feeds
  // inner (kTA1i=367, g=gTA1_in=0.50). Separate buffer per stage per loop.
  // AP1 outer: N=1087, g from Diffusion (~0.70 at default). Inner: N=367, g=0.50.
  // AP2 outer: N=1471, g from Diffusion (~0.50 at default). Inner: N=491, g=0.50.
  // L and R loops use SEPARATE buffers and write indices for all 8 buffers.
  //
  // 0.1.0.11: OUTER AP buffers sized base + 2*kAPHeadroom for Brownian modulation.
  //   kAPHeadroom=16: generous for ±6-sample max excursion + 1 interp neighbor = 7 < 16.
  //   kTA1_size = 1087 + 32 = 1119; kTA2_size = 1471 + 32 = 1503.
  // INNER AP buffers (kTA1i=367, kTA2i=491) UNCHANGED — exact size, UNMODULATED.
  static const int kTA1  = 543;   // AP1 outer base delay
  static const int kTA1i = 183;    // AP1 inner delay — series cascade (0.1.0.7), UNMODULATED
  static const int kTA2  = 735;   // AP2 outer base delay
  static const int kTA2i = 245;    // AP2 inner delay — series cascade (0.1.0.7), UNMODULATED

  // ---------------------------------------------------------------------------
  // Outer AP modulation headroom and buffer sizes (0.1.0.11).
  // kAPHeadroom: headroom samples each side for Brownian walk + interp neighbor.
  //   Max excursion = kAPModMax = 6 samples; interp neighbor = 1 → total = 7 < 16.
  //   Safe with 9-sample margin.
  // kTA1_size = kTA1 + 2*kAPHeadroom = 1087 + 32 = 1119
  // kTA2_size = kTA2 + 2*kAPHeadroom = 1471 + 32 = 1503
  // ---------------------------------------------------------------------------
  static const int kAPHeadroom = 16;
  static const int kTA1_size   = 1024;   // pow2 (>= kTA1+2*kAPHeadroom=1119) for & mask
  static const int kTA2_size   = 1024;   // pow2 (>= 1503) for & mask

  // ---------------------------------------------------------------------------
  // Outer AP modulation excursion constants (0.1.0.11).
  // Driven by the Mod parameter (unified modulation-depth control).
  //   apExcursion = kAPModMin + Mod * (kAPModMax - kAPModMin)
  //   Mod=0.00 → apExcursion = 0.5 smp (≈ static — no audible effect)
  //   Mod=0.40 → apExcursion = 0.5 + 0.40*5.5 = 2.7 smp (gentle, sub-chorus)
  //   Mod=1.00 → apExcursion = 6.0 smp (±0.55% on 1087, ±0.41% on 1471)
  // AP excursion is NOT size-scaled: the outer AP delays are fixed (1087/1471),
  // not Size-scaled, so the ratio is already constant across all room sizes.
  // Rate: reuses the D1/D2 step_size (ModRate-derived) — same gentle Brownian
  // drift, just applied to a smaller excursion window.
  // ---------------------------------------------------------------------------
  static const double kAPModMin = 0.25;   // min AP excursion (samples) — near-static at Mod=0
  static const double kAPModMax = 3.0;   // max AP excursion (samples) — subtle, below chorus

  // ---------------------------------------------------------------------------
  // Tank delay line base lengths (Size=0.5 default = current approved sound)
  // ---------------------------------------------------------------------------
  // These four values are the "canonical" lengths from fabula-design.md §2.
  // With Size=0.5 → sizeFactor=1.0, the scaled lengths reproduce these exactly.
  static const int kD1_L_base = 3593;
  static const int kD2_L_base = 2551;
  static const int kD1_R_base = 3401;
  static const int kD2_R_base = 3171;

  // ---------------------------------------------------------------------------
  // Tank delay line buffer sizes: must hold MAX scaled length + headroom.
  //
  // Max sizeFactor = kSizeMax = 1.5
  //   D1_L: 7187 * 1.5 = 10780.5 → round to nearest odd → 10781
  //   D2_L: 5101 * 1.5 = 7651.5  → round to nearest odd → 7651
  //   D1_R: 6803 * 1.5 = 10204.5 → round to nearest odd → 10205
  //   D2_R: 6343 * 1.5 = 9514.5  → round to nearest odd → 9515
  //
  // Headroom: 128 samples each side for modulation + 1 for linear interp.
  // Buffer = max_scaled_length + 2*128 + 1.
  //
  // BOUNDS PROOF at max Size + max walk:
  //   Walk clamped to [-excursion, +excursion] where excursion ≤ kMaxExcursion=72.
  //   (0.1.0.10: excursion is scaled by sizeFactor/kSizeRef but hard-capped at
  //   kMaxExcursion=72 in process(), so the 128-sample headroom always applies.)
  //   Linear interp reads i0 and i1=i0+1.
  //   Worst additional reach from center = 72 + 1 = 73 samples.
  //   Headroom = 128 > 73 — safe with 55 samples margin on each side.
  //   At min Size=0.0 (sizeFactor=0.18): scaledD2_L≈919 smp (shortest line).
  //   Scaled excursion = 72*(0.18/0.85) ≈ 15 smp. Min positive delay = 919-15-1=903>0.
  //   All index arithmetic uses modular wrap (% bufSize), so no OOB access.
  // ---------------------------------------------------------------------------
  static const int kD1_headroom    = 128;   // modulation headroom each side

  static const int kD1_L_maxBase  = 5391;  // 7187*1.5 rounded to odd
  static const int kD1_L_size     = 8192;  // pow2 (>= 11038) for & mask

  static const int kD2_L_maxBase  = 3827;   // 5101*1.5 rounded to odd
  static const int kD2_L_size     = 4096;   // pow2 (>= 7908) for & mask

  static const int kD1_R_maxBase  = 5103;  // 6803*1.5 rounded to odd
  static const int kD1_R_size     = 8192;  // pow2 (>= 10462) for & mask

  static const int kD2_R_maxBase  = 4757;   // 6343*1.5 rounded to odd
  static const int kD2_R_size     = 8192;  // pow2 (>= 9772) for & mask

  // ---------------------------------------------------------------------------
  // Modulation tuning constants — adjust these by ear at the 0.1.0.4 gate.
  // ---------------------------------------------------------------------------

  // Excursion mapping: Mod 0..1 → kMinExcursion..kMaxExcursion samples.
  // At 48 kHz: 9 samples ≈ 0.19 ms, 72 samples ≈ 1.5 ms.
  // Max excursion 72 + 1 interp neighbor = 73 < 128 headroom — safe.
  static const double kMinExcursion = 4.5;
  static const double kMaxExcursion = 36.0;

  // Step size mapping: ModRate 0..1 → kMinStep..kMaxStep per sample.
  // This is the per-sample walk increment applied to the integrated noise.
  // Range chosen so ModRate=0.2 (default) gives slow pleasant drift and
  // ModRate=1.0 gives faster but still somewhat gentle wander.
  static const double kMinStep = 0.0002;
  static const double kMaxStep = 0.1;

  // ---------------------------------------------------------------------------
  // Decay → g_d tuning constants.
  // Power-curve mapping: g_d = kGdMin + (kGdMax - kGdMin) * pow(decay, kDecayShape)
  //
  //   EXTENDED FLOOR (0.1.0.10): kGdMin lowered from 0.60 → 0.30, kDecayShape
  //   re-solved to preserve the existing default output at Decay=0.30.
  //
  //   Constraint: Decay=0.30 must still → g_d≈0.78 (the current approved sound).
  //     0.78 = 0.30 + (0.97 - 0.30) * 0.30^k
  //     0.48 = 0.67 * 0.30^k
  //     0.30^k = 0.71641
  //     k = log(0.71641) / log(0.30) = 0.2767 → use 0.277
  //   Verify: 0.30 + 0.67 * 0.30^0.277 = 0.30 + 0.67*0.7167 = 0.30+0.480 = 0.780 ✓
  //
  //   Calibration table:
  //     Decay=0.00 → g_d = 0.30   RT60 ≈ 0.42 s  (tight room, dry)
  //     Decay=0.30 → g_d = 0.780  RT60 ≈ 3.1 s   (default — UNCHANGED)
  //     Decay=0.50 → g_d = 0.864  RT60 ≈ 5.0 s
  //     Decay=1.00 → g_d = 0.970  RT60 ≈ 28 s
  //
  //   RT60 formula: -3 * RTT / log10(g_d), RTT ≈ 0.330 s (Size=0.35 default).
  //
  //   kGdMin = 0.30:  Decay=0 → very short tail (~0.4 s RT60), tight/dry
  //   kGdMax = 0.97:  Decay=1 → long hall tail (~28 s RT60) — UNCHANGED
  //   kGdCap = 0.985: hard unconditional cap — g_d must stay < 1.0
  //   kDecayShape = 0.277: re-solved to pin Decay=0.30 → g_d=0.780
  //
  //   STABILITY: lower g_d is strictly more stable; no new runaway path.
  // ---------------------------------------------------------------------------
  static const double kGdMin      = 0.30;
  static const double kGdMax      = 0.97;
  static const double kGdCap      = 0.985;
  static const double kDecayShape = 0.277;

  // ---------------------------------------------------------------------------
  // Damp → one-pole LP coefficient tuning constants.
  // Mapping: coeff = 1.0 - Damp * (1.0 - kMinDampCoeff)
  //   kMinDampCoeff = 0.08: darkest setting (corner ~600 Hz at 48 kHz)
  //   Damp=0.00 → coeff=1.0 (open, passthrough)
  //   Damp=0.25 → coeff=0.77 (default: mild HF shelf, barely audible)
  //   Damp=0.50 → coeff=0.54 (~1.5 kHz corner, noticeably warmer)
  //   Damp=1.00 → coeff=0.08 (~600 Hz corner, very dark)
  // Primary tuning knob: kMinDampCoeff (lower = darker maximum damp).
  // ---------------------------------------------------------------------------
  static const double kMinDampCoeff = 0.08;

  // ---------------------------------------------------------------------------
  // Size → sizeFactor tuning constants.
  //
  //   EXTENDED FLOOR (0.1.0.10): now a POWER CURVE (not linear) so the floor
  //   drops to a genuine small room while the default knob position (Size=0.35)
  //   maps to the SAME sizeFactor as before (0.850), preserving the approved sound.
  //
  //   Formula: sizeFactor = kSizeMin + (kSizeMax - kSizeMin) * pow(Size, kSizeShape)
  //
  //   Constraints:
  //     Size=0.0 → sizeFactor = kSizeMin = 0.18  (new small-room floor)
  //     Size=1.0 → sizeFactor = kSizeMax = 1.50  (UNCHANGED large room)
  //     Size=0.35 must still → sizeFactor = 0.850 (current approved sound):
  //       0.850 = 0.18 + (1.50 - 0.18) * 0.35^p
  //       0.670 = 1.32 * 0.35^p
  //       0.35^p = 0.50758
  //       p = log(0.50758) / log(0.35) = 0.6468 → use 0.647
  //   Verify: 0.18 + 1.32 * 0.35^0.647 = 0.18 + 1.32*0.5077 = 0.18+0.670 = 0.850 ✓
  //
  //   Calibration table:
  //     Size=0.00 → sizeFactor=0.18  D1_L≈1294 smp ≈27 ms  (small room)
  //     Size=0.35 → sizeFactor=0.850 D1_L≈6109 smp ≈127 ms (default — UNCHANGED)
  //     Size=0.50 → sizeFactor=0.994 D1_L≈7144 smp ≈149 ms
  //     Size=1.00 → sizeFactor=1.500 D1_L≈10781 smp ≈225 ms (large hall)
  //
  //   BUFFER BOUNDS at new minimum sizeFactor=0.18:
  //     D1_L: round(7187*0.18)=1294 → toOdd→1295 smp; walk ±72; min read=1295-72-1=1222>0 ✓
  //     D2_L: round(5101*0.18)=918  → toOdd→919  smp; 919-72-1=846>0 ✓
  //     D1_R: round(6803*0.18)=1225 (odd); 1225-72-1=1152>0 ✓
  //     D2_R: round(6343*0.18)=1142 → toOdd→1143 smp; 1143-72-1=1070>0 ✓
  //   All reads stay well above zero. Modular wrap handles the large buffer safely.
  //
  //   MODULATION SCALING: the effective excursion is scaled by (sizeFactor / kSizeRef)
  //   where kSizeRef=0.850 (the default sizeFactor). This keeps the pitch-mod ratio
  //   constant across room sizes (~0.56% relative at all Size values) and prevents
  //   audible warble in small rooms. At Size=0.35 (sizeFactor=0.850 = kSizeRef) the
  //   scale factor is 1.0 — EXACTLY the current behavior.
  //
  //   kSizeMin = 0.18: Size=0 floor (D1_L≈1295 smp ≈27 ms — genuine small room)
  //   kSizeMax = 1.50: Size=1 ceiling — UNCHANGED (large hall)
  //   kSizeShape = 0.647: power exponent, re-solved to pin Size=0.35→0.850
  //   kSizeRef  = 0.850: reference sizeFactor for modulation scaling (= default)
  // ---------------------------------------------------------------------------
  static const double kSizeMin   = 0.18;
  static const double kSizeMax   = 1.50;
  static const double kSizeShape = 0.647;
  static const double kSizeRef   = 0.850;

  // ---------------------------------------------------------------------------
  // Diffusion → allpass coefficient tuning constants.
  //
  // All six allpass coefficients (4 input diffusers + 2 tank APs, shared
  // between L and R loops) are derived from the Diffusion parameter via a
  // per-coefficient linear offset around each stage's baseline:
  //
  //   coeff = clamp(base + (Diffusion - 0.6) * kDiffSlope, kDiffLo, kDiffHi)
  //
  // INVARIANT: at Diffusion=0.6 (default), each coefficient equals its
  // historical fixed value exactly (no change to the approved sound):
  //   gID12 baseline = 0.75   (input diffusers ID1, ID2)
  //   gID34 baseline = 0.625  (input diffusers ID3, ID4)
  //   gTA1  baseline = 0.70   (tank AP1, both loops)
  //   gTA2  baseline = 0.50   (tank AP2, both loops)
  //
  // The slope kDiffSlope = 0.25 (per unit of Diffusion) is shared across
  // all four coefficient families, preserving their relative spacing while
  // scaling together:
  //
  //   Diffusion=0.0: gID12=0.600, gID34=0.475, gTA1=0.550, gTA2=0.350
  //   Diffusion=0.6: gID12=0.750, gID34=0.625, gTA1=0.700, gTA2=0.500  ← default
  //   Diffusion=1.0: gID12=0.850, gID34=0.725, gTA1=0.800, gTA2=0.600
  //
  // Stability proof: kDiffHi = 0.85 < 1.0 unconditionally (hard clamp
  // applied after mapping). kDiffLo = 0.30 keeps coefficients positive
  // and well within unity-gain allpass range at all parameter values.
  // All Schroeder allpasses remain unity-gain for any |g| < 1.
  //
  // PRIMARY TUNING KNOBS:
  //   kDiffSlope — increase to widen the Diffusion sweep range
  //   kDiffLo    — floor coefficient (raise to prevent very open/thin sound)
  //   kDiffHi    — ceiling coefficient (MUST stay < 1.0 for stability)
  // ---------------------------------------------------------------------------
  static const double kDiffSlope = 0.25;   // per unit of Diffusion
  static const double kDiffLo    = 0.30;   // floor: allpass stays positive
  static const double kDiffHi    = 0.85;   // ceiling: strictly < 1.0

  // Per-coefficient baselines at Diffusion=0.6.
  // Changing these shifts the entire family up/down; kDiffSlope handles spread.
  static const double kDiffBaseID12 = 0.75;
  static const double kDiffBaseID34 = 0.625;
  static const double kDiffBaseTA1  = 0.70;
  static const double kDiffBaseTA2  = 0.50;

  // ---------------------------------------------------------------------------
  // DC blocker coefficient.
  // Form: y[n] = x[n] - x[n-1] + kDCBlockR * y[n-1]
  // fc ≈ (1-R)*sr/(2π) ≈ 3.8 Hz at 48 kHz — inaudible in reverb tail.
  // ---------------------------------------------------------------------------
  static const float kDCBlockR = 0.999;

  // Static wet-output highpass (housekeeping): keeps the reverb out of the low
  // mids so it doesn't mud up the mix or pump the tank on bass. Two cascaded
  // one-pole highpasses = 12 dB/oct at ~200 Hz, run at the 48 kHz host rate on
  // the reconstructed wet (per channel). Coeff a = 1 - exp(-2*pi*fc/sr); at
  // fc=200, sr=48000 -> a ~= 0.02593. hp = x - lp; lp += a*(x - lp).
  static const float kWetHpF = 200.0f;
  static const float kWetHpA = 0.025918f;

  // Living Freeze (continuous 0..1): as Freeze rises, the tank feedback ramps to
  // unity (the spiral governor keeps it bounded/stable), the tank input mutes so
  // the frozen content is preserved, and the HF damp lifts toward passthrough so
  // the cloud stays bright. The two tank halves lock in a stagger (L reaches unity
  // by fz=0.85, R by fz=1.0) so the tail "sets" progressively rather than snapping.
  // The Brownian mod keeps running -> a LIVING frozen cloud, not a static loop.
  static const float kFreezeSlew       = 0.02f;   // block-rate one-pole (~30 ms glide)
  static const float kFreezeGd         = 1.0f;    // unity feedback at full freeze
  static const float kFreezeStaggerL   = 0.85f;   // L reaches unity by fz=0.85
  static const float kFreezeStaggerOff = 0.15f;   // R begins locking at fz=0.15

  // Reverse (0..1): a granular reverse buffer on the wet. Two Hann-windowed grains
  // read the recent wet BACKWARD, offset by half a grain (50% overlap-add -> flat,
  // click-free). Reverse crossfades the wet toward its reversed self: the tail
  // rises INTO the hits. Runs at the 48 kHz host rate, per channel.
  static const int   kRevBufSize   = 16384;       // pow2 ring (>= 2.5*grain)
  static const int   kRevGrain     = 5760;        // grain length (~120 ms @48k)
  static const int   kRevGrainHalf = 2880;        // grain B phase offset
  static const int   kHannSize     = 1024;        // window LUT resolution
  static const float kRevHannRatio = 1024.0f / 5760.0f; // phase -> LUT index
  static const float kRevSlew      = 0.02f;       // block-rate amount smoother

  // Character (0..1): morphs the in-loop nonlinearity clean -> saturated -> folded.
  // First half raises the saturator density; second half morphs in the wavefold.
  static const float kCharSlew     = 0.02f;       // block-rate amount smoother
  static const float kCharSatMax   = 3.0f;        // densityA 1..(1+max)
  static const float kCharFoldMax  = 2.0f;        // foldDrive 1..(1+max)

  // ---------------------------------------------------------------------------
  // Series-cascade inner AP coefficients (0.1.0.7).
  // Both inner stages fixed at 0.50 this sub-phase (Dattorro value).
  // Hard cap |g| < 0.95 applied in process() regardless of these constants.
  // Diffusion scaling of inner coefficients is deferred to the polish pass.
  // ---------------------------------------------------------------------------
  static const double kGTA1_in = 0.50;   // AP1 inner cascade coefficient
  static const double kGTA2_in = 0.50;   // AP2 inner cascade coefficient

  // ---------------------------------------------------------------------------
  // Multi-tap output constants (0.1.0.7) — Dattorro-style signed wet sum.
  //
  // Intermediate tap positions expressed as fractions of the Size-scaled delay
  // length. At block rate the fractions are multiplied by the current scaled
  // length and rounded to the nearest odd integer (anti-comb, matches rig).
  //
  // D1 intermediate fractions (three taps along D1):
  //   fD1a = 0.2277  →  Size=0.5: 7187*0.2277 = 1636.6 → 1637 (odd)
  //   fD1b = 0.4524  →  Size=0.5: 7187*0.4524 = 3251.3 → 3251 (odd)
  //   fD1c = 0.6960  →  Size=0.5: 7187*0.6960 = 5002.2 → 5003 (odd)
  //
  // D2 intermediate fractions (two taps along D2):
  //   fD2a = 0.2838  →  Size=0.5: 5101*0.2838 = 1447.7 → 1447 (odd)
  //   fD2b = 0.6113  →  Size=0.5: 5101*0.6113 = 3118.8 → 3119 (odd)
  //
  // Bounds safety: all fractions < 1.0, so off < scaledLength ≤ bufSize.
  // True at ALL Size values [0,1]. Modular wrap in the read is still applied.
  //
  // Wet sum weights and signs from the rig (per channel, e.g. L):
  //   wetL = kWetLevel * ( +kWap1  * ap1Out_L
  //                        +kWd1a  * D1tap(fD1a) - kWd1b * D1tap(fD1b) + kWd1c * D1tap(fD1c)
  //                        +kWd1e  * d1Read_L     (full D1 end read)
  //                        -kWd2a  * D2tap(fD2a) + kWd2b * D2tap(fD2b)
  //                        +kWd2e  * d2Read_L )   (full D2 end read)
  //
  // kWetLevel = 2.2 restores perceived wet level: the signed sum is ~7 dB
  // quieter than the old 0.5*(d1+d2), and 2.2× compensates (~7 dB gain).
  // ---------------------------------------------------------------------------
  static const double kFD1a = 0.2277;    // D1 intermediate tap fraction a
  static const double kFD1b = 0.4524;    // D1 intermediate tap fraction b
  static const double kFD1c = 0.6960;    // D1 intermediate tap fraction c
  static const double kFD2a = 0.2838;    // D2 intermediate tap fraction a
  static const double kFD2b = 0.6113;    // D2 intermediate tap fraction b

  static const float kWap1  = 0.167;    // weight: ap1Out (early cascade tap)
  static const float kWd1a  = 0.111;    // weight: D1 tap a (+)
  static const float kWd1b  = 0.111;    // weight: D1 tap b (-)
  static const float kWd1c  = 0.111;    // weight: D1 tap c (+)
  static const float kWd1e  = 0.139;    // weight: D1 end read (+)
  static const float kWd2a  = 0.111;    // weight: D2 tap a (-)
  static const float kWd2b  = 0.111;    // weight: D2 tap b (+)
  static const float kWd2e  = 0.139;    // weight: D2 end read (+)
  static const float kWetLevel = 2.2;   // level match for signed-sum cancellation

  // ---------------------------------------------------------------------------
  // Early-reflection (ER) FIR network — Tier 3 (0.1.0.9).
  //
  // Tap pattern adapted from Moorer (1979) "About This Reverberation Business"
  // Table II (small concert hall ER times) and Gardner (1992) "The Virtual
  // Acoustic Room" Appendix B (reflection decay model), scaled to 48 kHz.
  //
  // Design choices:
  //   - 9 taps per channel, spanning 7–70 ms (336–3360 samples at 48 kHz).
  //   - IRREGULAR spacing (no two gaps equal) avoids comb resonances.
  //   - DIFFERENT L/R delays for stereo decorrelation.
  //   - Gains: exponential decay from ~0.70 at 7 ms to ~0.15 at 70 ms
  //     (6 dB per doubling of time ≈ –6 dB/oct energy decay), with mixed
  //     signs on every other tap (emulating surface-reflection phase
  //     inversions measured by Moorer in live rooms).
  //   - All tap delays < kER=4096 — guaranteed in-bounds (max = 3408 < 4096).
  //
  // Source signal: pre-diffusion predelayed mono (so reflections stay discrete,
  // not smeared by the four input-diffusion allpasses). Written to mER[] each
  // sample. Reads via ((mWrER - delay) & (kER-1)) — pure integer, no interp
  // needed (discrete reflections, not a modulated line).
  //
  // L tap delays (samples at 48 kHz) — Moorer Table II row A scaled:
  //   7 ms=336, 13 ms=624, 20 ms=960, 29 ms=1392, 37 ms=1776,
  //   46 ms=2208, 55 ms=2640, 62 ms=2976, 70 ms=3360
  //
  // R tap delays (offset by ~2–3 ms per tap for decorrelation):
  //   9 ms=432, 16 ms=768, 23 ms=1104, 31 ms=1488, 40 ms=1920,
  //   49 ms=2352, 57 ms=2736, 65 ms=3120, 71 ms=3408
  //
  // Gains (alternating sign, exponential envelope):
  //   index 0: +0.68, 1: -0.56, 2: +0.45, 3: -0.36, 4: +0.28,
  //   index 5: -0.22, 6: +0.17, 7: -0.13, 8: +0.10
  //
  // kERLevel = 1.5: at Early=0.4, typical ER sum magnitude ~0.4 (signed),
  //   contribution = 1.5 * 0.4 * 0.4 = 0.24 — clearly present but below
  //   the tank tail level (~0.4–0.6 at mix=0.4), giving a natural balance.
  //   PRIMARY TUNING KNOB: raise to strengthen the "room snap" character.
  //
  // Bounds proof: all tap delays < kER=4096.
  //   L: max delay = 3360 < 4096.   R: max delay = 3408 < 4096.
  //   Read: ((mWrER - delay) & (kER-1)) is always in [0, 4095]. Safe.
  // ---------------------------------------------------------------------------
  static const int kER_tapCount = 9;

  // ER tap delay tables and gain array — namespace-scope, defined inline.
  // Single TU (zaum_swig.cpp) guarantees no ODR violation.
  //
  // L delays in samples at 48 kHz (Moorer 1979 Table II row A adapted):
  //   7, 13, 20, 29, 37, 46, 55, 62, 70 ms
  static const int kER_delayL[9] = { 336, 624, 960, 1392, 1776, 2208, 2640, 2976, 3360 };
  //
  // R delays in samples at 48 kHz (offset ~2–3 ms per tap for decorrelation):
  //   9, 16, 23, 31, 40, 49, 57, 65, 71 ms
  static const int kER_delayR[9] = { 432, 768, 1104, 1488, 1920, 2352, 2736, 3120, 3408 };
  //
  // Shared gain envelope: alternating-sign exponential decay.
  // Energy model: -6 dB per octave of delay time (Moorer measured).
  // Signs alternate to emulate surface-reflection phase inversions.
  // Absolute magnitudes: 0.68, 0.56, 0.45, 0.36, 0.28, 0.22, 0.17, 0.13, 0.10
  static const float kER_gain[9] = {
    +0.68, -0.56, +0.45, -0.36, +0.28,
    -0.22, +0.17, -0.13, +0.10
  };
  //
  // Output level scaler. PRIMARY TUNING KNOB for ER-vs-tail balance.
  // At Early=0.4, mean |erSum| ~0.4 → contribution = 1.5 * 0.4 * 0.4 = 0.24.
  static const float kERLevel = 1.5;

  // ---------------------------------------------------------------------------
  // Early room-macro coupling tuning constants (0.1.0.12).
  //
  // macro = kEarlyMacroDepth * earlyParam   (0..kEarlyMacroDepth)
  //
  // At Early=0: macro=0 → all effective values equal manual → BIT-IDENTICAL output.
  // At Early=1: macro=kEarlyMacroDepth → maximum bias toward small/short/warm room.
  //
  // Per-target weights scale how much of the macro is applied to each dimension.
  // All weights are in [0,1] and are independent — tunable by ear without
  // affecting the other two dimensions.
  //
  //   Size bias:  sizeFactorEff = sizeFactor * (1 - macro*kMacroSizeAmt)
  //     At Early=1: sizeFactorEff = sizeFactor * (1 - 0.5*0.5) = sizeFactor * 0.75
  //   Decay bias: g_d_eff = g_d - macro*kMacroDecayAmt*(g_d - kGdMin)
  //     At Early=1: g_d_eff = g_d - 0.5*0.6*(g_d-kGdMin)  →  shorter tail
  //   Damp bias:  effective Damp raised by macro*kMacroDampAmt (more absorption)
  //     At Early=1: dampEff = min(dampD + 0.5*0.5, 1.0)  →  warmer/darker tail
  //
  // kSizeFactorSmooth: per-block one-pole smoothing coefficient for sizeFactorEff.
  // Prevents hard delay-length jumps on Early sweeps. At FRAMELENGTH=32, 48 kHz:
  //   0.05 per block ≈ ~20 blocks (~13 ms) to settle 63% of a step — gentle.
  //   PRIMARY CLICK-SOFTENING KNOB: raise toward 1.0 for instant (clickier),
  //   lower toward 0.0 for slower (smoother) transitions.
  //
  // kSizeFactorMin: hard floor on sizeFactorEff after smoothing.
  // Prevents delay lengths from going below a safe minimum under any Early value.
  // At kSizeFactorMin=0.12, shortest delay = D2_L: round(5101*0.12)=612→613 (odd).
  // Walk at Mod=1: excursion = 72*(0.12/0.85)=10.2 smp; min read = 613-10-1=602 > 0. Safe.
  // ---------------------------------------------------------------------------
  static const double kEarlyMacroDepth = 0.5;   // global macro scale; tunable by ear
  static const double kMacroSizeAmt    = 0.5;   // fraction of macro applied to Size
  static const double kMacroDecayAmt   = 0.6;   // fraction of macro applied to Decay
  static const double kMacroDampAmt    = 0.5;   // fraction of macro applied to Damp
  static const double kSizeFactorSmooth = 0.05; // per-block smoothing toward sizeFactorEff target
  static const double kSizeFactorMin    = 0.12; // hard floor on sizeFactorEff (bounds safety)
  // Per-sample one-pole slew for the delay bases + predelay + ER level, so Size/
  // Pre/Early changes glide (Doppler-style) instead of stepping at block rate ->
  // no zipper. ~25 ms: alpha = 1 - exp(-1/(0.025*48000)). Per feedback_doppler_
  // basedelay_smoother (Pecto's combSize fix). The read is fractional (already is,
  // for the Brownian walk), so a fractional base just glides continuously.
  static const float kBaseSlew = 0.000833f;
  // Brownian-walk decimation: the 8 per-line walks are sub-Hz drift, so their
  // xorshift64 + integrate + clamp runs only once per kWalkDecim samples; between
  // updates each walk ramps linearly toward the new target (no zipper). The
  // boundary step is scaled by sqrt(kWalkDecim) to preserve the random-walk
  // variance (std of a K-sample displacement), so the drift rate is unchanged.
  static const int   kWalkDecim     = 16;
  static const float kWalkStepScale = 4.0f;    // sqrt(kWalkDecim)
  static const float kWalkIncScale  = 0.0625f; // 1 / kWalkDecim

  // ---------------------------------------------------------------------------
  // xorshift64 PRNG — fast, period 2^64-1, audio-thread safe (no libc).
  // From Marsaglia (2003). NEVER pass seed=0 (degenerate fixed point).
  // ---------------------------------------------------------------------------
  static inline uint64_t xorshift64(uint64_t s)
  {
    s ^= s << 13;
    s ^= s >> 7;
    s ^= s << 17;
    return s;
  }

  // ---------------------------------------------------------------------------
  // Round x to the nearest odd integer >= 1.
  // Used at block rate for Size-scaled delay lengths.
  // (Full prime-snapping deferred to 0.1.0.7; Brownian modulation already
  // smears eigentones so strict primeness is less critical post-modulation.)
  // ---------------------------------------------------------------------------
  static inline int toOdd(int x)
  {
    if (x < 1) x = 1;
    return (x % 2 == 0) ? x + 1 : x;
  }

  class APFTank : public od::Object
  {
  public:
    APFTank()
    {
      addInput(mInL);
      addInput(mInR);
      addOutput(mOutL);
      addOutput(mOutR);
      addParameter(mSize);
      addParameter(mDecay);
      addParameter(mDamp);
      addParameter(mDiffusion);
      // Mod / ModRate are DEMOTED to baked-in character (not exposed): the knobs
      // did little, and the organic Brownian drift is part of Fabula's identity.
      // The members are kept (mMod.value()/mModRate.value() return their defaults)
      // so they can be re-exposed or CV-tied later without a layout change.
      addParameter(mPredelay);
      addParameter(mMix);
      addParameter(mEarly);
      addParameter(mFreeze);
      addParameter(mReverse);
      addParameter(mCharacter);

      memset(mPD,     0, sizeof(mPD));
      memset(mER,     0, sizeof(mER));   // ER ring buffer — all silence
      mWrER = 0;
      // ER diffuser allpass buffers (0.1.0.12) — 2 stages per channel, g=0.6.
      // Zeroed so first-block output is silence (consistent with zero input on startup).
      memset(mERD_L1, 0, sizeof(mERD_L1));
      memset(mERD_L2, 0, sizeof(mERD_L2));
      memset(mERD_R1, 0, sizeof(mERD_R1));
      memset(mERD_R2, 0, sizeof(mERD_R2));
      mWrERD_L1 = 0; mWrERD_L2 = 0;
      mWrERD_R1 = 0; mWrERD_R2 = 0;
      memset(mID1,    0, sizeof(mID1));
      memset(mID2,    0, sizeof(mID2));
      memset(mID3,    0, sizeof(mID3));
      memset(mID4,    0, sizeof(mID4));
      // L-loop allpass + delay buffers
      // Outer AP buffers (mTA1_L, mTA2_L) sized kTA1_size/kTA2_size (headroom for Brownian mod).
      // Inner AP buffers (mTA1i_L, mTA2i_L) sized exactly kTA1i/kTA2i — UNMODULATED.
      memset(mTA1_L,   0, sizeof(mTA1_L));
      memset(mTA1i_L,  0, sizeof(mTA1i_L));   // AP1 inner cascade buffer (UNMODULATED)
      memset(mTA2_L,   0, sizeof(mTA2_L));
      memset(mTA2i_L,  0, sizeof(mTA2i_L));   // AP2 inner cascade buffer (UNMODULATED)
      memset(mD1_L,    0, sizeof(mD1_L));
      memset(mD2_L,    0, sizeof(mD2_L));
      // R-loop allpass + delay buffers (same sizing rationale as L).
      memset(mTA1_R,   0, sizeof(mTA1_R));
      memset(mTA1i_R,  0, sizeof(mTA1i_R));   // AP1 inner cascade buffer (UNMODULATED)
      memset(mTA2_R,   0, sizeof(mTA2_R));
      memset(mTA2i_R,  0, sizeof(mTA2i_R));   // AP2 inner cascade buffer (UNMODULATED)
      memset(mD1_R,    0, sizeof(mD1_R));
      memset(mD2_R,    0, sizeof(mD2_R));

      mWrPD  = 0;
      mWrID1 = 0; mWrID2 = 0; mWrID3 = 0; mWrID4 = 0;
      // L-loop write heads (outer allpasses)
      mWrTA1_L = 0; mWrTA1i_L = 0;   // AP1 outer + inner
      mWrTA2_L = 0; mWrTA2i_L = 0;   // AP2 outer + inner
      mWrD1_L  = 0; mWrD2_L  = 0;
      // R-loop write heads (outer allpasses)
      mWrTA1_R = 0; mWrTA1i_R = 0;   // AP1 outer + inner
      mWrTA2_R = 0; mWrTA2i_R = 0;   // AP2 outer + inner
      mWrD1_R  = 0; mWrD2_R  = 0;

      mFeedback_L = 0.0;
      mFeedback_R = 0.0;

      // Brownian LFO seeds — four DISTINCT non-zero compile-time constants.
      // L-line seeds (D1_L, D2_L) and R-line seeds (D1_R, D2_R) are in
      // separate numeric neighborhoods to guarantee decorrelation from
      // sample 0 (per fabula-design.md §5). Never use 0 (xorshift64 fixed
      // point). Values chosen to be far apart in the 64-bit state space.
      mSeed_D1_L = UINT64_C(0x9E3779B97F4A7C15);  // golden-ratio constant
      mSeed_D2_L = UINT64_C(0x6C62272E07BB0142);  // pi bits
      mSeed_D1_R = UINT64_C(0xBF58476D1CE4E5B9);  // splitmix64 constant
      mSeed_D2_R = UINT64_C(0x94D049BB133111EB);  // splitmix64 constant 2

      // Walk accumulators start at 0 (center of headroom window).
      mWalk_D1_L = 0.0;
      mWalk_D2_L = 0.0;
      mWalk_D1_R = 0.0;
      mWalk_D2_R = 0.0;

      // Outer AP Brownian walk seeds (0.1.0.11) — four NEW seeds, DISTINCT from each
      // other and from all four D1/D2 delay-line seeds above. Chosen from different
      // numeric neighborhoods (wyhash / Murmur constants) for guaranteed decorrelation
      // from sample 0. Never use 0 (xorshift64 fixed point).
      //   D1/D2 seeds use: 0x9E37..., 0x6C62..., 0xBF58..., 0x94D0... (golden/pi/splitmix)
      //   AP seeds use:    0xA076..., 0xE703..., 0x3184..., 0xC6BC... (wyhash/murmur)
      mSeed_AP1_L = UINT64_C(0xA0761D6478BD642F);  // wyhash constant a
      mSeed_AP2_L = UINT64_C(0xE7037ED1A0B428DB);  // wyhash constant b
      mSeed_AP1_R = UINT64_C(0x31848A9BCDB0E235);  // distinct neighborhood
      mSeed_AP2_R = UINT64_C(0xC6BC279692B5CC83);  // Murmur3 finalizer constant

      // AP walk accumulators — start at 0 (center of headroom window).
      mWalk_AP1_L = 0.0;
      mWalk_AP2_L = 0.0;
      mWalk_AP1_R = 0.0;
      mWalk_AP2_R = 0.0;

      // HF damp filter state (one per loop, initialized to silence).
      mDampL = 0.0;
      mDampR = 0.0;

      // DC blocker state (one pair per loop, initialized to 0).
      mDCx1_L = 0.0;  mDCy1_L = 0.0;
      mDCx1_R = 0.0;  mDCy1_R = 0.0;

      // Size-scaled delay lengths — initialize consistent with defaults Size=0.35, Early=0.4.
      //
      // 0.1.0.12: scaled lengths now use the EFFECTIVE sizeFactor after macro coupling.
      //   Raw sizeFactor at Size=0.35: 0.18 + 1.32*0.35^0.647 = 0.850
      //   macro = kEarlyMacroDepth * earlyDefault = 0.5 * 0.4 = 0.20
      //   sizeFactorEff = 0.850 * (1 - 0.20 * kMacroSizeAmt)
      //                 = 0.850 * (1 - 0.20 * 0.5) = 0.850 * 0.90 = 0.7650
      //
      //   D1_L: round(7187*0.765)=round(5498.1)=5498 → toOdd → 5499
      //   D2_L: round(5101*0.765)=round(3902.3)=3902 → toOdd → 3903
      //   D1_R: round(6803*0.765)=round(5204.3)=5204 → toOdd → 5205
      //   D2_R: round(6343*0.765)=round(4852.4)=4852 → toOdd → 4853
      //
      //   mSizeFactorSmoothed initialized to 0.7650 (matches first-block target exactly
      //   → no first-block step; the smoother starts converged).
      mScaledD1_L = 2749;   // SR/2 tank: half the 48k default scaled lengths
      mScaledD2_L = 1951;   // (block-rate compute overwrites on first block)
      mScaledD1_R = 2603;
      mScaledD2_R = 2427;
      mSmD1_L = 2749.0f; mSmD2_L = 1951.0f; mSmD1_R = 2603.0f; mSmD2_R = 2427.0f;
      mSmPD = 0.0f; mSmEarly = 0.4f;
      mWalkPhase = 0;
      mWalkInc_D1_L = mWalkInc_D2_L = mWalkInc_D1_R = mWalkInc_D2_R = 0.0f;
      mWalkInc_AP1_L = mWalkInc_AP2_L = mWalkInc_AP1_R = mWalkInc_AP2_R = 0.0f;
      mTankPhase = 0;
      mDecimPrev = 0.0f;
      mTankWetL_prev = mTankWetL_curr = 0.0f;
      mTankWetR_prev = mTankWetR_curr = 0.0f;
      mWetHpLp1_L = mWetHpLp2_L = mWetHpLp1_R = mWetHpLp2_R = 0.0f;
      mFreezeSmoothed = 0.0f;

      // Reverse: zero buffers, seed grains (B offset by half a grain), fill the
      // Hann LUT via a cosine RECURRENCE (no runtime trig — am335x package sinf/
      // cosf can miscompute; see feedback_package_trig_lut). Recurrence:
      // cos((n+1)t) = 2 cos(t) cos(nt) - cos((n-1)t), Hann[n] = 0.5*(1 - cos(nt)).
      memset(mRevBufL, 0, sizeof(mRevBufL));
      memset(mRevBufR, 0, sizeof(mRevBufR));
      mRevWr = 0;
      mRevPhaseA = 0;            mRevAnchorA = 0;
      mRevPhaseB = kRevGrainHalf; mRevAnchorB = 0;
      mReverseSmoothed = 0.0f;
      mCharacterSmoothed = 0.0f;
      {
        const double ct = 0.99998117528260111;  // cos(2*pi/1024)
        double cm1 = 1.0;   // cos(0)
        double c0  = ct;    // cos(t)
        mHannLut[0] = 0.0f;
        mHannLut[1] = (float)(0.5 * (1.0 - c0));
        for (int n = 2; n < kHannSize; n++)
        {
          double cn = 2.0 * ct * c0 - cm1;
          mHannLut[n] = (float)(0.5 * (1.0 - cn));
          cm1 = c0; c0 = cn;
        }
      }
      mLastSize         = 0.35f;
      mLastEarly        = 0.4f;
      mSizeFactor       = 0.850;    // raw sizeFactor (Size only), for reference
      mSizeFactorSmoothed = 0.7650; // effective sizeFactor at defaults — smoother starts converged
    }

    virtual ~APFTank() {}

#ifndef SWIGLUA
    od::Inlet     mInL{"In L"};
    od::Inlet     mInR{"In R"};
    od::Outlet    mOutL{"Out L"};
    od::Outlet    mOutR{"Out R"};
    od::Parameter mSize{"Size", 0.35f};
    od::Parameter mDecay{"Decay", 0.30f};
    od::Parameter mDamp{"Damp", 0.40f};
    od::Parameter mDiffusion{"Diffusion", 0.45f};
    od::Parameter mMod{"Mod", 0.40f};
    od::Parameter mModRate{"ModRate", 0.2f};
    od::Parameter mPredelay{"Predelay", 0.041f};
    od::Parameter mMix{"Mix", 0.40f};
    od::Parameter mEarly{"Early", 0.4f};
    od::Parameter mFreeze{"Freeze", 0.0f};
    od::Parameter mReverse{"Reverse", 0.0f};
    od::Parameter mCharacter{"Character", 0.0f};

    virtual void process()
    {
      float *in1  = mInL.buffer();
      float *in2  = mInR.buffer();
      float *out1 = mOutL.buffer();
      float *out2 = mOutR.buffer();

      // ------------------------------------------------------------------
      // Read parameters (block rate).
      // Predelay:  0..1 maps to 0..(kPD-1) samples. Integer tap.
      // Mix:       0..1 linear crossfade.
      // Mod:       0..1 → excursion in samples (kMinExcursion..kMaxExcursion).
      // ModRate:   0..1 → walk step_size per sample (kMinStep..kMaxStep).
      // Damp:      0..1 → one-pole LP coeff for HF damping.
      // Decay:     0..1 → g_d via power-curve map.
      // Size:      0..1 → sizeFactor → scaled delay lengths (block rate).
      // Diffusion: 0..1 → scales all 6 allpass coefficients around baselines.
      //            Default 0.6 reproduces the historical fixed values exactly.
      // ------------------------------------------------------------------
      const float predelayParam  = mPredelay.value();   // 0..1
      const float mix            = mMix.value();        // 0..1
      const float modParam       = mMod.value();        // 0..1 (baked-in, see ctor)
      const float modRateParam   = mModRate.value();    // 0..1 (baked-in, see ctor)
      const float dampParam      = mDamp.value();       // 0..1
      const float decayParam     = mDecay.value();      // 0..1
      const float sizeParam      = mSize.value();       // 0..1
      const float diffusionParam = mDiffusion.value();  // 0..1
      // Early: 0..1, scales ER contribution. At 0 → no ER (exact 0.1.0.8 output).
      const float earlyParam     = mEarly.value();      // 0..1

      // Predelay tap is now a per-sample-smoothed FLOAT (targetPD below); the old
      // integer predelayTap is gone (zipper fix on Pre).

      // Decay → g_d via power-curve: g_d = kGdMin + (kGdMax - kGdMin) * decay^kDecayShape
      // Hard cap at kGdCap prevents g_d >= 1 unconditionally.
      // pow(0, kDecayShape) = 0 safely; pow(1, kDecayShape) = 1 safely.
      double decayD = (double)decayParam;
      // Clamp input to [0,1] before pow to avoid domain errors.
      if (decayD < 0.0) decayD = 0.0;
      if (decayD > 1.0) decayD = 1.0;
      double g_d = kGdMin + (kGdMax - kGdMin) * pow(decayD, kDecayShape);
      if (g_d > kGdCap) g_d = kGdCap;   // unconditional hard safety cap

      // Damp → base dampD for the one-pole LP coefficient.
      // 0.1.0.12: dampCoeff is no longer computed directly here; instead the Early
      // macro coupling (below) computes dampCoeffEff from dampD + macro bias.
      // coeff = 1.0 - dampEff * (1.0 - kMinDampCoeff)
      // Damp=0,Early=0 → dampEff=0 → coeff=1.0 (open, passthrough).
      // Damp=1,Early=0 → dampEff=1 → coeff=kMinDampCoeff (dark).
      double dampD     = (double)dampParam;
      if (dampD < 0.0) dampD = 0.0;
      if (dampD > 1.0) dampD = 1.0;
      // dampCoeffEff computed in the macro coupling block below.

      // Diffusion → allpass coefficients.
      // Linear offset from each baseline: coeff = base + (D - 0.6) * kDiffSlope.
      // Clamped to [kDiffLo, kDiffHi] = [0.30, 0.85] — all values strictly < 1.
      // At Diffusion=0.6 (default): diffDelta=0 → each coeff equals its
      // historical fixed value exactly (approved sound preserved).
      // Higher Diffusion → denser smearing; lower → more open/transient-clear.
      double diffD = (double)diffusionParam;
      if (diffD < 0.0) diffD = 0.0;
      if (diffD > 1.0) diffD = 1.0;
      const double diffDelta = (diffD - 0.6) * kDiffSlope;

      // Helper: clamp a mapped coefficient to the stable allpass range.
      // Inline expansion — no function call overhead in the hot path.
      #define DIFFCLAMP(v) ((v) < kDiffLo ? kDiffLo : ((v) > kDiffHi ? kDiffHi : (v)))

      const float gID12 = DIFFCLAMP(kDiffBaseID12 + diffDelta);
      const float gID34 = DIFFCLAMP(kDiffBaseID34 + diffDelta);
      const float gTA1  = DIFFCLAMP(kDiffBaseTA1  + diffDelta);
      const float gTA2  = DIFFCLAMP(kDiffBaseTA2  + diffDelta);

      #undef DIFFCLAMP

      // Early macro coupling (0.1.0.12) — compute EFFECTIVE Size/Decay/Damp.
      //
      // macro ∈ [0, kEarlyMacroDepth]. At Early=0: macro=0 → effective=manual exactly.
      // At Early=1: macro=kEarlyMacroDepth → maximum bias toward smaller/shorter/warmer room.
      //
      // Size bias: sizeFactorEff = sizeFactor * (1 - macro*kMacroSizeAmt)
      //   Smaller room as Early rises. Smoothed per block to avoid click on Early sweep.
      // Decay bias: g_d_eff = g_d - macro*kMacroDecayAmt*(g_d - kGdMin)
      //   Shorter tail as Early rises. Gain change — click-free.
      // Damp bias: effective Damp raised, pulling dampCoeff toward more absorption (lower coeff).
      //   Warmer/darker tail as Early rises. Coeff change — click-free.
      //
      // Size → raw sizeFactor (block rate, always recomputed — cheap pow(), and
      // sizeFactorEff changes every block during smoothing anyway).
      double sizeD = (double)sizeParam;
      if (sizeD < 0.0) sizeD = 0.0;
      if (sizeD > 1.0) sizeD = 1.0;
      const double sizeFactor = kSizeMin + (kSizeMax - kSizeMin) * pow(sizeD, kSizeShape);
      mSizeFactor = sizeFactor;   // raw (Size-only) stored for reference

      // Early macro: pull toward smaller/shorter/warmer room.
      const double macro = kEarlyMacroDepth * (double)earlyParam;

      // Effective sizeFactor: smaller room as Early rises.
      const double sizeFactorEffTarget = sizeFactor * (1.0 - macro * kMacroSizeAmt);
      // Clamp effective sizeFactor to a safe minimum (bounds-safety at extreme Early+small Size).
      const double sizeFactorEffClamped = (sizeFactorEffTarget < kSizeFactorMin)
                                        ? kSizeFactorMin : sizeFactorEffTarget;
      // One-pole smooth toward the target to soften delay-length jumps on Early/Size sweeps.
      mSizeFactorSmoothed += kSizeFactorSmooth * (sizeFactorEffClamped - mSizeFactorSmoothed);
      const double sizeFactorEff = mSizeFactorSmoothed;
      // Float glide targets for the per-sample base smoother (zipper fix). No
      // toOdd/int quantization here -> a continuous target the loop glides toward.
      // Clamp to the same buffer-safe max base as the int taps below.
      const float sfEff       = (float)sizeFactorEff;
      const float targetD1_L  = fminf((float)kD1_L_maxBase, (float)kD1_L_base * sfEff);
      const float targetD2_L  = fminf((float)kD2_L_maxBase, (float)kD2_L_base * sfEff);
      const float targetD1_R  = fminf((float)kD1_R_maxBase, (float)kD1_R_base * sfEff);
      const float targetD2_R  = fminf((float)kD2_R_maxBase, (float)kD2_R_base * sfEff);
      const float targetPD    = (float)predelayParam * (float)(kPD - 1);
      const float targetEarly = (float)earlyParam;

      // Effective g_d: shorter tail as Early rises.
      // g_d_eff = g_d - macro*kMacroDecayAmt*(g_d - kGdMin)
      float g_d_eff = g_d - macro * kMacroDecayAmt * (g_d - kGdMin);
      if (g_d_eff < kGdMin)  g_d_eff = kGdMin;   // floor: never below kGdMin
      if (g_d_eff > kGdCap)  g_d_eff = kGdCap;   // ceiling: same hard cap as raw

      // Effective dampCoeff: more HF absorption as Early rises.
      // Bias Damp toward 1.0 (max damp) by raising the effective Damp before coeff mapping.
      // dampEff ∈ [dampD, 1.0]; the coeff mapping then gives a lower (darker) value.
      double dampEffD = dampD + macro * kMacroDampAmt;
      if (dampEffD > 1.0) dampEffD = 1.0;
      const float dampCoeffEff48 = 1.0 - dampEffD * (1.0 - kMinDampCoeff);
      // SR/2 tank: the HF-damp one-pole runs at 24k, so preserve its cutoff-in-Hz.
      // For pole p, cutoff ~ (1-p)*SR; same Hz at half SR needs (1-p')=(1-p)^2,
      // i.e. alpha' = alpha*(2-alpha) (larger alpha = brighter, compensating the
      // rate drop). TUNING-SENSITIVE: verify tail brightness by ear (approx rule).
      const float dampCoeffEff = dampCoeffEff48 * (2.0f - dampCoeffEff48);
      // (dampCoeffEff replaces dampCoeff everywhere damping is applied in the tank.)

      // --- Living Freeze (continuous) — block-rate macro derivation ---------------
      const float freezeParam = mFreeze.value();                 // 0..1
      mFreezeSmoothed += kFreezeSlew * (freezeParam - mFreezeSmoothed);
      const float fz = mFreezeSmoothed;
      // Staggered lock: fzA drives L's cross-feed (unity by fz=0.85), fzB drives
      // R's (begins at fz=0.15, unity by fz=1.0) -> the tail sets in stages.
      float fzA = fz / kFreezeStaggerL;
      if (fzA > 1.0f) fzA = 1.0f;
      float fzB = (fz - kFreezeStaggerOff) / kFreezeStaggerL;
      if (fzB < 0.0f) fzB = 0.0f;
      if (fzB > 1.0f) fzB = 1.0f;
      // Feedback -> unity as freeze rises (spiral governor keeps it bounded).
      const float gdFreezeA = g_d_eff + fzA * (kFreezeGd - g_d_eff);
      const float gdFreezeB = g_d_eff + fzB * (kFreezeGd - g_d_eff);
      // Lift damp toward passthrough so the frozen cloud stays bright.
      const float fzMean = 0.5f * (fzA + fzB);
      const float dampCoeffFreeze = dampCoeffEff + fzMean * (1.0f - dampCoeffEff);
      // Mute new input into the tank as it freezes (preserve the frozen content).
      const float tankInGain = 1.0f - fz;

      // --- Reverse amount (block-rate smoothed) ---
      const float reverseParam = mReverse.value();               // 0..1
      mReverseSmoothed += kRevSlew * (reverseParam - mReverseSmoothed);
      const float revAmt = mReverseSmoothed;

      // --- Character: clean -> saturated -> folded (block-rate smoothed) ---
      const float characterParam = mCharacter.value();           // 0..1
      mCharacterSmoothed += kCharSlew * (characterParam - mCharacterSmoothed);
      const float ch = mCharacterSmoothed;
      const float charDensity = 1.0f + ch * kCharSatMax;         // saturator drive
      float charFoldAmt = (ch - 0.5f) * 2.0f;                    // fold in over top half
      if (charFoldAmt < 0.0f) charFoldAmt = 0.0f;
      const float charFoldDrive = 1.0f + charFoldAmt * kCharFoldMax;

      // Scaled delay lengths — recomputed every block from sizeFactorEff (smoothed),
      // since the smoothed value moves continuously when converging.
      // Round to nearest odd; clamp to [1, maxBase].
      mLastSize  = sizeParam;    // track for reference (no longer used as change guard)
      mLastEarly = earlyParam;
      {
        int d1L = toOdd((int)(kD1_L_base * sizeFactorEff + 0.5));
        int d2L = toOdd((int)(kD2_L_base * sizeFactorEff + 0.5));
        int d1R = toOdd((int)(kD1_R_base * sizeFactorEff + 0.5));
        int d2R = toOdd((int)(kD2_R_base * sizeFactorEff + 0.5));
        if (d1L > kD1_L_maxBase) d1L = kD1_L_maxBase;
        if (d2L > kD2_L_maxBase) d2L = kD2_L_maxBase;
        if (d1R > kD1_R_maxBase) d1R = kD1_R_maxBase;
        if (d2R > kD2_R_maxBase) d2R = kD2_R_maxBase;
        if (d1L < 1) d1L = 1;
        if (d2L < 1) d2L = 1;
        if (d1R < 1) d1R = 1;
        if (d2R < 1) d2R = 1;
        mScaledD1_L = d1L;
        mScaledD2_L = d2L;
        mScaledD1_R = d1R;
        mScaledD2_R = d2R;
      }

      // Load scaled lengths as local ints for the sample loop.
      const int scaledD1_L = mScaledD1_L;
      const int scaledD2_L = mScaledD2_L;
      const int scaledD1_R = mScaledD1_R;
      const int scaledD2_R = mScaledD2_R;

      // Multi-tap intermediate offset computation (block rate).
      // Each offset = fraction * scaledLength, rounded to nearest odd integer.
      // Bounds: fraction < 1.0 so off < scaledLength <= bufSize — always safe.
      // The same fractions apply to both L and R loops (only scaled lengths differ).
      // toOdd() guarantees odd integer >= 1; clamp is unnecessary (fractions < 0.70).
      //
      // L-loop D1 taps (three intermediate points):
      const int offD1a_L = toOdd((int)(kFD1a * scaledD1_L + 0.5));
      const int offD1b_L = toOdd((int)(kFD1b * scaledD1_L + 0.5));
      const int offD1c_L = toOdd((int)(kFD1c * scaledD1_L + 0.5));
      // L-loop D2 taps (two intermediate points):
      const int offD2a_L = toOdd((int)(kFD2a * scaledD2_L + 0.5));
      const int offD2b_L = toOdd((int)(kFD2b * scaledD2_L + 0.5));
      // R-loop D1 taps:
      const int offD1a_R = toOdd((int)(kFD1a * scaledD1_R + 0.5));
      const int offD1b_R = toOdd((int)(kFD1b * scaledD1_R + 0.5));
      const int offD1c_R = toOdd((int)(kFD1c * scaledD1_R + 0.5));
      // R-loop D2 taps:
      const int offD2a_R = toOdd((int)(kFD2a * scaledD2_R + 0.5));
      const int offD2b_R = toOdd((int)(kFD2b * scaledD2_R + 0.5));

      // Inner AP coefficients — fixed this sub-phase; hard cap < 0.95.
      const float gTA1_in = (kGTA1_in > 0.95) ? 0.95 : kGTA1_in;
      const float gTA2_in = (kGTA2_in > 0.95) ? 0.95 : kGTA2_in;

      // Modulation parameters — computed once per block.
      // excursion: how far the walk can stray from center, in samples.
      // step_size: per-sample walk increment multiplied by noise ∈ [-0.5, 0.5].
      // TUNING: to shift the sweet spot, adjust kMinExcursion/kMaxExcursion
      //   and kMinStep/kMaxStep at the top of this file.
      //
      // EXCURSION SCALING (0.1.0.10): scale excursion proportionally to sizeFactor
      // so the RELATIVE pitch-mod (excursion/delayLength) stays constant across
      // the Size range. Without scaling, a ±72-sample walk on a ~900-sample delay
      // (~8% relative) would produce audible warble in small rooms. With scaling,
      // the ratio stays ~0.56% at all sizes — the same pleasant chorus as default.
      // At Size=0.35 (mSizeFactor=0.850=kSizeRef) the scale factor is exactly 1.0:
      // the current default behavior is UNCHANGED.
      // The raw (unscaled) excursion is still bounded to kMaxExcursion for safety;
      // the size-scaled value is always <= raw, so headroom margins still hold.
      const double rawExcursion = kMinExcursion + (double)modParam * (kMaxExcursion - kMinExcursion);
      // Scale excursion by (sizeFactor/kSizeRef) to keep relative pitch-mod constant.
      // Cap at kMaxExcursion so that at large Size the headroom margin is preserved:
      //   at Size=1.0, scale=1.5/0.85≈1.76; without cap, scaled excursion could reach
      //   72*1.76=127 smp → walk+interp=128 = headroom limit with zero margin. Cap
      //   holds it at 72 smp max — safe with the 128-sample headroom as before.
      float excursion = rawExcursion * (sizeFactorEff / kSizeRef);
      if (excursion > kMaxExcursion) excursion = kMaxExcursion;
      const float step_size = kMinStep + (double)modRateParam * (kMaxStep - kMinStep);

      // Outer AP modulation excursion (0.1.0.11).
      // Driven by same Mod param (unified depth control) but mapped to a smaller
      // window suitable for allpass modulation (not Size-scaled: AP delays are fixed).
      //   Mod=0.00 → 0.5 smp (near-static)
      //   Mod=0.40 → 2.7 smp (gentle, default)
      //   Mod=1.00 → 6.0 smp (subtle, below audible chorus on 1087/1471 smp delays)
      // Bounds: apExcursion=6 + interp neighbor=1 → 7 < kAPHeadroom=16 — safe.
      const float apExcursion = kAPModMin + (double)modParam * (kAPModMax - kAPModMin);

      // Copy walk accumulators and seeds into local variables for the inner
      // loop. Propagate back to members at end of block.
      float walk_D1_L = mWalk_D1_L;
      float walk_D2_L = mWalk_D2_L;
      float walk_D1_R = mWalk_D1_R;
      float walk_D2_R = mWalk_D2_R;

      uint64_t seed_D1_L = mSeed_D1_L;
      uint64_t seed_D2_L = mSeed_D2_L;
      uint64_t seed_D1_R = mSeed_D1_R;
      uint64_t seed_D2_R = mSeed_D2_R;

      // Outer AP Brownian walk locals (0.1.0.11) — propagated back after the block.
      float walk_AP1_L = mWalk_AP1_L;
      float walk_AP2_L = mWalk_AP2_L;
      float walk_AP1_R = mWalk_AP1_R;
      float walk_AP2_R = mWalk_AP2_R;

      uint64_t seed_AP1_L = mSeed_AP1_L;
      uint64_t seed_AP2_L = mSeed_AP2_L;
      uint64_t seed_AP1_R = mSeed_AP1_R;
      uint64_t seed_AP2_R = mSeed_AP2_R;

      // Copy HF damp filter state local for the sample loop.
      float dampL = mDampL;
      float dampR = mDampR;

      // Copy DC blocker state local for the sample loop.
      float dcx1_L = mDCx1_L;  float dcy1_L = mDCy1_L;
      float dcx1_R = mDCx1_R;  float dcy1_R = mDCy1_R;

      // Per-sample-smoothed delay bases / predelay tap / ER level (zipper fix):
      // pulled local, glided one-pole toward the block targets each sample, then
      // written back below. Fractional throughout -> no stepping on Size/Pre/ER.
      float smD1_L = mSmD1_L, smD2_L = mSmD2_L, smD1_R = mSmD1_R, smD2_R = mSmD2_R;
      float smPD    = mSmPD;
      float smEarly = mSmEarly;

      // Decimated Brownian-walk locals (phase + per-line ramp increments).
      int   walkPhase = mWalkPhase;
      float walkInc_D1_L = mWalkInc_D1_L, walkInc_D2_L = mWalkInc_D2_L;
      float walkInc_D1_R = mWalkInc_D1_R, walkInc_D2_R = mWalkInc_D2_R;
      float walkInc_AP1_L = mWalkInc_AP1_L, walkInc_AP2_L = mWalkInc_AP2_L;
      float walkInc_AP1_R = mWalkInc_AP1_R, walkInc_AP2_R = mWalkInc_AP2_R;

      int sampleFrames = FRAMELENGTH;
      while (--sampleFrames >= 0)
      {
        // Tank runs at SR/2: this host sample ticks the recirculating tank only
        // when phase==0 (every 2nd host sample). The Brownian walk, the tank
        // fractional reads, and the DC/damp/feedback state all advance at tank
        // rate, so they live inside the tankTick gate below.
        bool tankTick = (mTankPhase == 0);

        // ----------------------------------------------------------------
        // 0. Brownian-walk update (decimated 1/kWalkDecim, linearly ramped).
        //    All 8 per-line walks (4 tank D + 4 outer AP) advance their PRNG and
        //    re-target only once per kWalkDecim samples; each sample every walk
        //    ramps by its stored increment. The boundary step is x sqrt(kWalkDecim)
        //    so the random-walk variance (drift rate) matches the old per-sample
        //    integrator. Both target endpoints are clamped, so the linear ramp
        //    stays in bounds -> no per-sample clamp needed. ~16x fewer xorshift64.
        // ----------------------------------------------------------------
        if (tankTick) {
        if (walkPhase == 0)
        {
          #define ZAUM_WALK_RETARGET(seedv, walkv, incv, exc)                 \
            do {                                                              \
              seedv = xorshift64(seedv);                                      \
              float _n = (seedv & 0xFFFF) * (1.0f / 65535.0f) - 0.5f;         \
              float _t = walkv + _n * step_size * kWalkStepScale;             \
              if (_t >  (exc)) _t =  (exc);                                   \
              if (_t < -(exc)) _t = -(exc);                                   \
              incv = (_t - walkv) * kWalkIncScale;                            \
            } while (0)
          ZAUM_WALK_RETARGET(seed_D1_L, walk_D1_L, walkInc_D1_L, excursion);
          ZAUM_WALK_RETARGET(seed_D2_L, walk_D2_L, walkInc_D2_L, excursion);
          ZAUM_WALK_RETARGET(seed_D1_R, walk_D1_R, walkInc_D1_R, excursion);
          ZAUM_WALK_RETARGET(seed_D2_R, walk_D2_R, walkInc_D2_R, excursion);
          ZAUM_WALK_RETARGET(seed_AP1_L, walk_AP1_L, walkInc_AP1_L, apExcursion);
          ZAUM_WALK_RETARGET(seed_AP2_L, walk_AP2_L, walkInc_AP2_L, apExcursion);
          ZAUM_WALK_RETARGET(seed_AP1_R, walk_AP1_R, walkInc_AP1_R, apExcursion);
          ZAUM_WALK_RETARGET(seed_AP2_R, walk_AP2_R, walkInc_AP2_R, apExcursion);
          #undef ZAUM_WALK_RETARGET
        }
        walk_D1_L += walkInc_D1_L;  walk_D2_L += walkInc_D2_L;
        walk_D1_R += walkInc_D1_R;  walk_D2_R += walkInc_D2_R;
        walk_AP1_L += walkInc_AP1_L; walk_AP2_L += walkInc_AP2_L;
        walk_AP1_R += walkInc_AP1_R; walk_AP2_R += walkInc_AP2_R;
        if (++walkPhase >= kWalkDecim) walkPhase = 0;
        }  // end if (tankTick) — the Brownian walk advances at tank rate

        // ----------------------------------------------------------------
        // 1. Mono sum of L + R inputs.
        // ----------------------------------------------------------------
        float drySampleL = *in1;
        float drySampleR = *in2;
        float monoIn = (drySampleL + drySampleR) * 0.5f;

        // ----------------------------------------------------------------
        // 2. Predelay ring buffer.
        //    Write, then read at tap distance behind write head.
        //    Buffer size kPD is power-of-two: wrap with & mask.
        // ----------------------------------------------------------------
        mPD[mWrPD] = monoIn;
        // Glide the fractional predelay tap toward its target (zipper fix), then
        // read with linear interpolation. rd0 is the nearer (smaller-delay) sample,
        // rd1 one step older; pdF crossfades toward the older sample as the
        // fractional distance grows.
        smPD += kBaseSlew * (targetPD - smPD);
        int   pdI = (int)smPD;
        float pdF = smPD - (float)pdI;
        int   rd0 = (mWrPD - pdI) & (kPD - 1);
        int   rd1 = (rd0 - 1) & (kPD - 1);
        float diffIn = mPD[rd0] + pdF * (mPD[rd1] - mPD[rd0]);
        mWrPD = (mWrPD + 1) & (kPD - 1);

        // Glide the ER level toward its target (zipper fix on the Early knob).
        // The tap-read guards + final scale below both key off smEarly so the ER
        // network fades in/out instead of switching.
        smEarly += kBaseSlew * (targetEarly - smEarly);

        // ----------------------------------------------------------------
        // 2b. Early-reflection (ER) network — Tier 3 (0.1.0.9).
        //
        // Write the pre-diffusion predelayed sample into the dedicated ER
        // ring buffer. Then read 9 taps per channel at irregular delays in
        // the 7–70 ms window (adapted from Moorer 1979 / Gardner 1992).
        // L and R use DIFFERENT tap delays for stereo decorrelation.
        // All reads: ((mWrER - delay) & (kER-1)) — in-bounds guaranteed
        // because all delays < kER=4096. Power-of-two & mask handles wrap.
        //
        // erSumL and erSumR are the signed weighted tap sums. They are added
        // to wetL/wetR (below, after the tank multi-tap) scaled by:
        //   kERLevel * earlyParam
        // At earlyParam=0.0f the entire ER contribution is zero — the
        // 0.1.0.8 signal path is reproduced exactly (no floating-point
        // residual beyond the invariant 0 * anything = 0).
        // ----------------------------------------------------------------
        mER[mWrER] = diffIn;
        float erSumL = 0.0f;
        float erSumR = 0.0f;
        if (smEarly > 1e-4f)
        {
          for (int t = 0; t < kER_tapCount; t++)
          {
            int idxL = (mWrER - kER_delayL[t]) & (kER - 1);
            int idxR = (mWrER - kER_delayR[t]) & (kER - 1);
            erSumL += kER_gain[t] * (float)mER[idxL];
            erSumR += kER_gain[t] * (float)mER[idxR];
          }
        }
        mWrER = (mWrER + 1) & (kER - 1);

        // ----------------------------------------------------------------
        // 2c. ER diffuser — 2 series plain Schroeder allpasses per channel (0.1.0.12).
        //
        // Passes erSumL and erSumR through short static allpasses BEFORE
        // they are scaled into the wet output. This smears the 9 discrete
        // FIR tap positions into a continuous early wash, eliminating
        // slapback echoes at high Early. Purely feedforward — no connection
        // to the recirculating tank. Stability proof: |H_allpass|=1, g=0.6<1.
        //
        // Same allpassNestedStep form as the input-diffusion chain:
        //   vNew = x + g * v[n-N]
        //   yOut = -g * vNew + v[n-N]
        //
        // L: stage 1 (N=211), then stage 2 (N=317). g=kERDiffG=0.6 for all.
        // R: stage 1 (N=241), then stage 2 (N=359). Decorrelated from L.
        //
        // When earlyParam=0: erSumL=erSumR=0 (guarded above), so the diffuser
        // runs on zero input → output is zero → no contribution to wetL/wetR.
        // The Early=0 output is BIT-IDENTICAL to 0.1.0.11.
        // ----------------------------------------------------------------
        if (smEarly > 1e-4f)
        {
          // L channel diffuser — stage 1 (N=kERD_L1=211)
          {
            float vD = mERD_L1[mWrERD_L1];
            float vNew, yOut;
            allpassNestedStepF(erSumL, vD, kERDiffG, vNew, yOut);
            mERD_L1[mWrERD_L1] = vNew;
            mWrERD_L1++;
            if (mWrERD_L1 >= kERD_L1) mWrERD_L1 = 0;
            erSumL = yOut;
          }
          // L channel diffuser — stage 2 (N=kERD_L2=317)
          {
            float vD = mERD_L2[mWrERD_L2];
            float vNew, yOut;
            allpassNestedStepF(erSumL, vD, kERDiffG, vNew, yOut);
            mERD_L2[mWrERD_L2] = vNew;
            mWrERD_L2++;
            if (mWrERD_L2 >= kERD_L2) mWrERD_L2 = 0;
            erSumL = yOut;
          }
          // R channel diffuser — stage 1 (N=kERD_R1=241)
          {
            float vD = mERD_R1[mWrERD_R1];
            float vNew, yOut;
            allpassNestedStepF(erSumR, vD, kERDiffG, vNew, yOut);
            mERD_R1[mWrERD_R1] = vNew;
            mWrERD_R1++;
            if (mWrERD_R1 >= kERD_R1) mWrERD_R1 = 0;
            erSumR = yOut;
          }
          // R channel diffuser — stage 2 (N=kERD_R2=359)
          {
            float vD = mERD_R2[mWrERD_R2];
            float vNew, yOut;
            allpassNestedStepF(erSumR, vD, kERDiffG, vNew, yOut);
            mERD_R2[mWrERD_R2] = vNew;
            mWrERD_R2++;
            if (mWrERD_R2 >= kERD_R2) mWrERD_R2 = 0;
            erSumR = yOut;
          }
        }

        // ----------------------------------------------------------------
        // 3. Input diffusion: 4 series allpasses (fixed coefficients).
        //    Each AP: standard (non-nested) allpassNestedStep pattern.
        //      vNew = x + g * v[n-N]
        //      y    = -g * vNew + v[n-N]
        //    Write vNew to buffer; yOut feeds next stage.
        // ----------------------------------------------------------------

        // ID1 (delay 229, g=0.75)
        {
          float vD = mID1[mWrID1];   // read BEFORE write (v[n-N])
          float vNew, yOut;
          allpassNestedStepF(diffIn, vD, gID12, vNew, yOut);
          mID1[mWrID1] = vNew;
          mWrID1++;
          if (mWrID1 >= kID1) mWrID1 = 0;
          diffIn = yOut;
        }

        // ID2 (delay 173, g=0.75)
        {
          float vD = mID2[mWrID2];
          float vNew, yOut;
          allpassNestedStepF(diffIn, vD, gID12, vNew, yOut);
          mID2[mWrID2] = vNew;
          mWrID2++;
          if (mWrID2 >= kID2) mWrID2 = 0;
          diffIn = yOut;
        }

        // ID3 (delay 613, g=0.625)
        {
          float vD = mID3[mWrID3];
          float vNew, yOut;
          allpassNestedStepF(diffIn, vD, gID34, vNew, yOut);
          mID3[mWrID3] = vNew;
          mWrID3++;
          if (mWrID3 >= kID3) mWrID3 = 0;
          diffIn = yOut;
        }

        // ID4 (delay 449, g=0.625)
        {
          float vD = mID4[mWrID4];
          float vNew, yOut;
          allpassNestedStepF(diffIn, vD, gID34, vNew, yOut);
          mID4[mWrID4] = vNew;
          mWrID4++;
          if (mWrID4 >= kID4) mWrID4 = 0;
          diffIn = yOut;
        }

        // ----------------------------------------------------------------
        // 4. Figure-8 tank — L loop and R loop, stereo cross-coupled.
        //
        // MODULATION: each tank delay line is read at a fractional offset
        // that drifts via a per-line Brownian walk. The walk is driven by
        // xorshift64 noise integrated into a clamped accumulator. Linear
        // interpolation (two-point) resolves the fractional position so
        // there are no integer-step zippers.
        //
        // READ MECHANICS for each modulated delay line:
        //   1. Advance PRNG: seed = xorshift64(seed)
        //   2. Derive noise ∈ [-0.5, 0.5]: n = (seed & 0xFFFF)/65535.0 - 0.5
        //   3. Integrate:  walk += n * step_size
        //   4. Clamp:      walk = clamp(walk, -excursion, +excursion)
        //   5. Compute fractional read position:
        //        readPos = wrHead - scaledBase + walk   (float)
        //        offset  = (int)floor(readPos)
        //        frac    = readPos - (float)offset
        //   6. Wrap to valid buffer indices (add size, mod size):
        //        i0 = ((offset % bufSize) + bufSize) % bufSize
        //        i1 = (i0 + 1) % bufSize
        //   7. Interpolate: out = (1-frac)*buf[i0] + frac*buf[i1]
        //
        // DC BLOCKER: applied to tankIn_L / tankIn_R (before AP1), after
        // accumulating diffIn + feedback. Removes DC and sub-4 Hz content
        // before it enters the tank; prevents long-decay accumulation.
        // Form: y[n] = x[n] - x[n-1] + R*y[n-1]  (R = kDCBlockR = 0.9995)
        //
        // HF DAMP: one-pole LP applied to d1Read (after D1, before AP2).
        // Form: dampState += dampCoeff * (d1Read - dampState)
        // Output = dampState. This is in the recirculating feedback path
        // so each round trip accumulates one LP application → longer Decay
        // with higher Damp = progressively darker tail.
        //
        // CROSS-COUPLE WIRING (Dattorro, coupling coefficient = 1.0):
        //   mFeedback_L = spiralFastSaturate(d2Read_R * g_d_eff, 1.0f)
        //   mFeedback_R = spiralFastSaturate(d2Read_L * g_d_eff, 1.0f)
        //   (0.1.0.12: uses g_d_eff — macro-biased decay — not raw g_d)
        //
        // ORDERING: Both loops fully computed for THIS sample before
        // either feedback value is updated — no same-sample causality leak.
        // ----------------------------------------------------------------

        // --- SR/2 tank input: 2-tap half-band decimation of diffIn ---
        // tankInDec = 0.5*(diffIn[n] + diffIn[n-1]): null at 24k Nyquist, -3 dB at
        // 12k. Anti-aliases before the tank drops to half rate. mDecimPrev tracks
        // every host sample; tankInDec is only consumed on ticks.
        float tankInDec = 0.5f * (diffIn + mDecimPrev) * tankInGain;
        mDecimPrev = diffIn;

        if (tankTick)
        {
        // -- L LOOP --

        // Accumulate: diffusion output + previous-sample cross-feed from R.
        float tankIn_L = tankInDec + mFeedback_L;

        // DC blocker on tank input (L loop).
        // y[n] = x[n] - x[n-1] + R*y[n-1]
        {
          float dcOut = tankIn_L - dcx1_L + kDCBlockR * dcy1_L;
          dcx1_L = tankIn_L;
          dcy1_L = dcOut;
          tankIn_L = dcOut;
        }

        // AP1_L: series cascade — outer (kTA1=1087, g=gTA1) → inner (kTA1i=367, g=gTA1_in).
        // SERIES form (NOT in-feedback nesting): outer AP's feedforward output feeds
        // a second independent AP. Unity-gain by construction: |H_outer|·|H_inner|=1.
        //
        // 0.1.0.11: outer AP read is now a BROWNIAN-MODULATED fractional (linear-interp)
        // read at (kTA1 ± walk) behind the write head, using the headroom'd buffer
        // (kTA1_size=1119). Write still advances by 1 each sample at the write head.
        // Inner AP (kTA1i=367) remains UNMODULATED — exact read at write head.
        float ap1Out_L;
        {
          // Outer AP (kTA1=1087, g=gTA1 from Diffusion) — MODULATED read:
          // walk_AP1_L is updated at the top of the loop (decimated Brownian walk).

          // Fractional read at kTA1 + walk behind write head.
          float apReadPos1L = (float)(mWrTA1_L - kTA1) + walk_AP1_L + (float)kTA1_size;
          int    apOff1L     = (int)apReadPos1L;   // biased >=0: truncation == floor
          float apFrac1L    = apReadPos1L - (float)apOff1L;
          int    apI0_1L     = (apOff1L & (kTA1_size - 1));
          int    apI1_1L     = ((apI0_1L + 1) & (kTA1_size - 1));
          float vO1d = (1.0f - apFrac1L) * (float)mTA1_L[apI0_1L]
                      +        apFrac1L  * (float)mTA1_L[apI1_1L];

          float vO1n = tankIn_L + gTA1 * vO1d;
          float in1  = -gTA1 * vO1n + vO1d;               // outer AP output → inner input
          mTA1_L[mWrTA1_L] = vO1n;
          mWrTA1_L++;
          if (mWrTA1_L >= kTA1_size) mWrTA1_L = 0;
          // Inner AP (kTA1i=367, g=gTA1_in=0.50) — UNMODULATED, exact read:
          float vI1d = mTA1i_L[mWrTA1i_L];        // inner read v[n-367]
          float vI1n = in1 + gTA1_in * vI1d;
          ap1Out_L    = -gTA1_in * vI1n + vI1d;            // cascade output
          mTA1i_L[mWrTA1i_L] = vI1n;
          mWrTA1i_L++;
          if (mWrTA1i_L >= kTA1i) mWrTA1i_L = 0;
        }

        // D1_L: Brownian-modulated read with linear interpolation.
        // Uses Size-scaled base length (scaledD1_L).
        float d1Read_L;
        {
          // Write current sample to buffer.
          mD1_L[mWrD1_L] = ap1Out_L;

          // walk_D1_L is updated at the top of the loop (decimated Brownian walk).

          // Fractional read position relative to write head. The Size base is
          // per-sample-smoothed (smD1_L glides toward targetD1_L) so Size changes
          // Doppler-glide instead of stepping (zipper fix); walk offsets it.
          smD1_L += kBaseSlew * (targetD1_L - smD1_L);
          // + kD1_L_size biases readPos >= 0 so the (int) cast truncates == floor
          // WITHOUT a libm floor() call; the bias is an exact buffer length, so the
          // &-mask below yields a bit-identical index + frac. (drops 8 floor/sample)
          float readPos = ((float)mWrD1_L - smD1_L) + walk_D1_L + (float)kD1_L_size;
          int    offset  = (int)readPos;
          float frac    = readPos - (float)offset;

          // Map offset to valid buffer indices [0, kD1_L_size).
          int i0 = (offset & (kD1_L_size - 1));
          int i1 = ((i0 + 1) & (kD1_L_size - 1));

          d1Read_L = (1.0f - frac) * (float)mD1_L[i0] + frac * (float)mD1_L[i1];

          mWrD1_L++;
          if (mWrD1_L >= kD1_L_size) mWrD1_L = 0;
        }

        // D1_L intermediate taps — STATIC (unmodulated), read from the same buffer.
        // Offsets computed at block rate as fraction of scaledD1_L, rounded to odd.
        // Modular wrap matches the end-read pattern; all offsets < scaledD1_L < bufSize.
        float d1tap_a_L, d1tap_b_L, d1tap_c_L;
        {
          int ia = ((mWrD1_L - offD1a_L) & (kD1_L_size - 1));
          int ib = ((mWrD1_L - offD1b_L) & (kD1_L_size - 1));
          int ic = ((mWrD1_L - offD1c_L) & (kD1_L_size - 1));
          d1tap_a_L = mD1_L[ia];
          d1tap_b_L = mD1_L[ib];
          d1tap_c_L = mD1_L[ic];
        }

        // HF damp: one-pole LP on D1 output (Schroeder/Jot feedback form).
        // y += coeff * (x - y)  with state dampL.
        // Uses dampCoeffEff (macro-biased: more absorption as Early rises).
        // Damp=0,Early=0 → dampCoeffEff=1.0 → dampL tracks x exactly (passthrough).
        dampL += dampCoeffFreeze * (d1Read_L - dampL);
        float dampedD1_L = dampL;

        // AP2_L: series cascade — outer (kTA2=1471, g=gTA2) → inner (kTA2i=491, g=gTA2_in).
        // 0.1.0.11: outer AP2_L also uses Brownian-modulated fractional read (kTA2_size=1503).
        // Inner AP2i_L (491) remains UNMODULATED.
        float ap2Out_L;
        {
          // Outer AP (kTA2=1471, g=gTA2 from Diffusion) — MODULATED read:
          // walk_AP2_L updated at loop top (decimated Brownian walk).

          float apReadPos2L = (float)(mWrTA2_L - kTA2) + walk_AP2_L + (float)kTA2_size;
          int    apOff2L     = (int)apReadPos2L;   // biased >=0: truncation == floor
          float apFrac2L    = apReadPos2L - (float)apOff2L;
          int    apI0_2L     = (apOff2L & (kTA2_size - 1));
          int    apI1_2L     = ((apI0_2L + 1) & (kTA2_size - 1));
          float vO2d = (1.0f - apFrac2L) * (float)mTA2_L[apI0_2L]
                      +        apFrac2L  * (float)mTA2_L[apI1_2L];

          float vO2n = dampedD1_L + gTA2 * vO2d;
          float in2  = -gTA2 * vO2n + vO2d;
          mTA2_L[mWrTA2_L] = vO2n;
          mWrTA2_L++;
          if (mWrTA2_L >= kTA2_size) mWrTA2_L = 0;
          // Inner AP (kTA2i=491, g=gTA2_in=0.50) — UNMODULATED, exact read:
          float vI2d = mTA2i_L[mWrTA2i_L];
          float vI2n = in2 + gTA2_in * vI2d;
          ap2Out_L    = -gTA2_in * vI2n + vI2d;
          mTA2i_L[mWrTA2i_L] = vI2n;
          mWrTA2i_L++;
          if (mWrTA2i_L >= kTA2i) mWrTA2i_L = 0;
        }

        // D2_L: Brownian-modulated read with linear interpolation.
        // Uses Size-scaled base length (scaledD2_L).
        float d2Read_L;
        {
          mD2_L[mWrD2_L] = ap2Out_L;

          // walk_D2_L updated at loop top (decimated Brownian walk).

          smD2_L += kBaseSlew * (targetD2_L - smD2_L);
          float readPos = ((float)mWrD2_L - smD2_L) + walk_D2_L + (float)kD2_L_size;
          int    offset  = (int)readPos;   // biased >=0: truncation == floor, no libm
          float frac    = readPos - (float)offset;

          int i0 = (offset & (kD2_L_size - 1));
          int i1 = ((i0 + 1) & (kD2_L_size - 1));

          d2Read_L = (1.0f - frac) * (float)mD2_L[i0] + frac * (float)mD2_L[i1];

          mWrD2_L++;
          if (mWrD2_L >= kD2_L_size) mWrD2_L = 0;
        }

        // D2_L intermediate taps — STATIC (unmodulated).
        float d2tap_a_L, d2tap_b_L;
        {
          int ia = ((mWrD2_L - offD2a_L) & (kD2_L_size - 1));
          int ib = ((mWrD2_L - offD2b_L) & (kD2_L_size - 1));
          d2tap_a_L = mD2_L[ia];
          d2tap_b_L = mD2_L[ib];
        }

        // -- R LOOP --

        // Accumulate: diffusion output + previous-sample cross-feed from L.
        float tankIn_R = tankInDec + mFeedback_R;

        // DC blocker on tank input (R loop).
        {
          float dcOut = tankIn_R - dcx1_R + kDCBlockR * dcy1_R;
          dcx1_R = tankIn_R;
          dcy1_R = dcOut;
          tankIn_R = dcOut;
        }

        // AP1_R: series cascade — outer (kTA1=1087, g=gTA1) → inner (kTA1i=367, g=gTA1_in).
        // Same coefficients as L; separate buffers (mTA1_R, mTA1i_R) for independent state.
        // 0.1.0.11: outer AP1_R uses independent Brownian walk (seed_AP1_R / walk_AP1_R).
        // Inner AP1i_R (367) remains UNMODULATED.
        float ap1Out_R;
        {
          // Outer AP (kTA1=1087, g=gTA1 from Diffusion) — MODULATED read:
          // walk_AP1_R updated at loop top (decimated Brownian walk).

          float apReadPos1R = (float)(mWrTA1_R - kTA1) + walk_AP1_R + (float)kTA1_size;
          int    apOff1R     = (int)apReadPos1R;   // biased >=0: truncation == floor
          float apFrac1R    = apReadPos1R - (float)apOff1R;
          int    apI0_1R     = (apOff1R & (kTA1_size - 1));
          int    apI1_1R     = ((apI0_1R + 1) & (kTA1_size - 1));
          float vO1d = (1.0f - apFrac1R) * (float)mTA1_R[apI0_1R]
                      +        apFrac1R  * (float)mTA1_R[apI1_1R];

          float vO1n = tankIn_R + gTA1 * vO1d;
          float in1  = -gTA1 * vO1n + vO1d;
          mTA1_R[mWrTA1_R] = vO1n;
          mWrTA1_R++;
          if (mWrTA1_R >= kTA1_size) mWrTA1_R = 0;
          // Inner AP (kTA1i=367, g=gTA1_in=0.50) — UNMODULATED, exact read:
          float vI1d = mTA1i_R[mWrTA1i_R];
          float vI1n = in1 + gTA1_in * vI1d;
          ap1Out_R    = -gTA1_in * vI1n + vI1d;
          mTA1i_R[mWrTA1i_R] = vI1n;
          mWrTA1i_R++;
          if (mWrTA1i_R >= kTA1i) mWrTA1i_R = 0;
        }

        // D1_R: Brownian-modulated read, ASYMMETRIC base (scaledD1_R), R-specific seed.
        float d1Read_R;
        {
          mD1_R[mWrD1_R] = ap1Out_R;

          // walk_D1_R updated at loop top (decimated Brownian walk).

          smD1_R += kBaseSlew * (targetD1_R - smD1_R);
          float readPos = ((float)mWrD1_R - smD1_R) + walk_D1_R + (float)kD1_R_size;
          int    offset  = (int)readPos;   // biased >=0: truncation == floor, no libm
          float frac    = readPos - (float)offset;

          int i0 = (offset & (kD1_R_size - 1));
          int i1 = ((i0 + 1) & (kD1_R_size - 1));

          d1Read_R = (1.0f - frac) * (float)mD1_R[i0] + frac * (float)mD1_R[i1];

          mWrD1_R++;
          if (mWrD1_R >= kD1_R_size) mWrD1_R = 0;
        }

        // D1_R intermediate taps — STATIC (unmodulated).
        float d1tap_a_R, d1tap_b_R, d1tap_c_R;
        {
          int ia = ((mWrD1_R - offD1a_R) & (kD1_R_size - 1));
          int ib = ((mWrD1_R - offD1b_R) & (kD1_R_size - 1));
          int ic = ((mWrD1_R - offD1c_R) & (kD1_R_size - 1));
          d1tap_a_R = mD1_R[ia];
          d1tap_b_R = mD1_R[ib];
          d1tap_c_R = mD1_R[ic];
        }

        // HF damp: one-pole LP on D1_R output. Uses dampCoeffEff (macro-biased).
        dampR += dampCoeffFreeze * (d1Read_R - dampR);
        float dampedD1_R = dampR;

        // AP2_R: series cascade — outer (kTA2=1471, g=gTA2) → inner (kTA2i=491, g=gTA2_in).
        // 0.1.0.11: outer AP2_R uses independent Brownian walk (seed_AP2_R / walk_AP2_R).
        // Inner AP2i_R (491) remains UNMODULATED.
        float ap2Out_R;
        {
          // Outer AP (kTA2=1471, g=gTA2 from Diffusion) — MODULATED read:
          // walk_AP2_R updated at loop top (decimated Brownian walk).

          float apReadPos2R = (float)(mWrTA2_R - kTA2) + walk_AP2_R + (float)kTA2_size;
          int    apOff2R     = (int)apReadPos2R;   // biased >=0: truncation == floor
          float apFrac2R    = apReadPos2R - (float)apOff2R;
          int    apI0_2R     = (apOff2R & (kTA2_size - 1));
          int    apI1_2R     = ((apI0_2R + 1) & (kTA2_size - 1));
          float vO2d = (1.0f - apFrac2R) * (float)mTA2_R[apI0_2R]
                      +        apFrac2R  * (float)mTA2_R[apI1_2R];

          float vO2n = dampedD1_R + gTA2 * vO2d;
          float in2  = -gTA2 * vO2n + vO2d;
          mTA2_R[mWrTA2_R] = vO2n;
          mWrTA2_R++;
          if (mWrTA2_R >= kTA2_size) mWrTA2_R = 0;
          // Inner AP (kTA2i=491, g=gTA2_in=0.50) — UNMODULATED, exact read:
          float vI2d = mTA2i_R[mWrTA2i_R];
          float vI2n = in2 + gTA2_in * vI2d;
          ap2Out_R    = -gTA2_in * vI2n + vI2d;
          mTA2i_R[mWrTA2i_R] = vI2n;
          mWrTA2i_R++;
          if (mWrTA2i_R >= kTA2i) mWrTA2i_R = 0;
        }

        // D2_R: Brownian-modulated read, ASYMMETRIC base (scaledD2_R), R-specific seed.
        float d2Read_R;
        {
          mD2_R[mWrD2_R] = ap2Out_R;

          // walk_D2_R updated at loop top (decimated Brownian walk).

          smD2_R += kBaseSlew * (targetD2_R - smD2_R);
          float readPos = ((float)mWrD2_R - smD2_R) + walk_D2_R + (float)kD2_R_size;
          int    offset  = (int)readPos;   // biased >=0: truncation == floor, no libm
          float frac    = readPos - (float)offset;

          int i0 = (offset & (kD2_R_size - 1));
          int i1 = ((i0 + 1) & (kD2_R_size - 1));

          d2Read_R = (1.0f - frac) * (float)mD2_R[i0] + frac * (float)mD2_R[i1];

          mWrD2_R++;
          if (mWrD2_R >= kD2_R_size) mWrD2_R = 0;
        }

        // D2_R intermediate taps — STATIC (unmodulated).
        float d2tap_a_R, d2tap_b_R;
        {
          int ia = ((mWrD2_R - offD2a_R) & (kD2_R_size - 1));
          int ib = ((mWrD2_R - offD2b_R) & (kD2_R_size - 1));
          d2tap_a_R = mD2_R[ia];
          d2tap_b_R = mD2_R[ib];
        }

        // -- CROSS-FEED UPDATE (for next sample) --
        // R's D2 output × g_d feeds L's next-sample accumulator, and
        // vice versa. Spiral governor bounds each independently.
        // Both d2Read_L and d2Read_R are fully computed above before
        // either feedback value is updated — no same-sample causality leak.
        // Cross-feed uses g_d_eff (macro-biased decay) — shorter tail as Early rises.
        // gdFreezeA/B ramp to unity as Freeze rises (staggered L/R); the Character
        // shaper (clean/saturated/folded) is the in-loop governor and bounds the
        // loop, so unity feedback sustains without runaway. Unity through-gain at
        // low level keeps decay/stability independent of Character.
        mFeedback_L = characterShapeF(d2Read_R * gdFreezeA, charDensity, charFoldAmt, charFoldDrive);
        mFeedback_R = characterShapeF(d2Read_L * gdFreezeB, charDensity, charFoldAmt, charFoldDrive);

        // ----------------------------------------------------------------
        // 5. Stereo wet taps — Dattorro-style multi-tap signed sum (0.1.0.7).
        //
        // Signed weighted sum of: ap1Out (early cascade tap), three
        // intermediate D1 points, the full D1 end read, two intermediate
        // D2 points, and the full D2 end read. Signs and weights from the
        // rig-validated scheme. kWetLevel=2.2 restores perceived wet level
        // (~7 dB boost vs old 0.5*(d1+d2), matching the signed-sum loss).
        //
        // Loop stability unaffected — output-tap change only. Governors,
        // DC blocker, and cross-feed are all unchanged.
        // ----------------------------------------------------------------
        // Tank wet multi-tap sum (24k). Shift prev<-curr, store this tick's output.
        mTankWetL_prev = mTankWetL_curr;
        mTankWetL_curr = kWetLevel * (
            + kWap1 * ap1Out_L
            + kWd1a * d1tap_a_L - kWd1b * d1tap_b_L + kWd1c * d1tap_c_L
            + kWd1e * d1Read_L
            - kWd2a * d2tap_a_L + kWd2b * d2tap_b_L
            + kWd2e * d2Read_L
        );
        mTankWetR_prev = mTankWetR_curr;
        mTankWetR_curr = kWetLevel * (
            + kWap1 * ap1Out_R
            + kWd1a * d1tap_a_R - kWd1b * d1tap_b_R + kWd1c * d1tap_c_R
            + kWd1e * d1Read_R
            - kWd2a * d2tap_a_R + kWd2b * d2tap_b_R
            + kWd2e * d2Read_R
        );
        }  // end if (tankTick) — tank core ran at 24k

        // --- Reconstruct 48k wet: 2:1 linear interp of the last two tank outputs ---
        // interpF = 0.5 on the tick sample (midpoint prev<->curr), 1.0 on the
        // between sample (the fresh curr). One-host-sample group delay. Upgrade
        // path if imaging is heard: a half-band upsampler.
        float interpF = tankTick ? 0.5f : 1.0f;
        float wetL = mTankWetL_prev + (mTankWetL_curr - mTankWetL_prev) * interpF;
        float wetR = mTankWetR_prev + (mTankWetR_curr - mTankWetR_prev) * interpF;
        mTankPhase ^= 1;

        // Add ER contribution (parallel, AFTER tank multi-tap).
        // Scales to zero when earlyParam=0 → exact 0.1.0.8 output.
        // ER is purely feedforward (FIR): no feedback, no stability concern.
        float erScale = (kERLevel * smEarly);
        wetL += erScale * erSumL;
        wetR += erScale * erSumR;

        // Static 200 Hz wet highpass (12 dB/oct = two cascaded one-poles/ch).
        // Applied to the full wet (tank + ER) before the mix, at host rate.
        mWetHpLp1_L += kWetHpA * (wetL - mWetHpLp1_L);
        float hp1L = wetL - mWetHpLp1_L;
        mWetHpLp2_L += kWetHpA * (hp1L - mWetHpLp2_L);
        wetL = hp1L - mWetHpLp2_L;
        mWetHpLp1_R += kWetHpA * (wetR - mWetHpLp1_R);
        float hp1R = wetR - mWetHpLp1_R;
        mWetHpLp2_R += kWetHpA * (hp1R - mWetHpLp2_R);
        wetR = hp1R - mWetHpLp2_R;

        // --- Reverse: write wet to the ring, sum two backward Hann grains,
        //     crossfade wet -> reversed by revAmt. Grains are offset by half a
        //     grain (50% overlap) so the Hann windows sum flat / click-free. The
        //     buffer + phase always advance so history/grains stay live at rev=0.
        mRevBufL[mRevWr] = wetL;
        mRevBufR[mRevWr] = wetR;
        float revL = 0.0f, revR = 0.0f;
        {
          int   ri  = (mRevAnchorA - mRevPhaseA) & (kRevBufSize - 1);
          float win = mHannLut[(int)(mRevPhaseA * kRevHannRatio)];
          revL += win * mRevBufL[ri];  revR += win * mRevBufR[ri];
          if (++mRevPhaseA >= kRevGrain) { mRevPhaseA = 0; mRevAnchorA = mRevWr; }
        }
        {
          int   ri  = (mRevAnchorB - mRevPhaseB) & (kRevBufSize - 1);
          float win = mHannLut[(int)(mRevPhaseB * kRevHannRatio)];
          revL += win * mRevBufL[ri];  revR += win * mRevBufR[ri];
          if (++mRevPhaseB >= kRevGrain) { mRevPhaseB = 0; mRevAnchorB = mRevWr; }
        }
        mRevWr = (mRevWr + 1) & (kRevBufSize - 1);
        wetL += revAmt * (revL - wetL);
        wetR += revAmt * (revR - wetR);

        // ----------------------------------------------------------------
        // 6. Dry/wet mix — true stereo.
        //    Each channel's dry is preserved; each channel's wet is drawn
        //    from its own loop's delay taps. Cross-coupling has already
        //    mixed information between the loops via the figure-8 feedback,
        //    so wetL and wetR are decorrelated even from a mono source.
        // ----------------------------------------------------------------
        float dryMix  = (1.0f - mix);
        float wetMix  = mix;
        float outL = drySampleL * dryMix + wetL * wetMix;
        float outR = drySampleR * dryMix + wetR * wetMix;

        *out1 = outL;
        *out2 = outR;
        in1++; in2++; out1++; out2++;
      }

      // Propagate the per-sample-smoothed bases / predelay / ER level (zipper fix).
      mSmD1_L = smD1_L; mSmD2_L = smD2_L; mSmD1_R = smD1_R; mSmD2_R = smD2_R;
      mSmPD = smPD; mSmEarly = smEarly;

      // Propagate decimated-walk phase + per-line ramp increments.
      mWalkPhase = walkPhase;
      mWalkInc_D1_L = walkInc_D1_L; mWalkInc_D2_L = walkInc_D2_L;
      mWalkInc_D1_R = walkInc_D1_R; mWalkInc_D2_R = walkInc_D2_R;
      mWalkInc_AP1_L = walkInc_AP1_L; mWalkInc_AP2_L = walkInc_AP2_L;
      mWalkInc_AP1_R = walkInc_AP1_R; mWalkInc_AP2_R = walkInc_AP2_R;

      // Propagate local walk + seed state back to members.
      mWalk_D1_L = walk_D1_L;
      mWalk_D2_L = walk_D2_L;
      mWalk_D1_R = walk_D1_R;
      mWalk_D2_R = walk_D2_R;

      mSeed_D1_L = seed_D1_L;
      mSeed_D2_L = seed_D2_L;
      mSeed_D1_R = seed_D1_R;
      mSeed_D2_R = seed_D2_R;

      // Propagate outer AP walk + seed state (0.1.0.11).
      mWalk_AP1_L = walk_AP1_L;
      mWalk_AP2_L = walk_AP2_L;
      mWalk_AP1_R = walk_AP1_R;
      mWalk_AP2_R = walk_AP2_R;

      mSeed_AP1_L = seed_AP1_L;
      mSeed_AP2_L = seed_AP2_L;
      mSeed_AP1_R = seed_AP1_R;
      mSeed_AP2_R = seed_AP2_R;

      // Propagate HF damp state.
      mDampL = dampL;
      mDampR = dampR;

      // Propagate DC blocker state.
      mDCx1_L = dcx1_L;  mDCy1_L = dcy1_L;
      mDCx1_R = dcx1_R;  mDCy1_R = dcy1_R;
    }

  private:
    // Predelay buffer (power-of-two for & wrap)
    float mPD[kPD];
    int   mWrPD;

    // Early-reflection ring buffer (power-of-two for & wrap).
    // Fed from the pre-diffusion predelayed mono signal each sample.
    // kER=4096 → 85.3 ms at 48 kHz; all 9 ER taps read within this range.
    float mER[kER];
    int   mWrER;

    // ER diffuser allpass buffers (0.1.0.12) — 2 stages per channel, static, g=0.6.
    // Sized exactly to their delays; no headroom (UNMODULATED).
    // L: 211 + 317 smp; R: 241 + 359 smp (prime, decorrelated).
    float mERD_L1[kERD_L1];   // L ER diffuser stage 1 (211 smp)
    float mERD_L2[kERD_L2];   // L ER diffuser stage 2 (317 smp)
    float mERD_R1[kERD_R1];   // R ER diffuser stage 1 (241 smp)
    float mERD_R2[kERD_R2];   // R ER diffuser stage 2 (359 smp)
    int   mWrERD_L1, mWrERD_L2, mWrERD_R1, mWrERD_R2;

    // Input diffusion allpass buffers (4 series, shared mono path)
    float mID1[kID1];
    float mID2[kID2];
    float mID3[kID3];
    float mID4[kID4];
    int   mWrID1, mWrID2, mWrID3, mWrID4;

    // Tank allpass buffers — L loop (series-cascade Schroeder APF).
    // Outer buffers (0.1.0.11): sized kTA1_size=1119, kTA2_size=1503 (base + 2*kAPHeadroom=32).
    //   Write head wraps at kTA1_size / kTA2_size. Provides headroom for ±6-smp Brownian walk.
    // Inner buffers (kTA1i=367, kTA2i=491): UNMODULATED, exact size, no headroom.
    // All four zeroed in constructor; all four write heads initialized to 0.
    float mTA1_L[kTA1_size];
    float mTA1i_L[kTA1i];   // AP1 inner cascade — UNMODULATED
    float mTA2_L[kTA2_size];
    float mTA2i_L[kTA2i];   // AP2 inner cascade — UNMODULATED
    int   mWrTA1_L, mWrTA1i_L, mWrTA2_L, mWrTA2i_L;

    // Tank delay lines — L loop (sized for max sizeFactor=1.5 + headroom).
    float mD1_L[kD1_L_size];   // 11038 samples
    float mD2_L[kD2_L_size];   // 7908 samples
    int   mWrD1_L, mWrD2_L;

    // Tank allpass buffers — R loop (same lengths as L, separate state).
    float mTA1_R[kTA1_size];
    float mTA1i_R[kTA1i];   // AP1 inner cascade — UNMODULATED
    float mTA2_R[kTA2_size];
    float mTA2i_R[kTA2i];   // AP2 inner cascade — UNMODULATED
    int   mWrTA1_R, mWrTA1i_R, mWrTA2_R, mWrTA2i_R;

    // Tank delay lines — R loop (sized for max sizeFactor=1.5 + headroom).
    float mD1_R[kD1_R_size];   // 10462 samples
    float mD2_R[kD2_R_size];   // 9772 samples
    int   mWrD1_R, mWrD2_R;

    // Recirculating feedback accumulators (double precision: these values
    // traverse the full round-trip path each sample; precision matters
    // for long-decay tails where accumulated rounding would drift pitch).
    // Cross-coupled: mFeedback_L is written from d2Read_R×g_d (R feeds L),
    //                mFeedback_R is written from d2Read_L×g_d (L feeds R).
    float mFeedback_L;
    float mFeedback_R;

    // HF damp filter state — one per loop (one-pole LP on D1 output).
    // Initialized to 0 in constructor; converges quickly on first use.
    double mDampL;
    double mDampR;

    // DC blocker state — one pair per loop.
    // mDCx1_X: previous input sample; mDCy1_X: previous output sample.
    double mDCx1_L;  double mDCy1_L;
    double mDCx1_R;  double mDCy1_R;

    // Size-scaled delay lengths — updated every block from sizeFactorEff (smoothed).
    // Initialized consistent with defaults Size=0.35, Early=0.4 in constructor.
    int    mScaledD1_L;
    int    mScaledD2_L;
    int    mScaledD1_R;
    int    mScaledD2_R;
    // Per-sample-smoothed FLOAT delay bases (zipper fix): the main feedback
    // end-reads glide off these instead of the int mScaled* (which still feed the
    // secondary intermediate taps). Plus smoothed predelay tap + ER level.
    float  mSmD1_L, mSmD2_L, mSmD1_R, mSmD2_R;
    float  mSmPD;
    float  mSmEarly;
    // Decimated Brownian-walk state: phase counter + per-line ramp increments
    // (the walk RNG updates once per kWalkDecim samples; walk += inc each sample).
    int    mWalkPhase;
    float  mWalkInc_D1_L, mWalkInc_D2_L, mWalkInc_D1_R, mWalkInc_D2_R;
    float  mWalkInc_AP1_L, mWalkInc_AP2_L, mWalkInc_AP1_R, mWalkInc_AP2_R;
    // SR/2 downsampled tank: phase toggle (tank ticks every 2nd host sample),
    // 2-tap half-band decimation history of diffIn, and the last two tank wet
    // outputs (24k) for 2:1 linear reconstruction back to the 48k host rate.
    int    mTankPhase;
    float  mDecimPrev;            // previous host-sample diffIn (decimator tap)
    float  mTankWetL_prev, mTankWetL_curr;
    float  mTankWetR_prev, mTankWetR_curr;
    // Wet-output 200 Hz highpass: LP states of the two cascaded one-poles/ch.
    float  mWetHpLp1_L, mWetHpLp2_L, mWetHpLp1_R, mWetHpLp2_R;
    float  mFreezeSmoothed;       // block-rate smoothed Freeze amount (0..1)
    // Reverse granular buffer (per channel) + shared grain state + Hann LUT.
    float  mRevBufL[kRevBufSize];
    float  mRevBufR[kRevBufSize];
    float  mHannLut[kHannSize];
    int    mRevWr, mRevPhaseA, mRevPhaseB, mRevAnchorA, mRevAnchorB;
    float  mReverseSmoothed;      // block-rate smoothed Reverse amount (0..1)
    float  mCharacterSmoothed;    // block-rate smoothed Character amount (0..1)
    float  mLastSize;           // last seen Size (reference only; no longer used as change guard)
    float  mLastEarly;          // last seen Early (reference only)
    double mSizeFactor;         // raw sizeFactor from Size param only (for reference)
    double mSizeFactorSmoothed; // effective sizeFactor after macro + one-pole smoothing

    // Brownian LFO state — four independent xorshift64 PRNGs + walk accumulators.
    // Seeds initialized to distinct non-zero constants in constructor.
    // L-line seeds are in a different numeric neighborhood from R-line seeds
    // to guarantee decorrelation from sample 0 (fabula-design.md §5).
    uint64_t mSeed_D1_L;
    uint64_t mSeed_D2_L;
    uint64_t mSeed_D1_R;
    uint64_t mSeed_D2_R;

    // Walk accumulators (double, fractional) — each stays in [-excursion, +excursion].
    // Initialized to 0.0 (center of headroom window) in constructor.
    double mWalk_D1_L;
    double mWalk_D2_L;
    double mWalk_D1_R;
    double mWalk_D2_R;

    // Outer AP Brownian walk state (0.1.0.11) — 4 seeds + 4 accumulators.
    // Seeds are DISTINCT from each other and from all D1/D2 seeds above
    // (different numeric neighborhoods: wyhash/Murmur constants).
    // Accumulators stay in [-apExcursion, +apExcursion] = [-6, +6] samples max.
    uint64_t mSeed_AP1_L;
    uint64_t mSeed_AP2_L;
    uint64_t mSeed_AP1_R;
    uint64_t mSeed_AP2_R;

    double mWalk_AP1_L;
    double mWalk_AP2_L;
    double mWalk_AP1_R;
    double mWalk_AP2_R;

#endif
  };

} // namespace zaum
