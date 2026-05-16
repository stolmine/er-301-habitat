// Presse -- 3-band multiband compressor for ER-301
// Crossover engine from Parfait, compression algo adapted from tomf's CPR
// (Giannoulis/Massberg/Reiss feedforward design)
//
// Per-band compressor vectorized to 4-lane NEON SoA on Cortex-A8:
// detector update, dB compute, gain reduction, and apply are all 3-of-4
// lane operations. Lane 3 is zero-padded; horizontal sum of lanes 0..2
// recovers the per-sample summed output.
//
// no-tree-vectorize is load-bearing per feedback_neon_hint_surfaces:
// prevents GCC from auto-vectorizing the SoA init code and emitting
// :64 alignment hints that trap on Cortex-A8.
#pragma GCC optimize("no-tree-vectorize")

// FFT viz update period (in audio process() blocks). At 48k/128 = 375
// blocks/sec, 4 blocks = 94 Hz, 6 blocks = 62 Hz, 8 blocks = 47 Hz.
// Must stay above the 55 Hz viz framerate floor.
// rmsDecay is calibrated for the chosen period: rmsDecay^(period/4)
// preserves the original 4-block time-constant. For period=6 use 0.784f.
//
// NOTE: tried 6/0.784 in 2.6.2.45 → measured 3pp idle CPU regression
// on hardware vs 2.6.2.44 (Parfait benefited, Impasto did not — likely
// asymmetric iCache effects from the recompile). Reverted Impasto to
// every-4 in 2.6.2.46; Parfait stays at every-6.
#define FFT_BLOCKS_PER_UPDATE 4
#define FFT_RMS_DECAY 0.85f

#include "MultibandCompressor.h"
#include "util/neon_math.h"
#include <od/config.h>
#include <hal/ops.h>
#include <math.h>
#include <string.h>

#include <pffft.h>

namespace stolmine
{

  // IEEE 754 fast log2/exp2 (from Parfait)
  static inline float fast_log2(float x)
  {
    union { float f; int32_t i; } v;
    v.f = x;
    float y = (float)(v.i);
    y *= 1.0f / (1 << 23);
    y -= 127.0f;
    return y;
  }

  static inline float fast_exp2(float x)
  {
    union { float f; int32_t i; } v;
    v.i = (int32_t)((x + 127.0f) * (1 << 23));
    return v.f;
  }

  static inline float fast_log10(float x)
  {
    return fast_log2(x) * 0.30103f; // log10(2)
  }

  static inline float fast_fromDb(float db)
  {
    return fast_exp2(db * 0.16609640474f); // 1 / (20 * log10(2))
  }

  struct MultibandCompressor::Internal
  {
    // Tilt EQ
    float tiltLpState;

    // Crossover (2 split points, LR4 = 4 cascaded one-pole, 24dB/oct)
    float crossoverHz[2];
    float xoverState[2][4];    // audio crossover
    float scXoverState[2][4];  // sidechain crossover (separate state)

    // Per-band compressor state — SoA, padded to 4 lanes for NEON.
    // Lane 3 is padding; init to 0 and the math keeps it at 0 forever.
    // (Old BandComp::gainReduction was a dead store and is dropped.)
    float detector_[4] __attribute__((aligned(16)));

    // Block-rate per-band coefficients, SoA-padded. Heap-allocated
    // (Internal lives on the heap) so the NEON loads against these
    // arrays don't surface :64 stack-local alignment hints. Filled
    // in process()'s block-rate setup; consumed by the per-sample
    // NEON kernel. Lane 3 is neutral (no-op coefficients).
    float riseCoeff_[4] __attribute__((aligned(16)));
    float fallCoeff_[4] __attribute__((aligned(16)));
    float thresholdDb_[4] __attribute__((aligned(16)));
    float ratioI_[4] __attribute__((aligned(16)));
    float makeupCombined_[4] __attribute__((aligned(16)));

    // Per-sample NEON scratch — heap-allocated to keep vld1q_f32 off
    // stack-local addresses (which can emit :64 alignment hints that
    // trap on Cortex-A8 per feedback_neon_intrinsics_drumvoice).
    float scBandsScratch_[4] __attribute__((aligned(16)));
    float aBandsScratch_[4] __attribute__((aligned(16)));
    float compScratch_[4] __attribute__((aligned(16)));
    float grScratch_[4] __attribute__((aligned(16)));

