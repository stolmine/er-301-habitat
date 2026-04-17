// BBCut -- clock-driven breakbeat cutting processor
// Based on Nick Collins' BBCut library and the Livecut C++ port.

#include "BBCut.h"
#include <od/config.h>
#include <hal/ops.h>
#include <math.h>
#include <string.h>

namespace stolmine
{

  static inline float fast_tanh(float x)
  {
    float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
  }

  static const int kBufferSize = 384000;
  static const int kCrossfadeSamples = 64;
  static const int kMaxCuts = 64;
  static const int kVizSize = 128;

  static const int kValidSubdivs[] = {6, 8, 12, 16, 24, 32};
  static const int kNumSubdivs = 6;

  static const float kStutterMultWeights[] = {0.4f, 0.3f, 0.1f, 0.1f, 0.05f, 0.05f};
  static const int kStutterMults[] = {1, 2, 3, 4, 6, 8};
  static const int kNumStutterMults = 6;

  static const float kSQWeights[] = {0.0f, 0.3f, 0.0f, 0.5f, 0.7f, 0.8f, 0.9f, 0.6f};

  struct BBCut::Internal
  {
    int16_t buffer[kBufferSize];
    int writePos;

    int phraseUnits;
    int phrasePos;

    int blockRepeats;
    int blockRepeatIdx;
    int sliceOrigin;

    int cutSizes[kMaxCuts];
    float cutAmps[kMaxCuts];
    int numCutsInBlock;
    int currentCutInBlock;
    int cutPos;
    int cutSizeInSamples;
    float cutAmp;
    bool passthrough;

    int samplesPerUnit;
    int unitSampleCounter;

    float fadeEnvelope;
    float prevOutput;
    int crossfadeCounter;

    float vizRing[kVizSize];
    int vizPos;
    int vizDecimCounter;

    uint32_t rngState;

    void Init()
    {
      memset(buffer, 0, sizeof(buffer));
      writePos = 0;
      phraseUnits = 16;
      phrasePos = 0;
      blockRepeats = 1;
      blockRepeatIdx = 0;
      sliceOrigin = 0;
      numCutsInBlock = 1;
      currentCutInBlock = 0;
      cutPos = 0;
      cutSizeInSamples = 24000;
      cutAmp = 1.0f;
      passthrough = true;
      samplesPerUnit = 12000;
      unitSampleCounter = 0;
      fadeEnvelope = 0.0f;
      prevOutput = 0.0f;
      crossfadeCounter = 0;
      memset(vizRing, 0, sizeof(vizRing));
      vizPos = 0;
      vizDecimCounter = 0;
      rngState = 98765;
      memset(cutSizes, 0, sizeof(cutSizes));
      memset(cutAmps, 0, sizeof(cutAmps));
      cutSizes[0] = 12000;
      cutAmps[0] = 1.0f;
    }
  };

  static inline float iRandFloat(uint32_t &state)
  {
    state = state * 1664525u + 1013904223u;
    return (float)(state >> 1) / (float)0x7FFFFFFF;
  }

  static inline int iRandInt(uint32_t &state, int lo, int hi)
  {
    if (lo >= hi) return lo;
    return lo + (int)(iRandFloat(state) * (float)(hi - lo + 1));
  }

  static inline void bufWrite(int16_t *buf, int pos, float sample)
  {
    int s = (int)(CLAMP(-1.0f, 1.0f, sample) * 32767.0f);
    buf[pos] = (int16_t)s;
  }

  static inline float bufRead(const int16_t *buf, int pos)
  {
    return (float)buf[pos] * (1.0f / 32767.0f);
  }

  static int snapSubdiv(float raw)
  {
    int v = (int)(raw + 0.5f);
    int best = kValidSubdivs[0];
    int bestDist = (v > best) ? (v - best) : (best - v);
    for (int i = 1; i < kNumSubdivs; i++)
    {
      int d = (v > kValidSubdivs[i]) ? (v - kValidSubdivs[i]) : (kValidSubdivs[i] - v);
      if (d < bestDist) { best = kValidSubdivs[i]; bestDist = d; }
    }
    return best;
  }

