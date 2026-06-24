// zaum::STFTSpectral
//
// BUILD SUB-PHASE 0.2.0.4 — continuous Freeze (g→1, inputGain→0).
// ShyFFT N=1024, hop R=256, sine window. Inherent latency 1280 smp ≈ 26.7 ms.
//
// What changed from 0.2.0.3:
//   FREEZE: continuous 0..1 parameter that simultaneously blends g toward 1
//   and fades the incoming input toward 0, so the captured spectrum holds
//   indefinitely without growing without bound.
//
//   SMD accumulate step (per complex bin, replacing 0.2.0.3 line):
//     gEff      = g_k + freeze * (1.0f - g_k)   // lerp g_k → 1 as freeze rises
//     inputGain = 1.0f - freeze                  // fade fresh input toward 0
//     M_out[k]  = inputGain * mag + M_out[k] * gEff
//     (clamp M_out[k] ≤ kMagClamp as before)
//
//   freeze=0: gEff=g_k, inputGain=1 → bit-identical to 0.2.0.3. No change.
//   freeze=1: gEff=1,   inputGain=0 → M_out[k] = M_out[k]. Infinite hold.
//   mid-freeze: partial input + near-infinite decay → smooth lengthening tail.
//
//   Damp interaction: gEff uses the per-bin Damp-tilted g_k. At freeze=1 all
//   bins reach gEff=1 regardless of Damp — freeze wins unconditionally. Correct.
//
//   Diffuse interaction: the phase path (phi = phase + V*xi) is UNCHANGED and
//   still runs every hop during freeze. Frozen + Diffuse>0 → the magnitude is
//   held but the phase re-randomizes each hop → shimmering, evolving drone.
//   Frozen + Diffuse=0 → static tonal hold. Both are expected and correct.
//
//   DC and Nyquist also get the Freeze lerp (same formula, no phase path).
//   The same kMagClamp backstop applies.
//
// SMD pipeline per hop (full, with Decay, Damp, Diffuse, Freeze):
//   analysis ring → window → ShyFFT::Direct
//   → per bin: mag = |X[k]|, phase = atan2(-nim, re)
//              gEff = g_k + freeze*(1-g_k);  inputGain = 1-freeze
//              M_out[k] = inputGain*mag + M_out[k]*gEff  (clamped)
//              phi = phase + V * xi_k
//              out = M_out[k] * e^{j*phi}
//   → ShyFFT::Inverse → window × 1/(2N) → overlap-add
//
// ShyFFT packing, normalization, PRNG, OLA — all unchanged from 0.2.0.3.
// Wired params: Decay, Damp, Diffuse, Freeze, Mix. Blur/Bloom/Predelay INERT.

#pragma once

#include <od/config.h>
#include <od/objects/Object.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

#ifndef SWIGLUA
#include "stmlib/fft/shy_fft.h"
#endif

namespace zaum
{

  // ---------------------------------------------------------------------------
  // STFT framework constants
  // ---------------------------------------------------------------------------
  static const int   kStftN    = 1024;
  static const int   kStftR    = 256;
  static const int   kStftBuf  = kStftN + kStftR;
  static const int   kStftLat  = kStftBuf;
  static const float kStftNorm = 1.0f / (2.0f * (float)kStftN);
  static const int   kStftBins = kStftN / 2 + 1;   // 513

  // ---------------------------------------------------------------------------
  // SMD + Damp tuning constants
  // ---------------------------------------------------------------------------
  static const double kRT60Min  = 0.3;
  static const double kRT60Max  = 120.0;
  static const double kGMax     = 0.9999;
  static const float  kMagClamp = 32.0f;

  // Damp: per-bin g multiplier per bin index.
  //   g_k = g_base * damp_factor^k
  //   damp_factor = pow(kDampFloor, Damp)
  //   Damp=0  → damp_factor=1.0  → flat decay across all bins (no tilt)
  //   Damp=1  → damp_factor=kDampFloor → HF bins decay much faster than LF
  //
  // kDampFloor: the damp_factor at Damp=1.
  // Must satisfy: damp_factor^(N/2) gives a reasonable HF rolloff at Damp=1.
  // At N/2=512 bins (Nyquist), damp_factor^512 = kDampFloor^512.
  // We want g_Nyquist / g_DC ≈ a large ratio (e.g. HF decays ~10× faster).
  // kDampFloor = 0.99: damp_factor^512 = 0.99^512 ≈ 0.006 → ~44 dB faster decay at Nyq.
  // This is a strong tilt; at Damp=0.5: factor=0.99^0.5≈0.995, ^512≈0.075 → ~22 dB.
  // PRIMARY TUNING KNOB: adjust kDampFloor for darker/brighter maximum tilt.
  static const double kDampFloor = 0.99;

