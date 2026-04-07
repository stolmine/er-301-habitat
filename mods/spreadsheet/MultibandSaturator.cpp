#include "MultibandSaturator.h"
#include "pffft.h"
#include <od/config.h>
#include <hal/ops.h>
#include <math.h>
#include <string.h>
#include <new>
#include <stdlib.h>

namespace stolmine
{

  static inline float fast_tanh(float x)
  {
    if (x < -4.0f) return -1.0f;
    if (x >  4.0f) return  1.0f;
    float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
  }

  // --- Waveshapers (stateless, ported from Discont) ---

  static inline float fold2(float x)
  {
    while (x > 1.0f) x = 2.0f - x;
    while (x < -1.0f) x = -2.0f - x;
    return x;
  }

  static inline float applyShaper(float x, int type, float amount, float bias)
  {
    // Bias shifts input asymmetrically
    float sig = (x + bias) * (1.0f + amount * 3.0f);
    float wet;
    switch (type)
    {
    default:
    case 0: // Soft -- tanh
      wet = fast_tanh(sig);
      break;
    case 1: // Hard -- clip
      wet = CLAMP(-1.0f, 1.0f, sig);
      break;
    case 2: // Fold -- triangle wavefolder
      wet = fold2(sig);
      break;
    case 3: // Rectify -- full-wave
      wet = fabsf(sig);
      break;
    case 4: // Crush -- bit reduction (8 levels)
      wet = ((int)(sig * 8.0f + (sig < 0 ? -0.5f : 0.5f))) / 8.0f;
      break;
    case 5: // Sine -- sinusoidal
      wet = sinf(sig * 3.14159f);
      break;
    case 6: // Polynomial -- odd-harmonic x - x^3/3
    {
      float driven = sig;
      wet = driven - (driven * driven * driven) / 3.0f;
      break;
    }
    }
    // Blend: amount=0 -> dry, amount>0 -> progressively more wet
    return x + (wet - x) * CLAMP(0.0f, 1.0f, amount);
  }

  struct MultibandSaturator::Internal
  {
    // Tilt EQ state
    float tiltLpState = 0.0f;

    // Crossover (2 split points, 2-pole each for 12dB/oct)
    float crossoverHz[2];
    float xoverState[2][2];  // [crossover][cascade stage]

    // Per-band post-shaper SVF filter (inline state, not stmlib::Svf)
    float svfState1[3];
    float svfState2[3];
    float svfG[3];   // frequency coefficient
    float svfR[3];   // damping (1/Q)
    float svfH[3];   // denominator

    // Compressor state
    float compDetector = 0.0f;
    float scHpState = 0.0f;

    // Per-band energy (for graphic)
    float bandEnergy[3];

    // FFT state
    PFFFT_Setup *fftSetup;
    float *fftIn;
    float *fftOut;
    float hannWindow[256];
    float fftPeak[128];
    float fftRms[128];
    float ringBuf[256];
    int ringPos;
    int fftFrameCount;

    void Init()
    {
      tiltLpState = 0.0f;
      crossoverHz[0] = 200.0f;
      crossoverHz[1] = 2000.0f;
      for (int i = 0; i < 2; i++)
        for (int j = 0; j < 2; j++)
          xoverState[i][j] = 0.0f;
      for (int i = 0; i < 3; i++)
      {
        svfState1[i] = 0.0f;
        svfState2[i] = 0.0f;
        svfG[i] = 0.1f;
        svfR[i] = 1.0f;
        svfH[i] = 0.5f;
      }
      compDetector = 0.0f;
      scHpState = 0.0f;
      for (int i = 0; i < 3; i++)
        bandEnergy[i] = 0.0f;

      // FFT
      fftSetup = pffft_new_setup(256, PFFFT_REAL);
      fftIn = (float *)pffft_aligned_malloc(256 * sizeof(float));
      fftOut = (float *)pffft_aligned_malloc(256 * sizeof(float));
      for (int i = 0; i < 256; i++)
        hannWindow[i] = 0.5f * (1.0f - cosf(2.0f * 3.14159f * (float)i / 255.0f));
      memset(fftPeak, 0, sizeof(fftPeak));
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
    }
  };

