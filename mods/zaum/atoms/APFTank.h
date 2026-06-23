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
// BUILD SUB-PHASE 0.1.0.4 — Brownian delay-line modulation.
//   Four independent Brownian LFOs, one per tank delay line (D1_L,
//   D2_L, D1_R, D2_R). Each uses a fast xorshift64 PRNG (never libc
//   rand() on the audio thread) with distinct compile-time seeds so the
//   four lines are decorrelated from sample 0. L-line seeds and R-line
//   seeds are distinct per fabula-design.md §5.
//
//   Each tank delay read is now MODULATED via linear (two-point)
//   interpolation: the walk accumulator is a double, so the read
//   position is fractional; we split into integer offset + fraction and
//   interpolate between buf[i0] and buf[i0+1]. This pulls forward the
//   §3 "upgrade path" deliberately because:
//     1. Integer-step zipper at low Mod would confound the depth/rate
//        calibration audition (§3 rationale + §9 0.1.0.4 note).
//     2. Cost is minimal (2 multiplies + 1 add + 1 floor per line per
//        sample) and the math is already in place for the walk double.
//
//   No one-pole position smoother added: the walk is already a smooth
//   integrator (noise → accumulate → clamp), and linear interp handles
//   sub-sample continuity. Adding a smoother on top of that would only
//   reduce the achieved modulation depth vs the intended excursion. If
//   audition reveals the walk still steps too coarsely at high ModRate,
//   add a one-pole with α≈0.9995 per line as a post-walk smoother.
//
//   MODULATION PARAMETER MAPPINGS (easy to retune by ear):
//
//   Mod 0..1 → excursion in samples:
//     excursion = 9.0 + mod * 63.0   (range: 9..72 samples = 0.19..1.5 ms)
//     Default Mod=0.3 → excursion = 9 + 0.3*63 = 27.9 samples (≈0.58 ms)
//
//   ModRate 0..1 → walk step_size per sample (exponential-feeling taper):
//     step_size = 0.0002 + modRate * 0.0998   (range: 0.0002..0.1)
//     (linear; the logarithmic feel comes from Brownian diffusion itself)
//     Default ModRate=0.2 → step_size = 0.0002 + 0.2*0.0998 ≈ 0.020 per sample
//
//   CALIBRATION CONTEXT for §11 gate:
//     At Mod=0, ModRate=0: no walk → static reads → slightly metallic tail.
//     At Mod=0.3 (excursion≈28), ModRate=0.2 (step≈0.020): the walk
//       covers ±28 samples slowly (diffusion time to reach ±28 from 0 at
//       step 0.020: N≈(28/0.020)²=1.96M samples≈40s, so in practice the
//       walk is always within a modest fraction of max excursion, drifting
//       slowly). This is the intended sweet spot.
//     At Mod=1.0 (excursion=72), ModRate=1.0 (step=0.1): walks reach
//       ±72 and wander faster — audible pitch wander at extremes.
//     PRIMARY TUNING KNOBS: kMaxExcursion (72→lower if too warbly),
//       kMinExcursion (9→lower for a drier minimum), kMaxStep (0.1→
//       lower if ModRate=1 is too fast, higher if sluggish).
//
//   BUFFER BOUNDS SAFETY:
//     Max excursion = 72 samples. Linear interp reads buf[i0] and buf[i0+1].
//     Worst-case additional reach = 72 + 1 = 73 samples. Headroom = 128.
//     73 < 128 — safe with margin of 55 samples on each side.
//     Wrap arithmetic: readPos = wrHead - base_delay + walk (double).
//       Integer part offset = (int)floor(readPos). We add size and take
//       modulo to handle any negative result before splitting into i0/i1.
//       Both i0 and i0+1 are taken mod size, so i1 wraps correctly when
//       i0 == size-1. Walk is clamped strictly to [-excursion, +excursion]
//       before any index arithmetic, bounding the deviation from center.
//
//   STABILITY CONFIRMATION:
//     Modulating read position on a delay line does NOT affect loop gain:
//     the allpass stages remain unity-gain (|H|=1 for all ω, regardless of
//     reading offset). Decay gain g_d=0.85 is fixed. The 2×2 cross-coupling
//     eigenvalues remain ±0.85 < 1. Spiral governors remain in place.
//     Varying the read offset introduces a mild pitch smear (the desired
//     lushness effect) but does not change the energy-recirculation gain.
//     No new runaway path exists.
//
// INERT parameters this sub-phase (declared, Lua-tied, DSP-unused):
//   Size, Decay, Damp, Diffusion.
// Wired parameters: Predelay, Mix, Mod, ModRate.
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

  // Tank delay lines with modulation headroom — L loop.
  // D1_L: base 7187 + 128 head slack each end = 7187 + 256 = 7443.
  // D2_L: base 5101 + 128 head slack each end = 5101 + 256 = 5357.
  // Modulated reads stay within [center-72, center+73] — 73 < 128 headroom.
  static const int kD1_L          = 7187;
  static const int kD1_headroom   = 128;
  static const int kD1_L_size     = kD1_L + 2 * kD1_headroom;   // 7443

  static const int kD2_L          = 5101;
  static const int kD2_headroom   = 128;
  static const int kD2_L_size     = kD2_L + 2 * kD2_headroom;   // 5357

  // Tank delay lines with modulation headroom — R loop (ASYMMETRIC).
  // Asymmetry (6803 vs 7187, 6343 vs 5101) decorrelates L from R.
  // All four lengths (7187, 5101, 6803, 6343) are mutually prime:
  //   gcd(7187,5101)=1, gcd(7187,6803)=1, gcd(7187,6343)=1,
  //   gcd(5101,6803)=1, gcd(5101,6343)=1, gcd(6803,6343)=1.
  // Mutual primality suppresses shared modal reinforcement in the
  // coupled feedback system; the eigen-frequency distribution is
  // incoherent → smooth dense tail instead of flutter/ring.
  static const int kD1_R          = 6803;
  static const int kD1_R_size     = kD1_R + 2 * kD1_headroom;   // 7059

  static const int kD2_R          = 6343;
  static const int kD2_R_size     = kD2_R + 2 * kD2_headroom;   // 6599

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
      // All other params INERT this sub-phase.
      // ------------------------------------------------------------------
      const float predelayParam = mPredelay.value();  // 0..1
      const float mix           = mMix.value();       // 0..1
      const float modParam      = mMod.value();       // 0..1
      const float modRateParam  = mModRate.value();   // 0..1

      // Max tap = kPD - 1 (keep at least 1-sample separation from write head)
      const int predelayTap = (int)(predelayParam * (float)(kPD - 1));

      // Hard-coded decay gain this sub-phase (Decay param wired in 0.1.0.5).
      const double g_d = 0.85;

      // Input diffusion coefficients (Diffusion param wired in 0.1.0.5).
      const double gID12 = 0.75;
      const double gID34 = 0.625;

      // Tank AP coefficients (plain Schroeder allpass, unity-gain by construction).
      const double gTA1 = 0.70;
      const double gTA2 = 0.50;

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
        //        readPos = wrHead - base_delay + walk   (double)
        //        offset  = (int)floor(readPos)
        //        frac    = readPos - (double)offset
        //   6. Wrap to valid buffer indices (add size, mod size):
        //        i0 = ((wrHead + offset + bufSize) % bufSize + bufSize) % bufSize
        //        i1 = (i0 + 1) % bufSize
        //        NOTE: wrHead - base_delay is typically negative relative to
        //        wrHead, so (wrHead - base_delay + walk) in absolute coords
        //        is wrHead - (base_delay - walk). We fold this into the
        //        modular arithmetic. Adding bufSize twice ensures no negative
        //        result even if offset is deeply negative after floating mod.
        //   7. Interpolate: out = (1-frac)*buf[i0] + frac*buf[i1]
        //
        // BOUNDS PROOF:
        //   walk clamped to [-72, +72].
        //   Fractional part frac ∈ [0, 1).
        //   So i1 = i0+1 mod bufSize — always valid (mod handles wrap).
        //   The integer offset from center is at most 72; i0 always lands
        //   somewhere in [wrHead-base_delay-72 .. wrHead-base_delay+72]
        //   mod bufSize — all of which are in [0, bufSize), since the
        //   headroom is 128 samples each side and 72 < 128.
        //
        // ORDERING DISCIPLINE (critical for correct cross-coupling):
        //   Each loop's tankIn is formed from diffIn + last sample's
        //   mFeedback_X (already set). Both loops advance completely
        //   (producing d2Read_L and d2Read_R) for THIS sample. Then
        //   mFeedback_L/R are updated from the cross outputs for NEXT
        //   sample. This matches the existing single-loop convention:
        //   mFeedback is set at end of sample for use on next sample.
        //   No loop consumes the other's same-sample D2 read before
        //   it is computed.
        //
        // Cross-couple wiring (coupling coefficient = 1.0, Dattorro §5):
        //   mFeedback_L = spiralFastSaturate(d2Read_R * g_d, 1.0)
        //   mFeedback_R = spiralFastSaturate(d2Read_L * g_d, 1.0)
        //
        // 2×2 stability: the coupling matrix is [[0, g_d],[g_d, 0]].
        // Eigenvalues are ±g_d = ±0.85, magnitude 0.85 < 1. Both
        // allpass stages are unity-gain (|H|=1 for all ω). Per-loop
        // Spiral governors are the additional hard safety net.
        // ----------------------------------------------------------------

        // -- L LOOP --

        // Accumulate: diffusion output + previous-sample cross-feed from R.
        double tankIn_L = diffIn + mFeedback_L;

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
          // (mWrD1_L - kD1_L) is the integer center; walk offsets it.
          double readPos = (double)(mWrD1_L - kD1_L) + walk_D1_L;
          int    offset  = (int)floor(readPos);
          double frac    = readPos - (double)offset;

          // Map offset to valid buffer indices [0, kD1_L_size).
          // The expression (offset % size + size) % size handles negative
          // values correctly without UB (offset is always far from INT_MIN).
          int i0 = ((offset % kD1_L_size) + kD1_L_size) % kD1_L_size;
          int i1 = (i0 + 1) % kD1_L_size;

          d1Read_L = (1.0 - frac) * (double)mD1_L[i0] + frac * (double)mD1_L[i1];

          mWrD1_L++;
          if (mWrD1_L >= kD1_L_size) mWrD1_L = 0;
        }

        // HF damp: DISABLED this sub-phase.
        double dampedD1_L = d1Read_L;

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
        double d2Read_L;
        {
          mD2_L[mWrD2_L] = (float)ap2Out_L;

          seed_D2_L = xorshift64(seed_D2_L);
          double noise = (double)(seed_D2_L & 0xFFFF) / 65535.0 - 0.5;
          walk_D2_L += noise * step_size;
          if (walk_D2_L >  excursion) walk_D2_L =  excursion;
          if (walk_D2_L < -excursion) walk_D2_L = -excursion;

          double readPos = (double)(mWrD2_L - kD2_L) + walk_D2_L;
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

        // D1_R: Brownian-modulated read, ASYMMETRIC base (6803), R-specific seed.
        double d1Read_R;
        {
          mD1_R[mWrD1_R] = (float)ap1Out_R;

          seed_D1_R = xorshift64(seed_D1_R);
          double noise = (double)(seed_D1_R & 0xFFFF) / 65535.0 - 0.5;
          walk_D1_R += noise * step_size;
          if (walk_D1_R >  excursion) walk_D1_R =  excursion;
          if (walk_D1_R < -excursion) walk_D1_R = -excursion;

          double readPos = (double)(mWrD1_R - kD1_R) + walk_D1_R;
          int    offset  = (int)floor(readPos);
          double frac    = readPos - (double)offset;

          int i0 = ((offset % kD1_R_size) + kD1_R_size) % kD1_R_size;
          int i1 = (i0 + 1) % kD1_R_size;

          d1Read_R = (1.0 - frac) * (double)mD1_R[i0] + frac * (double)mD1_R[i1];

          mWrD1_R++;
          if (mWrD1_R >= kD1_R_size) mWrD1_R = 0;
        }

        // HF damp: DISABLED this sub-phase.
        double dampedD1_R = d1Read_R;

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

        // D2_R: Brownian-modulated read, ASYMMETRIC base (6343), R-specific seed.
        double d2Read_R;
        {
          mD2_R[mWrD2_R] = (float)ap2Out_R;

          seed_D2_R = xorshift64(seed_D2_R);
          double noise = (double)(seed_D2_R & 0xFFFF) / 65535.0 - 0.5;
          walk_D2_R += noise * step_size;
          if (walk_D2_R >  excursion) walk_D2_R =  excursion;
          if (walk_D2_R < -excursion) walk_D2_R = -excursion;

          double readPos = (double)(mWrD2_R - kD2_R) + walk_D2_R;
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

    // Tank delay lines — L loop (with modulation headroom)
    float mD1_L[kD1_L_size];
    float mD2_L[kD2_L_size];
    int   mWrD1_L, mWrD2_L;

    // Tank allpass buffers — R loop (same lengths as L, separate state)
    float mTA1_R[kTA1];
    float mTA2_R[kTA2];
    int   mWrTA1_R, mWrTA2_R;

    // Tank delay lines — R loop (ASYMMETRIC: D1_R=6803, D2_R=6343)
    float mD1_R[kD1_R_size];
    float mD2_R[kD2_R_size];
    int   mWrD1_R, mWrD2_R;

    // Recirculating feedback accumulators (double precision: these values
    // traverse the full round-trip path each sample; precision matters
    // for long-decay tails where accumulated rounding would drift pitch).
    // Cross-coupled: mFeedback_L is written from d2Read_R×g_d (R feeds L),
    //                mFeedback_R is written from d2Read_L×g_d (L feeds R).
    double mFeedback_L;
    double mFeedback_R;

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
