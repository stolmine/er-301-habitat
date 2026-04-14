// Chime -- coupled pulse-excited resonator bank

#include "Chime.h"
#include <od/config.h>
#include <hal/ops.h>
#include <string.h>
#include <math.h>

#ifndef TEST
#define TEST
#endif
#include "stmlib/dsp/filter.h"

namespace stolmine
{
  // --- Built-in scale tables (cents) ---

  struct ScaleData { const float *degrees; int count; };

  static const float kChimeChromatic[] = {0,100,200,300,400,500,600,700,800,900,1000,1100};
  static const float kChimeMajor[]     = {0,200,400,500,700,900,1100};
  static const float kChimeMinorPent[] = {0,300,500,700,1000};
  static const float kChimeMajorPent[] = {0,200,400,700,900};
  static const float kChimeWhole[]     = {0,200,400,600,800,1000};
  // Harmonic series (first 8 overtones above a fundamental, in cents)
  static const float kChimeHarmonic[]  = {0,1200,1902,2400,2786,3102,3369,3600};
  // Pelog-ish: 5-note stretched-interval scale common to Javanese gamelan
  static const float kChimePelog[]     = {0,120,270,520,670,820,970};
  // Slendro-ish: 5-note roughly equal-stretched intervals
  static const float kChimeSlendro[]   = {0,240,480,720,960};

  static const ScaleData kChimeScales[CHIME_SCALE_COUNT] = {
      {kChimeChromatic, 12},
      {kChimeMajor, 7},
      {kChimeMinorPent, 5},
      {kChimeMajorPent, 5},
      {kChimeWhole, 6},
      {kChimeHarmonic, 8},
      {kChimePelog, 7},
      {kChimeSlendro, 5}
  };

  struct Chime::Internal
  {
    stmlib::Svf filters[kChimeMaxBands];
    float freqHz[kChimeMaxBands];     // base frequency
    float currentHz[kChimeMaxBands];  // detuned cutoff actually set on filter
    float gain[kChimeMaxBands];
    float lastOut[kChimeMaxBands];    // prev-sample output for coupling
    float envFollower[kChimeMaxBands];

    // Candidate buffer for scale distribution (heap via Internal)
    float candidates[256];
    float logCandidates[256];
    bool  used[256];
    float selected[kChimeMaxBands];
    float selectedLog[kChimeMaxBands];
    float rotated[kChimeMaxBands];
    int   candidateCount;

    void Init()
    {
      // Log-spaced defaults across 100 Hz .. 4 kHz so cold-boot pitches are audible
      float logMin = logf(100.0f), logMax = logf(4000.0f);
      for (int i = 0; i < kChimeMaxBands; i++)
      {
        float t = (float)i / (float)(kChimeMaxBands - 1);
        float hz = expf(logMin + t * (logMax - logMin));
        freqHz[i] = hz;
        currentHz[i] = hz;
        gain[i] = 1.0f;
        lastOut[i] = 0.0f;
        envFollower[i] = 0.0f;
        filters[i].Init();
      }
      candidateCount = 0;
    }
  };

  Chime::Chime()
  {
    addInput(mIn);
    addInput(mTrigger);
    addOutput(mOut);
    addParameter(mBandCount);
    addParameter(mScale);
    addParameter(mRotate);
    addParameter(mQ);
    addParameter(mCouple);
    addParameter(mDetune);
    addParameter(mDrive);
    addParameter(mLevel);
    addParameter(mInputLevel);
    addParameter(mImpulseGain);
    addParameter(mSpread);

    mpInternal = new Internal();
    mpInternal->Init();
  }

  Chime::~Chime() { delete mpInternal; }

  int   Chime::getBandCount() { return mCachedBandCount; }
  float Chime::getBandFreq(int i) { return mpInternal->currentHz[CLAMP(0, kChimeMaxBands - 1, i)]; }
  float Chime::getBandEnergy(int i) { return mpInternal->envFollower[CLAMP(0, kChimeMaxBands - 1, i)]; }
  float Chime::getCouple() { return CLAMP(0.0f, 1.0f, mCouple.value()); }

  // --- Scale distribution (greedy max-min log-distance), lifted from Filterbank ---

