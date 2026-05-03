#pragma once

// Threshold-reflection folder per the BIA technical manual:
// "as many as will continue to fold". When |x| > threshold, reflect
// across ±threshold; iterate until in-range. Above fold > 0.75 we mix
// in a polarity-pulse train per spec.
//
// Header-only inline so Visadhara.cpp's vtable stays COMDAT-clean per
// feedback_no_out_of_line_virtuals.
//
// CPU: typical fold depth at fold=1.0 is 2-4 stages on a unit-amplitude
// sine; at fold=0.0 we early-out. ~1-2 reflections per voice per sample.
// Acceptable scalar.

namespace stolmine
{
  namespace visadhara_folder
  {
    static const int kMaxStages = 8;

    // Compensation gain table: undoes the amplitude collapse the fold
    // operation imposes. Calibrated by listening test in Phase 5; for
    // Phase 2 we use a mild log-shape that nudges each fold stage back
    // toward unity output. Stage 0 = pass-through (gain 1), stages
    // 1..7 = progressively boosted.
    static inline float compensation(int stages)
    {
      static const float kComp[kMaxStages + 1] = {
        1.0f, 1.10f, 1.22f, 1.35f, 1.50f, 1.66f, 1.84f, 2.04f, 2.26f
      };
      if (stages < 0) return 1.0f;
      if (stages > kMaxStages) stages = kMaxStages;
      return kComp[stages];
    }

    // threshold parameter is the reflection point; smaller = harder fold,
    // larger = passes more cleanly. We compute threshold from the user
    // fold position via: threshold = 1 - fold * 0.95 (so fold=0 →
    // threshold=1.0, no fold; fold=1 → threshold=0.05, deep fold).
    static inline float threshold_from_fold(float fold)
    {
      if (fold < 0.0f) fold = 0.0f;
      if (fold > 1.0f) fold = 1.0f;
      return 1.0f - fold * 0.95f;
    }

    // Threshold-reflection folder. Returns folded sample with compensation.
    static inline float fold(float x, float threshold)
    {
      int stages = 0;
      while (stages < kMaxStages)
      {
        if (x > threshold)
        {
          x = 2.0f * threshold - x;
          stages++;
        }
        else if (x < -threshold)
        {
          x = -2.0f * threshold - x;
          stages++;
        }
        else
        {
          break;
        }
      }
      return x * compensation(stages);
    }

    // Top-quarter pulse train: when fold > 0.75, mix in a polarity-pulse
    // proportional to fold position past the knee. At fold=1.0 the pulse
    // contribution is at full-strength signum(folded) * threshold; at
    // fold=0.75 it is zero.
    static inline float pulse_mix(float folded, float fold, float threshold)
    {
      if (fold <= 0.75f) return 0.0f;
      const float amount = (fold - 0.75f) * 4.0f;     // 0..1 across 0.75..1.0
      const float sign = folded >= 0.0f ? 1.0f : -1.0f;
      return sign * threshold * amount;
    }
  }
}
