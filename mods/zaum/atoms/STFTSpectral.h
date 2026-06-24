// zaum::STFTSpectral
//
// BUILD SUB-PHASE 0.2.0.10 — Bloom: per-bin asymmetric IIR slow-rise/fast-fall
//   + frequency stagger; chains after Blur (magAcc → blurState → bloomState → synth).
//
//   0.2.0.9 Blur (cross-time symmetric IIR) unchanged. This adds a second per-bin
//   IIR stage with ASYMMETRIC coefficients: slow rise (controlled by Bloom param,
//   freq-staggered so HF builds in later), instant fall (kBloomAlphaFall = 0).
//
//   Per-bin rise-alpha table (recomputed only when Bloom param changes):
//     mBloomStagger[k] = 1 + kBloomStagger * k/(N/2)    // 1.0 at DC, 4.0 at Nyquist
//     mBloomAlphaRise[k] = 1 - exp(-6 * Bloom * mBloomStagger[k])
//   → HF bins have larger alpha_rise → slower rise → bloom in later → brightening swell.
//
//   Per-hop update (feed from blurState[k]; fall = snap; rise = freq-staggered slow):
//     if blurState[k] >= bloomState[k]: rise path (slow)
//       bloomState[k] = mBloomAlphaRise[k]*bloomState[k] + (1-mBloomAlphaRise[k])*blurState[k]
//     else: fall path (fast)
//       bloomState[k] = kBloomAlphaFall*bloomState[k]   + (1-kBloomAlphaFall)*blurState[k]
//   Synth reads bloomState[k]. magAcc and blurState are UNTOUCHED by Bloom.
//
//   Bloom=0 → mBloomAlphaRise[k]=0, kBloomAlphaFall=0 → bloomState[k]=blurState[k]
//   every hop → bit-identical to 0.2.0.9.
//
//   Three-pass smdProcess structure (0.2.0.10):
//   1. Accumulate pass (k=0..N/2): SMD+Freeze+Damp into magAcc[k]; update
//      blurState[k] via symmetric IIR; store base phase in phaseScratch[k].
//   2. Bloom pass (k=0..N/2): asymmetric IIR from blurState[k] into bloomState[k].
//   3. Synth pass (k=0..N/2): synthesize from bloomState[k] + stored phase + PRNG xi.
//
//   Below: 0.2.0.7 spiral wet-output governor (clip/runaway safety).
//   0.2.0.6 Freeze fix (decay extension + floored input).
//   0.2.0.4 made freeze extend the decay (gEff→1) → gorgeous long tails, BUT
//   zeroed the input at freeze=1 → starved to near-silence ("dry"). 0.2.0.5's
//   snapshot/hold rewrite fixed the dry but REMOVED the decay extension → lost
//   the tail (short metallic ring). This restores the decay extension and only
//   FLOORS the input (kFreezeInFloor) so freeze=1 keeps filling + holds forever:
//     gkEff  = gk + freeze*(1-gk)                  // decay → infinite at 1
//     inGain = max(1-freeze, kFreezeInFloor)       // never starve the input
//     M[k]   = inGain*mag + M[k]*gkEff             // (magnitude-clamped)
//   freeze=0 = plain reverb; rising = ever-longer tail; 1 = infinite sustain.
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
// SMD pipeline per hop (full, with Decay, Damp, Diffuse, Freeze, Blur, Bloom):
//   analysis ring → window → ShyFFT::Direct
//   → pass 1: per bin: mag = |X[k]|, phase = atan2(-nim, re)
//              gEff = g_k + freeze*(1-g_k);  inputGain = max(1-freeze, floor)
//              M_out[k] = inputGain*mag + M_out[k]*gEff  (clamped)
//              blurState[k] = alpha*blurState[k] + (1-alpha)*M_out[k]   // symmetric IIR
//              phaseScratch[k] = phase (or sign for DC/Nyquist)
//   → pass 2: per bin: asymmetric IIR bloomState[k] from blurState[k]
//              rise (blurState>=bloomState): bloomState[k] = alphaRise[k]*bloomState[k] + (1-alphaRise[k])*blurState[k]
//              fall (blurState<bloomState):  bloomState[k] = kBloomAlphaFall*bloomState[k] + (1-kBloomAlphaFall)*blurState[k]
//   → pass 3: per bin: phi = phaseScratch[k] + V * xi_k (complex); or sign*bloomState (DC/Nyq)
//              out = bloomState[k] * e^{j*phi}
//   → ShyFFT::Inverse → window × 1/(2N) → overlap-add
//
// ShyFFT packing, normalization, PRNG, OLA — all unchanged from 0.2.0.3.
// Wired params: Decay, Damp, Diffuse, Freeze, Blur, Bloom, Mix. Predelay INERT.

