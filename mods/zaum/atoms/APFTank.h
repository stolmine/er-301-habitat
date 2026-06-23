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
// BUILD SUB-PHASE 0.1.0.5 — HF damping + Decay/RT60 + Size scaling +
// DC blocker.
//
//   This sub-phase wires three previously inert parameters and adds a DC
//   blocker to the recirculating path. After this, Diffusion is the only
//   inert parameter.
//
//   A. DAMP — one-pole HF low-pass in each loop's D1 feedback path.
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
// INERT parameters this sub-phase (declared, Lua-tied, DSP-unused):
//   Diffusion.
// Wired parameters: Predelay, Mix, Mod, ModRate, Damp, Decay, Size.
//
// Tank allpass convention (plain Schroeder, provably unity-gain):
//   vDelayed = buf[read N samples behind write head]   // v[n-N]
//   vNew     = x + g * v[n-N]
//   yOut     = -g * vNew + v[n-N]
//   buf[w]   = vNew; advance w
// Gardner nesting (inner 367/491) deferred per fabula-design.md §6;
// plain Schroeder allpass used for guaranteed unity-gain stability.
// Revisit nesting (with correct separate inner buffers) as a density
// enhancement if the tail sounds thin.
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

  // Tank allpass buffers (plain Schroeder APF, BOTH L and R loops).
  // AP1: delay N=1087, g=0.70. AP2: delay N=1471, g=0.50.
  // L and R loops share the SAME delay lengths and coefficients but use
  // SEPARATE buffers (mTA1_L/mTA1_R, mTA2_L/mTA2_R) and write indices.
  // Each buffer is exactly N samples; write head wraps at N.
  // Gardner inner delays kTA1i/kTA2i are reserved for future nesting
  // (deferred per fabula-design.md §6 — requires separate inner buffers).
  static const int kTA1  = 1087;   // AP1 delay (= buffer size), both loops
  static const int kTA1i = 367;    // AP1 Gardner inner delay (UNUSED this phase)
  static const int kTA2  = 1471;   // AP2 delay (= buffer size), both loops
  static const int kTA2i = 491;    // AP2 Gardner inner delay (UNUSED this phase)

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
  // DC blocker coefficient.
  // Form: y[n] = x[n] - x[n-1] + kDCBlockR * y[n-1]
  // fc ≈ (1-R)*sr/(2π) ≈ 3.8 Hz at 48 kHz — inaudible in reverb tail.
  // ---------------------------------------------------------------------------
  static const double kDCBlockR = 0.9995;

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

      memset(mPD,     0, sizeof(mPD));
      memset(mID1,    0, sizeof(mID1));
      memset(mID2,    0, sizeof(mID2));
      memset(mID3,    0, sizeof(mID3));
      memset(mID4,    0, sizeof(mID4));
      // L-loop allpass + delay buffers
      memset(mTA1_L,  0, sizeof(mTA1_L));
      memset(mTA2_L,  0, sizeof(mTA2_L));
      memset(mD1_L,   0, sizeof(mD1_L));
      memset(mD2_L,   0, sizeof(mD2_L));
      // R-loop allpass + delay buffers
      memset(mTA1_R,  0, sizeof(mTA1_R));
      memset(mTA2_R,  0, sizeof(mTA2_R));
      memset(mD1_R,   0, sizeof(mD1_R));
      memset(mD2_R,   0, sizeof(mD2_R));

      mWrPD  = 0;
      mWrID1 = 0; mWrID2 = 0; mWrID3 = 0; mWrID4 = 0;
      // L-loop write heads
      mWrTA1_L = 0; mWrTA2_L = 0;
      mWrD1_L  = 0; mWrD2_L  = 0;
      // R-loop write heads
      mWrTA1_R = 0; mWrTA2_R = 0;
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

      // Size-scaled delay lengths — initialize to base lengths (Size=0.5 default).
      mScaledD1_L = kD1_L_base;
      mScaledD2_L = kD2_L_base;
      mScaledD1_R = kD1_R_base;
      mScaledD2_R = kD2_R_base;
      mLastSize   = 0.5f;
    }

    virtual ~APFTank() {}

