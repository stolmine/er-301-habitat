// Filterbank -- parallel resonant fixed filter bank for ER-301

#include "Filterbank.h"
#include <od/config.h>
#include <hal/ops.h>
#include <string.h>
#include <math.h>

// Must define TEST to avoid stmlib ARM inline asm on Cortex-A8
#ifndef TEST
#define TEST
#endif
#include "stmlib/dsp/filter.h"

namespace stolmine
{

  // --- Built-in scale tables (cents, 1200 = octave) ---

  struct ScaleData
  {
    const float *degrees;
    int count;
  };

  static const float kChromatic[] = {0, 100, 200, 300, 400, 500, 600, 700, 800, 900, 1000, 1100};
  static const float kMajor[] = {0, 200, 400, 500, 700, 900, 1100};
  static const float kNaturalMinor[] = {0, 200, 300, 500, 700, 800, 1000};
  static const float kHarmonicMinor[] = {0, 200, 300, 500, 700, 800, 1100};
  static const float kMajorPent[] = {0, 200, 400, 700, 900};
  static const float kMinorPent[] = {0, 300, 500, 700, 1000};
  static const float kWholeTone[] = {0, 200, 400, 600, 800, 1000};
  static const float kDorian[] = {0, 200, 300, 500, 700, 900, 1000};
  static const float kPhrygian[] = {0, 100, 300, 500, 700, 800, 1000};
  static const float kLydian[] = {0, 200, 400, 600, 700, 900, 1100};
  static const float kMixolydian[] = {0, 200, 400, 500, 700, 900, 1000};
  static const float kLocrian[] = {0, 100, 300, 500, 600, 800, 1000};

  static const ScaleData kScales[SCALE_COUNT] = {
      {kChromatic, 12},
      {kMajor, 7},
      {kNaturalMinor, 7},
      {kHarmonicMinor, 7},
      {kMajorPent, 5},
      {kMinorPent, 5},
      {kWholeTone, 6},
      {kDorian, 7},
      {kPhrygian, 7},
      {kLydian, 7},
      {kMixolydian, 7},
      {kLocrian, 7},
  };

  // --- Internal struct ---

  struct Filterbank::Internal
  {
    float freqHz[kMaxBands];
    float gain[kMaxBands];
    int filterType[kMaxBands];

    stmlib::Svf filters[kMaxBands];

    float targetFreq[kMaxBands]; // normalized (Hz / sampleRate)
    float currentFreq[kMaxBands];

    // Candidate buffer for scale distribution (avoid stack allocation)
    float candidates[256];
    float logCandidates[256];
    bool used[256];
    float selected[kMaxBands];
    float selectedLog[kMaxBands];
    float rotated[kMaxBands];
    int candidateCount;

    // Custom scale slots (cents per degree)
    float customDegrees[kMaxCustomScales][kMaxScaleDegrees];
    int customDegreeCounts[kMaxCustomScales];
    int numCustomScales;
    float customBuildBuf[kMaxScaleDegrees]; // temp buffer for loading
    int customBuildCount;

    void Init()
    {
      float logMin = logf(100.0f);
      float logMax = logf(10000.0f);
      for (int i = 0; i < kMaxBands; i++)
      {
        float t = (float)i / (float)(kMaxBands - 1);
        float hz = expf(logMin + t * (logMax - logMin));
        freqHz[i] = hz;
        targetFreq[i] = hz / 48000.0f;
        currentFreq[i] = targetFreq[i];
        gain[i] = 1.0f;
        filterType[i] = FTYPE_PEAK;
        filters[i].Init();
      }
      candidateCount = 0;
      numCustomScales = 0;
      customBuildCount = 0;
      memset(customDegreeCounts, 0, sizeof(customDegreeCounts));
    }
  };

  Filterbank::Filterbank()
  {
    addInput(mIn);
    addOutput(mOut);
    addParameter(mMix);
    addParameter(mMacroQ);
    addParameter(mBandCount);
    addParameter(mScale);
    addParameter(mRotate);
    addParameter(mSkew);
    addParameter(mSlew);
    addParameter(mInputLevel);
    addParameter(mOutputLevel);
    addParameter(mTanhAmt);
    addParameter(mEditFreq);
    addParameter(mEditGain);
    addParameter(mEditType);

    mpInternal = new Internal();
    mpInternal->Init();
  }

  Filterbank::~Filterbank()
  {
    delete mpInternal;
  }

  // --- Band data accessors ---

  float Filterbank::getBandFreq(int i)
  {
    return mpInternal->freqHz[CLAMP(0, kMaxBands - 1, i)];
  }

  void Filterbank::setBandFreq(int i, float hz)
  {
    i = CLAMP(0, kMaxBands - 1, i);
    hz = CLAMP(20.0f, 20000.0f, hz);
    mpInternal->freqHz[i] = hz;
    mpInternal->targetFreq[i] = hz / globalConfig.sampleRate;
    mpInternal->currentFreq[i] = mpInternal->targetFreq[i];
  }

