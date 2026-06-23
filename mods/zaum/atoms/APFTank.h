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
// BUILD SUB-PHASE 0.1.0.3 — stereo figure-8 cross-coupling.
//   Full R tank loop added alongside L loop. Asymmetric R delay lines
//   (D1_R=6803, D2_R=6343 vs L's D1_L=7187, D2_L=5101) ensure
//   per-channel decorrelation — all four lengths are mutually prime.
//   Input diffusion remains SHARED/mono (sum L+R → predelay → 4 stages
//   → diffIn feeds BOTH tank loops). Per-channel diffusion deferred as
//   a listen-decision; §9 explicitly permits shared diffusion this phase.
//   Figure-8 cross-feed: L's D2 output × g_d → mFeedback_R (feeds R
//   next sample); R's D2 output × g_d → mFeedback_L (feeds L next
//   sample). Coupling coefficient 1.0 per Dattorro/§5. Spiral governor
//   applied independently to each loop's recirculating feedback.
//   Wet taps: wetL=0.5*(d1Read_L+d2Read_L), wetR=0.5*(d1Read_R+d2Read_R).
//   True stereo output replaces the previous mono-duplicated output.
//
// INERT parameters this sub-phase (declared, Lua-tied, DSP-unused):
//   Size, Decay, Damp, Diffusion, Mod, ModRate.
// Wired parameters: Predelay, Mix.
//
// Modulation headroom reserved on all four tank delay buffers
// (base + 128 samples each side) so 0.1.0.4 can add Brownian reads
// without resizing. Reads are static (center of headroom) this phase.
//
// Tank allpass convention (plain Schroeder, provably unity-gain):
//   vDelayed = buf[read N samples behind write head]   // v[n-N]
//   vNew     = x + g * vDelayed
//   yOut     = -g * vNew + vDelayed
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
  // Static reads this sub-phase use the center offset (base + 128).
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
      // Mix: 0..1 linear crossfade.
      // All other params INERT this sub-phase.
      // ------------------------------------------------------------------
      const float predelayParam = mPredelay.value();  // 0..1
      const float mix           = mMix.value();       // 0..1

      // Max tap = kPD - 1 (keep at least 1-sample separation from write head)
      const int predelayTap = (int)(predelayParam * (float)(kPD - 1));

      // Hard-coded decay gain this sub-phase (Decay param wired in 0.1.0.5).
      const double g_d = 0.85;

      // Input diffusion coefficients (Diffusion param wired in 0.1.0.5).
      const double gID12 = 0.75;
      const double gID34 = 0.625;

      // Tank AP coefficients (plain Schroeder allpass, unity-gain by construction).
      // AP1 uses the outer (TA1) coefficient from fabula-design.md §2 table.
      // AP2 uses the outer (TA2) coefficient. Inner g values unused this phase.
      const double gTA1 = 0.70;
      const double gTA2 = 0.50;

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
        //
        // Plain Schroeder APF (unity-gain by construction):
        //   vDelayed = buf[w]  (oldest slot, buffer is exactly N deep)
        //   vNew     = x + g * vDelayed
        //   yOut     = -g * vNew + vDelayed
        //   buf[w]   = vNew; w = (w+1) % N
        // Modulation headroom: D reads are base_N samples behind write
        // head, centered in the ±128-sample headroom window.
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

        // D1_L: 7187 samples base, ±128 headroom; static center read.
        double d1Read_L;
        {
          mD1_L[mWrD1_L] = (float)ap1Out_L;
          int rD1 = mWrD1_L - kD1_L;
          if (rD1 < 0) rD1 += kD1_L_size;
          d1Read_L = (double)mD1_L[rD1];
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

        // D2_L: 5101 samples base, ±128 headroom; static center read.
        double d2Read_L;
        {
          mD2_L[mWrD2_L] = (float)ap2Out_L;
          int rD2 = mWrD2_L - kD2_L;
          if (rD2 < 0) rD2 += kD2_L_size;
          d2Read_L = (double)mD2_L[rD2];
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

        // D1_R: 6803 samples base (ASYMMETRIC vs L's 7187), ±128 headroom.
        double d1Read_R;
        {
          mD1_R[mWrD1_R] = (float)ap1Out_R;
          int rD1 = mWrD1_R - kD1_R;
          if (rD1 < 0) rD1 += kD1_R_size;
          d1Read_R = (double)mD1_R[rD1];
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

        // D2_R: 6343 samples base (ASYMMETRIC vs L's 5101), ±128 headroom.
        double d2Read_R;
        {
          mD2_R[mWrD2_R] = (float)ap2Out_R;
          int rD2 = mWrD2_R - kD2_R;
          if (rD2 < 0) rD2 += kD2_R_size;
          d2Read_R = (double)mD2_R[rD2];
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

#endif
  };

} // namespace zaum