  MultibandSaturator::MultibandSaturator()
  {
    addInput(mIn);
    addOutput(mOut);

    // Global
    addParameter(mDrive);
    addParameter(mToneAmount);
    addParameter(mToneFreq);
    addParameter(mSkew);
    addParameter(mMix);
    addParameter(mOutputLevel);
    addParameter(mCompressAmt);
    addParameter(mTanhAmt);
    addParameter(mScHpf);

    // Per-band
    addParameter(mBandLevel0);
    addParameter(mBandLevel1);
    addParameter(mBandLevel2);
    addParameter(mBandAmount0);
    addParameter(mBandAmount1);
    addParameter(mBandAmount2);
    addParameter(mBandBias0);
    addParameter(mBandBias1);
    addParameter(mBandBias2);
    addParameter(mBandType0);
    addParameter(mBandType1);
    addParameter(mBandType2);
    addParameter(mBandWeight0);
    addParameter(mBandWeight1);
    addParameter(mBandWeight2);
    addParameter(mBandFilterFreq0);
    addParameter(mBandFilterFreq1);
    addParameter(mBandFilterFreq2);
    addParameter(mBandFilterMorph0);
    addParameter(mBandFilterMorph1);
    addParameter(mBandFilterMorph2);
    addParameter(mBandFilterQ0);
    addParameter(mBandFilterQ1);
    addParameter(mBandFilterQ2);
    addParameter(mBandMute0);
    addParameter(mBandMute1);
    addParameter(mBandMute2);

    mpInternal = new Internal();
    mpInternal->Init();

    for (int i = 0; i < 3; i++)
      mLastWeight[i] = -1.0f;
    mLastSkew = -1.0f;
    for (int b = 0; b < 3; b++)
    {
      for (int p = 0; p < kBiasCount; p++)
        mBandBias[b][p] = 0;
      mBandLevelBias[b] = 0;
    }
  }

  MultibandSaturator::~MultibandSaturator()
  {
    mpInternal->Cleanup();
    delete mpInternal;
  }

  // --- Crossover frequency derivation (from Etcher weight/skew pattern) ---

  void MultibandSaturator::recomputeCrossovers()
  {
    Internal &s = *mpInternal;
    float w0 = CLAMP(0.1f, 4.0f, mBandWeight0.value());
    float w1 = CLAMP(0.1f, 4.0f, mBandWeight1.value());
    float w2 = CLAMP(0.1f, 4.0f, mBandWeight2.value());
    // Fader is -1..+1, convert to power exponent: 0 = no skew (exponent 1.0)
    // -1 = crossovers bunch high (exponent 0.25), +1 = bunch low (exponent 4.0)
    float skewParam = CLAMP(-1.0f, 1.0f, mSkew.value());
    float skew = powf(4.0f, skewParam); // -1->0.25, 0->1.0, +1->4.0

    float total = w0 + w1 + w2;
    float accum0 = w0 / total;
    float accum1 = (w0 + w1) / total;

    float b0 = (skew != 1.0f) ? powf(accum0, skew) : accum0;
    float b1 = (skew != 1.0f) ? powf(accum1, skew) : accum1;

    // Map 0-1 boundary to 20-20000 Hz (log scale)
    s.crossoverHz[0] = 20.0f * powf(1000.0f, b0);
    s.crossoverHz[1] = 20.0f * powf(1000.0f, b1);

    // Clamp to safe range
    float sr = globalConfig.sampleRate;
    s.crossoverHz[0] = CLAMP(30.0f, sr * 0.33f, s.crossoverHz[0]);
    s.crossoverHz[1] = CLAMP(s.crossoverHz[0] + 10.0f, sr * 0.33f, s.crossoverHz[1]);

    mLastWeight[0] = w0;
    mLastWeight[1] = w1;
    mLastWeight[2] = w2;
    mLastSkew = skew;
  }

  float MultibandSaturator::getCrossoverFreq(int band)
  {
    if (band < 0 || band > 1) return 0.0f;
    return mpInternal->crossoverHz[band];
  }

  void MultibandSaturator::setBandLevelBias(int band, od::Parameter *p)
  {
    if (band >= 0 && band < 3)
      mBandLevelBias[band] = p;
  }

