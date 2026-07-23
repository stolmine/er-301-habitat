#include "ExpoEnvelope.h"
#include <od/config.h>
#include <hal/ops.h>

namespace stolmine
{

  // ---- ExpoD ---------------------------------------------------------------

  ExpoD::ExpoD()
  {
    addInput(mTrigger);
    addInput(mDecay);
    addInput(mCurve);
    addInput(mLevel);
    addOutput(mOutput);
  }

  ExpoD::~ExpoD()
  {
  }

  void ExpoD::process()
  {
    float *trig = mTrigger.buffer();
    float *decay = mDecay.buffer();
    float *curve = mCurve.buffer();
    float *level = mLevel.buffer();
    float *out = mOutput.buffer();

    // Block-rate: phase increments from the decay time (and the fixed
    // anti-click onset rise), curvature from the curve inlet. Sampling
    // time/curve once per block keeps the loop cheap (no per-sample
    // divide-by-time or expf); envelopes respond fine at block rate.
    float dtRise = globalConfig.samplePeriod / kExpoDeclickRise;
    float dtDecay = globalConfig.samplePeriod / MAX(kExpoMinTime, decay[0]);
    float kRise = expoCurveToK(1.0f); // soft (fully-expo) onset, low initial slope
    float kDecay = expoCurveToK(curve[0]);

    for (int i = 0; i < FRAMELENGTH; i++)
    {
      bool high = trig[i] > 0.5f;
      if (high && !mGateHigh)
      {
        // Ramp up from wherever we are now, never a step (tomf's Strike
        // principle) - kills the onset pop AND the retrigger pop.
        mStartEnv = mCurrentEnv;
        mState = EXPO_ATTACK;
        mPhase = 0.0f;
      }
      mGateHigh = high;

      float env;
      switch (mState)
      {
      case EXPO_ATTACK:
        // Shaped rise from mStartEnv to the peak (1.0).
        env = mStartEnv + (1.0f - mStartEnv) * expoShape(mPhase, kRise);
        mPhase += dtRise;
        if (mPhase >= 1.0f)
        {
          mState = EXPO_DECAY;
          mPhase = 0.0f;
        }
        break;
      case EXPO_DECAY:
        env = expoShape(1.0f - mPhase, kDecay); // peak at phase 0 -> 0
        mPhase += dtDecay;
        if (mPhase >= 1.0f)
        {
          mState = EXPO_IDLE;
          mPhase = 0.0f;
        }
        break;
      default:
        env = 0.0f;
        break;
      }

      mCurrentEnv = env;
      out[i] = env * level[i];
    }
  }

  // ---- ExpoAD --------------------------------------------------------------

  ExpoAD::ExpoAD()
  {
    addInput(mTrigger);
    addInput(mAttack);
    addInput(mDecay);
    addInput(mAttackCurve);
    addInput(mDecayCurve);
    addInput(mLevel);
    addOutput(mOutput);
  }

  ExpoAD::~ExpoAD()
  {
  }

  void ExpoAD::process()
  {
    float *trig = mTrigger.buffer();
    float *attack = mAttack.buffer();
    float *decay = mDecay.buffer();
    float *acurve = mAttackCurve.buffer();
    float *dcurve = mDecayCurve.buffer();
    float *level = mLevel.buffer();
    float *out = mOutput.buffer();

    float dtA = globalConfig.samplePeriod / MAX(kExpoMinTime, attack[0]);
    float dtD = globalConfig.samplePeriod / MAX(kExpoMinTime, decay[0]);
    float kA = expoCurveToK(acurve[0]);
    float kD = expoCurveToK(dcurve[0]);

    for (int i = 0; i < FRAMELENGTH; i++)
    {
      bool high = trig[i] > 0.5f;
      if (high && !mGateHigh)
      {
        // Attack ramps from the current level, never a step (tomf's Strike
        // principle) - retriggering mid-envelope glides up instead of popping.
        mStartEnv = mCurrentEnv;
        mState = EXPO_ATTACK;
        mPhase = 0.0f;
      }
      mGateHigh = high;

      float env;
      switch (mState)
      {
      case EXPO_ATTACK:
        // Shaped rise from mStartEnv to the peak (1.0).
        env = mStartEnv + (1.0f - mStartEnv) * expoShape(mPhase, kA);
        mPhase += dtA;
        if (mPhase >= 1.0f)
        {
          mState = EXPO_DECAY;
          mPhase = 0.0f;
        }
        break;
      case EXPO_DECAY:
        env = expoShape(1.0f - mPhase, kD); // peak at phase 0 -> 0
        mPhase += dtD;
        if (mPhase >= 1.0f)
        {
          mState = EXPO_IDLE;
          mPhase = 0.0f;
        }
        break;
      default:
        env = 0.0f;
        break;
      }

      mCurrentEnv = env;
      out[i] = env * level[i];
    }
  }

} // namespace stolmine