  float Filterbank::getBandGain(int i)
  {
    return mpInternal->gain[CLAMP(0, kMaxBands - 1, i)];
  }

  void Filterbank::setBandGain(int i, float v)
  {
    mpInternal->gain[CLAMP(0, kMaxBands - 1, i)] = CLAMP(0.0f, 4.0f, v);
  }

  int Filterbank::getBandType(int i)
  {
    return mpInternal->filterType[CLAMP(0, kMaxBands - 1, i)];
  }

  void Filterbank::setBandType(int i, int v)
  {
    mpInternal->filterType[CLAMP(0, kMaxBands - 1, i)] = CLAMP(0, (int)FTYPE_COUNT - 1, v);
  }

  void Filterbank::loadBand(int i)
  {
    i = CLAMP(0, kMaxBands - 1, i);
    mEditFreq.hardSet(mpInternal->freqHz[i]);
    mEditGain.hardSet(mpInternal->gain[i]);
    mEditType.hardSet((float)mpInternal->filterType[i]);
  }

  void Filterbank::storeBand(int i)
  {
    i = CLAMP(0, kMaxBands - 1, i);
    float hz = CLAMP(20.0f, 20000.0f, mEditFreq.value());
    mpInternal->freqHz[i] = hz;
    mpInternal->targetFreq[i] = hz / globalConfig.sampleRate;
    mpInternal->currentFreq[i] = mpInternal->targetFreq[i];
    mpInternal->gain[i] = CLAMP(0.0f, 4.0f, mEditGain.value());
    mpInternal->filterType[i] = CLAMP(0, (int)FTYPE_COUNT - 1, (int)(mEditType.value() + 0.5f));
  }

  int Filterbank::getBandCount()
  {
    mCachedBandCount = CLAMP(2, kMaxBands, (int)(mBandCount.value() + 0.5f));
    return mCachedBandCount;
  }

  // --- Scale distribution ---

  // --- Custom scale loading ---

  void Filterbank::beginCustomScale(int slot)
  {
    if (slot < 0 || slot >= kMaxCustomScales) return;
    mpInternal->customBuildCount = 0;
  }

  void Filterbank::addCustomDegree(float cents)
  {
    Internal &s = *mpInternal;
    if (s.customBuildCount < kMaxScaleDegrees)
    {
      s.customBuildBuf[s.customBuildCount++] = cents;
    }
  }

  void Filterbank::endCustomScale(int slot)
  {
    Internal &s = *mpInternal;
    if (slot < 0 || slot >= kMaxCustomScales) return;
    s.customDegreeCounts[slot] = s.customBuildCount;
    for (int i = 0; i < s.customBuildCount; i++)
      s.customDegrees[slot][i] = s.customBuildBuf[i];
    if (slot >= s.numCustomScales)
      s.numCustomScales = slot + 1;
    mLastScale = -1; // force dirty
  }

  int Filterbank::getCustomScaleCount()
  {
    return mpInternal->numCustomScales;
  }

  // --- Scale distribution ---

