// GateSeq - 64-step gate sequencer with ratchet and algorithmic transforms

#include "GateSeq.h"
#include <od/config.h>
#include <hal/ops.h>
#include <string.h>
#include <math.h>

namespace stolmine
{

  // Simple LCG random
  static uint32_t gsRandState = 54321;
  static inline float gsRandFloat()
  {
    gsRandState = gsRandState * 1664525u + 1013904223u;
    return (float)((int32_t)gsRandState) / (float)0x7FFFFFFF;
  }

  struct GateSeq::Internal
  {
    bool gate[kGateMaxSteps];
    int length[kGateMaxSteps];
    float velocity[kGateMaxSteps];

    // Snapshot
    bool snapGate[kGateMaxSteps];
    int snapLength[kGateMaxSteps];
    float snapVelocity[kGateMaxSteps];
    bool hasSnapshot;

    void Init()
    {
      for (int i = 0; i < kGateMaxSteps; i++)
      {
        gate[i] = false;
        length[i] = 1;
        velocity[i] = 1.0f;
      }
      hasSnapshot = false;
    }
  };

  GateSeq::GateSeq()
  {
    addInput(mClock);
    addInput(mReset);
    addInput(mRatchet);
    addInput(mTransform);
    addOutput(mOut);
    addParameter(mSeqLength);
    addParameter(mLoopLength);
    addParameter(mRatchetMult);
    addOption(mRatchetLenToggle);
    addOption(mRatchetVelToggle);
    addParameter(mTransformFunc);
    addParameter(mTransformParamA);
    addParameter(mTransformParamB);
    addParameter(mTransformScope);
    addParameter(mGateWidth);
    addParameter(mEditGate);
    addParameter(mEditLength);
    addParameter(mEditVelocity);

    mpInternal = new Internal();
    mpInternal->Init();
  }

  GateSeq::~GateSeq()
  {
    delete mpInternal;
  }

  bool GateSeq::getStepGate(int i) { return mpInternal->gate[CLAMP(0, kGateMaxSteps - 1, i)]; }
  void GateSeq::setStepGate(int i, bool v) { mpInternal->gate[CLAMP(0, kGateMaxSteps - 1, i)] = v; }
  int GateSeq::getStepLength(int i) { return mpInternal->length[CLAMP(0, kGateMaxSteps - 1, i)]; }
  void GateSeq::setStepLength(int i, int v) { mpInternal->length[CLAMP(0, kGateMaxSteps - 1, i)] = MAX(1, v); }
  float GateSeq::getStepVelocity(int i) { return mpInternal->velocity[CLAMP(0, kGateMaxSteps - 1, i)]; }
  void GateSeq::setStepVelocity(int i, float v) { mpInternal->velocity[CLAMP(0, kGateMaxSteps - 1, i)] = CLAMP(0.0f, 1.0f, v); }

  void GateSeq::loadStep(int i)
  {
    i = CLAMP(0, kGateMaxSteps - 1, i);
    mEditingStep = i;
    mEditGate.hardSet(mpInternal->gate[i] ? 1.0f : 0.0f);
    mEditLength.hardSet((float)mpInternal->length[i]);
    mEditVelocity.hardSet(mpInternal->velocity[i]);
  }

  void GateSeq::storeStep(int i)
  {
    i = CLAMP(0, kGateMaxSteps - 1, i);
    mpInternal->gate[i] = mEditGate.value() > 0.5f;
    mpInternal->length[i] = MAX(1, (int)(mEditLength.value() + 0.5f));
    mpInternal->velocity[i] = CLAMP(0.0f, 1.0f, mEditVelocity.value());
  }

  int GateSeq::getTotalTicks()
  {
    int total = 0;
    for (int i = 0; i < mCachedSeqLength; i++)
      total += mpInternal->length[i];
    return total;
  }

  void GateSeq::fireTransform() { mManualFire = true; }

  void GateSeq::snapshotSave()
  {
    Internal &s = *mpInternal;
    memcpy(s.snapGate, s.gate, sizeof(s.gate));
    memcpy(s.snapLength, s.length, sizeof(s.length));
    memcpy(s.snapVelocity, s.velocity, sizeof(s.velocity));
    s.hasSnapshot = true;
  }