  BBCut::BBCut()
  {
    addInput(mIn);
    addInput(mClock);
    addOutput(mOut);
    addParameter(mAlgorithm);
    addParameter(mDensity);
    addParameter(mStutterArea);
    addParameter(mRepeats);
    addParameter(mAccel);
    addParameter(mSubdiv);
    addParameter(mPhraseMin);
    addParameter(mPhraseMax);
    addParameter(mDutyCycle);
    addParameter(mAmpMin);
    addParameter(mAmpMax);
    addParameter(mFade);
    addParameter(mMix);
    addParameter(mInputLevel);
    addParameter(mOutputLevel);
    addParameter(mTanhAmt);
    mpInternal = new Internal();
    mpInternal->Init();
  }

  BBCut::~BBCut() { delete mpInternal; }

  int BBCut::getPhraseBars()
  {
    Internal &s = *mpInternal;
    int subdiv = snapSubdiv(mSubdiv.value());
    return (subdiv > 0) ? s.phraseUnits / subdiv : 1;
  }

  int BBCut::getPhrasePosition() { return mpInternal->phrasePos; }
  int BBCut::getPhraseLength() { return mpInternal->phraseUnits; }

  float BBCut::getOutputSample(int idx)
  {
    return mpInternal->vizRing[(mpInternal->vizPos + idx) & (kVizSize - 1)];
  }

  void BBCut::choosePhraseLength()
  {
    Internal &s = *mpInternal;
    int pMin = MAX(1, (int)(mPhraseMin.value() + 0.5f));
    int pMax = MAX(pMin, (int)(mPhraseMax.value() + 0.5f));
    int bars = iRandInt(s.rngState, pMin, pMax);
    int subdiv = snapSubdiv(mSubdiv.value());
    s.phraseUnits = bars * subdiv;
    s.phrasePos = 0;
  }

  void BBCut::chooseBlock_CutProc11(int unitsLeft)
  {
    Internal &s = *mpInternal;
    float density = CLAMP(0.0f, 1.0f, mDensity.value());
    float stutterArea = CLAMP(0.0f, 1.0f, mStutterArea.value());
    int maxRep = MAX(0, (int)(mRepeats.value() + 0.5f));
    float ampMin = CLAMP(0.0f, 1.0f, mAmpMin.value());
    float ampMax = CLAMP(ampMin, 1.0f, mAmpMax.value());
    int subdiv = snapSubdiv(mSubdiv.value());

    s.sliceOrigin = s.writePos;
    s.passthrough = false;

    float phraseRatio = (subdiv > 0) ? (float)unitsLeft / (float)subdiv : 1.0f;
    bool doStutter = (phraseRatio < stutterArea) && (iRandFloat(s.rngState) < density);

    if (doStutter && unitsLeft > 0)
    {
      float r = iRandFloat(s.rngState);
      float cumulative = 0.0f;
      int mult = 1;
      for (int i = 0; i < kNumStutterMults; i++)
      {
        cumulative += kStutterMultWeights[i];
        if (r < cumulative) { mult = kStutterMults[i]; break; }
      }
      int cutUnits = MAX(1, unitsLeft / mult);
      int reps = (cutUnits > 0) ? unitsLeft / cutUnits : 1;
      reps = MAX(1, reps);

      s.numCutsInBlock = MIN(reps, kMaxCuts);
      for (int i = 0; i < s.numCutsInBlock; i++)
      {
        s.cutSizes[i] = cutUnits * s.samplesPerUnit;
        s.cutAmps[i] = ampMin + iRandFloat(s.rngState) * (ampMax - ampMin);
      }
      s.blockRepeats = s.numCutsInBlock;
    }
    else
    {
      int halfSubdiv = MAX(1, subdiv / 4);
      int cutUnits = 2 * (int)(iRandFloat(s.rngState) * (float)halfSubdiv + 0.5f) + 1;
      cutUnits = CLAMP(1, unitsLeft, cutUnits);

      int reps = (maxRep > 0) ? iRandInt(s.rngState, 1, maxRep) : 1;
      int totalUnits = cutUnits * reps;
      while (totalUnits > unitsLeft && reps > 1) { reps--; totalUnits = cutUnits * reps; }
      if (totalUnits > unitsLeft) { cutUnits = unitsLeft; reps = 1; }

      s.numCutsInBlock = reps;
      for (int i = 0; i < s.numCutsInBlock; i++)
      {
        s.cutSizes[i] = cutUnits * s.samplesPerUnit;
        s.cutAmps[i] = ampMin + iRandFloat(s.rngState) * (ampMax - ampMin);
      }
      s.blockRepeats = reps;
    }

    s.blockRepeatIdx = 0;
    s.currentCutInBlock = 0;
    s.cutPos = 0;
    if (s.numCutsInBlock > 0)
    {
      s.cutSizeInSamples = MAX(1, s.cutSizes[0]);
      s.cutAmp = s.cutAmps[0];
    }
  }