  void Filterbank::distributeFrequencies()
  {
    Internal &s = *mpInternal;
    int scaleIdx = CLAMP(0, (int)SCALE_CUSTOM + kMaxCustomScales - 1, (int)(mScale.value() + 0.5f));
    int bandCount = mCachedBandCount;
    int rotate = (int)(mRotate.value() + 0.5f);
    float skew = CLAMP(0.0f, 1.0f, mSkew.value());

    // Get scale degrees (built-in or custom slot)
    const float *degrees;
    int degreeCount;
    if (scaleIdx >= (int)SCALE_CUSTOM)
    {
      int slot = scaleIdx - (int)SCALE_CUSTOM;
      if (slot < s.numCustomScales && s.customDegreeCounts[slot] > 0)
      {
        degrees = s.customDegrees[slot];
        degreeCount = s.customDegreeCounts[slot];
      }
      else
      {
        return; // no valid custom scale in this slot
      }
    }
    else if (scaleIdx < (int)SCALE_CUSTOM)
    {
      degrees = kScales[scaleIdx].degrees;
      degreeCount = kScales[scaleIdx].count;
    }
    else
    {
      return; // no valid scale
    }

    float sr = globalConfig.sampleRate;

    // Generate all candidate frequencies across 60Hz-16kHz
    float minHz = 60.0f;
    float maxHz = 16000.0f;
    float baseHz = 16.3516f; // C0
    s.candidateCount = 0;

    for (int octave = 1; octave <= 10 && s.candidateCount < 255; octave++)
    {
      float octaveHz = baseHz * (float)(1 << octave);
      for (int d = 0; d < degreeCount && s.candidateCount < 255; d++)
      {
        float hz = octaveHz * powf(2.0f, degrees[d] / 1200.0f);
        if (hz >= minHz && hz <= maxHz)
        {
          s.candidates[s.candidateCount++] = hz;
        }
      }
    }

    if (s.candidateCount == 0 || bandCount == 0)
      return;

    // Convert to log for distance calculations
    for (int i = 0; i < s.candidateCount; i++)
      s.logCandidates[i] = logf(s.candidates[i]);

    // Greedy selection: maximize minimum log-distance
    memset(s.used, 0, sizeof(s.used));

    // Start near geometric center, skewed
    float logMin = logf(minHz);
    float logMax = logf(maxHz);
    float logCenter = logMin + (logMax - logMin) * skew;

    int bestIdx = 0;
    float bestDist = 1e10f;
    for (int i = 0; i < s.candidateCount; i++)
    {
      float d = fabsf(s.logCandidates[i] - logCenter);
      if (d < bestDist)
      {
        bestDist = d;
        bestIdx = i;
      }
    }
    s.selected[0] = s.candidates[bestIdx];
    s.used[bestIdx] = true;

    int numSelected = 1;
    s.selectedLog[0] = s.logCandidates[bestIdx];

    // Pick remaining bands
    for (int n = 1; n < bandCount && n < s.candidateCount; n++)
    {
      int bestCandidate = -1;
      float bestMinDist = -1.0f;

      for (int i = 0; i < s.candidateCount; i++)
      {
        if (s.used[i])
          continue;
        float minDist = 1e10f;
        for (int j = 0; j < numSelected; j++)
        {
          float d = fabsf(s.logCandidates[i] - s.selectedLog[j]);
          if (d < minDist)
            minDist = d;
        }
        if (minDist > bestMinDist)
        {
          bestMinDist = minDist;
          bestCandidate = i;
        }
      }

      if (bestCandidate >= 0)
      {
        s.selected[numSelected] = s.candidates[bestCandidate];
        s.selectedLog[numSelected] = s.logCandidates[bestCandidate];
        s.used[bestCandidate] = true;
        numSelected++;
      }
    }

    // Sort ascending
    for (int i = 0; i < numSelected - 1; i++)
    {
      for (int j = i + 1; j < numSelected; j++)
      {
        if (s.selected[j] < s.selected[i])
        {
          float tmp = s.selected[i];
          s.selected[i] = s.selected[j];
          s.selected[j] = tmp;
        }
      }
    }

    // Apply rotate (circular shift)
    if (rotate != 0 && numSelected > 1)
    {
      int n = numSelected;
      for (int i = 0; i < n; i++)
      {
        int src = ((i + rotate) % n + n) % n;
        s.rotated[i] = s.selected[src];
      }
      memcpy(s.selected, s.rotated, sizeof(float) * n);
    }

    // Set target frequencies (slew handles transition)
    for (int i = 0; i < numSelected && i < bandCount; i++)
    {
      s.freqHz[i] = s.selected[i];
      s.targetFreq[i] = s.selected[i] / sr;
    }
  }

  void Filterbank::checkDistributionDirty()
  {
    int scaleIdx = CLAMP(0, (int)SCALE_CUSTOM + kMaxCustomScales - 1, (int)(mScale.value() + 0.5f));
    int rotate = (int)(mRotate.value() + 0.5f);
    int bandCount = mCachedBandCount;
    float skew = CLAMP(0.0f, 1.0f, mSkew.value());

    if (scaleIdx != mLastScale || rotate != mLastRotate ||
        bandCount != mLastBandCount || skew != mLastSkew)
    {
      mLastScale = scaleIdx;
      mLastRotate = rotate;
      mLastBandCount = bandCount;
      mLastSkew = skew;
      distributeFrequencies();
    }
  }

  // --- Filter coefficient update ---

