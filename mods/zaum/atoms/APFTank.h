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
// BUILD SUB-PHASE 0.1.0.9 — Tier 3 early-reflection (ER) network + Early control.
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
//      Endpoints from fabula-design.md §7:
//        Decay=0 → g_d_min = kGdMin = 0.60  (short tail, ~4-5 s RT60)
//        Decay=1 → g_d_max = kGdMax = 0.97  (long tail, ~25-30 s RT60)
//
//      Mapping (power curve, upward-biased to place default at approved feel):
//        g_d = kGdMin + (kGdMax - kGdMin) * pow(decay, kDecayShape)
//        kDecayShape = 0.565
//
//      Calibration:
//        Decay=0.0 → g_d = 0.60
//        Decay=0.5 → g_d = 0.60 + 0.37 * 0.5^0.565 = 0.60 + 0.37*0.676 = 0.850
//        Decay=1.0 → g_d = 0.97
//        → Decay=0.5 maps to g_d≈0.850, preserving the 0.1.0.4 approved sound.
//
//      RT60 formula (fabula-design.md §2):
//        RT60 = -3 * RTT / log10(g_d)
//        RTT (L loop) = AP1(1087) + D1(7187) + AP2(1471) + D2(5101) =
//          15846 smp = 0.330 s at 48 kHz (Size=0.5)
//        Decay=0: RT60 = -3*0.330/log10(0.60) ≈ 4.5 s
//        Decay=1: RT60 = -3*0.330/log10(0.97) ≈ 31 s
//        Note: "Decay=0→~2s" in §11 assumed a shorter RTT (0.323 s) and
//        lower g_d_min; the ~4.5 s minimum is the actual result with the
//        longer tank. Recalibrate endpoint in 0.1.0.7 if a shorter minimum
//        tail is desired.
//
//      HARD SAFETY CAP: g_d = min(g_d, kGdCap = 0.985) regardless of Decay.
//      The Spiral governors are the second backstop but g_d<1 must hold
//      unconditionally for passive stability.
//
//      PRIMARY TUNING KNOBS: kGdMin (lift to shorten the short tail),
//        kGdMax (lower to reduce maximum decay), kDecayShape (shift midpoint).
//        kDecayShape derivation: 0.5^k = (0.85-0.60)/(0.97-0.60) = 0.676
//        → k = log(0.676)/log(0.5) = 0.565.
//
//   C. SIZE — delay-length scaling (block rate, careful bounds).
//
//      sizeFactor = kSizeMin + Size * (kSizeMax - kSizeMin)
//        kSizeMin = 0.5  → Size=0.0 (smallest room)
//        kSizeMax = 1.5  → Size=1.0 (largest room)
//        Size=0.5 → sizeFactor=1.0 → EXACTLY the 0.1.0.4 base lengths:
//          D1_L=7187, D2_L=5101, D1_R=6803, D2_R=6343.
//          The 0.1.0.4 approved sound is preserved at the default.
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

  // Tank allpass buffers (series-cascade Schroeder APF, BOTH L and R loops).
  // Each tank AP is a SERIES CASCADE: outer (kTA1=1087, g=gTA1_out) feeds
  // inner (kTA1i=367, g=gTA1_in=0.50). Separate buffer per stage per loop.
  // AP1 outer: N=1087, g from Diffusion (~0.70 at default). Inner: N=367, g=0.50.
  // AP2 outer: N=1471, g from Diffusion (~0.50 at default). Inner: N=491, g=0.50.
  // L and R loops use SEPARATE buffers and write indices for all 8 buffers.
  static const int kTA1  = 1087;   // AP1 delay (= buffer size), both loops
  static const int kTA1i = 367;    // AP1 inner delay — series cascade (0.1.0.7)
  static const int kTA2  = 1471;   // AP2 delay (= buffer size), both loops
  static const int kTA2i = 491;    // AP2 inner delay — series cascade (0.1.0.7)

  // ---------------------------------------------------------------------------
  // Tank delay line base lengths (Size=0.5 default = current approved sound)
  // ---------------------------------------------------------------------------
  // These four values are the "canonical" lengths from fabula-design.md §2.
  // With Size=0.5 → sizeFactor=1.0, the scaled lengths reproduce these exactly.
  static const int kD1_L_base = 7187;
  static const int kD2_L_base = 5101;
  static const int kD1_R_base = 6803;
  static const int kD2_R_base = 6343;

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
  //   Walk clamped to [-kMaxExcursion, +kMaxExcursion] = [-72, +72].
  //   Linear interp reads i0 and i1=i0+1.
  //   Worst additional reach from center = 72 + 1 = 73 samples.
  //   Headroom = 128 > 73 — safe with 55 samples margin on each side.
  //   All index arithmetic uses modular wrap (% bufSize), so no OOB access.
  // ---------------------------------------------------------------------------
  static const int kD1_headroom    = 128;   // modulation headroom each side

  static const int kD1_L_maxBase  = 10781;  // 7187*1.5 rounded to odd
  static const int kD1_L_size     = kD1_L_maxBase + 2 * kD1_headroom + 1; // 11038

  static const int kD2_L_maxBase  = 7651;   // 5101*1.5 rounded to odd
  static const int kD2_L_size     = kD2_L_maxBase + 2 * kD1_headroom + 1; // 7908

  static const int kD1_R_maxBase  = 10205;  // 6803*1.5 rounded to odd
  static const int kD1_R_size     = kD1_R_maxBase + 2 * kD1_headroom + 1; // 10462

  static const int kD2_R_maxBase  = 9515;   // 6343*1.5 rounded to odd
  static const int kD2_R_size     = kD2_R_maxBase + 2 * kD1_headroom + 1; // 9772

  // ---------------------------------------------------------------------------
  // Modulation tuning constants — adjust these by ear at the 0.1.0.4 gate.
  // ---------------------------------------------------------------------------

  // Excursion mapping: Mod 0..1 → kMinExcursion..kMaxExcursion samples.
  // At 48 kHz: 9 samples ≈ 0.19 ms, 72 samples ≈ 1.5 ms.
  // Max excursion 72 + 1 interp neighbor = 73 < 128 headroom — safe.
  static const double kMinExcursion = 9.0;
  static const double kMaxExcursion = 72.0;

  // Step size mapping: ModRate 0..1 → kMinStep..kMaxStep per sample.
  // This is the per-sample walk increment applied to the integrated noise.
  // Range chosen so ModRate=0.2 (default) gives slow pleasant drift and
  // ModRate=1.0 gives faster but still somewhat gentle wander.
  static const double kMinStep = 0.0002;
  static const double kMaxStep = 0.1;

  // ---------------------------------------------------------------------------
  // Decay → g_d tuning constants.
  // Power-curve mapping: g_d = kGdMin + (kGdMax - kGdMin) * pow(decay, kDecayShape)
  //   kDecayShape = 0.565: chosen so Decay=0.5 → g_d≈0.850
  //     Derivation: 0.5^k = (0.85-0.60)/(0.97-0.60) = 0.676 → k=0.565
  //   kGdMin = 0.60:  Decay=0 tail (~4-5 s RT60 with this tank's RTT)
  //   kGdMax = 0.97:  Decay=1 tail (~25-30 s RT60)
  //   kGdCap = 0.985: hard unconditional cap — g_d must stay < 1.0
  // To shorten the minimum tail, raise kGdMin toward 0.50 or lower.
  // To shift the Decay=0.5 feel brighter/darker, adjust kDecayShape.
  // ---------------------------------------------------------------------------
  static const double kGdMin      = 0.60;
  static const double kGdMax      = 0.97;
  static const double kGdCap      = 0.985;
  static const double kDecayShape = 0.565;

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
  // sizeFactor = kSizeMin + Size * (kSizeMax - kSizeMin)
  //   kSizeMin = 0.5:  Size=0.0 → smallest room (50% of base lengths)
  //   kSizeMax = 1.5:  Size=1.0 → largest room (150% of base lengths)
  //   Size=0.5 → sizeFactor=1.0 → EXACT 0.1.0.4 base lengths (sound preserved)
  // ---------------------------------------------------------------------------
  static const double kSizeMin = 0.5;
  static const double kSizeMax = 1.5;

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
  static const double kDCBlockR = 0.9995;

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

  static const double kWap1  = 0.167;    // weight: ap1Out (early cascade tap)
  static const double kWd1a  = 0.111;    // weight: D1 tap a (+)
  static const double kWd1b  = 0.111;    // weight: D1 tap b (-)
  static const double kWd1c  = 0.111;    // weight: D1 tap c (+)
  static const double kWd1e  = 0.139;    // weight: D1 end read (+)
  static const double kWd2a  = 0.111;    // weight: D2 tap a (-)
  static const double kWd2b  = 0.111;    // weight: D2 tap b (+)
  static const double kWd2e  = 0.139;    // weight: D2 end read (+)
  static const double kWetLevel = 2.2;   // level match for signed-sum cancellation

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
  static const double kER_gain[9] = {
    +0.68, -0.56, +0.45, -0.36, +0.28,
    -0.22, +0.17, -0.13, +0.10
  };
  //
  // Output level scaler. PRIMARY TUNING KNOB for ER-vs-tail balance.
  // At Early=0.4, mean |erSum| ~0.4 → contribution = 1.5 * 0.4 * 0.4 = 0.24.
  static const double kERLevel = 1.5;

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
      addParameter(mMod);
      addParameter(mModRate);
      addParameter(mPredelay);
      addParameter(mMix);
      addParameter(mEarly);

      memset(mPD,     0, sizeof(mPD));
      memset(mER,     0, sizeof(mER));   // ER ring buffer — all silence
      mWrER = 0;
      memset(mID1,    0, sizeof(mID1));
      memset(mID2,    0, sizeof(mID2));
      memset(mID3,    0, sizeof(mID3));
      memset(mID4,    0, sizeof(mID4));
      // L-loop allpass + delay buffers
      memset(mTA1_L,   0, sizeof(mTA1_L));
      memset(mTA1i_L,  0, sizeof(mTA1i_L));   // AP1 inner cascade buffer
      memset(mTA2_L,   0, sizeof(mTA2_L));
      memset(mTA2i_L,  0, sizeof(mTA2i_L));   // AP2 inner cascade buffer
      memset(mD1_L,    0, sizeof(mD1_L));
      memset(mD2_L,    0, sizeof(mD2_L));
      // R-loop allpass + delay buffers
      memset(mTA1_R,   0, sizeof(mTA1_R));
      memset(mTA1i_R,  0, sizeof(mTA1i_R));   // AP1 inner cascade buffer
      memset(mTA2_R,   0, sizeof(mTA2_R));
      memset(mTA2i_R,  0, sizeof(mTA2i_R));   // AP2 inner cascade buffer
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

      // HF damp filter state (one per loop, initialized to silence).
      mDampL = 0.0;
      mDampR = 0.0;

      // DC blocker state (one pair per loop, initialized to 0).
      mDCx1_L = 0.0;  mDCy1_L = 0.0;
      mDCx1_R = 0.0;  mDCy1_R = 0.0;

      // Size-scaled delay lengths — initialize to Size=0.35 default lengths.
      // sizeFactor = kSizeMin + 0.35*(kSizeMax-kSizeMin) = 0.5 + 0.35*1.0 = 0.85
      //   D1_L: round(7187*0.85)=round(6108.95)=6109 (odd)
      //   D2_L: round(5101*0.85)=round(4335.85)=4335 (odd) — wait: 4335 is odd ✓
      //   D1_R: round(6803*0.85)=round(5782.55)=5783 (odd)
      //   D2_R: round(6343*0.85)=round(5391.55)=5391 (odd)
      mScaledD1_L = 6109;
      mScaledD2_L = 4335;
      mScaledD1_R = 5783;
      mScaledD2_R = 5391;
      mLastSize   = 0.35f;
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
      const float modParam       = mMod.value();        // 0..1
      const float modRateParam   = mModRate.value();    // 0..1
      const float dampParam      = mDamp.value();       // 0..1
      const float decayParam     = mDecay.value();      // 0..1
      const float sizeParam      = mSize.value();       // 0..1
      const float diffusionParam = mDiffusion.value();  // 0..1
      // Early: 0..1, scales ER contribution. At 0 → no ER (exact 0.1.0.8 output).
      const float earlyParam     = mEarly.value();      // 0..1

      // Max tap = kPD - 1 (keep at least 1-sample separation from write head)
      const int predelayTap = (int)(predelayParam * (float)(kPD - 1));

      // Decay → g_d via power-curve: g_d = kGdMin + (kGdMax - kGdMin) * decay^kDecayShape
      // Hard cap at kGdCap prevents g_d >= 1 unconditionally.
      // pow(0, kDecayShape) = 0 safely; pow(1, kDecayShape) = 1 safely.
      double decayD = (double)decayParam;
      // Clamp input to [0,1] before pow to avoid domain errors.
      if (decayD < 0.0) decayD = 0.0;
      if (decayD > 1.0) decayD = 1.0;
      double g_d = kGdMin + (kGdMax - kGdMin) * pow(decayD, kDecayShape);
      if (g_d > kGdCap) g_d = kGdCap;   // unconditional hard safety cap

      // Damp → one-pole LP coefficient.
      // coeff = 1.0 - Damp * (1.0 - kMinDampCoeff)
      // Damp=0 → coeff=1.0 (open). Damp=1 → coeff=kMinDampCoeff (dark).
      double dampD     = (double)dampParam;
      if (dampD < 0.0) dampD = 0.0;
      if (dampD > 1.0) dampD = 1.0;
      const double dampCoeff = 1.0 - dampD * (1.0 - kMinDampCoeff);

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

      const double gID12 = DIFFCLAMP(kDiffBaseID12 + diffDelta);
      const double gID34 = DIFFCLAMP(kDiffBaseID34 + diffDelta);
      const double gTA1  = DIFFCLAMP(kDiffBaseTA1  + diffDelta);
      const double gTA2  = DIFFCLAMP(kDiffBaseTA2  + diffDelta);

      #undef DIFFCLAMP

      // Size → scaled delay lengths (block rate, only recompute when changed).
      // Clamped to [kSizeMin, kSizeMax] so sizeFactor is always in bounds.
      if (sizeParam != mLastSize)
      {
        mLastSize = sizeParam;
        double sizeD = (double)sizeParam;
        if (sizeD < 0.0) sizeD = 0.0;
        if (sizeD > 1.0) sizeD = 1.0;
        const double sizeFactor = kSizeMin + sizeD * (kSizeMax - kSizeMin);

        // Round to nearest odd to minimize comb resonances.
        // Result is clamped to [1, maxBase] implicitly by toOdd().
        mScaledD1_L = toOdd((int)(kD1_L_base * sizeFactor + 0.5));
        mScaledD2_L = toOdd((int)(kD2_L_base * sizeFactor + 0.5));
        mScaledD1_R = toOdd((int)(kD1_R_base * sizeFactor + 0.5));
        mScaledD2_R = toOdd((int)(kD2_R_base * sizeFactor + 0.5));

        // Clamp to strictly less than their respective buffer sizes minus
        // the headroom+interp margin, so read index can never exceed buffer.
        // Effective max usable scaled base = bufSize - 2*headroom - 1.
        // With headroom=128: D1_L usable max = 11038 - 257 = 10781 = kD1_L_maxBase.
        if (mScaledD1_L > kD1_L_maxBase) mScaledD1_L = kD1_L_maxBase;
        if (mScaledD2_L > kD2_L_maxBase) mScaledD2_L = kD2_L_maxBase;
        if (mScaledD1_R > kD1_R_maxBase) mScaledD1_R = kD1_R_maxBase;
        if (mScaledD2_R > kD2_R_maxBase) mScaledD2_R = kD2_R_maxBase;
        // Min: 1 (toOdd guarantees >= 1, but guard explicitly).
        if (mScaledD1_L < 1) mScaledD1_L = 1;
        if (mScaledD2_L < 1) mScaledD2_L = 1;
        if (mScaledD1_R < 1) mScaledD1_R = 1;
        if (mScaledD2_R < 1) mScaledD2_R = 1;
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
      const double gTA1_in = (kGTA1_in > 0.95) ? 0.95 : kGTA1_in;
      const double gTA2_in = (kGTA2_in > 0.95) ? 0.95 : kGTA2_in;

      // Modulation parameters — computed once per block.
      // excursion: how far the walk can stray from center, in samples.
      // step_size: per-sample walk increment multiplied by noise ∈ [-0.5, 0.5].
      // TUNING: to shift the sweet spot, adjust kMinExcursion/kMaxExcursion
      //   and kMinStep/kMaxStep at the top of this file.
      const double excursion = kMinExcursion + (double)modParam * (kMaxExcursion - kMinExcursion);
      const double step_size = kMinStep + (double)modRateParam * (kMaxStep - kMinStep);

      // Copy walk accumulators and seeds into local variables for the inner
      // loop. Propagate back to members at end of block.
      double walk_D1_L = mWalk_D1_L;
      double walk_D2_L = mWalk_D2_L;
      double walk_D1_R = mWalk_D1_R;
      double walk_D2_R = mWalk_D2_R;

      uint64_t seed_D1_L = mSeed_D1_L;
      uint64_t seed_D2_L = mSeed_D2_L;
      uint64_t seed_D1_R = mSeed_D1_R;
      uint64_t seed_D2_R = mSeed_D2_R;

      // Copy HF damp filter state local for the sample loop.
      double dampL = mDampL;
      double dampR = mDampR;

      // Copy DC blocker state local for the sample loop.
      double dcx1_L = mDCx1_L;  double dcy1_L = mDCy1_L;
      double dcx1_R = mDCx1_R;  double dcy1_R = mDCy1_R;

      int sampleFrames = FRAMELENGTH;
      while (--sampleFrames >= 0)
      {
        // ----------------------------------------------------------------
        // 1. Mono sum of L + R inputs.
        // ----------------------------------------------------------------
        double drySampleL = (double)*in1;
        double drySampleR = (double)*in2;
        double monoIn = (drySampleL + drySampleR) * 0.5;

        // ----------------------------------------------------------------
        // 2. Predelay ring buffer.
        //    Write, then read at tap distance behind write head.
        //    Buffer size kPD is power-of-two: wrap with & mask.
        // ----------------------------------------------------------------
        mPD[mWrPD] = (float)monoIn;
        int rdPD = mWrPD - predelayTap;
        if (rdPD < 0) rdPD += kPD;
        double diffIn = (double)mPD[rdPD];
        mWrPD = (mWrPD + 1) & (kPD - 1);

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
        mER[mWrER] = (float)diffIn;
        double erSumL = 0.0;
        double erSumR = 0.0;
        if (earlyParam > 0.0f)
        {
          for (int t = 0; t < kER_tapCount; t++)
          {
            int idxL = (mWrER - kER_delayL[t]) & (kER - 1);
            int idxR = (mWrER - kER_delayR[t]) & (kER - 1);
            erSumL += kER_gain[t] * (double)mER[idxL];
            erSumR += kER_gain[t] * (double)mER[idxR];
          }
        }
        mWrER = (mWrER + 1) & (kER - 1);

        // ----------------------------------------------------------------
        // 3. Input diffusion: 4 series allpasses (fixed coefficients).
        //    Each AP: standard (non-nested) allpassNestedStep pattern.
        //      vNew = x + g * v[n-N]
        //      y    = -g * vNew + v[n-N]
        //    Write vNew to buffer; yOut feeds next stage.
        // ----------------------------------------------------------------

        // ID1 (delay 229, g=0.75)
        {
          double vD = (double)mID1[mWrID1];   // read BEFORE write (v[n-N])
          double vNew, yOut;
          house::allpassNestedStep(diffIn, vD, gID12, vNew, yOut);
          mID1[mWrID1] = (float)vNew;
          mWrID1++;
          if (mWrID1 >= kID1) mWrID1 = 0;
          diffIn = yOut;
        }

        // ID2 (delay 173, g=0.75)
        {
          double vD = (double)mID2[mWrID2];
          double vNew, yOut;
          house::allpassNestedStep(diffIn, vD, gID12, vNew, yOut);
          mID2[mWrID2] = (float)vNew;
          mWrID2++;
          if (mWrID2 >= kID2) mWrID2 = 0;
          diffIn = yOut;
        }

        // ID3 (delay 613, g=0.625)
        {
          double vD = (double)mID3[mWrID3];
          double vNew, yOut;
          house::allpassNestedStep(diffIn, vD, gID34, vNew, yOut);
          mID3[mWrID3] = (float)vNew;
          mWrID3++;
          if (mWrID3 >= kID3) mWrID3 = 0;
          diffIn = yOut;
        }

        // ID4 (delay 449, g=0.625)
        {
          double vD = (double)mID4[mWrID4];
          double vNew, yOut;
          house::allpassNestedStep(diffIn, vD, gID34, vNew, yOut);
          mID4[mWrID4] = (float)vNew;
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
        //        readPos = wrHead - scaledBase + walk   (double)
        //        offset  = (int)floor(readPos)
        //        frac    = readPos - (double)offset
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
        //   mFeedback_L = spiralFastSaturate(d2Read_R * g_d, 1.0)
        //   mFeedback_R = spiralFastSaturate(d2Read_L * g_d, 1.0)
        //
        // ORDERING: Both loops fully computed for THIS sample before
        // either feedback value is updated — no same-sample causality leak.
        // ----------------------------------------------------------------

        // -- L LOOP --

        // Accumulate: diffusion output + previous-sample cross-feed from R.
        double tankIn_L = diffIn + mFeedback_L;

        // DC blocker on tank input (L loop).
        // y[n] = x[n] - x[n-1] + R*y[n-1]
        {
          double dcOut = tankIn_L - dcx1_L + kDCBlockR * dcy1_L;
          dcx1_L = tankIn_L;
          dcy1_L = dcOut;
          tankIn_L = dcOut;
        }

        // AP1_L: series cascade — outer (kTA1=1087, g=gTA1) → inner (kTA1i=367, g=gTA1_in).
        // SERIES form (NOT in-feedback nesting): outer AP's feedforward output feeds
        // a second independent AP. Unity-gain by construction: |H_outer|·|H_inner|=1.
        double ap1Out_L;
        {
          // Outer AP (kTA1=1087, g=gTA1 from Diffusion):
          double vO1d = (double)mTA1_L[mWrTA1_L];          // outer read v[n-1087]
          double vO1n = tankIn_L + gTA1 * vO1d;
          double in1  = -gTA1 * vO1n + vO1d;               // outer AP output → inner input
          mTA1_L[mWrTA1_L] = (float)vO1n;
          mWrTA1_L++;
          if (mWrTA1_L >= kTA1) mWrTA1_L = 0;
          // Inner AP (kTA1i=367, g=gTA1_in=0.50):
          double vI1d = (double)mTA1i_L[mWrTA1i_L];        // inner read v[n-367]
          double vI1n = in1 + gTA1_in * vI1d;
          ap1Out_L    = -gTA1_in * vI1n + vI1d;            // cascade output
          mTA1i_L[mWrTA1i_L] = (float)vI1n;
          mWrTA1i_L++;
          if (mWrTA1i_L >= kTA1i) mWrTA1i_L = 0;
        }

        // D1_L: Brownian-modulated read with linear interpolation.
        // Uses Size-scaled base length (scaledD1_L).
        double d1Read_L;
        {
          // Write current sample to buffer.
          mD1_L[mWrD1_L] = (float)ap1Out_L;

          // Advance PRNG and update walk.
          seed_D1_L = xorshift64(seed_D1_L);
          double noise = (double)(seed_D1_L & 0xFFFF) / 65535.0 - 0.5;
          walk_D1_L += noise * step_size;
          if (walk_D1_L >  excursion) walk_D1_L =  excursion;
          if (walk_D1_L < -excursion) walk_D1_L = -excursion;

          // Fractional read position relative to write head.
          // (mWrD1_L - scaledD1_L) is the integer center; walk offsets it.
          double readPos = (double)(mWrD1_L - scaledD1_L) + walk_D1_L;
          int    offset  = (int)floor(readPos);
          double frac    = readPos - (double)offset;

          // Map offset to valid buffer indices [0, kD1_L_size).
          int i0 = ((offset % kD1_L_size) + kD1_L_size) % kD1_L_size;
          int i1 = (i0 + 1) % kD1_L_size;

          d1Read_L = (1.0 - frac) * (double)mD1_L[i0] + frac * (double)mD1_L[i1];

          mWrD1_L++;
          if (mWrD1_L >= kD1_L_size) mWrD1_L = 0;
        }

        // D1_L intermediate taps — STATIC (unmodulated), read from the same buffer.
        // Offsets computed at block rate as fraction of scaledD1_L, rounded to odd.
        // Modular wrap matches the end-read pattern; all offsets < scaledD1_L < bufSize.
        double d1tap_a_L, d1tap_b_L, d1tap_c_L;
        {
          int ia = ((mWrD1_L - offD1a_L) % kD1_L_size + kD1_L_size) % kD1_L_size;
          int ib = ((mWrD1_L - offD1b_L) % kD1_L_size + kD1_L_size) % kD1_L_size;
          int ic = ((mWrD1_L - offD1c_L) % kD1_L_size + kD1_L_size) % kD1_L_size;
          d1tap_a_L = (double)mD1_L[ia];
          d1tap_b_L = (double)mD1_L[ib];
          d1tap_c_L = (double)mD1_L[ic];
        }

        // HF damp: one-pole LP on D1 output (Schroeder/Jot feedback form).
        // y += coeff * (x - y)  with state dampL.
        // Damp=0 → coeff=1.0 → dampL tracks x exactly (passthrough).
        // Damp>0 → coeff<1.0 → dampL lags x → LF-pass filtering.
        dampL += dampCoeff * (d1Read_L - dampL);
        double dampedD1_L = dampL;

        // AP2_L: series cascade — outer (kTA2=1471, g=gTA2) → inner (kTA2i=491, g=gTA2_in).
        double ap2Out_L;
        {
          // Outer AP (kTA2=1471, g=gTA2 from Diffusion):
          double vO2d = (double)mTA2_L[mWrTA2_L];
          double vO2n = dampedD1_L + gTA2 * vO2d;
          double in2  = -gTA2 * vO2n + vO2d;
          mTA2_L[mWrTA2_L] = (float)vO2n;
          mWrTA2_L++;
          if (mWrTA2_L >= kTA2) mWrTA2_L = 0;
          // Inner AP (kTA2i=491, g=gTA2_in=0.50):
          double vI2d = (double)mTA2i_L[mWrTA2i_L];
          double vI2n = in2 + gTA2_in * vI2d;
          ap2Out_L    = -gTA2_in * vI2n + vI2d;
          mTA2i_L[mWrTA2i_L] = (float)vI2n;
          mWrTA2i_L++;
          if (mWrTA2i_L >= kTA2i) mWrTA2i_L = 0;
        }

        // D2_L: Brownian-modulated read with linear interpolation.
        // Uses Size-scaled base length (scaledD2_L).
        double d2Read_L;
        {
          mD2_L[mWrD2_L] = (float)ap2Out_L;

          seed_D2_L = xorshift64(seed_D2_L);
          double noise = (double)(seed_D2_L & 0xFFFF) / 65535.0 - 0.5;
          walk_D2_L += noise * step_size;
          if (walk_D2_L >  excursion) walk_D2_L =  excursion;
          if (walk_D2_L < -excursion) walk_D2_L = -excursion;

          double readPos = (double)(mWrD2_L - scaledD2_L) + walk_D2_L;
          int    offset  = (int)floor(readPos);
          double frac    = readPos - (double)offset;

          int i0 = ((offset % kD2_L_size) + kD2_L_size) % kD2_L_size;
          int i1 = (i0 + 1) % kD2_L_size;

          d2Read_L = (1.0 - frac) * (double)mD2_L[i0] + frac * (double)mD2_L[i1];

          mWrD2_L++;
          if (mWrD2_L >= kD2_L_size) mWrD2_L = 0;
        }

        // D2_L intermediate taps — STATIC (unmodulated).
        double d2tap_a_L, d2tap_b_L;
        {
          int ia = ((mWrD2_L - offD2a_L) % kD2_L_size + kD2_L_size) % kD2_L_size;
          int ib = ((mWrD2_L - offD2b_L) % kD2_L_size + kD2_L_size) % kD2_L_size;
          d2tap_a_L = (double)mD2_L[ia];
          d2tap_b_L = (double)mD2_L[ib];
        }

        // -- R LOOP --

        // Accumulate: diffusion output + previous-sample cross-feed from L.
        double tankIn_R = diffIn + mFeedback_R;

        // DC blocker on tank input (R loop).
        {
          double dcOut = tankIn_R - dcx1_R + kDCBlockR * dcy1_R;
          dcx1_R = tankIn_R;
          dcy1_R = dcOut;
          tankIn_R = dcOut;
        }

        // AP1_R: series cascade — outer (kTA1=1087, g=gTA1) → inner (kTA1i=367, g=gTA1_in).
        // Same coefficients as L; separate buffers (mTA1_R, mTA1i_R) for independent state.
        double ap1Out_R;
        {
          // Outer AP:
          double vO1d = (double)mTA1_R[mWrTA1_R];
          double vO1n = tankIn_R + gTA1 * vO1d;
          double in1  = -gTA1 * vO1n + vO1d;
          mTA1_R[mWrTA1_R] = (float)vO1n;
          mWrTA1_R++;
          if (mWrTA1_R >= kTA1) mWrTA1_R = 0;
          // Inner AP:
          double vI1d = (double)mTA1i_R[mWrTA1i_R];
          double vI1n = in1 + gTA1_in * vI1d;
          ap1Out_R    = -gTA1_in * vI1n + vI1d;
          mTA1i_R[mWrTA1i_R] = (float)vI1n;
          mWrTA1i_R++;
          if (mWrTA1i_R >= kTA1i) mWrTA1i_R = 0;
        }

        // D1_R: Brownian-modulated read, ASYMMETRIC base (scaledD1_R), R-specific seed.
        double d1Read_R;
        {
          mD1_R[mWrD1_R] = (float)ap1Out_R;

          seed_D1_R = xorshift64(seed_D1_R);
          double noise = (double)(seed_D1_R & 0xFFFF) / 65535.0 - 0.5;
          walk_D1_R += noise * step_size;
          if (walk_D1_R >  excursion) walk_D1_R =  excursion;
          if (walk_D1_R < -excursion) walk_D1_R = -excursion;

          double readPos = (double)(mWrD1_R - scaledD1_R) + walk_D1_R;
          int    offset  = (int)floor(readPos);
          double frac    = readPos - (double)offset;

          int i0 = ((offset % kD1_R_size) + kD1_R_size) % kD1_R_size;
          int i1 = (i0 + 1) % kD1_R_size;

          d1Read_R = (1.0 - frac) * (double)mD1_R[i0] + frac * (double)mD1_R[i1];

          mWrD1_R++;
          if (mWrD1_R >= kD1_R_size) mWrD1_R = 0;
        }

        // D1_R intermediate taps — STATIC (unmodulated).
        double d1tap_a_R, d1tap_b_R, d1tap_c_R;
        {
          int ia = ((mWrD1_R - offD1a_R) % kD1_R_size + kD1_R_size) % kD1_R_size;
          int ib = ((mWrD1_R - offD1b_R) % kD1_R_size + kD1_R_size) % kD1_R_size;
          int ic = ((mWrD1_R - offD1c_R) % kD1_R_size + kD1_R_size) % kD1_R_size;
          d1tap_a_R = (double)mD1_R[ia];
          d1tap_b_R = (double)mD1_R[ib];
          d1tap_c_R = (double)mD1_R[ic];
        }

        // HF damp: one-pole LP on D1_R output.
        dampR += dampCoeff * (d1Read_R - dampR);
        double dampedD1_R = dampR;

        // AP2_R: series cascade — outer (kTA2=1471, g=gTA2) → inner (kTA2i=491, g=gTA2_in).
        double ap2Out_R;
        {
          // Outer AP:
          double vO2d = (double)mTA2_R[mWrTA2_R];
          double vO2n = dampedD1_R + gTA2 * vO2d;
          double in2  = -gTA2 * vO2n + vO2d;
          mTA2_R[mWrTA2_R] = (float)vO2n;
          mWrTA2_R++;
          if (mWrTA2_R >= kTA2) mWrTA2_R = 0;
          // Inner AP:
          double vI2d = (double)mTA2i_R[mWrTA2i_R];
          double vI2n = in2 + gTA2_in * vI2d;
          ap2Out_R    = -gTA2_in * vI2n + vI2d;
          mTA2i_R[mWrTA2i_R] = (float)vI2n;
          mWrTA2i_R++;
          if (mWrTA2i_R >= kTA2i) mWrTA2i_R = 0;
        }

        // D2_R: Brownian-modulated read, ASYMMETRIC base (scaledD2_R), R-specific seed.
        double d2Read_R;
        {
          mD2_R[mWrD2_R] = (float)ap2Out_R;

          seed_D2_R = xorshift64(seed_D2_R);
          double noise = (double)(seed_D2_R & 0xFFFF) / 65535.0 - 0.5;
          walk_D2_R += noise * step_size;
          if (walk_D2_R >  excursion) walk_D2_R =  excursion;
          if (walk_D2_R < -excursion) walk_D2_R = -excursion;

          double readPos = (double)(mWrD2_R - scaledD2_R) + walk_D2_R;
          int    offset  = (int)floor(readPos);
          double frac    = readPos - (double)offset;

          int i0 = ((offset % kD2_R_size) + kD2_R_size) % kD2_R_size;
          int i1 = (i0 + 1) % kD2_R_size;

          d2Read_R = (1.0 - frac) * (double)mD2_R[i0] + frac * (double)mD2_R[i1];

          mWrD2_R++;
          if (mWrD2_R >= kD2_R_size) mWrD2_R = 0;
        }

        // D2_R intermediate taps — STATIC (unmodulated).
        double d2tap_a_R, d2tap_b_R;
        {
          int ia = ((mWrD2_R - offD2a_R) % kD2_R_size + kD2_R_size) % kD2_R_size;
          int ib = ((mWrD2_R - offD2b_R) % kD2_R_size + kD2_R_size) % kD2_R_size;
          d2tap_a_R = (double)mD2_R[ia];
          d2tap_b_R = (double)mD2_R[ib];
        }

        // -- CROSS-FEED UPDATE (for next sample) --
        // R's D2 output × g_d feeds L's next-sample accumulator, and
        // vice versa. Spiral governor bounds each independently.
        // Both d2Read_L and d2Read_R are fully computed above before
        // either feedback value is updated — no same-sample causality leak.
        mFeedback_L = house::spiralFastSaturate(d2Read_R * g_d, 1.0);
        mFeedback_R = house::spiralFastSaturate(d2Read_L * g_d, 1.0);

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
        double wetL = kWetLevel * (
            + kWap1 * ap1Out_L
            + kWd1a * d1tap_a_L - kWd1b * d1tap_b_L + kWd1c * d1tap_c_L
            + kWd1e * d1Read_L
            - kWd2a * d2tap_a_L + kWd2b * d2tap_b_L
            + kWd2e * d2Read_L
        );
        double wetR = kWetLevel * (
            + kWap1 * ap1Out_R
            + kWd1a * d1tap_a_R - kWd1b * d1tap_b_R + kWd1c * d1tap_c_R
            + kWd1e * d1Read_R
            - kWd2a * d2tap_a_R + kWd2b * d2tap_b_R
            + kWd2e * d2Read_R
        );

        // Add ER contribution (parallel, AFTER tank multi-tap).
        // Scales to zero when earlyParam=0 → exact 0.1.0.8 output.
        // ER is purely feedforward (FIR): no feedback, no stability concern.
        double erScale = (double)(kERLevel * earlyParam);
        wetL += erScale * erSumL;
        wetR += erScale * erSumR;

        // ----------------------------------------------------------------
        // 6. Dry/wet mix — true stereo.
        //    Each channel's dry is preserved; each channel's wet is drawn
        //    from its own loop's delay taps. Cross-coupling has already
        //    mixed information between the loops via the figure-8 feedback,
        //    so wetL and wetR are decorrelated even from a mono source.
        // ----------------------------------------------------------------
        double dryMix  = (double)(1.0f - mix);
        double wetMix  = (double)mix;
        double outL = drySampleL * dryMix + wetL * wetMix;
        double outR = drySampleR * dryMix + wetR * wetMix;

        *out1 = (float)outL;
        *out2 = (float)outR;
        in1++; in2++; out1++; out2++;
      }

      // Propagate local walk + seed state back to members.
      mWalk_D1_L = walk_D1_L;
      mWalk_D2_L = walk_D2_L;
      mWalk_D1_R = walk_D1_R;
      mWalk_D2_R = walk_D2_R;

      mSeed_D1_L = seed_D1_L;
      mSeed_D2_L = seed_D2_L;
      mSeed_D1_R = seed_D1_R;
      mSeed_D2_R = seed_D2_R;

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

    // Input diffusion allpass buffers (4 series, shared mono path)
    float mID1[kID1];
    float mID2[kID2];
    float mID3[kID3];
    float mID4[kID4];
    int   mWrID1, mWrID2, mWrID3, mWrID4;

    // Tank allpass buffers — L loop (series-cascade Schroeder APF).
    // Outer buffers (kTA1=1087, kTA2=1471): exact size, write head wraps at N.
    // Inner buffers (kTA1i=367, kTA2i=491): UNMODULATED, exact size, no headroom.
    // All four zeroed in constructor; all four write heads initialized to 0.
    float mTA1_L[kTA1];
    float mTA1i_L[kTA1i];   // AP1 inner cascade
    float mTA2_L[kTA2];
    float mTA2i_L[kTA2i];   // AP2 inner cascade
    int   mWrTA1_L, mWrTA1i_L, mWrTA2_L, mWrTA2i_L;

    // Tank delay lines — L loop (sized for max sizeFactor=1.5 + headroom).
    float mD1_L[kD1_L_size];   // 11038 samples
    float mD2_L[kD2_L_size];   // 7908 samples
    int   mWrD1_L, mWrD2_L;

    // Tank allpass buffers — R loop (same lengths as L, separate state).
    float mTA1_R[kTA1];
    float mTA1i_R[kTA1i];   // AP1 inner cascade
    float mTA2_R[kTA2];
    float mTA2i_R[kTA2i];   // AP2 inner cascade
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
    double mFeedback_L;
    double mFeedback_R;

    // HF damp filter state — one per loop (one-pole LP on D1 output).
    // Initialized to 0 in constructor; converges quickly on first use.
    double mDampL;
    double mDampR;

    // DC blocker state — one pair per loop.
    // mDCx1_X: previous input sample; mDCy1_X: previous output sample.
    double mDCx1_L;  double mDCy1_L;
    double mDCx1_R;  double mDCy1_R;

    // Size-scaled delay lengths — updated at block rate when Size changes.
    // Initialized to base lengths (Size=0.5 default) in constructor.
    int   mScaledD1_L;
    int   mScaledD2_L;
    int   mScaledD1_R;
    int   mScaledD2_R;
    float mLastSize;   // tracks last seen Size value to detect changes

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

#endif
  };

} // namespace zaum
