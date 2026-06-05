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

} // namespace house
#endif // !SWIGLUA