  class STFTSpectral : public od::Object
  {
  public:
    STFTSpectral()
    {
      addInput(mInL);
      addInput(mInR);
      addOutput(mOutL);
      addOutput(mOutR);
      addParameter(mDecay);
      addParameter(mDamp);
      addParameter(mDiffuse);
      addParameter(mFreeze);
      addParameter(mBlur);
      addParameter(mBloom);
      addParameter(mPredelay);
      addParameter(mMix);

      memset(mAnalysisL,  0, sizeof(mAnalysisL));
      memset(mAnalysisR,  0, sizeof(mAnalysisR));
      memset(mSynthesisL, 0, sizeof(mSynthesisL));
      memset(mSynthesisR, 0, sizeof(mSynthesisR));
      memset(mDryL,       0, sizeof(mDryL));
      memset(mDryR,       0, sizeof(mDryR));

#ifndef SWIGLUA
      for (int n = 0; n < kStftN; ++n) {
        mWindow[n] = sinf((float)M_PI * ((float)n + 0.5f) / (float)kStftN);
      }

      mFFT.Init();

      memset(mMagAccL, 0, sizeof(mMagAccL));
      memset(mMagAccR, 0, sizeof(mMagAccR));
#endif

      mBufPtr    = 0;
      mProcPtr   = (2 * kStftR) % kStftBuf;
      mBlockSize = 0;
      mReady     = 0;
      mDone      = 0;
      mDryPtr    = 0;
      mG         = 0.0f;
      mV         = 0.4f;
      mDampFactor = 1.0f;
      mFreezeAmt = 0.0f;

      // PRNG seeds: L and R use distinct non-zero constants so the two channels
      // generate independent xi sequences from sample 0 → stereo decorrelation.
      // Seed neighborhoods chosen to be far apart in 64-bit state space.
      mPrngL = UINT64_C(0x9E3779B97F4A7C15);   // golden-ratio constant
      mPrngR = UINT64_C(0xBF58476D1CE4E5B9);   // splitmix64 constant
    }

    virtual ~STFTSpectral() {}

#ifndef SWIGLUA
    od::Inlet     mInL{"In L"};
    od::Inlet     mInR{"In R"};
    od::Outlet    mOutL{"Out L"};
    od::Outlet    mOutR{"Out R"};
    od::Parameter mDecay{"Decay",     0.5f};
    od::Parameter mDamp{"Damp",       0.3f};
    od::Parameter mDiffuse{"Diffuse", 0.4f};
    od::Parameter mFreeze{"Freeze",   0.0f};
    od::Parameter mBlur{"Blur",       0.0f};
    od::Parameter mBloom{"Bloom",     0.0f};
    od::Parameter mPredelay{"Predelay", 0.0f};
    od::Parameter mMix{"Mix",         0.4f};

    virtual void process()
    {
      float* in1  = mInL.buffer();
      float* in2  = mInR.buffer();
      float* out1 = mOutL.buffer();
      float* out2 = mOutR.buffer();

      const float mix   = mMix.value();
      const float dry   = 1.0f - mix;
      const float decay = mDecay.value();
      const float damp  = mDamp.value();
      mV      = mDiffuse.value();   // cached for processHop
      mFreezeAmt = mFreeze.value();   // 0..1, cached for processHop

      // Decay → RT60 → g_base (uniform base decay, same as 0.2.0.2).
      {
        const double fs   = (double)globalConfig.sampleRate;
        const double rt60 = kRT60Min * pow(kRT60Max / kRT60Min, (double)decay);
        double g = pow(10.0, -3.0 * (double)kStftR / (rt60 * fs));
        if (g > kGMax) g = kGMax;
        mG = (float)g;
      }

      // Damp → per-bin tilt factor.
      // damp_factor = pow(kDampFloor, Damp):  Damp=0→1.0 (flat), Damp=1→kDampFloor.
      // g_k = g_base * damp_factor^k, computed by running multiplication in the bin loop.
      // pow() called once per block (block rate), not per bin.
      {
        mDampFactor = (float)pow(kDampFloor, (double)damp);
      }

      // Sample loop: ring I/O + dry delay + mix.
      for (int i = 0; i < FRAMELENGTH; ++i) {
        const float inL = in1[i];
        const float inR = in2[i];

        mAnalysisL[mBufPtr] = inL;
        mAnalysisR[mBufPtr] = inR;

        const float wetL = mSynthesisL[mBufPtr];
        const float wetR = mSynthesisR[mBufPtr];

        mDryL[mDryPtr] = inL;
        mDryR[mDryPtr] = inR;
        const int   dryRead = (mDryPtr + 1) % kStftLat;
        const float dryL    = mDryL[dryRead];
        const float dryR    = mDryR[dryRead];
        ++mDryPtr;
        if (mDryPtr >= kStftLat) mDryPtr = 0;

        out1[i] = dry * dryL + mix * wetL;
        out2[i] = dry * dryR + mix * wetR;

        ++mBufPtr;
        if (mBufPtr >= kStftBuf) mBufPtr = 0;

        ++mBlockSize;
        if (mBlockSize >= kStftR) {
          mBlockSize = 0;
          ++mReady;
        }
      }

      while (mReady != mDone) {
        processHop();
        ++mDone;
        mProcPtr += kStftR;
        if (mProcPtr >= kStftBuf) mProcPtr -= kStftBuf;
      }
    }