#pragma once

#include <od/config.h>
#include <od/objects/Object.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

#ifndef SWIGLUA
#include "stmlib/fft/shy_fft.h"
#include <Spiral.h>   // house::spiralFastSaturate — wet output governor
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
  // Wet output governor: Spiral soft-saturator on the wet signal, bounding it
  // to [-1/kGovDensity, 1/kGovDensity]. Near-linear below ~0.8/kGovDensity,
  // so transparent at normal levels; only catches the magnitude-accumulator
  // build-up at high Freeze/Decay (prevents output clipping / runaway). Hoisted.
  static const double kGovDensity = 1.0;
  // Freeze input floor: at freeze=1 the input gain is held at this value (not 0)
  // so the spectrum keeps filling and HOLDS (infinite sustain) instead of
  // starving to silence. Hoisted for ear-tuning.
  static const float  kFreezeInFloor = 0.15f;

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

  // Bloom: per-bin asymmetric IIR — slow rise (freq-staggered), fast fall.
  //   mBloomStagger[k] = 1 + kBloomStagger * k/(N/2)
  //   → DC rise multiplier = 1.0×, Nyquist = (1 + kBloomStagger)×
  //   HF bins get a larger alpha_rise → slower rise → bloom in later → brightening swell.
  static const float  kBloomStagger  = 3.0f;    // HF rise time up to 4× LF
  static const float  kBloomAlphaFall = 0.0f;   // snap release: fall tracks magAcc/blurState immediately

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

      memset(mAnalysisL,    0, sizeof(mAnalysisL));
      memset(mAnalysisR,    0, sizeof(mAnalysisR));
      memset(mSynthesisL,   0, sizeof(mSynthesisL));
      memset(mSynthesisR,   0, sizeof(mSynthesisR));
      memset(mDryL,         0, sizeof(mDryL));
      memset(mDryR,         0, sizeof(mDryR));