  void GateSeq::snapshotRestore()
  {
    Internal &s = *mpInternal;
    if (!s.hasSnapshot)
      return;
    memcpy(s.gate, s.snapGate, sizeof(s.gate));
    memcpy(s.length, s.snapLength, sizeof(s.length));
    memcpy(s.velocity, s.snapVelocity, sizeof(s.velocity));
    s.hasSnapshot = false;
  }

  // --- Algorithmic transforms ---

  void GateSeq::applyEuclidean(bool *pattern, int steps, int hits, int rotation)
  {
    hits = CLAMP(0, steps, hits);
    rotation = CLAMP(0, steps - 1, rotation);

    // Bjorklund algorithm
    bool temp[kGateMaxSteps];
    memset(temp, 0, sizeof(bool) * steps);

    if (hits == 0)
    {
      memcpy(pattern, temp, sizeof(bool) * steps);
      return;
    }
    if (hits == steps)
    {
      for (int i = 0; i < steps; i++)
        temp[i] = true;
      // Apply rotation
      for (int i = 0; i < steps; i++)
        pattern[(i + rotation) % steps] = temp[i];
      return;
    }

    // Build euclidean pattern using bresenham-like approach
    for (int i = 0; i < steps; i++)
    {
      temp[i] = ((i * hits) % steps) < hits;
      // More accurate Bjorklund: use integer accumulator
    }

    // Better: true Bjorklund via accumulator
    memset(temp, 0, sizeof(bool) * steps);
    int bucket = 0;
    for (int i = 0; i < steps; i++)
    {
      bucket += hits;
      if (bucket >= steps)
      {
        bucket -= steps;
        temp[i] = true;
      }
    }

    // Apply rotation
    for (int i = 0; i < steps; i++)
      pattern[(i + rotation) % steps] = temp[i];
  }

  void GateSeq::applyNR(bool *pattern, int steps, int prime, int mask)
  {
    prime = CLAMP(0, 31, prime);
    mask = CLAMP(0, 3, mask);

    uint16_t rhythm = gs_table_nr[prime];
    switch (mask)
    {
    case 1: rhythm &= 0x0F0F; break;
    case 2: rhythm &= 0xF003; break;
    case 3: rhythm &= 0x01F0; break;
    default: break;
    }

    for (int i = 0; i < steps; i++)
    {
      int bit = i % 16;
      pattern[i] = (rhythm >> (15 - bit)) & 1;
    }
  }

  void GateSeq::applyNecklace(bool *pattern, int steps, int density, int index)
  {
    density = CLAMP(0, steps, density);

    // Simple necklace generation: use euclidean with different rotations
    // as an approximation. True FKM necklace enumeration is complex.
    // Each unique "necklace" is a euclidean pattern at a unique rotation
    // that hasn't been seen before. For practical purposes, index
    // selects rotation.
    int rotation = index % steps;
    applyEuclidean(pattern, steps, density, rotation);
  }