  void BBCut::chooseBlock_WarpCut(int unitsLeft)
  {
    Internal &s = *mpInternal;
    float density = CLAMP(0.0f, 1.0f, mDensity.value());
    float accel = CLAMP(0.5f, 0.999f, mAccel.value());
    int maxRep = MAX(1, (int)(mRepeats.value() + 0.5f));
    float ampMin = CLAMP(0.0f, 1.0f, mAmpMin.value());
    float ampMax = CLAMP(ampMin, 1.0f, mAmpMax.value());

    s.sliceOrigin = s.writePos;
    s.passthrough = false;

    float straightChance = 1.0f - density;
    float roll = iRandFloat(s.rngState);

    if (roll < straightChance)
    {
      int cutUnits = CLAMP(1, MIN(2, unitsLeft), unitsLeft);
      s.numCutsInBlock = 1;
      s.cutSizes[0] = cutUnits * s.samplesPerUnit;
      s.cutAmps[0] = ampMin + iRandFloat(s.rngState) * (ampMax - ampMin);
      s.blockRepeats = 1;
    }
    else
    {
      bool doAccel = iRandFloat(s.rngState) > 0.5f;
      int totalUnits = CLAMP(1, MAX(2, unitsLeft / 2), unitsLeft);
      int reps = CLAMP(2, maxRep, MAX(2, maxRep));

      if (doAccel)
      {
        float accelPow = 1.0f;
        float sumPow = 0.0f;
        for (int i = 0; i < reps; i++) { sumPow += accelPow; accelPow *= accel; }
        float base = (float)(totalUnits * s.samplesPerUnit) / sumPow;

        s.numCutsInBlock = MIN(reps, kMaxCuts);
        accelPow = 1.0f;
        for (int i = 0; i < s.numCutsInBlock; i++)
        {
          s.cutSizes[i] = MAX(1, (int)(base * accelPow));
          accelPow *= accel;
          float t = (s.numCutsInBlock > 1) ? (float)i / (float)(s.numCutsInBlock - 1) : 0.0f;
          s.cutAmps[i] = ampMin + (ampMax - ampMin) * (1.0f - t * 0.5f);
        }

        bool ritard = iRandFloat(s.rngState) < 0.5f;
        if (ritard)
        {
          for (int i = 0; i < s.numCutsInBlock / 2; i++)
          {
            int j = s.numCutsInBlock - 1 - i;
            int ts = s.cutSizes[i]; s.cutSizes[i] = s.cutSizes[j]; s.cutSizes[j] = ts;
            float ta = s.cutAmps[i]; s.cutAmps[i] = s.cutAmps[j]; s.cutAmps[j] = ta;
          }
        }
        s.blockRepeats = s.numCutsInBlock;
      }
      else
      {
        int cutUnits = CLAMP(1, totalUnits / reps, unitsLeft);
        reps = CLAMP(1, reps, unitsLeft / MAX(1, cutUnits));
        s.numCutsInBlock = MIN(reps, kMaxCuts);
        for (int i = 0; i < s.numCutsInBlock; i++)
        {
          s.cutSizes[i] = cutUnits * s.samplesPerUnit;
          float t = (s.numCutsInBlock > 1) ? (float)i / (float)(s.numCutsInBlock - 1) : 0.0f;
          s.cutAmps[i] = ampMin + (ampMax - ampMin) * (1.0f - t * 0.3f);
        }
        s.blockRepeats = s.numCutsInBlock;
      }
    }

    s.blockRepeatIdx = 0;
    s.currentCutInBlock = 0;
    s.cutPos = 0;
    if (s.numCutsInBlock > 0)
    {
      s.cutSizeInSamples = MAX(1, s.cutSizes[0]);
      s.cutAmp = s.cutAmps[0];
    }
  }

