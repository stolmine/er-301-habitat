// house::Spiral
//
// Component-only atom (no od::Object, no Lua unit, no toc entry)
// per feedback_atoms_as_components. Smoothest-curve sin()
// saturator from the Airwindows catalog (Chris Johnson, MIT).
// Stateless, header-only inline function.
//
// Lifted from AW Spiral source (~/repos/airwindows/plugins/MacVST/
// Spiral/source/SpiralProc.cpp lines 50-65). Identical math.
// Per feedback_identical_means_identical: do not "improve"
// the curve shape.
//
// Use as the head-bump / smooth-saturation building block for
// composition. Currently consumed by RotCoat (low-band head-bump
// option, deferred to Phase 2). ChromeOxide also uses this same
// math on its high band, lifted inline rather than calling out
// to this helper to avoid the function-call cost in the tight
// per-line inner loop.

#pragma once

#include <math.h>

#ifndef SWIGLUA
// SWIG (during %include of consuming atom headers) should not
// see component-only helpers, otherwise it tries to generate
// Lua bindings for them. The C++ compiler still sees the helper
// because every actual build path has SWIGLUA undefined (the
// SWIG wrapper compile undefs it via the %{}/atoms include
// pattern; other .cpp files never define it).
namespace house
{

  // x: input sample (range ~[-1, 1] typical, will clip beyond)
  // densityA: drive amount, must be > 0. AW source uses
  //   densityA = intensity*80 + 1.0 (range [1, 81] for
  //   intensity in [0, 1]). At densityA=1, output ≈ x*sin(1)/1
  //   for small x — gentle. At densityA=81, output is heavily
  //   compressed.
  //
  // Output is bounded to [-1/densityA, 1/densityA].
  static inline double spiralSaturate(double x, double densityA)
  {
    double absX = fabs(x) * densityA;
    if (absX > 1.57079633) absX = 1.57079633; // clamp to pi/2
    double s = sin(absX);
    return (x > 0.0) ? (s / densityA) : -(s / densityA);
  }

  // Fast variant: same curve as spiralSaturate but uses a 5th-
  // order Taylor approximation of sin() on the bounded [0, pi/2]
  // domain instead of a libm sin() call. Cost is ~10 cycles
  // vs ~200 cycles for scalar libm sin on Cortex-A8 (which has
  // no DP NEON), a ~20x speedup. Max error at pi/2 is 0.45%
  // which is inaudible for a saturator.
  //
  // Use this in tight per-sample / per-cycle paths where the
  // exact AW sin shape isn't sonically critical. ChromeOxide's
  // high-band sat and Spiral as a standalone unit (if it ever
  // becomes one) should stick with the libm version for AW
  // fidelity; XYZ's per-line governors / per-cycle sat use this
  // fast version since they're called many times per sample.
  //
  // Polynomial: sin(x) ≈ x * (1 + x² * (c1 + x² * c2))
  // where c1 = -1/6, c2 = 1/120. Horner form, 3 muls + 2 adds.
  static inline double spiralFastSaturate(double x, double densityA)
  {
    double absX = fabs(x) * densityA;
    if (absX > 1.5707963267948966) absX = 1.5707963267948966;
    double x2 = absX * absX;
    double s = absX * (1.0 + x2 * (-0.16666666666666666
                                   + x2 * 0.008333333333333333));
    return (x > 0.0) ? (s / densityA) : -(s / densityA);
  }

  // Float overload: identical Taylor poly in float, for hybrid-float per-sample paths that
  // would otherwise cast to/from double on every call (the non-pipelined VFPv3 cost on
  // Cortex-A8). Same curve; float precision is ample for a saturator.
  static inline float spiralFastSaturate(float x, float densityA)
  {
    float absX = fabsf(x) * densityA;
    if (absX > 1.5707963f) absX = 1.5707963f;
    float x2 = absX * absX;
    float s = absX * (1.0f + x2 * (-0.16666667f + x2 * 0.0083333333f));
    return (x > 0.0f) ? (s / densityA) : -(s / densityA);
  }

} // namespace house
#endif // !SWIGLUA