  private:
    void processHop()
    {
      // 1. Windowed analysis frame.
      {
        int src = mProcPtr;
        for (int i = 0; i < kStftN; ++i) {
          mFftBufL[i] = mWindow[i] * mAnalysisL[src];
          mFftBufR[i] = mWindow[i] * mAnalysisR[src];
          ++src;
          if (src >= kStftBuf) src -= kStftBuf;
        }
      }

      // 2. Forward FFT.
      mFFT.Direct(mFftBufL, mFftOutL);
      mFFT.Direct(mFftBufR, mFftOutR);

      // 3. SMD + Diffuse + Damp — L and R channels.
      //    Independent PRNG state per channel → decorrelated stereo phase noise.
      smdProcess(mFftOutL, mFftBufL, mMagAccL, mG, mV, mDampFactor, mFreezeAmt, mPrngL);
      smdProcess(mFftOutR, mFftBufR, mMagAccR, mG, mV, mDampFactor, mFreezeAmt, mPrngR);

      // 4. Inverse FFT.
      mFFT.Inverse(mFftBufL, mIfftOutL);
      mFFT.Inverse(mFftBufR, mIfftOutR);

      // 5. Synthesis window + 1/(2N) + overlap-add.
      {
        int dst = mProcPtr;
        for (int i = 0; i < kStftN; ++i) {
          const float sL = mIfftOutL[i] * mWindow[i] * kStftNorm;
          const float sR = mIfftOutR[i] * mWindow[i] * kStftNorm;
          if (i < kStftN - kStftR) {
            mSynthesisL[dst] += sL;
            mSynthesisR[dst] += sR;
          } else {
            mSynthesisL[dst]  = sL;
            mSynthesisR[dst]  = sR;
          }
          ++dst;
          if (dst >= kStftBuf) dst -= kStftBuf;
        }
      }
    }