#ifndef SWIGLUA
      for (int n = 0; n < kStftN; ++n) {
        mWindow[n] = sinf((float)M_PI * ((float)n + 0.5f) / (float)kStftN);
      }

      mFFT.Init();

      memset(mMagAccL,      0, sizeof(mMagAccL));
      memset(mMagAccR,      0, sizeof(mMagAccR));
      memset(mPhaseScratch, 0, sizeof(mPhaseScratch));
      memset(mBlurStateL,   0, sizeof(mBlurStateL));
      memset(mBlurStateR,   0, sizeof(mBlurStateR));
      memset(mBloomStateL,  0, sizeof(mBloomStateL));
      memset(mBloomStateR,  0, sizeof(mBloomStateR));

      // Pre-compute the per-bin stagger multiplier (static, never changes).
      // mBloomStagger[k] = 1.0 + kBloomStagger * k/(N/2)
      // → DC=1.0, Nyquist=1+kBloomStagger=4.0. HF rises up to 4× slower than DC.
      for (int k = 0; k <= kStftN / 2; ++k) {
        mBloomStagger[k] = 1.0f + kBloomStagger * (float)k / (float)(kStftN / 2);
      }

      // Bloom alpha-rise table initialised to 0 (Bloom=0 passthrough).
      // Recomputed lazily in process() when Bloom param changes.
      memset(mBloomAlphaRise, 0, sizeof(mBloomAlphaRise));
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
      mBlurAmt   = 0.0f;
      mBlurAlpha = 0.0f;
      mBloomAmt  = 0.0f;
      mLastBloom = -1.0f;   // force table recompute on first block

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
      mV         = mDiffuse.value();   // cached for processHop
      mFreezeAmt = mFreeze.value();    // 0..1, cached for processHop
      mBlurAmt   = mBlur.value();      // 0..1, cached for processHop
      // Cross-time IIR alpha: 0→0 (passthrough), 0.5→0.950 (~100ms), 1→0.998
      mBlurAlpha = 1.0f - expf(-6.0f * mBlurAmt);

      // Bloom: per-bin asymmetric IIR rise-alpha table, lazily recomputed.
      // 513 expf calls only when Bloom param actually changes.
      mBloomAmt = mBloom.value();
      if (mBloomAmt != mLastBloom) {
        for (int k = 0; k <= kStftN / 2; ++k) {
          mBloomAlphaRise[k] = 1.0f - expf(-6.0f * mBloomAmt * mBloomStagger[k]);
        }
        mLastBloom = mBloomAmt;
      }

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

        // Spiral governor on the wet only (dry stays clean), so the SMD
        // accumulator build-up at high Freeze/Decay can't blow up the output.
        const float wetSatL = (float)house::spiralFastSaturate((double)wetL, kGovDensity);
        const float wetSatR = (float)house::spiralFastSaturate((double)wetR, kGovDensity);
        out1[i] = dry * dryL + mix * wetSatL;
        out2[i] = dry * dryR + mix * wetSatR;

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

      // 3. SMD + Diffuse + Damp + Blur — L and R channels.
      //    Independent PRNG state per channel → decorrelated stereo phase noise.
      smdProcess(mFftOutL, mFftBufL, mMagAccL, mBlurStateL, mBloomStateL, mG, mV, mDampFactor, mFreezeAmt, mBlurAlpha, mBloomAlphaRise, mPrngL);
      smdProcess(mFftOutR, mFftBufR, mMagAccR, mBlurStateR, mBloomStateR, mG, mV, mDampFactor, mFreezeAmt, mBlurAlpha, mBloomAlphaRise, mPrngR);

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
    // smdProcess(): three-pass SMD accumulate + Blur + Bloom + Synth.
    //
    // Pass 1 — Accumulate+Blur: compute mag, SMD+Freeze+Damp into magAcc[k],
    //           update blurState[k] via symmetric IIR, store base phase.
    // Pass 2 — Bloom: asymmetric IIR from blurState[k] into bloomState[k].
    //           Rise path: slow, freq-staggered (alphaRise[k] larger at HF → HF later).
    //           Fall path: kBloomAlphaFall=0 → instant (snap to blurState).
    // Pass 3 — Synth: synthesize ifft_in from bloomState[k] + stored phase + PRNG xi.
    //
    // spectrum[]:    ShyFFT Direct output (N floats).
    // ifft_in[]:     repacked spectrum for Inverse.
    // magAcc[]:      per-bin magnitude accumulator (kStftBins floats, persistent).
    // blurState[]:   per-bin symmetric IIR state (kStftBins floats, persistent).
    // bloomState[]:  per-bin asymmetric IIR state (kStftBins floats, persistent).
    // g:             base decay factor (uniform pre-tilt).
    // V:             phase randomization depth 0..1.
    // dampFactor:    per-bin g tilt multiplier (1.0=flat, <1.0=HF darker).
    // freeze:        freeze amount 0..1.
    // blurAlpha:     symmetric IIR coefficient (Blur); 0→passthrough.
    // bloomAlphaRise[]: per-bin asymmetric rise coefficients (shared L/R, block-rate).
    // prng:          per-channel xorshift64 state (updated in place → decorrelated L/R).
    //
    // Bloom=0 → bloomAlphaRise[k]=0 + kBloomAlphaFall=0 → bloomState[k]=blurState[k]
    // every hop → bit-identical to 0.2.0.9.
    // magAcc and blurState are NEVER modified by the Bloom path.
    // PRNG draw order (one xi per complex bin k=1..N/2-1) unchanged.
    // -------------------------------------------------------------------------
    void smdProcess(const float* spectrum, float* ifft_in,
                    float* magAcc, float* blurState, float* bloomState,
                    float g, float V, float dampFactor, float freeze,
                    float blurAlpha, const float* bloomAlphaRise, uint64_t& prng)
    {
      const int kNyq = kStftN / 2;
      const float blurAlphaComp = 1.0f - blurAlpha;   // (1-blurAlpha) pre-computed

      // -----------------------------------------------------------------------
      // PASS 1 — Accumulate + Blur IIR: SMD+Freeze+Damp into magAcc, then
      //          update blurState[k] via symmetric per-bin IIR, store base phase.
      // phaseScratch[k] for complex bins: atan2(-nim, re) (standard DFT phase).
      // phaseScratch[k] for DC/Nyquist:  +1.0 or -1.0 (sign of input real).
      // -----------------------------------------------------------------------

      // DC (k=0): real-only.
      {
        const float re  = spectrum[0];
        const float mag = (re >= 0.0f) ? re : -re;
        const float gEff   = g + freeze * (1.0f - g);
        const float inGain = (1.0f - freeze > kFreezeInFloor) ? (1.0f - freeze) : kFreezeInFloor;
        float m = inGain * mag + magAcc[0] * gEff;
        if (m > kMagClamp) m = kMagClamp;
        magAcc[0]    = m;
        blurState[0] = blurAlpha * blurState[0] + blurAlphaComp * m;
        mPhaseScratch[0] = (re >= 0.0f) ? 1.0f : -1.0f;   // sign-encoding
      }

      // Complex bins k=1..N/2-1: SMD + Damp tilt + Blur IIR + store phase.
      float g_k = g * dampFactor;   // g for k=1; multiplied by dampFactor each step
      for (int k = 1; k < kNyq; ++k) {
        const float gk = (g_k < (float)kGMax) ? g_k : (float)kGMax;

        const float re  = spectrum[k];
        const float nim = spectrum[kNyq + k];   // nim = -imag_DFT[k]
        const float mag   = sqrtf(re * re + nim * nim);
        const float phase = atan2f(-nim, re);

        const float gkEff  = gk + freeze * (1.0f - gk);
        const float inGain = (1.0f - freeze > kFreezeInFloor) ? (1.0f - freeze) : kFreezeInFloor;
        float m = inGain * mag + magAcc[k] * gkEff;
        if (m > kMagClamp) m = kMagClamp;
        magAcc[k]    = m;
        blurState[k] = blurAlpha * blurState[k] + blurAlphaComp * m;
        mPhaseScratch[k] = phase;

        g_k *= dampFactor;
      }

      // Nyquist (k=N/2): real-only.
      {
        const float re   = spectrum[kNyq];
        const float mag  = (re >= 0.0f) ? re : -re;
        const float gEff   = g + freeze * (1.0f - g);
        const float inGain = (1.0f - freeze > kFreezeInFloor) ? (1.0f - freeze) : kFreezeInFloor;
        float m = inGain * mag + magAcc[kNyq] * gEff;
        if (m > kMagClamp) m = kMagClamp;
        magAcc[kNyq]    = m;
        blurState[kNyq] = blurAlpha * blurState[kNyq] + blurAlphaComp * m;
        mPhaseScratch[kNyq] = (re >= 0.0f) ? 1.0f : -1.0f;   // sign-encoding
      }

      // -----------------------------------------------------------------------
      // PASS 2 — Bloom: asymmetric IIR from blurState[k] into bloomState[k].
      //
      // Rise (blurState >= bloomState): slow, freq-staggered.
      //   bloomState[k] = ar[k]*bloomState[k] + (1-ar[k])*blurState[k]
      //   ar[k] = bloomAlphaRise[k] (larger at HF → HF rises more slowly → later bloom)
      // Fall (blurState < bloomState):  snap release.
      //   bloomState[k] = kBloomAlphaFall*bloomState[k] + (1-kBloomAlphaFall)*blurState[k]
      //   kBloomAlphaFall=0 → bloomState[k] = blurState[k] instantly.
      //
      // Bloom=0 → bloomAlphaRise[k]=0 → both paths collapse to bloomState[k]=blurState[k].
      // magAcc and blurState are UNTOUCHED here.
      // -----------------------------------------------------------------------
      for (int k = 0; k <= kNyq; ++k) {
        const float src = blurState[k];
        const float bs  = bloomState[k];
        if (src >= bs) {
          const float ar = bloomAlphaRise[k];
          bloomState[k] = ar * bs + (1.0f - ar) * src;
        } else {
          // kBloomAlphaFall = 0.0f: snap to src
          bloomState[k] = src;
        }
      }

      // -----------------------------------------------------------------------
      // PASS 3 — Synth: synthesize ifft_in from bloomState + stored phase + PRNG xi.
      //
      // DC and Nyquist: real-only, sign from phaseScratch (no PRNG).
      // Complex bins: phi = phaseScratch[k] + V*xi; xi drawn from PRNG in same
      // k order (k=1..N/2-1) as 0.2.0.7 → PRNG draw count/order unchanged.
      // -----------------------------------------------------------------------

      // DC (k=0)
      ifft_in[0] = mPhaseScratch[0] * bloomState[0];

      // Complex bins k=1..N/2-1
      for (int k = 1; k < kNyq; ++k) {
        // xorshift64 inline (Marsaglia 2003) — avoids ODR collision with APFTank.h
        prng ^= prng << 13; prng ^= prng >> 7; prng ^= prng << 17;
        // Map upper 24 bits → float [-pi, pi)
        const float xi  = ((float)(prng >> 40) * (1.0f / 8388608.0f) - 1.0f) * (float)M_PI;
        const float phi = mPhaseScratch[k] + V * xi;

        ifft_in[k]          = bloomState[k] * cosf(phi);
        ifft_in[kNyq + k]   = bloomState[k] * sinf(phi);
      }

      // Nyquist (k=N/2)
      ifft_in[kNyq] = mPhaseScratch[kNyq] * bloomState[kNyq];
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

    // Phase scratch — shared between L and R calls (fully overwritten each smdProcess).
    // phaseScratch[k]: base phase for complex bins (atan2); sign (+1/-1) for DC/Nyquist.
    float mPhaseScratch[kStftBins];

    // Cross-time IIR blur state — persistent per-channel per-bin smoother.
    // blurState[k] = alpha*blurState[k] + (1-alpha)*magAcc[k], updated every hop.
    // alpha=0 (Blur=0) → blurState[k] == magAcc[k] each hop → bit-identical to 0.2.0.7.
    float mBlurStateL[kStftBins];
    float mBlurStateR[kStftBins];

    // Asymmetric Bloom IIR state — persistent per-channel per-bin.
    // Updated from blurState[k] each hop: slow freq-staggered rise, instant fall.
    // Bloom=0 → bloomState[k] == blurState[k] each hop → bit-identical to 0.2.0.9.
    float mBloomStateL[kStftBins];
    float mBloomStateR[kStftBins];

    // Bloom stagger table (static, computed once in ctor):
    //   mBloomStagger[k] = 1.0 + kBloomStagger * k/(N/2)  — DC=1.0, Nyquist=4.0
    float mBloomStagger[kStftBins];

    // Per-bin rise-alpha table (shared L/R, recomputed lazily when Bloom changes):
    //   mBloomAlphaRise[k] = 1 - exp(-6 * Bloom * mBloomStagger[k])
    float mBloomAlphaRise[kStftBins];

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
    float mBlurAmt;     // raw Blur param 0..1
    float mBlurAlpha;   // IIR smoothing coefficient: 1 - exp(-6*mBlurAmt)
    float mBloomAmt;    // raw Bloom param 0..1
    float mLastBloom;   // cached previous Bloom value for lazy table recompute

#endif  // SWIGLUA
  };

}  // namespace zaum
