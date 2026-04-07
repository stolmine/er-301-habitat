#include "MultibandSaturator.h"
#include <od/config.h>
#include <hal/ops.h>
// stmlib::Svf used later for per-band post-shaper filter (Phase 3)
#include <math.h>
#include <string.h>
#include <new>

namespace stolmine
{

  static inline float fast_tanh(float x)
  {
    if (x < -4.0f) return -1.0f;
    if (x >  4.0f) return  1.0f;
    float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
  }

  struct MultibandSaturator::Internal
  {
    // Tilt EQ state
    float tiltLpState = 0.0f;

    // Crossover (2 split points, 2-pole each for 12dB/oct)
    float crossoverHz[2];
    float xoverState[2][2];  // [crossover][cascade stage]

    // Compressor state
    float compDetector = 0.0f;
    float scHpState = 0.0f;

    // Per-band energy (for graphic)
    float bandEnergy[3];

    void Init()
    {
      tiltLpState = 0.0f;
      crossoverHz[0] = 200.0f;
      crossoverHz[1] = 2000.0f;
      for (int i = 0; i < 2; i++)
        for (int j = 0; j < 2; j++)
          xoverState[i][j] = 0.0f;
      compDetector = 0.0f;
      scHpState = 0.0f;
      for (int i = 0; i < 3; i++)
        bandEnergy[i] = 0.0f;
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
  }

  MultibandSaturator::~MultibandSaturator()
  {
    delete mpInternal;
  }

  // --- Crossover frequency derivation (from Etcher weight/skew pattern) ---

  void MultibandSaturator::recomputeCrossovers()
  {
    Internal &s = *mpInternal;
    float w0 = CLAMP(0.1f, 4.0f, mBandWeight0.value());
    float w1 = CLAMP(0.1f, 4.0f, mBandWeight1.value());
    float w2 = CLAMP(0.1f, 4.0f, mBandWeight2.value());
    float skew = CLAMP(0.1f, 4.0f, mSkew.value());

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

  float MultibandSaturator::getBandEnergy(int band)
  {
    if (band < 0 || band > 2) return 0.0f;
    return mpInternal->bandEnergy[band];
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

    // Dirty-check crossovers
    float w0 = mBandWeight0.value();
    float w1 = mBandWeight1.value();
    float w2 = mBandWeight2.value();
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

      // TODO Phase 3: per-band shaping + filtering here

      // Sum bands with level and mute
      float bandSig[3] = { band0, band1, band2 };
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

      // TODO Phase 4: compressor here

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
  }

} // namespace stolmine