    // Per-band energy (for graphic)
    float bandEnergy[3];
    float bandGR[3]; // smoothed GR for viz (0 = no reduction, 1 = full)

    // FFT state
    PFFFT_Setup *fftSetup;
    float *fftIn;
    float *fftOut;
    float *fftWork;
    float hannWindow[256];
    float fftRms[128];
    float ringBuf[256];
    int ringPos;
    int fftFrameCount;
    bool fftReady;

    void Init()
    {
      tiltLpState = 0.0f;
      crossoverHz[0] = 200.0f;
      crossoverHz[1] = 2000.0f;
      for (int i = 0; i < 2; i++)
        for (int j = 0; j < 4; j++)
        {
          xoverState[i][j] = 0.0f;
          scXoverState[i][j] = 0.0f;
        }
      for (int i = 0; i < 3; i++)
      {
        bandEnergy[i] = 0.0f;
        bandGR[i] = 0.0f;
      }
      for (int i = 0; i < 4; i++)
      {
        detector_[i] = 0.0f;
        riseCoeff_[i] = 1.0f;
        fallCoeff_[i] = 1.0f;
        thresholdDb_[i] = 0.0f;
        ratioI_[i] = 0.0f;
        makeupCombined_[i] = 0.0f;
      }

      fftSetup = 0;
      fftIn = 0;
      fftOut = 0;
      fftWork = 0;
      fftReady = false;
      memset(fftRms, 0, sizeof(fftRms));
      memset(ringBuf, 0, sizeof(ringBuf));
      ringPos = 0;
      fftFrameCount = 0;
    }

    void Cleanup()
    {
      if (fftSetup) { pffft_destroy_setup(fftSetup); fftSetup = 0; }
      if (fftIn) { pffft_aligned_free(fftIn); fftIn = 0; }
      if (fftOut) { pffft_aligned_free(fftOut); fftOut = 0; }
      if (fftWork) { pffft_aligned_free(fftWork); fftWork = 0; }
    }
  };

  // --- Constructor / Destructor ---

  MultibandCompressor::MultibandCompressor()
  {
    addInput(mIn);
    addInput(mSidechain);
    addOutput(mOut);

    addParameter(mDrive);
    addParameter(mToneAmount);
    addParameter(mToneFreq);
    addParameter(mSkew);
    addParameter(mMix);
    addParameter(mOutputLevel);
    addParameter(mInputGain);

    addParameter(mBandThreshold0); addParameter(mBandThreshold1); addParameter(mBandThreshold2);
    addParameter(mBandRatio0); addParameter(mBandRatio1); addParameter(mBandRatio2);
    addParameter(mBandSpeed0); addParameter(mBandSpeed1); addParameter(mBandSpeed2);
    addParameter(mBandAttack0); addParameter(mBandAttack1); addParameter(mBandAttack2);
    addParameter(mBandRelease0); addParameter(mBandRelease1); addParameter(mBandRelease2);
    addParameter(mBandWeight0); addParameter(mBandWeight1); addParameter(mBandWeight2);
    addParameter(mBandLevel0); addParameter(mBandLevel1); addParameter(mBandLevel2);

    addOption(mAutoMakeup);
    addOption(mEnableSidechain);
    mAutoMakeup.enableSerialization();
    mEnableSidechain.enableSerialization();

    mpInternal = new Internal();
    mpInternal->Init();

    for (int b = 0; b < 3; b++)
    {
      for (int p = 0; p < kCompBiasCount; p++)
        mBandBias[b][p] = 0;
      mBandLevelBias[b] = 0;
    }

    for (int i = 0; i < 3; i++)
      mLastWeight[i] = -1.0f;
    mLastSkew = -1.0f;
  }

  MultibandCompressor::~MultibandCompressor()
  {
    mpInternal->Cleanup();
    delete mpInternal;
  }

  // --- SWIG-visible ---

  void MultibandCompressor::setBandBias(int band, int param, od::Parameter *p)
  {
    if (band < 0 || band >= 3 || param < 0 || param >= kCompBiasCount) return;
    mBandBias[band][param] = p;
  }

  void MultibandCompressor::setBandLevelBias(int band, od::Parameter *p)
  {
    if (band < 0 || band >= 3) return;
    mBandLevelBias[band] = p;
  }

