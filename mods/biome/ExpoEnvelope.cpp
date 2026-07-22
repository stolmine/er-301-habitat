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

    // Block-rate: phase increment from the decay time, curvature from the
    // curve inlet. Sampling time/curve once per block keeps the loop cheap
    // (no per-sample divide-by-time or expf); envelopes respond fine at
    // block rate.
    float dt = globalConfig.samplePeriod / MAX(kExpoMinTime, decay[0]);
    float k = expoCurveToK(curve[0]);

    for (int i = 0; i < FRAMELENGTH; i++)
    {
      bool high = trig[i] > 0.5f;
      if (high && !mGateHigh)
      {
        mState = EXPO_DECAY;
        mPhase = 0.0f;
      }
      mGateHigh = high;

      float env;
      if (mState == EXPO_DECAY)
      {
        // Compute at the current phase (peak at phase 0), then advance.
        env = expoShape(1.0f - mPhase, k);
        mPhase += dt;
        if (mPhase >= 1.0f)
        {
          mState = EXPO_IDLE;
          mPhase = 0.0f;
        }
      }
      else
      {
        env = 0.0f;
      }

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
        mState = EXPO_ATTACK;
        mPhase = 0.0f;
      }
      mGateHigh = high;

      float env;
      switch (mState)
      {
      case EXPO_ATTACK:
        env = expoShape(mPhase, kA); // rising 0->1
        mPhase += dtA;
        if (mPhase >= 1.0f)
        {
          mState = EXPO_DECAY;
          mPhase = 0.0f;
        }
        break;
      case EXPO_DECAY:
        env = expoShape(1.0f - mPhase, kD); // falling 1->0
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

      out[i] = env * level[i];
    }
  }

} // namespace stolmine
