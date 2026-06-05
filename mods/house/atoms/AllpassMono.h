// house::AllpassMono
//
// Component-only atom (no od::Object, no Lua unit, no toc entry)
// per feedback_atoms_as_components. Schroeder all-pass filter in
// nested form, designed to share a delay line with an FDN line
// buffer. No internal state — the buffer the FDN already provides
// IS the APF's delay line.
//
// Nested form (direct form II for the APF transfer function
// H(z) = (-g + z^-N) / (1 - g * z^-N)):
//   v[n] = x[n] + g * v[n-N]
//   y[n] = -g * v[n] + v[n-N]
//
// In our usage:
//   x[n]      = the FDN-line input (avg + feedback*regen + cross)
//   v[n]      = the value we write to the line buffer THIS cycle
//   v[n-N]    = the tap we read from the buffer (delayed cycle)
//   y[n]      = the APF output (= the FDN line's effective tap)
//
// At g = 0: v[n] = x[n] (write input unchanged), y[n] = v[n-N]
//           (read pure delay). Identical to a non-APF FDN line.
// At g > 0: APF diffuses without changing magnitude response;
//           higher g = more smearing.
//
// Stability: |g| < 1 is required. We clamp X-derived g to [0,
// 0.7] in the consumer.

#pragma once

#include <math.h>

#ifndef SWIGLUA
namespace house
{

  // Compute the two values needed per cycle: vNew (what to write
  // back to the FDN line buffer) and yOut (the tap value going
  // into Householder). vDelayed is the FDN-line read (which is
  // v[n-N] in APF nested form).
  static inline void allpassNestedStep(
      double xNow,
      double vDelayed,
      double g,
      double& vNew,
      double& yOut)
  {
    vNew = xNow + g * vDelayed;
    yOut = -g * vNew + vDelayed;
  }

  // Inverse of Spiral (sin-based) saturator. Used by XYZ's
  // Nested mode for the post-FDN de-saturate stage. Spiral's
  // output is bounded to [-1/d, 1/d], so the input to asin is
  // bounded to [-1, 1] (within asin's natural domain). Cost:
  // 1 asin call, ~1.5x a sin call on scalar libm.
  //
  // Lives in the same component header as the APF helper to
  // keep XYZ's "math primitives" in one place.
  static inline double inverseSpiralSaturate(double y, double densityA)
  {
    double absY = fabs(y) * densityA;
    if (absY > 1.0) absY = 1.0; // safety
    double original = asin(absY) / densityA;
    return (y > 0.0) ? original : -original;
  }

} // namespace house
#endif // !SWIGLUA
