// BBCut -- clock-driven breakbeat cutting processor
// WarpCut-derived engine with parameterized block size, repeat count,
// accel/ritard bias, and bipolar duty cycle (reverse on negative).
// Algorithm lineage: Nick Collins' BBCut via Livecut (Remy Muller, GPLv2).

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
  static const int kMaxCuts = 128;
  static const int kVizSize = 128;
  static const int kMaxBlockUnits = 128;

  static const int kValidSubdivs[] = {6, 8, 12, 16, 24, 32};
  static const int kNumSubdivs = 6;

  struct CutInfo
  {
    int size;
    int length;
    float amp;
  };

  struct BBCut::Internal
  {
    int16_t buffer[kBufferSize];
    int writePos;

    int phraseUnits;
    int unitsDone;

    int unitsInBlock;
    int unitsInsideBlock;

    CutInfo cuts[kMaxCuts];
    int numCuts;
    int currentCut;
    int readIndex;
    int sliceOrigin;

    int samplesPerUnit;
    int unitSampleCounter;

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
      phraseUnits = 0;
      unitsDone = 0;
      unitsInBlock = 0;
      unitsInsideBlock = 0;
      numCuts = 0;
      currentCut = 0;
      readIndex = 0;
      sliceOrigin = 0;
      samplesPerUnit = 12000;
      unitSampleCounter = 0;
      prevOutput = 0.0f;
      crossfadeCounter = 0;
      memset(vizRing, 0, sizeof(vizRing));
      vizPos = 0;
      vizDecimCounter = 0;
      rngState = 98765;
      memset(cuts, 0, sizeof(cuts));
    }
  };

  static inline float iRandFloat(uint32_t &state)
  {
    state = state * 1664525u + 1013904223u;
    return (float)(state >> 1) / (float)0x7FFFFFFF;
  }

  static inline float iRandRange(uint32_t &state, float lo, float hi)
  {
    return lo + (hi - lo) * iRandFloat(state);
  }

  static inline int iRandInt(uint32_t &state, int lo, int hi)
  {
    if (lo >= hi) return lo;
    return lo + (int)(iRandFloat(state) * (float)(hi - lo + 1));
  }

  static inline void bufWrite(int16_t *buf, int pos, float sample)
  {
    buf[pos] = (int16_t)(CLAMP(-1.0f, 1.0f, sample) * 32767.0f);
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

  static inline float expenv(float i, float fade, float size)
  {
    if (fade < 1.0f) fade = 1.0f;
    return (1.0f - expf(-5.0f * i / fade)) * (1.0f - expf(5.0f * (i - size) / fade));
  }

  static int chooseWeightedBlockSize(uint32_t &rng, float bias, int blockMax)
  {
    if (blockMax <= 1) return 1;
    float invMax = 1.0f / (float)(blockMax - 1);
    float sum = 0.0f;
    for (int k = 0; k < blockMax; k++)
      sum += expf(bias * 4.0f * (float)k * invMax);
    float r = iRandFloat(rng) * sum;
    float cumul = 0.0f;
    for (int k = 0; k < blockMax; k++)
    {
      cumul += expf(bias * 4.0f * (float)k * invMax);
      if (r < cumul) return k + 1;
    }
    return blockMax;
  }

  BBCut::BBCut()
  {
    addInput(mIn);
    addInput(mClock);
    addOutput(mOut);
    addParameter(mDensity);
    addParameter(mBlockSize);
    addParameter(mBlockMax);
    addParameter(mRepeatCount);
    addParameter(mRitardBias);
    addParameter(mBlend);
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

  int BBCut::getPhrasePosition() { return mpInternal->unitsDone; }
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
    s.unitsDone = 0;
  }

  void BBCut::chooseBlock(int unitsLeft)
  {
    Internal &s = *mpInternal;
    int subdiv = snapSubdiv(mSubdiv.value());
    double spu = (double)s.samplesPerUnit;
    float density = CLAMP(0.0f, 1.0f, mDensity.value());
    float blockSizeBias = CLAMP(0.0f, 1.0f, mBlockSize.value());
    int blockMaxBeats = CLAMP(1, 16, (int)(mBlockMax.value() + 0.5f));
    int blockMax = CLAMP(1, kMaxBlockUnits, blockMaxBeats * subdiv / 4);
    if (blockMax < 1) blockMax = 1;
    int repeatCount = CLAMP(2, 64, (int)(mRepeatCount.value() + 0.5f));
    float ritardBias = CLAMP(0.0f, 1.0f, mRitardBias.value());
    float blend = CLAMP(0.0f, 1.0f, mBlend.value());
    float accel = CLAMP(0.5f, 0.999f, mAccel.value());
    float dutyVal = CLAMP(-1.0f, 1.0f, mDutyCycle.value());
    float dutyMag = (dutyVal > 0.0f) ? dutyVal : -dutyVal;
    if (dutyMag < 0.01f) dutyMag = 0.01f;
    float ampMin = CLAMP(0.0f, 1.0f, mAmpMin.value());
    float ampMax = CLAMP(ampMin, 1.0f, mAmpMax.value());

    s.sliceOrigin = s.writePos;

    s.unitsInBlock = chooseWeightedBlockSize(s.rngState, blockSizeBias, blockMax);
    if (s.unitsInBlock > unitsLeft) s.unitsInBlock = unitsLeft;

    float straightChance = 1.0f - density;

    if (iRandFloat(s.rngState) < straightChance)
    {
      s.numCuts = 1;
      int cutSamples = (int)(spu * (double)s.unitsInBlock);
      s.cuts[0].size = MAX(1, cutSamples);
      s.cuts[0].length = MAX(1, (int)((float)cutSamples * dutyMag));
      s.cuts[0].amp = iRandRange(s.rngState, ampMin, ampMax);
    }
    else
    {
      float startAmp = iRandRange(s.rngState, ampMin, ampMax);
      float endAmp = iRandRange(s.rngState, ampMin, ampMax);

      if (iRandFloat(s.rngState) < (1.0f - blend))
      {
        float temp = (float)s.unitsInBlock / (float)repeatCount;
        s.numCuts = MIN(repeatCount, kMaxCuts);
        for (int i = 0; i < s.numCuts; i++)
        {
          float phase = (float)i / (float)s.numCuts;
          int l = (int)(spu * temp + 0.5);
          s.cuts[i].size = MAX(1, l);
          s.cuts[i].length = MAX(1, (int)((float)l * dutyMag));
          s.cuts[i].amp = startAmp + (endAmp - startAmp) * phase;
        }
      }
      else
      {
        float temp = (float)s.unitsInBlock * (1.0f - accel) /
                     (1.0f - powf(accel, (float)repeatCount));
        s.numCuts = MIN(repeatCount, kMaxCuts);
        for (int i = 0; i < s.numCuts; i++)
        {
          float phase = (float)i / (float)s.numCuts;
          int l = (int)(spu * temp * powf(accel, (float)i));
          s.cuts[i].size = MAX(1, l);
          s.cuts[i].length = MAX(1, (int)((float)l * dutyMag));
          s.cuts[i].amp = startAmp + (endAmp - startAmp) * phase;
        }
        if (iRandFloat(s.rngState) < ritardBias)
        {
          for (int i = 0; i < s.numCuts / 2; i++)
          {
            int j = s.numCuts - 1 - i;
            CutInfo tmp = s.cuts[i];
            s.cuts[i] = s.cuts[j];
            s.cuts[j] = tmp;
          }
        }
      }
    }

    s.unitsInsideBlock = 0;
    s.currentCut = 0;
    s.readIndex = 0;
    s.crossfadeCounter = kCrossfadeSamples;
  }

  void BBCut::advanceUnit()
  {
    Internal &s = *mpInternal;

    if (s.phraseUnits <= 0 || s.unitsDone >= s.phraseUnits)
      choosePhraseLength();

    if (s.unitsInsideBlock >= s.unitsInBlock)
    {
      int unitsLeft = s.phraseUnits - s.unitsDone;
      if (unitsLeft <= 0)
      {
        choosePhraseLength();
        unitsLeft = s.phraseUnits;
      }
      chooseBlock(unitsLeft);
    }

    s.unitsInsideBlock++;
    s.unitsDone++;
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
    float fadeSamples = CLAMP(1.0f, globalConfig.sampleRate * 0.1f,
                              mFade.value() * globalConfig.sampleRate);
    float dutyVal = CLAMP(-1.0f, 1.0f, mDutyCycle.value());
    bool reverseRead = (dutyVal < 0.0f);
    int subdiv = snapSubdiv(mSubdiv.value());

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

      float wet = 0.0f;
      if (s.currentCut < s.numCuts)
      {
        CutInfo &ci = s.cuts[s.currentCut];

        if (s.readIndex < ci.length)
        {
          int readPos;
          if (reverseRead)
            readPos = (s.sliceOrigin + ci.size - 1 - s.readIndex) % kBufferSize;
          else
            readPos = (s.sliceOrigin + s.readIndex) % kBufferSize;
          if (readPos < 0) readPos += kBufferSize;

          float env = expenv((float)s.readIndex, fadeSamples, (float)ci.length);
          wet = bufRead(s.buffer, readPos) * ci.amp * env;
          s.readIndex++;
        }
        else
        {
          s.readIndex++;
          wet = 0.0f;
        }

        if (s.readIndex >= ci.size)
        {
          s.currentCut++;
          s.readIndex = 0;
          s.crossfadeCounter = kCrossfadeSamples;
        }
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