#ifndef SWIGLUA
    od::Inlet     mInL{"In L"};
    od::Inlet     mInR{"In R"};
    od::Outlet    mOutL{"Out L"};
    od::Outlet    mOutR{"Out R"};
    od::Parameter mSize{"Size", 0.5f};
    od::Parameter mDecay{"Decay", 0.5f};
    od::Parameter mDamp{"Damp", 0.25f};
    od::Parameter mDiffusion{"Diffusion", 0.6f};
    od::Parameter mMod{"Mod", 0.3f};
    od::Parameter mModRate{"ModRate", 0.2f};
    od::Parameter mPredelay{"Predelay", 0.0f};
    od::Parameter mMix{"Mix", 0.5f};

    virtual void process()
    {
      float *in1  = mInL.buffer();
      float *in2  = mInR.buffer();
      float *out1 = mOutL.buffer();
      float *out2 = mOutR.buffer();

      // ------------------------------------------------------------------
      // Read parameters (block rate).
      // Predelay: 0..1 maps to 0..(kPD-1) samples. Integer tap.
      // Mix:      0..1 linear crossfade.
      // Mod:      0..1 → excursion in samples (kMinExcursion..kMaxExcursion).
      // ModRate:  0..1 → walk step_size per sample (kMinStep..kMaxStep).
      // Damp:     0..1 → one-pole LP coeff for HF damping.
      // Decay:    0..1 → g_d via power-curve map.
      // Size:     0..1 → sizeFactor → scaled delay lengths (block rate).
      // Diffusion: INERT this sub-phase (wired in 0.1.0.7).
      // ------------------------------------------------------------------
      const float predelayParam = mPredelay.value();  // 0..1
      const float mix           = mMix.value();       // 0..1
      const float modParam      = mMod.value();       // 0..1
      const float modRateParam  = mModRate.value();   // 0..1
      const float dampParam     = mDamp.value();      // 0..1
      const float decayParam    = mDecay.value();     // 0..1
      const float sizeParam     = mSize.value();      // 0..1

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

      // Input diffusion coefficients (Diffusion param wired in 0.1.0.7).
      const double gID12 = 0.75;
      const double gID34 = 0.625;

      // Tank AP coefficients (plain Schroeder allpass, unity-gain by construction).
      const double gTA1 = 0.70;
      const double gTA2 = 0.50;

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

        // AP1_L: plain Schroeder allpass, delay=kTA1=1087, g=0.70
        double ap1Out_L;
        {
          double vD = (double)mTA1_L[mWrTA1_L];
          double vNew, yOut;
          house::allpassNestedStep(tankIn_L, vD, gTA1, vNew, yOut);
          mTA1_L[mWrTA1_L] = (float)vNew;
          mWrTA1_L++;
          if (mWrTA1_L >= kTA1) mWrTA1_L = 0;
          ap1Out_L = yOut;
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

        // HF damp: one-pole LP on D1 output (Schroeder/Jot feedback form).
        // y += coeff * (x - y)  with state dampL.
        // Damp=0 → coeff=1.0 → dampL tracks x exactly (passthrough).
        // Damp>0 → coeff<1.0 → dampL lags x → LF-pass filtering.
        dampL += dampCoeff * (d1Read_L - dampL);
        double dampedD1_L = dampL;

        // AP2_L: plain Schroeder allpass, delay=kTA2=1471, g=0.50
        double ap2Out_L;
        {
          double vD = (double)mTA2_L[mWrTA2_L];
          double vNew, yOut;
          house::allpassNestedStep(dampedD1_L, vD, gTA2, vNew, yOut);
          mTA2_L[mWrTA2_L] = (float)vNew;
          mWrTA2_L++;
          if (mWrTA2_L >= kTA2) mWrTA2_L = 0;
          ap2Out_L = yOut;
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

        // AP1_R: plain Schroeder allpass, delay=kTA1=1087, g=0.70
        // Same coefficients as L; separate buffer for independent state.
        double ap1Out_R;
        {
          double vD = (double)mTA1_R[mWrTA1_R];
          double vNew, yOut;
          house::allpassNestedStep(tankIn_R, vD, gTA1, vNew, yOut);
          mTA1_R[mWrTA1_R] = (float)vNew;
          mWrTA1_R++;
          if (mWrTA1_R >= kTA1) mWrTA1_R = 0;
          ap1Out_R = yOut;
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

        // HF damp: one-pole LP on D1_R output.
        dampR += dampCoeff * (d1Read_R - dampR);
        double dampedD1_R = dampR;

        // AP2_R: plain Schroeder allpass, delay=kTA2=1471, g=0.50
        double ap2Out_R;
        {
          double vD = (double)mTA2_R[mWrTA2_R];
          double vNew, yOut;
          house::allpassNestedStep(dampedD1_R, vD, gTA2, vNew, yOut);
          mTA2_R[mWrTA2_R] = (float)vNew;
          mWrTA2_R++;
          if (mWrTA2_R >= kTA2) mWrTA2_R = 0;
          ap2Out_R = yOut;
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

        // -- CROSS-FEED UPDATE (for next sample) --
        // R's D2 output × g_d feeds L's next-sample accumulator, and
        // vice versa. Spiral governor bounds each independently.
        // Both d2Read_L and d2Read_R are fully computed above before
        // either feedback value is updated — no same-sample causality leak.
        mFeedback_L = house::spiralFastSaturate(d2Read_R * g_d, 1.0);
        mFeedback_R = house::spiralFastSaturate(d2Read_L * g_d, 1.0);

        // ----------------------------------------------------------------
        // 5. Stereo wet taps.
        //    wetL draws from L-loop delay lines; wetR from R-loop.
        //    Summing D1+D2 per channel and halving keeps the wet level
        //    comparable to dry and blends the two tap points for density.
        // ----------------------------------------------------------------
        double wetL = (d1Read_L + d2Read_L) * 0.5;
        double wetR = (d1Read_R + d2Read_R) * 0.5;

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

    // Input diffusion allpass buffers (4 series, shared mono path)
    float mID1[kID1];
    float mID2[kID2];
    float mID3[kID3];
    float mID4[kID4];
    int   mWrID1, mWrID2, mWrID3, mWrID4;

    // Tank allpass buffers — L loop (plain Schroeder APF,
    // each buffer exactly N samples deep, write head wraps at N)
    float mTA1_L[kTA1];
    float mTA2_L[kTA2];
    int   mWrTA1_L, mWrTA2_L;

    // Tank delay lines — L loop (sized for max sizeFactor=1.5 + headroom).
    float mD1_L[kD1_L_size];   // 11038 samples
    float mD2_L[kD2_L_size];   // 7908 samples
    int   mWrD1_L, mWrD2_L;

    // Tank allpass buffers — R loop (same lengths as L, separate state)
    float mTA1_R[kTA1];
    float mTA2_R[kTA2];
    int   mWrTA1_R, mWrTA2_R;

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