  void BBCut::chooseBlock_SQPusher(int unitsLeft, int phrasePos)
  {
    Internal &s = *mpInternal;
    float density = CLAMP(0.0f, 1.0f, mDensity.value());
    float ampMin = CLAMP(0.0f, 1.0f, mAmpMin.value());
    float ampMax = CLAMP(ampMin, 1.0f, mAmpMax.value());
    int subdiv = snapSubdiv(mSubdiv.value());

    s.sliceOrigin = s.writePos;
    s.passthrough = false;

    int quaverPos = (subdiv > 0) ? (phrasePos % subdiv) % 8 : 0;
    float weight = kSQWeights[quaverPos] * density;
    bool subdivide = iRandFloat(s.rngState) < weight;

    bool inFillBar = (subdiv > 0) && (phrasePos >= s.phraseUnits - subdiv);
    if (inFillBar) subdivide = iRandFloat(s.rngState) < (density * 0.8f + 0.2f);

    if (subdivide && unitsLeft >= 2)
    {
      int reps = CLAMP(2, 4, MIN(4, unitsLeft));
      int cutUnits = 1;
      s.numCutsInBlock = MIN(reps, kMaxCuts);
      for (int i = 0; i < s.numCutsInBlock; i++)
      {
        s.cutSizes[i] = cutUnits * s.samplesPerUnit;
        s.cutAmps[i] = ampMin + iRandFloat(s.rngState) * (ampMax - ampMin);
      }
      s.blockRepeats = s.numCutsInBlock;
    }
    else
    {
      int cutUnits = CLAMP(1, 2, unitsLeft);
      s.numCutsInBlock = 1;
      s.cutSizes[0] = cutUnits * s.samplesPerUnit;
      s.cutAmps[0] = ampMin + iRandFloat(s.rngState) * (ampMax - ampMin);
      s.blockRepeats = 1;
    }

    s.blockRepeatIdx = 0;
    s.currentCutInBlock = 0;
    s.cutPos = 0;
    if (s.numCutsInBlock > 0)
    {
      s.cutSizeInSamples = MAX(1, s.cutSizes[0]);
      s.cutAmp = s.cutAmps[0];
    }
  }

  void BBCut::advanceUnit()
  {
    Internal &s = *mpInternal;
    int algo = CLAMP(0, 2, (int)(mAlgorithm.value() + 0.5f));

    s.currentCutInBlock++;
    if (s.currentCutInBlock >= s.numCutsInBlock)
    {
      s.phrasePos += s.numCutsInBlock;
      int unitsLeft = s.phraseUnits - s.phrasePos;

      if (unitsLeft <= 0)
      {
        choosePhraseLength();
        unitsLeft = s.phraseUnits;
      }

      switch (algo)
      {
        case 0: chooseBlock_CutProc11(unitsLeft); break;
        case 1: chooseBlock_WarpCut(unitsLeft); break;
        case 2: chooseBlock_SQPusher(unitsLeft, s.phrasePos); break;
        default: chooseBlock_CutProc11(unitsLeft); break;
      }
    }
    else
    {
      s.cutPos = 0;
      s.cutSizeInSamples = MAX(1, s.cutSizes[s.currentCutInBlock]);
      s.cutAmp = s.cutAmps[s.currentCutInBlock];
      s.crossfadeCounter = kCrossfadeSamples;
    }
  }