  void MultibandSaturator::setBandBias(int band, int param, od::Parameter *p)
  {
    if (band >= 0 && band < 3 && param >= 0 && param < kBiasCount)
      mBandBias[band][param] = p;
  }

  float MultibandSaturator::getBandEnergy(int band)
  {
    if (band < 0 || band > 2) return 0.0f;
    return mpInternal->bandEnergy[band];
  }

  float MultibandSaturator::getFFTPeak(int bin)
  {
    if (bin < 0 || bin > 127) return 0.0f;
    return mpInternal->fftPeak[bin];
  }

  float MultibandSaturator::getFFTRms(int bin)
  {
    if (bin < 0 || bin > 127) return 0.0f;
    return mpInternal->fftRms[bin];
  }

  float MultibandSaturator::getBandLevel(int band)
  {
    if (band < 0 || band > 2) return 1.0f;
    // Read from Bias ref if available (tied params may not be scheduled)
    if (mBandLevelBias[band])
      return mBandLevelBias[band]->value();
    switch (band)
    {
    case 0: return mBandLevel0.value();
    case 1: return mBandLevel1.value();
    case 2: return mBandLevel2.value();
    }
    return 1.0f;
  }

  int MultibandSaturator::getCrossoverBin(int band)
  {
    if (band < 0 || band > 1) return 0;
    float sr = globalConfig.sampleRate;
    return (int)(mpInternal->crossoverHz[band] / (sr / 256.0f));
  }

  bool MultibandSaturator::getBandMuted(int band)
  {
    if (band < 0 || band > 2) return false;
    switch (band)
    {
    case 0: return mBandMute0.value() > 0.5f;
    case 1: return mBandMute1.value() > 0.5f;
    case 2: return mBandMute2.value() > 0.5f;
    }
    return false;
  }

  // --- Process ---