  void Filterbank::updateFilterCoefficients()
  {
    Internal &s = *mpInternal;
    int bandCount = mCachedBandCount;
    float macroQ = CLAMP(0.0f, 1.0f, mMacroQ.value());
    float slewTime = CLAMP(0.0f, 5.0f, mSlew.value());

    // Q: 1-100 range, quadratic scaling for fine control at low end
    float baseQ = 1.0f + 99.0f * macroQ * macroQ;
    float q_loss = macroQ * (2.0f - macroQ) * 0.85f + 0.15f;

    // Slew: direct seconds, exponential smoothing per frame
    float slewCoeff = 0.0f;
    if (slewTime > 0.001f)
    {
      float framesPerSec = globalConfig.sampleRate / (float)FRAMELENGTH;
      float slewFrames = slewTime * framesPerSec;
      slewCoeff = 1.0f - 1.0f / slewFrames;
      if (slewCoeff < 0.0f)
        slewCoeff = 0.0f;
    }

    float q = baseQ;
    for (int i = 0; i < bandCount; i++)
    {
      if (slewCoeff > 0.0f)
        s.currentFreq[i] += (s.targetFreq[i] - s.currentFreq[i]) * (1.0f - slewCoeff);
      else
        s.currentFreq[i] = s.targetFreq[i];

      float freq = CLAMP(0.0001f, 0.49f, s.currentFreq[i]);
      // Q increases slightly with frequency for even-sounding resonance
      float bandQ = q * (0.5f + freq * 2.0f);
      if (bandQ < 0.5f) bandQ = 0.5f;
      // LP mode: moderate Q floor for audible resonance peak
      if (s.filterType[i] == FTYPE_LP && bandQ < 5.0f)
        bandQ = 5.0f;
      // Resonator mode: hard Q floor so bands always ring
      if (s.filterType[i] == FTYPE_RESON && bandQ < 20.0f)
        bandQ = 20.0f;
      s.filters[i].set_f_q<stmlib::FREQUENCY_FAST>(freq, bandQ);

      q *= q_loss;
    }
  }

  // --- Evaluate composite response ---

  float Filterbank::evaluateResponse(float normalizedFreq)
  {
    Internal &s = *mpInternal;
    int bandCount = mCachedBandCount;

    float logMin = logf(20.0f);
    float logMax = logf(20000.0f);
    float hz = expf(logMin + normalizedFreq * (logMax - logMin));
    float w = hz / globalConfig.sampleRate;

    float macroQ = CLAMP(0.0f, 1.0f, mMacroQ.value());
    float baseQ = 1.0f + 99.0f * macroQ * macroQ;
    float q_loss = macroQ * (2.0f - macroQ) * 0.85f + 0.15f;

    float totalResponse = 0.0f;
    float q = baseQ;
    for (int i = 0; i < bandCount; i++)
    {
      float fc = s.currentFreq[i];
      if (fc < 0.0001f)
      {
        q *= q_loss;
        continue;
      }

      float bandQ = q * (0.5f + fc * 2.0f);
      if (bandQ < 0.5f) bandQ = 0.5f;
      float wSq = w * w;
      float fcSq = fc * fc;
      float diff = wSq - fcSq;
      float bw = w * fc / bandQ;
      float denom = diff * diff + bw * bw;
      float mag = (denom > 0.000001f) ? bw / sqrtf(denom) : 1.0f;

      totalResponse += mag * s.gain[i];
      q *= q_loss;
    }

    return totalResponse;
  }

  // --- Process ---

  void Filterbank::process()
  {
    Internal &s = *mpInternal;

    float *in = mIn.buffer();
    float *out = mOut.buffer();

    int bandCount = CLAMP(2, kMaxBands, (int)(mBandCount.value() + 0.5f));
    mCachedBandCount = bandCount;

    // Auto-detect scale/rotate/skew/bandCount changes and redistribute
    checkDistributionDirty();

    float mix = CLAMP(0.0f, 1.0f, mMix.value());
    float inputLevel = CLAMP(0.0f, 4.0f, mInputLevel.value());
    float outputLevel = CLAMP(0.0f, 4.0f, mOutputLevel.value());
    float tanhAmt = CLAMP(0.0f, 1.0f, mTanhAmt.value());

    updateFilterCoefficients();

    // Normalize parallel sum by 1/sqrt(bandCount)
    float sumNorm = 1.0f / sqrtf((float)bandCount);

    for (int i = 0; i < FRAMELENGTH; i++)
    {
      float x = in[i] * inputLevel;
      float wet = 0.0f;

      for (int b = 0; b < bandCount; b++)
      {
        float bandOut;
        switch (s.filterType[b])
        {
        case FTYPE_BPF:
          bandOut = s.filters[b].Process<stmlib::FILTER_MODE_BAND_PASS_NORMALIZED>(x);
          break;
        case FTYPE_LP:
          bandOut = s.filters[b].Process<stmlib::FILTER_MODE_LOW_PASS>(x);
          break;
        case FTYPE_RESON:
          bandOut = s.filters[b].Process<stmlib::FILTER_MODE_BAND_PASS>(x);
          break;
        case FTYPE_PEAK:
        default:
          bandOut = s.filters[b].Process<stmlib::FILTER_MODE_BAND_PASS>(x);
          break;
        }
        wet += bandOut * s.gain[b];
      }
      wet *= sumNorm;

      float mixed = x * (1.0f - mix) + wet * mix;

      if (tanhAmt > 0.001f)
      {
        float drive = 1.0f + tanhAmt * 3.0f;
        mixed = mixed * (1.0f - tanhAmt) + tanhf(mixed * drive) * tanhAmt;
      }

      out[i] = mixed * outputLevel;
    }
  }

} // namespace stolmine
