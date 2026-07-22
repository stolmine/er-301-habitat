#pragma once

#include <od/objects/Object.h>

// Two simple trigger-fired envelope generators with a variable-curvature
// ("expo variation") control per shaped segment:
//   ExpoD  - decay only: trigger -> jump to peak -> shaped decay to 0.
//   ExpoAD - attack-decay: trigger -> shaped attack 0->1 -> shaped decay 1->0.
// Fire-and-forget (gate length ignored); a rising trigger edge restarts.
// All time/curve/level controls are audio-rate inlets. Cheap per sample
// (one rational-bend divide) - no NEON, no per-sample libm.
namespace stolmine
{
  // Rational bend: k = 0 -> linear; k > 0 -> convex (exponential-like);
  // k < 0 -> concave (log-like). Monotonic, maps [0,1] -> [0,1], bounded
  // for k > -1. x is the segment's normalized progress (0->1 for attack,
  // fed 1->0 for decay); orient per segment at the call site.
  static inline float expoShape(float x, float k)
  {
    return x / (1.0f + k * (1.0f - x));
  }

  // Curve inlet [-1,1] -> k via exp so the control is perceptually even;
  // k in (-0.80, +3.95). Baked per block (not per sample).
  static inline float expoCurveToK(float c)
  {
    if (c < -1.0f) c = -1.0f;
    if (c > 1.0f) c = 1.0f;
    return __builtin_expf(c * 1.6f) - 1.0f;
  }

  enum { EXPO_IDLE = 0, EXPO_ATTACK = 1, EXPO_DECAY = 2 };
  static const float kExpoMinTime = 0.0005f; // 0.5 ms floor caps the max rate

  // Decay-only envelope.
  class ExpoD : public od::Object
  {
  public:
    ExpoD();
    virtual ~ExpoD();

#ifndef SWIGLUA
    virtual void process();
    od::Inlet mTrigger{"Trigger"};
    od::Inlet mDecay{"Decay"};
    od::Inlet mCurve{"Curve"};
    od::Inlet mLevel{"Level"};
    od::Outlet mOutput{"Out"};
#endif

  private:
    int mState = EXPO_IDLE;
    float mPhase = 0.0f;
    bool mGateHigh = false;
  };

  // Attack-decay envelope.
  class ExpoAD : public od::Object
  {
  public:
    ExpoAD();
    virtual ~ExpoAD();

#ifndef SWIGLUA
    virtual void process();
    od::Inlet mTrigger{"Trigger"};
    od::Inlet mAttack{"Attack"};
    od::Inlet mDecay{"Decay"};
    od::Inlet mAttackCurve{"Attack Curve"};
    od::Inlet mDecayCurve{"Decay Curve"};
    od::Inlet mLevel{"Level"};
    od::Outlet mOutput{"Out"};
#endif

  private:
    int mState = EXPO_IDLE;
    float mPhase = 0.0f;
    bool mGateHigh = false;
  };

} // namespace stolmine