  void GateSeq::applyTransform()
  {
    Internal &s = *mpInternal;
    int func = CLAMP(0, (int)GS_XFORM_COUNT - 1, (int)(mTransformFunc.value() + 0.5f));
    int paramA = MAX(0, (int)(mTransformParamA.value() + 0.5f));
    int paramB = MAX(0, (int)(mTransformParamB.value() + 0.5f));
    int scope = CLAMP(0, (int)GS_SCOPE_COUNT - 1, (int)(mTransformScope.value() + 0.5f));
    int seqLen = mCachedSeqLength;

    // Gate pattern transforms
    if (scope == GS_SCOPE_GATE || scope == GS_SCOPE_ALL)
    {
      switch (func)
      {
      case GS_XFORM_EUCLIDEAN:
        applyEuclidean(s.gate, seqLen, paramA, paramB);
        break;
      case GS_XFORM_NR:
        applyNR(s.gate, seqLen, paramA, paramB);
        break;
      case GS_XFORM_GRIDS:
      {
        // Simple grids approximation: threshold a hash of (step, x, y)
        int x = CLAMP(0, 255, paramA);
        int y = CLAMP(0, 255, paramB);
        for (int i = 0; i < seqLen; i++)
        {
          // Simple deterministic hash
          uint32_t h = (uint32_t)i * 2654435761u;
          h ^= (uint32_t)x * 2246822519u;
          h ^= (uint32_t)y * 3266489917u;
          h = (h >> 16) ^ h;
          s.gate[i] = (h & 0xFF) < 128;
        }
        break;
      }
      case GS_XFORM_NECKLACE:
        applyNecklace(s.gate, seqLen, paramA, paramB);
        break;
      case GS_XFORM_INVERT:
        for (int i = 0; i < seqLen; i++)
          s.gate[i] = !s.gate[i];
        break;
      case GS_XFORM_ROTATE:
      {
        bool temp[kGateMaxSteps];
        int rot = ((paramA % seqLen) + seqLen) % seqLen;
        for (int i = 0; i < seqLen; i++)
          temp[(i + rot) % seqLen] = s.gate[i];
        memcpy(s.gate, temp, sizeof(bool) * seqLen);
        break;
      }
      case GS_XFORM_DENSITY:
      {
        float thresh = (float)paramA / 100.0f;
        for (int i = 0; i < seqLen; i++)
        {
          float r = (gsRandFloat() + 1.0f) * 0.5f; // 0-1
          s.gate[i] = r < thresh;
        }
        break;
      }
      }
    }

    // Length transforms (numeric operations)
    if (scope == GS_SCOPE_LENGTH || scope == GS_SCOPE_ALL)
    {
      int factor = MAX(1, paramA);
      switch (func)
      {
      case GS_XFORM_ROTATE:
      {
        int temp[kGateMaxSteps];
        int rot = ((factor % seqLen) + seqLen) % seqLen;
        for (int i = 0; i < seqLen; i++)
          temp[(i + rot) % seqLen] = s.length[i];
        memcpy(s.length, temp, sizeof(int) * seqLen);
        break;
      }
      case GS_XFORM_INVERT:
        // Invert lengths: max - current + 1
        for (int i = 0; i < seqLen; i++)
          s.length[i] = MAX(1, 17 - s.length[i]);
        break;
      default:
        break;
      }
    }

    // Velocity transforms
    if (scope == GS_SCOPE_VELOCITY || scope == GS_SCOPE_ALL)
    {
      switch (func)
      {
      case GS_XFORM_ROTATE:
      {
        float temp[kGateMaxSteps];
        int rot = ((paramA % seqLen) + seqLen) % seqLen;
        for (int i = 0; i < seqLen; i++)
          temp[(i + rot) % seqLen] = s.velocity[i];
        memcpy(s.velocity, temp, sizeof(float) * seqLen);
        break;
      }
      case GS_XFORM_INVERT:
        for (int i = 0; i < seqLen; i++)
          s.velocity[i] = 1.0f - s.velocity[i];
        break;
      case GS_XFORM_DENSITY:
      {
        // Randomize velocities
        for (int i = 0; i < seqLen; i++)
          s.velocity[i] = (gsRandFloat() + 1.0f) * 0.5f;
        break;
      }
      default:
        break;
      }
    }

    mLastTransformFunc = func;
    mLastTransformParamA = paramA;
    mLastTransformParamB = paramB;
    mLastTransformScope = scope;

    // Reload edit buffer
    loadStep(mEditingStep);
  }