  void BBCut::process()
  {
    Internal &s = *mpInternal;

    float *in = mIn.buffer();
    float *clk = mClock.buffer();
    float *out = mOut.buffer();

    float mix = CLAMP(0.0f, 1.0f, mMix.value());
    float inputLevel = CLAMP(0.0f, 4.0f, mInputLevel.value());
    float outputLevel = CLAMP(0.0f, 4.0f, mOutputLevel.value());
    float tanhAmt = CLAMP(0.0f, 1.0f, mTanhAmt.value());
    float dutyCycle = CLAMP(0.0f, 1.0f, mDutyCycle.value());
    float fadeSec = CLAMP(0.0f, 0.1f, mFade.value());
    int subdiv = snapSubdiv(mSubdiv.value());

    float fadeCoeff = (fadeSec > 0.0001f)
      ? 1.0f / (fadeSec * globalConfig.sampleRate)
      : 1.0f;

    s.samplesPerUnit = (subdiv > 0 && mClockPeriodSamples > 0)
      ? (mClockPeriodSamples * 4) / subdiv
      : 12000;
    s.samplesPerUnit = MAX(64, s.samplesPerUnit);

    for (int i = 0; i < FRAMELENGTH; i++)
    {
      float inSample = in[i] * inputLevel;

      bufWrite(s.buffer, s.writePos, inSample);
      s.writePos = (s.writePos + 1) % kBufferSize;

      bool clkHigh = clk[i] > 0.0f;
      if (clkHigh && !mClockWasHigh)
      {
        if (mSamplesSinceLastClock > 100)
          mClockPeriodSamples = mSamplesSinceLastClock;
        mSamplesSinceLastClock = 0;
      }
      mClockWasHigh = clkHigh;
      mSamplesSinceLastClock++;

      s.unitSampleCounter++;
      if (s.unitSampleCounter >= s.samplesPerUnit)
      {
        s.unitSampleCounter = 0;
        advanceUnit();
      }

      float wet;
      if (s.passthrough)
      {
        wet = inSample;
        s.fadeEnvelope = 0.0f;
      }
      else
      {
        float dutyGate = ((float)s.cutPos / (float)s.cutSizeInSamples < dutyCycle) ? 1.0f : 0.0f;
        float target = dutyGate;
        if (fadeCoeff >= 1.0f)
          s.fadeEnvelope = target;
        else
          s.fadeEnvelope += (target - s.fadeEnvelope) * fadeCoeff;

        int readIdx = (s.sliceOrigin + (s.cutPos % s.cutSizeInSamples)) % kBufferSize;
        if (readIdx < 0) readIdx += kBufferSize;
        wet = bufRead(s.buffer, readIdx) * s.cutAmp * s.fadeEnvelope;
        s.cutPos++;
      }

      if (s.crossfadeCounter > 0)
      {
        float blend = (float)s.crossfadeCounter / (float)kCrossfadeSamples;
        wet = s.prevOutput * blend + wet * (1.0f - blend);
        s.crossfadeCounter--;
      }
      s.prevOutput = wet;

      float mixed = inSample * (1.0f - mix) + wet * mix;

      if (tanhAmt > 0.001f)
        mixed = fast_tanh(mixed * (1.0f + tanhAmt * 4.0f));

      out[i] = mixed * outputLevel;

      s.vizDecimCounter++;
      if (s.vizDecimCounter >= 8)
      {
        s.vizDecimCounter = 0;
        s.vizRing[s.vizPos] = out[i];
        s.vizPos = (s.vizPos + 1) & (kVizSize - 1);
      }
    }
  }

}
