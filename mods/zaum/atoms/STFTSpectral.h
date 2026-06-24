// zaum::STFTSpectral
//
// BUILD SUB-PHASE 0.2.0.1 — STFT identity passthrough (ShyFFT, N=1024,
// hop 256, sine window, COLA-verified transparent in the offline rig).
// SMD + spectral ops land in 0.2.0.2+. Inherent latency 1280 smp ≈ 26.7 ms.
//
// Pipeline per hop:
//   analysis ring → analysis window → ShyFFT::Direct → (identity: copy bins)
//   → ShyFFT::Inverse → synthesis window × 1/(2N) → overlap-add into synthesis ring
//
// ShyFFT API (verified by offline rig — atom must mirror this exactly):
//   fft.Direct(input, output)   — forward; input is CLOBBERED (used as scratch)
//   fft.Inverse(input, output)  — inverse; input is CLOBBERED; output scaled by N
//   Bin layout for bin k (1 ≤ k ≤ N/2−1):
//     real[k]      = output[k]
//     −imag_DFT[k] = output[N/2 + k]   (ShyFFT stores negated imaginary)
//   DC:      output[0]    = real only
//   Nyquist: output[N/2]  = real only
//   For polar ops (SMD phase): mag = sqrt(r²+i²), phase = atan2(−output[N/2+k], output[k])
//
// Normalization (matches Clouds stft.cc ShyFFT path):
//   IFFT scale = N  (no built-in 1/N in ShyFFT Inverse)
//   COLA sum   = 2.0  (sine window at 4× overlap, verified at −307 dB ripple)
//   Combined:  IFFT_NORM = 1/(2N) = 1/2048 applied per sample after Inverse
//   Equivalent Clouds formula: 2R/N² = 2×256/1024² = 1/2048 ✓
//
// Latency: 1280 samples = N + R = 1024 + 256.
//   Due to Clouds-style ring init: process_ptr starts at (2R) % (N+R) = 512.
//   Dry delay compensates exactly: dry ring also sized 1280, providing unity
//   at Mix=0; at Mix=1 the wet output is the STFT round-trip ≈ input delayed
//   by 1280 smp — the emu transparency gate.
//
// Internal-stereo: L and R each have independent analysis/synthesis rings and
// dry delay. One shared ShyFFT instance (Direct/Inverse are stateless per call;
// L and R are processed sequentially in process()).
//
// All 8 parameters are declared for a stable surface.
// Only Mix is DSP-wired this sub-phase; the remaining 7 are INERT stubs.

#pragma once

#include <od/config.h>
#include <od/objects/Object.h>
#include <math.h>
#include <string.h>

// NOTE: ShyFFT is a template-heavy header that SWIG cannot parse.
// Everything that references it — the include, the member, and process() —
// must be inside #ifndef SWIGLUA so the SWIG step never sees it.
// The addInput/addOutput/addParameter calls live outside the guard
// so SWIG can build the Lua bindings for the public surface.

#ifndef SWIGLUA
#include "stmlib/fft/shy_fft.h"
#endif

namespace zaum
{

  // ---------------------------------------------------------------------------
  // STFT framework constants (N=1024, R=256, 4× overlap / 75%)
  // ---------------------------------------------------------------------------
  static const int kStftN    = 1024;         // FFT / frame size
  static const int kStftR    = 256;          // hop size
  static const int kStftBuf  = kStftN + kStftR;   // ring buffer size (1280)
  // Inherent latency = kStftBuf = N + R samples (Clouds ring-buffer scheme).
  static const int kStftLat  = kStftBuf;    // 1280 samples ≈ 26.7 ms @ 48 kHz
  // Normalization: 1/(2N). Compensates ShyFFT Inverse scale of N plus COLA sum of 2.
  static const float kStftNorm = 1.0f / (2.0f * (float)kStftN);

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

      // All ring/scratch buffers to zero so startup produces silence.
      memset(mAnalysisL,  0, sizeof(mAnalysisL));
      memset(mAnalysisR,  0, sizeof(mAnalysisR));
      memset(mSynthesisL, 0, sizeof(mSynthesisL));
      memset(mSynthesisR, 0, sizeof(mSynthesisR));
      memset(mDryL,       0, sizeof(mDryL));
      memset(mDryR,       0, sizeof(mDryR));

#ifndef SWIGLUA
      // Compute the MLT sine window: w(n) = sin(pi*(n+0.5)/N)
      // Analysis × synthesis product = w² = Hann shape.
      // COLA sum at 4× overlap = 2.0 (analytically exact, rig verified).
      for (int n = 0; n < kStftN; ++n) {
        mWindow[n] = sinf((float)M_PI * ((float)n + 0.5f) / (float)kStftN);
      }