  void GateSeq::process()
  {
    Internal &s = *mpInternal;

    float *clock = mClock.buffer();
    float *reset = mReset.buffer();
    float *ratchet = mRatchet.buffer();
    float *xform = mTransform.buffer();
    float *out = mOut.buffer();

    int seqLen = CLAMP(1, kGateMaxSteps, (int)(mSeqLength.value() + 0.5f));
    int loopLen = CLAMP(0, seqLen, (int)(mLoopLength.value() + 0.5f));
    int ratchetMult = CLAMP(1, 8, (int)(mRatchetMult.value() + 0.5f));
    float gateWidth = CLAMP(0.01f, 1.0f, mGateWidth.value());
    bool ratchetLenOn = mRatchetLenToggle.value() == 1;
    bool ratchetVelOn = mRatchetVelToggle.value() == 1;

    mCachedSeqLength = seqLen;
    mCachedLoopLength = loopLen;

    for (int i = 0; i < FRAMELENGTH; i++)
    {
      bool clockHigh = clock[i] > 0.0f;
      bool resetHigh = reset[i] > 0.0f;
      bool clockRise = clockHigh && !mClockWasHigh;
      bool resetRise = resetHigh && !mResetWasHigh;
      bool ratchetGateHigh = ratchet[i] > 0.0f;

      mClockWasHigh = clockHigh;
      mResetWasHigh = resetHigh;

      // Transform gate
      bool xformHigh = xform[i] > 0.0f;
      bool xformRise = xformHigh && !mTransformWasHigh;
      mTransformWasHigh = xformHigh;

      if (xformRise || mManualFire)
      {
        applyTransform();
        mManualFire = false;
      }

      if (resetRise)
      {
        mStep = 0;
        mTickCount = 0;
        mGateSamplesRemaining = 0;
        mRatchetActive = false;
      }

      if (clockRise)
      {
        // Measure clock period
        if (mSamplesSinceLastClock > 0)
          mClockPeriodSamples = mSamplesSinceLastClock;
        mSamplesSinceLastClock = 0;

        // Advance step: only move on when tick count reaches step length
        bool newStep = false;
        int stepLen = s.length[mStep % seqLen];
        if (mTickCount == 0)
        {
          // First tick of current step - fire gate below
          newStep = true;
        }
        mTickCount++;
        if (mTickCount >= stepLen)
        {
          mTickCount = 0;
          if (loopLen > 0)
          {
            int loopStart = mStep - (mStep % loopLen);
            mStep = loopStart + ((mStep - loopStart + 1) % loopLen);
          }
          else
          {
            mStep = (mStep + 1) % seqLen;
          }
        }

        // Fire gate only on the first tick of a step
        if (newStep)
        {
          bool stepOn = s.gate[mStep % seqLen];
          if (stepOn)
          {
            float vel = s.velocity[mStep % seqLen];
            int gateLen = s.length[mStep % seqLen];

            // Gate duration: step length in ticks * width fraction
            int gateSamples = 48;
            if (mClockPeriodSamples > 0)
            {
              gateSamples = (int)(mClockPeriodSamples * gateLen * gateWidth);
              if (gateSamples < 1) gateSamples = 1;
            }

            if (ratchetGateHigh && ratchetMult > 1)
            {
              // Start ratchet: subdivide clock period
              mRatchetActive = true;
              mRatchetSubGateTotal = ratchetMult;
              mRatchetSubGateIndex = 0;
              mRatchetSubGateSamples = mClockPeriodSamples / ratchetMult;
              mRatchetBaseVelocity = vel;
              mRatchetBaseLength = gateSamples;
              mRatchetSubGateRemaining = mRatchetSubGateSamples;

              float subVel = vel;
              int subLen = gateSamples;
              if (ratchetLenOn)
                subLen = MAX(1, gateSamples / ratchetMult);
              mGateSamplesRemaining = subLen;
              mGateAmplitude = subVel;
            }
            else
            {
              // Normal gate
              mRatchetActive = false;
              mGateSamplesRemaining = gateSamples;
              mGateAmplitude = vel;
            }
          }
          else
          {
            mRatchetActive = false;
          }
        }
      }

      mSamplesSinceLastClock++;

      // Handle ratchet sub-gate progression
      if (mRatchetActive)
      {
        mRatchetSubGateRemaining--;
        if (mRatchetSubGateRemaining <= 0 &&
            mRatchetSubGateIndex < mRatchetSubGateTotal - 1)
        {
          mRatchetSubGateIndex++;
          mRatchetSubGateRemaining = mRatchetSubGateSamples;

          // Compute sub-gate params
          float subVel = mRatchetBaseVelocity;
          int subLen = mRatchetBaseLength;

          if (ratchetVelOn)
          {
            // Decaying accent: vel * (N - i) / N
            float decay = (float)(mRatchetSubGateTotal - mRatchetSubGateIndex) /
                          (float)mRatchetSubGateTotal;
            subVel *= decay;
          }
          if (ratchetLenOn)
          {
            subLen = MAX(1, mRatchetBaseLength / mRatchetSubGateTotal);
          }

          mGateSamplesRemaining = subLen;
          mGateAmplitude = subVel;
        }
      }

      // Output
      if (mGateSamplesRemaining > 0)
      {
        out[i] = mGateAmplitude;
        mGateSamplesRemaining--;
      }
      else
      {
        out[i] = 0.0f;
      }
    }
  }

} // namespace stolmine