  float MultibandCompressor::getCrossoverFreq(int band)
  {
    if (band < 0 || band >= 2) return 0;
    return mpInternal->crossoverHz[band];
  }

  float MultibandCompressor::getBandGainReduction(int band)
  {
    if (band < 0 || band >= 3) return 0;
    return mpInternal->bandGR[band];
  }

  float MultibandCompressor::getFFTRms(int bin)
  {
    if (bin < 0 || bin >= 128) return 0;
    return mpInternal->fftRms[bin];
  }

  float MultibandCompressor::getBandLevel(int band)
  {
    if (band < 0 || band >= 3) return 0;
    return mpInternal->bandEnergy[band];
  }

  float MultibandCompressor::getBandLevelSetting(int band)
  {
    if (band < 0 || band >= 3) return 1.0f;
    return mBandLevelBias[band] ? mBandLevelBias[band]->value() : 1.0f;
  }

  int MultibandCompressor::getCrossoverBin(int band)
  {
    if (band < 0 || band >= 2) return 0;
    float sr = globalConfig.sampleRate;
    float binHz = sr / 256.0f;
    return (int)(mpInternal->crossoverHz[band] / binHz + 0.5f);
  }

  bool MultibandCompressor::isAutoMakeupEnabled()
  {
    return mAutoMakeup.value() == 1;
  }

  void MultibandCompressor::toggleAutoMakeup()
  {
    mAutoMakeup.set(isAutoMakeupEnabled() ? 2 : 1);
  }

  bool MultibandCompressor::isSidechainEnabled()
  {
    return mEnableSidechain.value() == 1;
  }

  void MultibandCompressor::toggleSidechainEnabled()
  {
    mEnableSidechain.set(isSidechainEnabled() ? 2 : 1);
  }

  // --- Crossover frequency computation (from Parfait) ---

  void MultibandCompressor::recomputeCrossovers()
  {
    Internal &s = *mpInternal;
    float w0 = CLAMP(0.1f, 4.0f, mBandBias[0][5] ? mBandBias[0][5]->value() : mBandWeight0.value());
    float w1 = CLAMP(0.1f, 4.0f, mBandBias[1][5] ? mBandBias[1][5]->value() : mBandWeight1.value());
    float w2 = CLAMP(0.1f, 4.0f, mBandBias[2][5] ? mBandBias[2][5]->value() : mBandWeight2.value());
    float skewParam = CLAMP(-1.0f, 1.0f, mSkew.value());

    float total = w0 + w1 + w2;
    float accum0 = w0 / total;
    float accum1 = (w0 + w1) / total;

    float f0 = (accum0 < 1.0f - accum0) ? accum0 : 1.0f - accum0;
    float f1 = (accum1 < 1.0f - accum1) ? accum1 : 1.0f - accum1;
    float b0 = accum0 - skewParam * f0;
    float b1 = accum1 - skewParam * f1;

    s.crossoverHz[0] = 20.0f * powf(1000.0f, b0);
    s.crossoverHz[1] = 20.0f * powf(1000.0f, b1);

    float sr = globalConfig.sampleRate;
    s.crossoverHz[0] = CLAMP(30.0f, sr * 0.33f, s.crossoverHz[0]);
    s.crossoverHz[1] = CLAMP(s.crossoverHz[0] + 10.0f, sr * 0.33f, s.crossoverHz[1]);

    mLastWeight[0] = w0;
    mLastWeight[1] = w1;
    mLastWeight[2] = w2;
    mLastSkew = skewParam;
  }

  // --- Process ---

