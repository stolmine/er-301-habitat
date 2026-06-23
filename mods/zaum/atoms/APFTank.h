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
// BUILD SUB-PHASE 0.1.0.2 — input diffusion + mono figure-8 tank.
//   Mono sum → predelay → 4-stage input diffusion → single L-loop
//   figure-8 tank with static delay reads (no modulation). Decay
//   hard-coded to g_d=0.85. HF damp disabled. No L↔R cross-couple
//   yet. Wet mono tap duplicated to Out L and Out R so the tail is
//   audible in stereo. DSP per sub-phase plan in fabula-design.md §9.
//
// INERT parameters this sub-phase (declared, Lua-tied, DSP-unused):
//   Size, Decay, Damp, Diffusion, Mod, ModRate.
// Wired parameters: Predelay, Mix.
//
// Modulation headroom already reserved on D1 and D2 delay buffers
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
// round trip on the recirculating feedback value. With densityA=1.0
// the output is bounded to [-1, +1]. Under normal use with g_d<1.0
// the saturator is inactive; it acts only as a hard wall against
// transient overloads or parameter edge cases.

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

  // Tank allpass buffers (plain Schroeder APF, L-loop only this sub-phase).
  // AP1: delay N=1087, g=0.70. AP2: delay N=1471, g=0.50.
  // Each buffer is exactly N samples; write head wraps at N.
  // Gardner inner delays kTA1i/kTA2i are reserved for future nesting
  // (deferred per fabula-design.md §6 — requires separate inner buffers).
  static const int kTA1  = 1087;   // AP1 delay (= buffer size)
  static const int kTA1i = 367;    // AP1 Gardner inner delay (UNUSED this phase)
  static const int kTA2  = 1471;   // AP2 delay (= buffer size)
  static const int kTA2i = 491;    // AP2 Gardner inner delay (UNUSED this phase)

  // Tank delay lines with modulation headroom.
  // D1: base 7187 + 128 head slack each end = 7187 + 256 = 7443.
  // D2: base 5101 + 128 head slack each end = 5101 + 256 = 5357.
  // Static reads this sub-phase use the center offset (base + 128).
  static const int kD1          = 7187;
  static const int kD1_headroom = 128;
  static const int kD1_size     = kD1 + 2 * kD1_headroom;   // 7443

  static const int kD2          = 5101;
  static const int kD2_headroom = 128;
  static const int kD2_size     = kD2 + 2 * kD2_headroom;   // 5357

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

      memset(mPD,  0, sizeof(mPD));
      memset(mID1, 0, sizeof(mID1));
      memset(mID2, 0, sizeof(mID2));
      memset(mID3, 0, sizeof(mID3));
      memset(mID4, 0, sizeof(mID4));
      memset(mTA1, 0, sizeof(mTA1));
      memset(mTA2, 0, sizeof(mTA2));
      memset(mD1,  0, sizeof(mD1));
      memset(mD2,  0, sizeof(mD2));

      mWrPD  = 0;
      mWrID1 = 0; mWrID2 = 0; mWrID3 = 0; mWrID4 = 0;
      mWrTA1 = 0; mWrTA2 = 0;
      mWrD1  = 0; mWrD2  = 0;

      mFeedback = 0.0;
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
        // 4. L-loop: mono figure-8 tank (no cross-couple this sub-phase).
        //
        //    tankIn accumulates diffused input + decayed feedback.
        //    Signal path: tankIn → AP1 → D1 → AP2 → D2 → ×g_d → mFeedback.
        //
        //    Plain Schroeder allpass (unity-gain by construction):
        //      vDelayed = buf[w - N]        // read BEFORE write
        //      vNew     = x + g * vDelayed
        //      yOut     = -g * vNew + vDelayed
        //      buf[w]   = vNew ; w = (w+1) % N
        //    Magnitude response |H(e^jw)| = 1 for all w when |g| < 1.
        //    This is provably unity-gain; the previously attempted Gardner
        //    nested form had mismatched tap/coefficient pairing that produced
        //    feedback-comb peak gain 1/(1-g_inner)=2 at inner resonances,
        //    pushing loop gain well above 1 with g_d=0.85.
        //
        //    Modulation headroom: D1 base read is kD1 samples behind write
        //    head, centered in the headroom window. Same for D2.
        // ----------------------------------------------------------------

        // Accumulate: new diffusion input + previous round-trip feedback.
        double tankIn = diffIn + mFeedback;

        // -- AP1: plain Schroeder allpass, delay=kTA1=1087, g=gTA1=0.70 --
        double ap1Out;
        {
          // Read kTA1 samples behind write head (= oldest slot, buffer is
          // exactly kTA1 deep). Reading BEFORE the write is mandatory for
          // correct Schroeder form; the (w - kTA1 + kTA1) % kTA1 = w case
          // means rD wraps to mWrTA1 itself — that slot holds the sample
          // written kTA1 iterations ago, i.e. v[n-kTA1]. Correct.
          double vD = (double)mTA1[mWrTA1];   // buf[w] is oldest; same as (w - N + N) % N
          double vNew, yOut;
          house::allpassNestedStep(tankIn, vD, gTA1, vNew, yOut);
          mTA1[mWrTA1] = (float)vNew;
          mWrTA1++;
          if (mWrTA1 >= kTA1) mWrTA1 = 0;
          ap1Out = yOut;
        }

        // -- D1: tank delay line (7187 samples base, +128 headroom each side) --
        // Static center read this sub-phase: kD1 samples behind write head.
        double d1Read;
        {
          mD1[mWrD1] = (float)ap1Out;
          int rD1 = mWrD1 - kD1;
          if (rD1 < 0) rD1 += kD1_size;
          d1Read = (double)mD1[rD1];
          mWrD1++;
          if (mWrD1 >= kD1_size) mWrD1 = 0;
        }

        // HF damp: DISABLED this sub-phase. Pass d1Read through unchanged.
        double dampedD1 = d1Read;

        // -- AP2: plain Schroeder allpass, delay=kTA2=1471, g=gTA2=0.50 --
        double ap2Out;
        {
          double vD = (double)mTA2[mWrTA2];   // oldest slot = v[n-kTA2]
          double vNew, yOut;
          house::allpassNestedStep(dampedD1, vD, gTA2, vNew, yOut);
          mTA2[mWrTA2] = (float)vNew;
          mWrTA2++;
          if (mWrTA2 >= kTA2) mWrTA2 = 0;
          ap2Out = yOut;
        }

        // -- D2: tank delay line (5101 samples base, +128 headroom each side) --
        double d2Read;
        {
          mD2[mWrD2] = (float)ap2Out;
          int rD2 = mWrD2 - kD2;
          if (rD2 < 0) rD2 += kD2_size;
          d2Read = (double)mD2[rD2];
          mWrD2++;
          if (mWrD2 >= kD2_size) mWrD2 = 0;
        }

        // Apply decay gain once per round trip, then pass through the Spiral
        // feedback governor (fabula-design.md §4, pulled forward from 0.1.0.6).
        // With densityA=1.0 the output is bounded to [-1, +1]. For a clean room
        // with g_d=0.85 the saturator is never active under normal use — the
        // true Schroeder allpasses in AP1/AP2 guarantee passive stability and
        // the governor only engages as a hard wall against transient overloads
        // or future high-regen parameter settings. No coloration in clean use.
        mFeedback = house::spiralFastSaturate(d2Read * g_d, 1.0);

        // ----------------------------------------------------------------
        // 5. Wet tap: sum of D1 and D2 reads, scaled to unit range.
        //    Both taps carry decorrelated versions of the tank signal;
        //    summing and halving keeps the wet level comparable to dry.
        // ----------------------------------------------------------------
        double wet = (d1Read + d2Read) * 0.5;

        // ----------------------------------------------------------------
        // 6. Dry/wet mix. Mono wet duplicated to L and R outputs so the
        //    tail is audible in stereo (cross-couple arrives in 0.1.0.3).
        // ----------------------------------------------------------------
        double dryMix  = (double)(1.0f - mix);
        double wetMix  = (double)mix;
        double outL = drySampleL * dryMix + wet * wetMix;
        double outR = drySampleR * dryMix + wet * wetMix;

        *out1 = (float)outL;
        *out2 = (float)outR;
        in1++; in2++; out1++; out2++;
      }
    }

  private:
    // Predelay buffer (power-of-two for & wrap)
    float mPD[kPD];
    int   mWrPD;

    // Input diffusion allpass buffers (4 series, mono)
    float mID1[kID1];
    float mID2[kID2];
    float mID3[kID3];
    float mID4[kID4];
    int   mWrID1, mWrID2, mWrID3, mWrID4;

    // Tank allpass buffers (L-loop, AP1 and AP2; plain Schroeder APF,
    // each buffer is exactly N samples deep, write head wraps at N)
    float mTA1[kTA1];
    float mTA2[kTA2];
    int   mWrTA1, mWrTA2;

    // Tank delay lines with modulation headroom
    float mD1[kD1_size];
    float mD2[kD2_size];
    int   mWrD1, mWrD2;

    // Recirculating feedback accumulator (double precision: this value
    // traverses the full round-trip path each sample; precision matters
    // for long-decay tails where accumulated rounding would drift pitch)
    double mFeedback;

#endif
  };

} // namespace zaum
