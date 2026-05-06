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

    // Topology note: Visadhara uses the classic Buchla 259/281 timbre-
    // folder approach — fixed threshold, variable INPUT DRIVE. As the
    // fold knob opens up, the signal is amplified into the folder so
    // every sample (not just rare peaks) experiences reflection. This
    // gives a perceptually-even progression of fold intensity, in
    // contrast to a "vary-the-threshold" approach where folding only
    // bites occasional peaks of the heavy-tailed additive bus until
    // threshold drops below RMS.
    //
    // See drive_from_fold() for the drive curve. Threshold is fixed at
    // 1.0 at the call site.
    static inline float drive_from_fold(float fold)
    {
      if (fold < 0.0f) fold = 0.0f;
      if (fold > 1.0f) fold = 1.0f;
      return 1.0f + fold * 5.0f;       // 1x..6x linear (no low-fold cut)
    }

    // Post-fold output gain. Folded signal RMS grows with fold drive
    // (more harmonic content + many fold stages compounding via the
    // compensation table). To level the perceived loudness across the
    // fold sweep we attenuate at high fold rather than boost at low
    // fold: 1.0× unfolded, ~0.35× at fold=1. Linear.
    static inline float post_fold_gain(float fold)
    {
      if (fold < 0.0f) fold = 0.0f;
      if (fold > 1.0f) fold = 1.0f;
      return 1.0f - fold * 0.65f;
    }

    // Master soft saturator. 3rd-order polynomial soft-knee bounded
    // ±1; hard clamp beyond ±1.5. Catches residual peaks from extreme
    // settings (Metal + max fold + max negative attack with cross-
    // injection, etc.) without introducing dynamics artifacts. No
    // state, suits percussion transients.
    //
    // y = x - x³/6.75   →   y(1.5) = 1, y(-1.5) = -1.
    static inline float master_sat(float x)
    {
      if (x > 1.5f) return 1.0f;
      if (x < -1.5f) return -1.0f;
      return x - x * x * x * (1.0f / 6.75f);
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
