// Presse -- 3-band multiband compressor for ER-301
// Crossover engine from Parfait, compression algo adapted from tomf's CPR
// (Giannoulis/Massberg/Reiss feedforward design)

#include "MultibandCompressor.h"
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

    // Per-band compressor
    struct BandComp
    {
      float detector;
      float gainReduction; // linear, 1.0 = no reduction
    };
    BandComp comp[3];

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
        comp[i].detector = 0.0f;
        comp[i].gainReduction = 1.0f;
        bandEnergy[i] = 0.0f;
        bandGR[i] = 0.0f;
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

    // Per-band compressor coefficients
    float riseCoeff[3], fallCoeff[3], thresholdDb[3], ratioI[3], makeupGain[3];
    for (int b = 0; b < 3; b++)
    {
      float sp = 1.0f / sr;
      riseCoeff[b] = expf(-sp / (attack[b] > sp ? attack[b] : sp));
      fallCoeff[b] = expf(-sp / (release[b] > sp ? release[b] : sp));

      thresholdDb[b] = 20.0f * fast_log10(threshold[b] + 1e-10f);
      ratioI[b] = 1.0f / (ratio[b] > 1.0f ? ratio[b] : 1.0f);

      // Auto makeup: compensate for gain reduction at threshold
      float overDb = -thresholdDb[b];
      if (overDb < 0.0f) overDb = 0.0f;
      float makeupDb = overDb - overDb * ratioI[b];
      makeupGain[b] = autoMakeup ? fast_fromDb(makeupDb) : 1.0f;
    }

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

      // Sidechain source (self-detect or external)
      float scSig = scEnabled ? (sc[i] * inputGain) : x;

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
      s.scXoverState[0][0] += (scSig - s.scXoverState[0][0]) * xCoeff[0];
      s.scXoverState[0][1] += (s.scXoverState[0][0] - s.scXoverState[0][1]) * xCoeff[0];
      s.scXoverState[0][2] += (s.scXoverState[0][1] - s.scXoverState[0][2]) * xCoeff[0];
      s.scXoverState[0][3] += (s.scXoverState[0][2] - s.scXoverState[0][3]) * xCoeff[0];
      float scBand0 = s.scXoverState[0][3];
      float scHp0 = scSig - scBand0;

      s.scXoverState[1][0] += (scHp0 - s.scXoverState[1][0]) * xCoeff[1];
      s.scXoverState[1][1] += (s.scXoverState[1][0] - s.scXoverState[1][1]) * xCoeff[1];
      s.scXoverState[1][2] += (s.scXoverState[1][1] - s.scXoverState[1][2]) * xCoeff[1];
      s.scXoverState[1][3] += (s.scXoverState[1][2] - s.scXoverState[1][3]) * xCoeff[1];
      float scBand1 = s.scXoverState[1][3];
      float scBand2 = scHp0 - scBand1;

      float scBands[3] = { scBand0, scBand1, scBand2 };
      float aBands[3] = { aBand0, aBand1, aBand2 };

      // --- Per-band compression ---
      float sum = 0.0f;
      for (int b = 0; b < 3; b++)
      {
        // Envelope detection on sidechain band
        float absLevel = scBands[b] < 0 ? -scBands[b] : scBands[b];
        float coeff = absLevel > s.comp[b].detector ? riseCoeff[b] : fallCoeff[b];
        s.comp[b].detector = coeff * s.comp[b].detector + (1.0f - coeff) * absLevel;

        // Gain computation in dB domain
        float levelDb = 20.0f * fast_log10(s.comp[b].detector + 1e-10f);
        float overDb = levelDb - thresholdDb[b];
        if (overDb < 0.0f) overDb = 0.0f;
        float reductionDb = overDb * (1.0f - ratioI[b]);
        float gr = fast_fromDb(-reductionDb);
        s.comp[b].gainReduction = gr;

        // Apply GR + makeup + band level to audio band
        float bandLevel = mBandLevelBias[b] ? CLAMP(0.0f, 2.0f, mBandLevelBias[b]->value()) : 1.0f;
        float compressed = aBands[b] * gr * makeupGain[b] * bandLevel;
        sum += compressed;

        // Metering accumulators
        float absComp = compressed < 0 ? -compressed : compressed;
        energyAccum[b] += absComp;
        grAccum[b] += (1.0f - gr); // 0 = no reduction, 1 = full
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

    // FFT (every 4 blocks = ~21ms at 48kHz)
    s.fftFrameCount++;
    if (s.fftFrameCount >= 4 && s.fftSetup)
    {
      s.fftFrameCount = 0;
      for (int i = 0; i < 256; i++)
        s.fftIn[i] = s.ringBuf[(s.ringPos + i) & 255] * s.hannWindow[i];

      pffft_transform_ordered(s.fftSetup, s.fftIn, s.fftOut, s.fftWork, PFFFT_FORWARD);

      float rmsDecay = 0.85f;
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