  void MultibandCompressor::process()
  {
    Internal &s = *mpInternal;
    float *in = mIn.buffer();
    float *sc = mSidechain.buffer();
    float *out = mOut.buffer();

    float drive = CLAMP(0.0f, 4.0f, mDrive.value());
    float toneAmt = CLAMP(0.0f, 1.0f, mToneAmount.value());
    float toneFreq = CLAMP(20.0f, 20000.0f, mToneFreq.value());
    float mix = CLAMP(0.0f, 1.0f, mMix.value());
    float outputLevel = CLAMP(0.0f, 2.0f, mOutputLevel.value());
    float inputGain = CLAMP(0.0f, 4.0f, mInputGain.value());
    bool scEnabled = isSidechainEnabled();
    bool autoMakeup = isAutoMakeupEnabled();

    // Read per-band params from Bias refs
    float threshold[3], ratio[3], attack[3], release[3];
    for (int b = 0; b < 3; b++)
    {
      // Fader is 0-1 linear; cube it for perceptual scaling
      // Fader 1.0 = thresh 1.0 (0dB), 0.5 = 0.125 (-18dB), 0.0 = 0.0 (-inf)
      float threshFader = mBandBias[b][0] ? CLAMP(0.0f, 1.0f, mBandBias[b][0]->value()) : 0.5f;
      threshold[b] = threshFader * threshFader * threshFader;
      if (threshold[b] < 0.001f) threshold[b] = 0.001f;
      ratio[b] = mBandBias[b][1] ? CLAMP(1.0f, 20.0f, mBandBias[b][1]->value()) : 2.0f;
      // Speed: G-Bus style breakpoints, interpolated
      // 0.0=30ms/1.2s  0.2=10ms/0.6s  0.4=3ms/0.3s  0.6=1ms/0.1s  0.8=0.3ms/0.1s  1.0=0.1ms/0.1s
      static const float kSpeedBP[] =   { 0.0f,   0.2f,  0.4f,  0.6f,  0.8f,   1.0f };
      static const float kAttackBP[] =  { 0.030f, 0.010f, 0.003f, 0.001f, 0.0003f, 0.0001f };
      static const float kReleaseBP[] = { 1.2f,   0.6f,  0.3f,  0.1f,  0.1f,   0.1f };
      float speed = mBandBias[b][2] ? CLAMP(0.0f, 1.0f, mBandBias[b][2]->value()) : 0.3f;
      // Find segment and interpolate
      int seg = 4;
      for (int k = 0; k < 5; k++)
      {
        if (speed < kSpeedBP[k + 1]) { seg = k; break; }
      }
      float segT = (speed - kSpeedBP[seg]) / (kSpeedBP[seg + 1] - kSpeedBP[seg]);
      attack[b] = kAttackBP[seg] + (kAttackBP[seg + 1] - kAttackBP[seg]) * segT;
      release[b] = kReleaseBP[seg] + (kReleaseBP[seg + 1] - kReleaseBP[seg]) * segT;
    }

    // Dirty-check crossovers
    float w0 = mBandBias[0][5] ? mBandBias[0][5]->value() : mBandWeight0.value();
    float w1 = mBandBias[1][5] ? mBandBias[1][5]->value() : mBandWeight1.value();
    float w2 = mBandBias[2][5] ? mBandBias[2][5]->value() : mBandWeight2.value();
    float skew = mSkew.value();
    if (w0 != mLastWeight[0] || w1 != mLastWeight[1] || w2 != mLastWeight[2] || skew != mLastSkew)
      recomputeCrossovers();

    float sr = globalConfig.sampleRate;

    // Tilt EQ coefficients
    float tiltCoeff = 1.0f / (1.0f + 1.0f / (2.0f * 3.14159f * toneFreq / sr));
    float tiltGain = powf(10.0f, toneAmt * 0.3f);
    float tiltLGain = 1.0f / tiltGain;

    // Crossover coefficients (LR4)
    float xCoeff[2];
    for (int c = 0; c < 2; c++)
    {
      float fc = s.crossoverHz[c] / sr;
      xCoeff[c] = 1.0f / (1.0f + 1.0f / (2.0f * 3.14159f * fc));
    }

    // Per-band compressor coefficients -- write into Internal's SoA
    // arrays so the per-sample NEON kernel can vld1q_f32 them directly.
    // makeupCombined folds makeup gain × band-level fader so the
    // per-sample apply is a single multiply.
    for (int b = 0; b < 3; b++)
    {
      float sp = 1.0f / sr;
      s.riseCoeff_[b] = expf(-sp / (attack[b] > sp ? attack[b] : sp));
      s.fallCoeff_[b] = expf(-sp / (release[b] > sp ? release[b] : sp));

      s.thresholdDb_[b] = 20.0f * fast_log10(threshold[b] + 1e-10f);
      s.ratioI_[b] = 1.0f / (ratio[b] > 1.0f ? ratio[b] : 1.0f);

      // Auto makeup: compensate for gain reduction at threshold
      float overDb = -s.thresholdDb_[b];
      if (overDb < 0.0f) overDb = 0.0f;
      float makeupDb = overDb - overDb * s.ratioI_[b];
      float makeupGain = autoMakeup ? fast_fromDb(makeupDb) : 1.0f;

      float bandLevel = mBandLevelBias[b] ? CLAMP(0.0f, 2.0f, mBandLevelBias[b]->value()) : 1.0f;
      s.makeupCombined_[b] = makeupGain * bandLevel;
    }
    // Lane 3 padding: neutral coeffs (init sets these to 0/1; rewrite
    // each block to be safe in case Init wasn't called recently).
    s.riseCoeff_[3] = 1.0f;
    s.fallCoeff_[3] = 1.0f;
    s.thresholdDb_[3] = 0.0f;
    s.ratioI_[3] = 0.0f;
    s.makeupCombined_[3] = 0.0f;  // zeros padding lane's contribution to sum

    // Fast-path flag: any band actually doing compression? A band needs
    // BOTH a sub-unity threshold (so signal can exceed it) AND a >1
    // ratio (so reduction is non-zero) to compress. If neither condition
    // holds on any band, skip the entire NEON detector→log2→exp2→apply
    // path per sample — just sum aBands × makeupCombined.
    //
    // When the fast path is taken the detector state goes stale; that's
    // OK — re-engaging compression starts with a fresh attack ramp,
    // which is musically natural. Detector is zeroed on the fast-path
    // edge below to keep re-engage clean.
    bool anyCompActive = false;
    for (int b = 0; b < 3; b++)
    {
      if (s.thresholdDb_[b] < -0.1f && s.ratioI_[b] < 0.995f)
      {
        anyCompActive = true;
        break;
      }
    }
    if (!anyCompActive && mPrevCompActive)
    {
      // Just entered no-comp state — flush detector so re-engage starts fresh
      for (int b = 0; b < 4; b++) s.detector_[b] = 0.0f;
    }
    mPrevCompActive = anyCompActive;

    float energyAccum[3] = {0, 0, 0};
    float grAccum[3] = {0, 0, 0};

    // Lazy FFT init
    if (!s.fftSetup)
    {
      s.fftSetup = pffft_new_setup(256, PFFFT_REAL);
      s.fftIn = (float *)pffft_aligned_malloc(256 * sizeof(float));
      s.fftOut = (float *)pffft_aligned_malloc(256 * sizeof(float));
      s.fftWork = (float *)pffft_aligned_malloc(256 * sizeof(float));
      for (int i = 0; i < 256; i++)
        s.hannWindow[i] = 0.5f * (1.0f - cosf(2.0f * 3.14159f * (float)i / 255.0f));
    }

    for (int i = 0; i < FRAMELENGTH; i++)
    {
      float dry = in[i];
      float x = dry * drive;

      // Tilt EQ
      if (toneAmt > 0.001f)
      {
        s.tiltLpState += (x - s.tiltLpState) * tiltCoeff;
        x = s.tiltLpState * tiltLGain + (x - s.tiltLpState) * tiltGain;
      }

      // --- Audio crossover (LR4, 24dB/oct) ---
      s.xoverState[0][0] += (x - s.xoverState[0][0]) * xCoeff[0];
      s.xoverState[0][1] += (s.xoverState[0][0] - s.xoverState[0][1]) * xCoeff[0];
      s.xoverState[0][2] += (s.xoverState[0][1] - s.xoverState[0][2]) * xCoeff[0];
      s.xoverState[0][3] += (s.xoverState[0][2] - s.xoverState[0][3]) * xCoeff[0];
      float aBand0 = s.xoverState[0][3];
      float aHp0 = x - aBand0;

      s.xoverState[1][0] += (aHp0 - s.xoverState[1][0]) * xCoeff[1];
      s.xoverState[1][1] += (s.xoverState[1][0] - s.xoverState[1][1]) * xCoeff[1];
      s.xoverState[1][2] += (s.xoverState[1][1] - s.xoverState[1][2]) * xCoeff[1];
      s.xoverState[1][3] += (s.xoverState[1][2] - s.xoverState[1][3]) * xCoeff[1];
      float aBand1 = s.xoverState[1][3];
      float aBand2 = aHp0 - aBand1;

      // --- Sidechain crossover (same coeffs, separate state) ---
      // When SC is disabled, scSig == x and the SC xover would produce
      // the same bands as the audio xover after state convergence. Skip
      // the SC xover entirely and copy audio bands to SC bands.
      float scBand0, scBand1, scBand2;
      if (scEnabled)
      {
        float scSig = sc[i] * inputGain;

        s.scXoverState[0][0] += (scSig - s.scXoverState[0][0]) * xCoeff[0];
        s.scXoverState[0][1] += (s.scXoverState[0][0] - s.scXoverState[0][1]) * xCoeff[0];
        s.scXoverState[0][2] += (s.scXoverState[0][1] - s.scXoverState[0][2]) * xCoeff[0];
        s.scXoverState[0][3] += (s.scXoverState[0][2] - s.scXoverState[0][3]) * xCoeff[0];
        scBand0 = s.scXoverState[0][3];
        float scHp0 = scSig - scBand0;

        s.scXoverState[1][0] += (scHp0 - s.scXoverState[1][0]) * xCoeff[1];
        s.scXoverState[1][1] += (s.scXoverState[1][0] - s.scXoverState[1][1]) * xCoeff[1];
        s.scXoverState[1][2] += (s.scXoverState[1][1] - s.scXoverState[1][2]) * xCoeff[1];
        s.scXoverState[1][3] += (s.scXoverState[1][2] - s.scXoverState[1][3]) * xCoeff[1];
        scBand1 = s.scXoverState[1][3];
        scBand2 = scHp0 - scBand1;
      }
      else
      {
        scBand0 = aBand0;
        scBand1 = aBand1;
        scBand2 = aBand2;
      }

      // --- Per-band compression: NEON 4-lane SoA, lane 3 padded to 0 ---
      // Scratch arrays live in Internal (heap) — stack-locals here would
      // emit :64 hints that trap on Cortex-A8.
      s.scBandsScratch_[0] = scBand0;
      s.scBandsScratch_[1] = scBand1;
      s.scBandsScratch_[2] = scBand2;
      s.scBandsScratch_[3] = 0.0f;
      s.aBandsScratch_[0] = aBand0;
      s.aBandsScratch_[1] = aBand1;
      s.aBandsScratch_[2] = aBand2;
      s.aBandsScratch_[3] = 0.0f;

      // Fast path: no band is configured to compress. Skip the entire
      // detector→log2→exp2 chain (the bulk of the per-sample NEON cost)
      // and just apply makeupCombined to the audio bands. Per-sample
      // branch is predictable (block-rate flag).
      if (!anyCompActive)
      {
        s.compScratch_[0] = aBand0 * s.makeupCombined_[0];
        s.compScratch_[1] = aBand1 * s.makeupCombined_[1];
        s.compScratch_[2] = aBand2 * s.makeupCombined_[2];
        s.grScratch_[0] = s.grScratch_[1] = s.grScratch_[2] = 1.0f;
      }
      else
      {

#if defined(__ARM_NEON__) || defined(__ARM_NEON)
      using namespace stolmine::neon_math;

      // Envelope detection (branchless coefficient select via vbslq_f32)
      float32x4_t scQ  = vld1q_f32(s.scBandsScratch_);
      float32x4_t absQ = vabsq_f32(scQ);
      float32x4_t detQ = vld1q_f32(s.detector_);
      uint32x4_t rising = vcgtq_f32(absQ, detQ);
      float32x4_t riseQ = vld1q_f32(s.riseCoeff_);
      float32x4_t fallQ = vld1q_f32(s.fallCoeff_);
      float32x4_t coeffQ = vbslq_f32(rising, riseQ, fallQ);
      float32x4_t one = vdupq_n_f32(1.0f);
      float32x4_t omc = vsubq_f32(one, coeffQ);
      detQ = vmlaq_f32(vmulq_f32(coeffQ, detQ), omc, absQ);
      vst1q_f32(s.detector_, detQ);

      // dB compute: 20*log10(x) = 20*0.30103*log2(x) = 6.0206*log2(x)
      float32x4_t detEps = vaddq_f32(detQ, vdupq_n_f32(1e-10f));
      float32x4_t levelDbQ = vmulq_f32(
          log2_poly_4lane(detEps),
          vdupq_n_f32(6.02059991f));
      float32x4_t threshQ  = vld1q_f32(s.thresholdDb_);
      float32x4_t overQ    = vmaxq_f32(vsubq_f32(levelDbQ, threshQ),
                                       vdupq_n_f32(0.0f));
      float32x4_t ratioIQ  = vld1q_f32(s.ratioI_);
      float32x4_t reductQ  = vmulq_f32(overQ, vsubq_f32(one, ratioIQ));

      // gr = exp2(-reductionDb * 0.16609640474)  (== fast_fromDb(-rd))
      float32x4_t grQ = exp2_poly_4lane(
          vmulq_f32(reductQ, vdupq_n_f32(-0.16609640474f)));
      vst1q_f32(s.grScratch_, grQ);

      // Apply: compressed = aBand * gr * makeupCombined
      float32x4_t aQ = vld1q_f32(s.aBandsScratch_);
      float32x4_t makeupQ = vld1q_f32(s.makeupCombined_);
      float32x4_t compQ = vmulq_f32(vmulq_f32(aQ, grQ), makeupQ);
      vst1q_f32(s.compScratch_, compQ);
#else
      // Scalar fallback (linux x86, darwin). Same math, same coeffs.
      using namespace stolmine::neon_math;
      for (int b = 0; b < 4; b++)
      {
        float absLevel = s.scBandsScratch_[b] < 0 ? -s.scBandsScratch_[b] : s.scBandsScratch_[b];
        float coeff = absLevel > s.detector_[b] ? s.riseCoeff_[b] : s.fallCoeff_[b];
        s.detector_[b] = coeff * s.detector_[b] + (1.0f - coeff) * absLevel;

        float levelDb = 6.02059991f * log2_poly(s.detector_[b] + 1e-10f);
        float overDb = levelDb - s.thresholdDb_[b];
        if (overDb < 0.0f) overDb = 0.0f;
        float reductionDb = overDb * (1.0f - s.ratioI_[b]);
        float gr = exp2_poly(-reductionDb * 0.16609640474f);
        s.grScratch_[b] = gr;
        s.compScratch_[b] = s.aBandsScratch_[b] * gr * s.makeupCombined_[b];
      }
#endif

      }  // end else (anyCompActive == true branch)

      // Horizontal sum lanes 0..2 (lane 3 = 0 by makeupCombined padding)
      float sum = s.compScratch_[0] + s.compScratch_[1] + s.compScratch_[2];

      // Metering accumulators (per-band scalar, only lanes 0..2)
      for (int b = 0; b < 3; b++)
      {
        float absComp = s.compScratch_[b] < 0 ? -s.compScratch_[b] : s.compScratch_[b];
        energyAccum[b] += absComp;
        grAccum[b] += (1.0f - s.grScratch_[b]);
      }

      // Output: level + dry/wet
      float wet = sum * outputLevel;
      out[i] = dry * (1.0f - mix) + wet * mix;

      // FFT ring buffer (post-compression)
      s.ringBuf[s.ringPos] = wet;
      s.ringPos = (s.ringPos + 1) & 255;
    }

    // Update smoothed per-band metering
    float invFrame = 1.0f / (float)FRAMELENGTH;
    float energySlew = 0.15f;
    float grSlew = 0.2f;
    for (int b = 0; b < 3; b++)
    {
      float avgEnergy = energyAccum[b] * invFrame;
      s.bandEnergy[b] += (avgEnergy - s.bandEnergy[b]) * energySlew;
      float avgGR = grAccum[b] * invFrame;
      s.bandGR[b] += (avgGR - s.bandGR[b]) * grSlew;
    }

    // FFT viz update — see FFT_BLOCKS_PER_UPDATE at file top for tuning.
    s.fftFrameCount++;
    if (s.fftFrameCount >= FFT_BLOCKS_PER_UPDATE && s.fftSetup)
    {
      s.fftFrameCount = 0;
      for (int i = 0; i < 256; i++)
        s.fftIn[i] = s.ringBuf[(s.ringPos + i) & 255] * s.hannWindow[i];

      pffft_transform_ordered(s.fftSetup, s.fftIn, s.fftOut, s.fftWork, PFFFT_FORWARD);

      float rmsDecay = FFT_RMS_DECAY;
      for (int bin = 0; bin < 128; bin++)
      {
        float re = s.fftOut[bin * 2];
        float im = s.fftOut[bin * 2 + 1];
        float mag = sqrtf(re * re + im * im) * (1.0f / 128.0f);

        // RMS with smooth decay
        if (mag > s.fftRms[bin])
          s.fftRms[bin] = mag;
        else
          s.fftRms[bin] = s.fftRms[bin] * rmsDecay + mag * (1.0f - rmsDecay);
      }
    }
  }

} // namespace stolmine