  void Chime::distributeFrequencies()
  {
    Internal &s = *mpInternal;
    int scaleIdx = CLAMP(0, (int)CHIME_SCALE_COUNT - 1, (int)(mScale.value() + 0.5f));
    int bandCount = mCachedBandCount;
    int rotate = (int)(mRotate.value() + 0.5f);

    const float *degrees = kChimeScales[scaleIdx].degrees;
    int degreeCount = kChimeScales[scaleIdx].count;

    float minHz = 80.0f, maxHz = 8000.0f;
    float baseHz = 16.3516f; // C0
    s.candidateCount = 0;

    for (int oct = 1; oct <= 10 && s.candidateCount < 255; oct++)
    {
      float octaveHz = baseHz * (float)(1 << oct);
      for (int d = 0; d < degreeCount && s.candidateCount < 255; d++)
      {
        float hz = octaveHz * powf(2.0f, degrees[d] / 1200.0f);
        if (hz >= minHz && hz <= maxHz)
          s.candidates[s.candidateCount++] = hz;
      }
    }

    if (s.candidateCount == 0 || bandCount == 0) return;

    for (int i = 0; i < s.candidateCount; i++)
      s.logCandidates[i] = logf(s.candidates[i]);

    memset(s.used, 0, sizeof(s.used));

    float logMin = logf(minHz), logMax = logf(maxHz);
    float logCenter = (logMin + logMax) * 0.5f;

    // Pick first band nearest geometric center
    int bestIdx = 0;
    float bestDist = 1e10f;
    for (int i = 0; i < s.candidateCount; i++)
    {
      float d = fabsf(s.logCandidates[i] - logCenter);
      if (d < bestDist) { bestDist = d; bestIdx = i; }
    }
    s.selected[0] = s.candidates[bestIdx];
    s.selectedLog[0] = s.logCandidates[bestIdx];
    s.used[bestIdx] = true;

    int numSelected = 1;
    for (int n = 1; n < bandCount && n < s.candidateCount; n++)
    {
      int bestCandidate = -1;
      float bestMinDist = -1.0f;
      for (int i = 0; i < s.candidateCount; i++)
      {
        if (s.used[i]) continue;
        float minDist = 1e10f;
        for (int j = 0; j < numSelected; j++)
        {
          float d = fabsf(s.logCandidates[i] - s.selectedLog[j]);
          if (d < minDist) minDist = d;
        }
        if (minDist > bestMinDist) { bestMinDist = minDist; bestCandidate = i; }
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
      for (int j = i + 1; j < numSelected; j++)
        if (s.selected[j] < s.selected[i])
        {
          float tmp = s.selected[i];
          s.selected[i] = s.selected[j];
          s.selected[j] = tmp;
        }

    // Rotate
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

    for (int i = 0; i < numSelected && i < bandCount; i++)
    {
      s.freqHz[i] = s.selected[i];
      s.currentHz[i] = s.selected[i];
    }
  }

  void Chime::checkDistributionDirty()
  {
    int scaleIdx = CLAMP(0, (int)CHIME_SCALE_COUNT - 1, (int)(mScale.value() + 0.5f));
    int rotate = (int)(mRotate.value() + 0.5f);
    int bandCount = mCachedBandCount;
    if (scaleIdx != mLastScale || rotate != mLastRotate || bandCount != mLastBandCount)
    {
      mLastScale = scaleIdx;
      mLastRotate = rotate;
      mLastBandCount = bandCount;
      distributeFrequencies();
    }
  }

  void Chime::updateFilterCoefficients(float qNorm, float detuneBudget)
  {
    Internal &s = *mpInternal;
    int bandCount = mCachedBandCount;
    float sr = globalConfig.sampleRate;

    // Q mapping: quadratic for fine control at the low end, range 10 .. 120
    float q = 10.0f + 110.0f * qNorm * qNorm;

    for (int i = 0; i < bandCount; i++)
    {
      // Pick the loudest neighbour as the detune modulator source.
      int nL = (i == 0) ? bandCount - 1 : i - 1;
      int nR = (i == bandCount - 1) ? 0 : i + 1;
      float modEnv = s.envFollower[nL] > s.envFollower[nR] ? s.envFollower[nL] : s.envFollower[nR];

      // detuneBudget already scaled to be a small per-sample deviation factor.
      float detuneFactor = 1.0f + detuneBudget * modEnv;
      float hz = s.freqHz[i] * detuneFactor;
      s.currentHz[i] = hz;

      float fNorm = CLAMP(0.0001f, 0.49f, hz / sr);
      s.filters[i].set_f_q<stmlib::FREQUENCY_FAST>(fNorm, q);
    }
  }

  // --- Process ---

  void Chime::process()
  {
    Internal &s = *mpInternal;

    float *in = mIn.buffer();
    float *trig = mTrigger.buffer();
    float *out = mOut.buffer();

    int bandCount = CLAMP(2, kChimeMaxBands, (int)(mBandCount.value() + 0.5f));
    mCachedBandCount = bandCount;

    checkDistributionDirty();

    float qNorm      = CLAMP(0.0f, 1.0f, mQ.value());
    float coupleAmt  = CLAMP(0.0f, 1.0f, mCouple.value());
    float detuneAmt  = CLAMP(0.0f, 1.0f, mDetune.value());
    float driveAmt   = CLAMP(0.0f, 1.0f, mDrive.value());
    float level      = CLAMP(0.0f, 2.0f, mLevel.value());
    float inputLevel = CLAMP(0.0f, 2.0f, mInputLevel.value());
    float impGain    = CLAMP(0.0f, 2.0f, mImpulseGain.value());
    float spread     = CLAMP(0.0f, 1.0f, mSpread.value());

    // detuneBudget: small multiplicative deviation, tuned so full detune * full
    // envelope gives roughly ±2% cutoff shift (~1/3 of a semitone).
    float detuneBudget = detuneAmt * 0.02f;

    updateFilterCoefficients(qNorm, detuneBudget);

    float sumNorm = 1.0f / sqrtf((float)bandCount);

    for (int i = 0; i < FRAMELENGTH; i++)
    {
      bool trigHigh = trig[i] > 0.5f;
      bool trigRise = trigHigh && !mTriggerWasHigh;
      mTriggerWasHigh = trigHigh;

      float audioIn = in[i] * inputLevel;
      float sum = 0.0f;

      // Determine per-band impulse weight if triggered.
      // spread=0: all bands get 1. spread=1: only mTriggerIndex fires (weight=bandCount).
      // In between: mTriggerIndex gets extra weight, others get the residual.
      int focusedIdx = mTriggerIndex;
      if (trigRise)
      {
        mTriggerIndex = (mTriggerIndex + 1) % bandCount;
      }

      for (int b = 0; b < bandCount; b++)
      {
        int bL = (b == 0) ? bandCount - 1 : b - 1;
        int bR = (b == bandCount - 1) ? 0 : b + 1;

        // Nearest-neighbour cross-coupling -- reads lastOut from previous sample.
        float coupleIn = coupleAmt * 0.5f * (s.lastOut[bL] + s.lastOut[bR]);

        float x = audioIn + coupleIn;

        if (trigRise)
        {
          float w = (1.0f - spread) + spread * ((b == focusedIdx) ? (float)bandCount : 0.0f);
          x += impGain * w;
        }

        float y = s.filters[b].Process<stmlib::FILTER_MODE_BAND_PASS_NORMALIZED>(x);

        // Soft-limit feedback path so high Couple + high Q can't run away.
        float ySoft = tanhf(y * 1.5f) * (1.0f / 1.5f);
        s.lastOut[b] = ySoft;

        // Envelope follower (~5 ms attack at 48 kHz)
        float absY = y < 0.0f ? -y : y;
        s.envFollower[b] = s.envFollower[b] * 0.995f + absY * 0.005f;

        sum += y * s.gain[b];
      }

      sum *= sumNorm;

      if (driveAmt > 0.001f)
      {
        float d = 1.0f + driveAmt * 3.0f;
        sum = sum * (1.0f - driveAmt) + tanhf(sum * d) * driveAmt;
      }

      out[i] = sum * level;
    }
  }

} // namespace stolmine