      // Initialize ShyFFT (builds trig tables and bit-reversal LUT).
      mFFT.Init();
#endif

      // Ring/state pointer init — mirrors Clouds STFT::Reset().
      mBufPtr    = 0;
      // process_ptr starts at (2*R) % BufSize = 512 (Clouds convention).
      // This provides N+R = 1280 samples inherent latency.
      mProcPtr   = (2 * kStftR) % kStftBuf;
      mBlockSize = 0;
      mReady     = 0;
      mDone      = 0;
      mDryPtr    = 0;
    }

    virtual ~STFTSpectral() {}

#ifndef SWIGLUA
    od::Inlet     mInL{"In L"};
    od::Inlet     mInR{"In R"};
    od::Outlet    mOutL{"Out L"};
    od::Outlet    mOutR{"Out R"};
    od::Parameter mDecay{"Decay",    0.5f};
    od::Parameter mDamp{"Damp",      0.3f};
    od::Parameter mDiffuse{"Diffuse", 0.4f};
    od::Parameter mFreeze{"Freeze",  0.0f};
    od::Parameter mBlur{"Blur",      0.0f};
    od::Parameter mBloom{"Bloom",    0.0f};
    od::Parameter mPredelay{"Predelay", 0.0f};
    od::Parameter mMix{"Mix",        0.4f};

    virtual void process()
    {
      float* in1  = mInL.buffer();
      float* in2  = mInR.buffer();
      float* out1 = mOutL.buffer();
      float* out2 = mOutR.buffer();

      // Read Mix at block rate (only wired DSP param this sub-phase).
      const float mix = mMix.value();
      const float dry = 1.0f - mix;

      // Process one FRAMELENGTH block.
      // The hop (R=256) typically spans multiple blocks (block=32 → 8 blocks per hop).
      // We accumulate samples until a hop is ready, then fire the FFT.
      for (int i = 0; i < FRAMELENGTH; ++i) {

        // --- Write input into analysis rings, read output from synthesis rings ---
        const float inSampleL = in1[i];
        const float inSampleR = in2[i];

        mAnalysisL[mBufPtr] = inSampleL;
        mAnalysisR[mBufPtr] = inSampleR;

        const float wetL = mSynthesisL[mBufPtr];
        const float wetR = mSynthesisR[mBufPtr];

        // --- Dry delay: compensates the 1280-sample inherent STFT latency ---
        // Write current input into the dry ring at the current dry write head,
        // then read the delayed sample that was written 1280 samples ago.
        // The dry ring is exactly kStftLat (1280) samples long.
        mDryL[mDryPtr] = inSampleL;
        mDryR[mDryPtr] = inSampleR;
        // Read the oldest sample in the ring (one full ring revolution ago).
        // Because the ring is exactly kStftLat long and we advance by 1 per sample,
        // the sample at (mDryPtr + 1) % kStftLat was written kStftLat samples ago.
        const int dryReadPtr = (mDryPtr + 1) % kStftLat;
        const float dryL = mDryL[dryReadPtr];
        const float dryR = mDryR[dryReadPtr];

        // Advance dry pointer
        ++mDryPtr;
        if (mDryPtr >= kStftLat) mDryPtr = 0;

        // --- Mix dry + wet ---
        out1[i] = dry * dryL + mix * wetL;
        out2[i] = dry * dryR + mix * wetR;

        // --- Advance buffer pointer ---
        ++mBufPtr;
        if (mBufPtr >= kStftBuf) mBufPtr = 0;

        // --- Accumulate toward next hop ---
        ++mBlockSize;
        if (mBlockSize >= kStftR) {
          mBlockSize = 0;
          ++mReady;
        }
      }

      // --- Process all pending hops (usually 0 or 1 per block) ---
      while (mReady != mDone) {
        processHop();
        ++mDone;
        mProcPtr += kStftR;
        if (mProcPtr >= kStftBuf) mProcPtr -= kStftBuf;
      }
    }

  private:
    // -------------------------------------------------------------------------
    // processHop(): one FFT→identity→IFFT→OLA cycle.
    // Called when a full hop (R samples) has accumulated in the analysis ring.
    // -------------------------------------------------------------------------
    void processHop()
    {
      // 1. Extract frame from analysis ring, apply analysis window.
      //    Read kStftN samples starting at mProcPtr (wrapping at kStftBuf).
      {
        int src = mProcPtr;
        for (int i = 0; i < kStftN; ++i) {
          mFftBufL[i] = mWindow[i] * mAnalysisL[src];
          mFftBufR[i] = mWindow[i] * mAnalysisR[src];
          ++src;
          if (src >= kStftBuf) src -= kStftBuf;
        }
      }

      // 2. Forward FFT — L channel.
      //    mFFT.Direct(input, output): input is CLOBBERED (used as internal scratch).
      //    mFftBufL is clobbered; mFftOutL receives the split-real spectrum.
      mFFT.Direct(mFftBufL, mFftOutL);

      // 3. IDENTITY processing (0.2.0.1 — no spectral modification yet).
      //    Copy spectrum unchanged from fft_out to ifft_in.
      //    SMD magnitude accumulator, phase synthesis, etc. replace this in 0.2.0.2+.
      //    mFftBufL is reused as ifft_in (safe: Direct has finished with it).
      for (int k = 0; k < kStftN; ++k) {
        mFftBufL[k] = mFftOutL[k];
      }

      // 4. Inverse FFT — L channel.
      //    mFFT.Inverse(input, output): input is CLOBBERED; output scaled by N.
      //    mFftBufL is clobbered; mIfftOutL receives time-domain signal ×N.
      mFFT.Inverse(mFftBufL, mIfftOutL);

      // 5. Apply synthesis window, normalize by 1/(2N), overlap-add into synthesis ring.
      //    OLA boundary: first (N−R) samples overlap with the previous frame's tail
      //    (accumulated via +=). The last R samples are new territory (overwrite).
      //    Getting this boundary wrong produces a click every R samples.
      {
        int dst = mProcPtr;
        for (int i = 0; i < kStftN; ++i) {
          const float s = mIfftOutL[i] * mWindow[i] * kStftNorm;
          if (i < kStftN - kStftR) {
            mSynthesisL[dst] += s;   // overlap-add region
          } else {
            mSynthesisL[dst]  = s;   // new region: overwrite stale content
          }
          ++dst;
          if (dst >= kStftBuf) dst -= kStftBuf;
        }
      }

      // --- Repeat for R channel ---

      // 2R. Forward FFT — R channel.
      mFFT.Direct(mFftBufR, mFftOutR);

      // 3R. Identity: copy spectrum.
      for (int k = 0; k < kStftN; ++k) {
        mFftBufR[k] = mFftOutR[k];
      }

      // 4R. Inverse FFT — R channel.
      mFFT.Inverse(mFftBufR, mIfftOutR);

      // 5R. Synthesis window + OLA — R channel.
      {
        int dst = mProcPtr;
        for (int i = 0; i < kStftN; ++i) {
          const float s = mIfftOutR[i] * mWindow[i] * kStftNorm;
          if (i < kStftN - kStftR) {
            mSynthesisR[dst] += s;
          } else {
            mSynthesisR[dst]  = s;
          }
          ++dst;
          if (dst >= kStftBuf) dst -= kStftBuf;
        }
      }
    }

    // -------------------------------------------------------------------------
    // Member declarations — inside #ifndef SWIGLUA so SWIG never parses them.
    // -------------------------------------------------------------------------

    // ShyFFT instance: one shared for L and R (stateless per call, sequential use).
    // ShyFFT<float, 1024, stmlib::RotationPhasor>: matches Clouds' non-ARM fallback.
    // DISALLOW_COPY_AND_ASSIGN is in ShyFFT — declare as member, not pointer.
    stmlib::ShyFFT<float, kStftN, stmlib::RotationPhasor> mFFT;

    // MLT sine window table (computed once in constructor).
    float mWindow[kStftN];

    // Analysis rings (input accumulation). Size = N + R = 1280.
    float mAnalysisL[kStftBuf];
    float mAnalysisR[kStftBuf];

    // Synthesis rings (OLA output accumulation). Size = N + R = 1280.
    float mSynthesisL[kStftBuf];
    float mSynthesisR[kStftBuf];

    // Dry delay lines (1280 samples = kStftLat, compensates inherent STFT latency).
    float mDryL[kStftLat];
    float mDryR[kStftLat];

    // FFT scratch buffers.
    // mFftBufL/R: windowed analysis frame → clobbered by Direct → reused as ifft_in.
    // mFftOutL/R: output of Direct (split-real spectrum).
    // mIfftOutL/R: output of Inverse (time-domain, scaled ×N).
    float mFftBufL[kStftN];
    float mFftBufR[kStftN];
    float mFftOutL[kStftN];
    float mFftOutR[kStftN];
    float mIfftOutL[kStftN];
    float mIfftOutR[kStftN];

    // Ring buffer state (mirrors Clouds STFT member names).
    int mBufPtr;      // write head for analysis ring / read head for synthesis ring
    int mProcPtr;     // frame extraction pointer (starts at 2R = 512)
    int mBlockSize;   // samples accumulated in current hop (0..R-1)
    int mReady;       // hops accumulated and waiting for FFT processing
    int mDone;        // hops already processed (mReady == mDone → nothing pending)
    int mDryPtr;      // write head for dry delay ring

#endif  // SWIGLUA
  };

}  // namespace zaum