    // -------------------------------------------------------------------------
    // smdProcess(): SMD accumulate + Damp tilt + Diffuse phase randomization.
    //
    // spectrum[]:  ShyFFT Direct output (N floats).
    // ifft_in[]:   repacked spectrum for Inverse.
    // magAcc[]:    per-bin magnitude accumulator (kStftBins floats, persistent).
    // g:           base decay factor (uniform pre-tilt).
    // V:           phase randomization depth 0..1.
    // dampFactor:  per-bin g tilt multiplier (1.0=flat, <1.0=HF darker).
    // prng:        per-channel xorshift64 state (updated in place → decorrelated L/R).
    // -------------------------------------------------------------------------
    void smdProcess(const float* spectrum, float* ifft_in,
                    float* magAcc, float g, float V,
                    float dampFactor, float freeze, uint64_t& prng)
    {
      // DC (k=0): real-only. No phase randomization. g_base used (no Damp tilt at DC).
      {
        const float re  = spectrum[0];
        const float mag = (re >= 0.0f) ? re : -re;
        const float gEff = g + freeze * (1.0f - g);
        float m = (1.0f - freeze) * mag + magAcc[0] * gEff;
        if (m > kMagClamp) m = kMagClamp;
        magAcc[0] = m;
        ifft_in[0] = (re >= 0.0f) ? m : -m;
      }

      // Complex bins k=1..N/2-1: SMD + Damp tilt + Diffuse phase randomization.
      // g_k = g_base * dampFactor^k, accumulated by running multiplication.
      // Starting with dampFactor^1 at k=1.
      float g_k = g * dampFactor;   // g for k=1; multiplied by dampFactor each step

      for (int k = 1; k < kStftN / 2; ++k) {
        // Clamp per-bin g to [0, kGMax] (dampFactor<1 can only reduce g, but
        // clamp anyway for safety in case of parameter edge cases).
        const float gk = (g_k < (float)kGMax) ? g_k : (float)kGMax;

        const float re  = spectrum[k];
        const float nim = spectrum[kStftN / 2 + k];   // nim = -imag_DFT[k]

        // RectToPolar.
        const float mag   = sqrtf(re * re + nim * nim);
        const float phase = atan2f(-nim, re);   // standard DFT phase

        // SMD accumulate with per-bin g and Freeze blend.
        // gkEff lerps gk→1 and the input term fades out as freeze→1, so
        // freeze=1 holds the spectrum (M=M) bounded; freeze=0 is identical.
        const float gkEff = gk + freeze * (1.0f - gk);
        float m = (1.0f - freeze) * mag + magAcc[k] * gkEff;
        if (m > kMagClamp) m = kMagClamp;
        magAcc[k] = m;

        // Diffuse: per-bin phase randomization.
        // Draw xi ~ uniform[-pi, pi] from the running PRNG.
        // V=0 → phi=phase (coherent, identical to 0.2.0.2).
        // V=1 → phi = phase + full-range random → decorrelated noise tail.
        // V=0.4 (default) → lush diffuse reverb, the Sujet sweet spot.
        // xorshift64 inline (Marsaglia 2003) — avoids ODR collision with APFTank.h
        prng ^= prng << 13; prng ^= prng >> 7; prng ^= prng << 17;
        // Map upper 24 bits → float [-pi, pi)
        const float xi = ((float)(prng >> 40) * (1.0f / 8388608.0f) - 1.0f) * (float)M_PI;
        const float phi = phase + V * xi;

        // PolarToRect + repack in ShyFFT convention.
        // ShyFFT spectrum[N/2+k] stores -imag_DFT, so we store m*sin(phi)
        // (sin of the output phase, which equals -(-imag)/mag when phi=phase).
        ifft_in[k]               = m * cosf(phi);
        ifft_in[kStftN / 2 + k] = m * sinf(phi);

        // Advance per-bin g tilt for next bin.
        g_k *= dampFactor;
      }

      // Nyquist (k=N/2): real-only. No phase randomization. g_base used.
      {
        const int   kNyq = kStftN / 2;
        const float re   = spectrum[kNyq];
        const float mag  = (re >= 0.0f) ? re : -re;
        const float gEff = g + freeze * (1.0f - g);
        float m = (1.0f - freeze) * mag + magAcc[kNyq] * gEff;
        if (m > kMagClamp) m = kMagClamp;
        magAcc[kNyq] = m;
        ifft_in[kNyq] = (re >= 0.0f) ? m : -m;
      }
    }

    // -------------------------------------------------------------------------
    // Members
    // -------------------------------------------------------------------------

    stmlib::ShyFFT<float, kStftN, stmlib::RotationPhasor> mFFT;

    float mWindow[kStftN];

    float mAnalysisL[kStftBuf];
    float mAnalysisR[kStftBuf];
    float mSynthesisL[kStftBuf];
    float mSynthesisR[kStftBuf];

    float mDryL[kStftLat];
    float mDryR[kStftLat];

    float mFftBufL[kStftN];
    float mFftBufR[kStftN];
    float mFftOutL[kStftN];
    float mFftOutR[kStftN];
    float mIfftOutL[kStftN];
    float mIfftOutR[kStftN];

    // SMD magnitude accumulators — persistent reverb tail state.
    float mMagAccL[kStftBins];
    float mMagAccR[kStftBins];

    // xorshift64 PRNG state — one per channel for independent stereo phase noise.
    uint64_t mPrngL;
    uint64_t mPrngR;

    // Ring/hop state.
    int   mBufPtr;
    int   mProcPtr;
    int   mBlockSize;
    int   mReady;
    int   mDone;
    int   mDryPtr;

    // Block-rate cached parameters.
    float mG;           // base decay factor from Decay param
    float mV;           // phase randomization depth from Diffuse param
    float mDampFactor;  // per-bin g tilt multiplier from Damp param
    float mFreezeAmt;   // freeze amount 0..1 from Freeze param

#endif  // SWIGLUA
  };

}  // namespace zaum
