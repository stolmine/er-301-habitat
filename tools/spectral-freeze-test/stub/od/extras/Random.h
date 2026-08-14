// Test stub: deterministic replacement for od::Random (only generateFloat is
// used by Breccia, and only inside reshuffle()).
#pragma once

namespace od
{
  class Random
  {
  public:
    static float generateFloat(float from, float to);
  };
}
