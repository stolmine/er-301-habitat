#pragma once

// Lightweight trig approximations for Network's geometry generator.
// No libm sinf / cosf in package paths per feedback_package_trig_lut
// (am335x package .so miscomputes them at runtime).
//
// poly_sin / poly_cos are Bhaskara approximations lifted from
// mods/spreadsheet/visadhara/morph.h:25-69 — accurate to ~1.5% over
// half a cycle, which is plenty for perceptual geometry (used to
// compute listener position on a unit circle from the motion macro).
//
// Header-only inline. Phase 1 only uses sin/cos for the listener
// motion path; the per-tap pan derivation is just `dy/dist` (the y
// component of the unit vector from listener to reflector), which
// is sin(azimuth) directly without needing atan2.

namespace stolmine
{
  namespace network_trig
  {
    // sin(πt) for phase t in [0, 1) — actually sin(π*(2*phase - 1))
    // which is -sin(2π*phase). Sign doesn't matter for the listener
    // path computation since the geometry is rotation-invariant.
    static inline float poly_sin(float phase)
    {
      const float t = 2.0f * phase - 1.0f;
      const float at = t < 0.0f ? -t : t;
      const float p = at * (1.0f - at);
      const float n = 16.0f * p;
      const float d = 5.0f - 4.0f * p;
      const float mag = n / d;
      return t < 0.0f ? -mag : mag;
    }

    // cos(2π·phase) via phase shift — cos(x) = sin(x + π/2).
    // For phase in [0, 1), that's sin offset by 0.25.
    static inline float poly_cos(float phase)
    {
      float shifted = phase + 0.25f;
      if (shifted >= 1.0f) shifted -= 1.0f;
      return poly_sin(shifted);
    }

    // Reciprocal — used for 1/r distance gain. Block-rate so the
    // compiler's `1.0f / x` is fine; Cortex-A8 emits VFP DIV.
    static inline float reciprocal(float x)
    {
      return 1.0f / x;
    }
  }
}