  void MultibandSaturator::process()
  {
    Internal &s = *mpInternal;
    float *in = mIn.buffer();
    float *out = mOut.buffer();

    float drive = CLAMP(0.0f, 16.0f, mDrive.value());
    float mix = CLAMP(0.0f, 1.0f, mMix.value());
    float outputLevel = CLAMP(0.0f, 4.0f, mOutputLevel.value());
    float tanhAmt = CLAMP(0.0f, 1.0f, mTanhAmt.value());
    float toneAmt = CLAMP(-1.0f, 1.0f, mToneAmount.value());
    float toneFreq = CLAMP(50.0f, 5000.0f, mToneFreq.value());
    float bandLevel[3] = {
      CLAMP(0.0f, 2.0f, mBandLevel0.value()),
      CLAMP(0.0f, 2.0f, mBandLevel1.value()),
      CLAMP(0.0f, 2.0f, mBandLevel2.value())
    };
    bool bandMute[3] = {
      mBandMute0.value() > 0.5f,
      mBandMute1.value() > 0.5f,
      mBandMute2.value() > 0.5f
    };
    // Read per-band params from Bias refs (direct from UI, bypasses unscheduled adapters)
    float bandAmount[3], bandBiasVal[3], bandFilterFreq[3], bandFilterMorph[3], bandFilterQ[3];
    int bandType[3];
    for (int b = 0; b < 3; b++)
    {
      bandAmount[b] = mBandBias[b][0] ? CLAMP(0.0f, 1.0f, mBandBias[b][0]->value()) : 0.5f;
      bandBiasVal[b] = mBandBias[b][1] ? CLAMP(-1.0f, 1.0f, mBandBias[b][1]->value()) : 0.0f;
      bandType[b] = mBandBias[b][2] ? CLAMP(0, 6, (int)(mBandBias[b][2]->value() + 0.5f)) : 0;
      bandFilterFreq[b] = mBandBias[b][4] ? CLAMP(20.0f, 20000.0f, mBandBias[b][4]->value()) : 1000.0f;
      // Morph is 0-4 integer in UI, convert to 0-1 for DSP
      float morphInt = mBandBias[b][5] ? mBandBias[b][5]->value() : 0.0f;
      bandFilterMorph[b] = CLAMP(0.0f, 1.0f, morphInt * 0.25f);
      bandFilterQ[b] = mBandBias[b][6] ? CLAMP(0.5f, 20.0f, mBandBias[b][6]->value()) : 0.5f;
    }

    // Dirty-check crossovers (read weights from Bias refs)
    float w0 = mBandBias[0][3] ? mBandBias[0][3]->value() : mBandWeight0.value();
    float w1 = mBandBias[1][3] ? mBandBias[1][3]->value() : mBandWeight1.value();
    float w2 = mBandBias[2][3] ? mBandBias[2][3]->value() : mBandWeight2.value();
    float skew = mSkew.value();
    if (w0 != mLastWeight[0] || w1 != mLastWeight[1] || w2 != mLastWeight[2] || skew != mLastSkew)
      recomputeCrossovers();

    float sr = globalConfig.sampleRate;

    // Tilt EQ coefficients
    float tiltCoeff = 1.0f / (1.0f + 1.0f / (2.0f * 3.14159f * toneFreq / sr));
    float tiltGain = powf(10.0f, toneAmt * 0.3f);
    float tiltLGain = 1.0f / tiltGain;

    // Crossover coefficients (2-pole = two cascaded one-pole filters, 12dB/oct)
    float xCoeff[2];
    for (int c = 0; c < 2; c++)
    {
      float fc = s.crossoverHz[c] / sr;
      xCoeff[c] = 1.0f / (1.0f + 1.0f / (2.0f * 3.14159f * fc));
    }

    // Set per-band SVF coefficients (ZDF topology)
    for (int b = 0; b < 3; b++)
    {
      float g = tanf(3.14159f * bandFilterFreq[b] / sr);
      float r = 1.0f / bandFilterQ[b];
      s.svfG[b] = g;
      s.svfR[b] = r;
      s.svfH[b] = 1.0f / (1.0f + r * g + g * g);
    }

    // Compressor constants (hoisted out of sample loop)
    float compAmt = CLAMP(0.0f, 1.0f, mCompressAmt.value());
    bool compActive = compAmt > 0.001f;
    bool scHpEnabled = mScHpf.value() > 0.5f;
    float compAttack = 1.0f - expf(-1.0f / (0.001f * sr));
    float compRelease = 1.0f - expf(-1.0f / (0.1f * sr));
    float compThreshold = 1.0f - compAmt * 0.8f;
    float compThreshDb = 10.0f * log10f(compThreshold * compThreshold + 1e-20f);
    float compRatioFactor = 1.0f - 1.0f / (1.0f + compAmt * 7.0f);
    float scHpCoeff = 100.0f / sr;

    float energyAccum[3] = {0.0f, 0.0f, 0.0f};

    for (int i = 0; i < FRAMELENGTH; i++)
    {
      float dry = in[i];
      float x = dry * drive;

      // Tilt EQ
      s.tiltLpState += (x - s.tiltLpState) * tiltCoeff;
      x = s.tiltLpState * tiltLGain + (x - s.tiltLpState) * tiltGain;

      // Band split: two 2-pole (cascaded one-pole) crossovers
      // Crossover 0: x -> lp0 (band0), hp0 (remainder)
      s.xoverState[0][0] += (x - s.xoverState[0][0]) * xCoeff[0];
      s.xoverState[0][1] += (s.xoverState[0][0] - s.xoverState[0][1]) * xCoeff[0];
      float band0 = s.xoverState[0][1];
      float hp0 = x - band0;

      // Crossover 1: hp0 -> lp1 (band1), hp1 (band2)
      s.xoverState[1][0] += (hp0 - s.xoverState[1][0]) * xCoeff[1];
      s.xoverState[1][1] += (s.xoverState[1][0] - s.xoverState[1][1]) * xCoeff[1];
      float band1 = s.xoverState[1][1];
      float band2 = hp0 - band1;

      // Per-band shaping + filtering
      float bandSig[3] = { band0, band1, band2 };
      for (int b = 0; b < 3; b++)
      {
        if (bandMute[b]) continue;

        // Waveshaper
        if (bandAmount[b] > 0.001f)
          bandSig[b] = applyShaper(bandSig[b], bandType[b], bandAmount[b], bandBiasVal[b]);

        // SVF morph filter: off(0) -> LP(0.25) -> BP(0.5) -> HP(0.75) -> Notch(1.0)
        float morph = bandFilterMorph[b];
        if (morph > 0.01f)
        {
          float g = s.svfG[b], r = s.svfR[b], h = s.svfH[b];
          float hp = (bandSig[b] - r * s.svfState1[b] - g * s.svfState1[b] - s.svfState2[b]) * h;
          float bp = g * hp + s.svfState1[b];
          s.svfState1[b] = g * hp + bp;
          float lp = g * bp + s.svfState2[b];
          s.svfState2[b] = g * bp + lp;

          // Remap 0.01-1.0 to filter sweep
          float m = (morph - 0.01f) / 0.99f; // 0-1 within active range
          float lp_g, bp_g, hp_g;
          if (m < 0.333f)
          {
            float t = m * 3.0f;
            lp_g = 1.0f - t; bp_g = t; hp_g = 0.0f;
          }
          else if (m < 0.666f)
          {
            float t = (m - 0.333f) * 3.0f;
            lp_g = 0.0f; bp_g = 1.0f - t; hp_g = t;
          }
          else
          {
            float t = (m - 0.666f) * 3.0f;
            lp_g = t; bp_g = 0.0f; hp_g = 1.0f;
          }
          bandSig[b] = lp * lp_g + bp * bp_g + hp * hp_g;
        }
      }
      float wet = 0.0f;
      for (int b = 0; b < 3; b++)
      {
        if (!bandMute[b])
        {
          float bv = bandSig[b] * bandLevel[b];
          wet += bv;
          energyAccum[b] += bv * bv;
        }
      }

      // FFT ring buffer (capture post-sum signal)
      s.ringBuf[s.ringPos] = wet;
      s.ringPos = (s.ringPos + 1) & 255;

      // Compressor
      if (compActive)
      {
        float sc = wet;
        if (scHpEnabled)
        {
          sc = wet - s.scHpState;
          s.scHpState += sc * scHpCoeff;
        }
        float energy = sc * sc;
        if (energy > s.compDetector)
          s.compDetector += (energy - s.compDetector) * compAttack;
        else
          s.compDetector += (energy - s.compDetector) * compRelease;

        float levelDb = 10.0f * log10f(s.compDetector + 1e-20f);
        float overDb = levelDb - compThreshDb;
        if (overDb > 0.0f)
        {
          float gainDb = overDb * compRatioFactor;
          wet *= powf(10.0f, -gainDb * 0.05f);
        }
      }

      // Output saturation
      if (tanhAmt > 0.001f)
      {
        float d = 1.0f + tanhAmt * 3.0f;
        wet = wet * (1.0f - tanhAmt) + fast_tanh(wet * d) * tanhAmt;
      }

      wet *= outputLevel;

      // Dry/wet mix
      out[i] = dry + (wet - dry) * mix;
    }

    // Update band energy (smoothed)
    float energyCoeff = 0.1f;
    for (int b = 0; b < 3; b++)
    {
      float rms = sqrtf(energyAccum[b] / (float)FRAMELENGTH);
      s.bandEnergy[b] += (rms - s.bandEnergy[b]) * energyCoeff;
    }

    // FFT: compute every 4 frames (~12 FFTs/sec at 48kHz/128)
    s.fftFrameCount++;
    if (s.fftFrameCount >= 4 && s.fftSetup && s.fftIn && s.fftOut)
    {
      s.fftFrameCount = 0;
      for (int k = 0; k < 256; k++)
        s.fftIn[k] = s.ringBuf[(s.ringPos + k) & 255] * s.hannWindow[k];

      pffft_transform_ordered(s.fftSetup, s.fftIn, s.fftOut, NULL, PFFFT_FORWARD);

      float peakDecay = 0.92f;
      float rmsSmooth = 0.3f;
      for (int k = 0; k < 128; k++)
      {
        float re = s.fftOut[k * 2];
        float im = s.fftOut[k * 2 + 1];
        float mag = sqrtf(re * re + im * im) / 256.0f;
        if (mag > s.fftPeak[k])
          s.fftPeak[k] = mag;
        else
          s.fftPeak[k] *= peakDecay;
        s.fftRms[k] += (mag - s.fftRms[k]) * rmsSmooth;
      }
    }
  }

} // namespace stolmine
