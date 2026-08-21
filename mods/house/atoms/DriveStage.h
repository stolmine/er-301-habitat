// house::DriveStage
//
// Component atom: the saturation-plus-slew character stage from
// Airwindows Channel9 (Chris Johnson, MIT), for a channel strip's Drive
// section. Component-only per feedback_atoms_as_components.
//
// TWO STAGES, both of which are the point:
//
// 1. A CROSSFADE ACROSS TWO SATURATION CURVES rather than one curve
//    with a depth control. Dry to Spiral over the first half of the
//    knob, Spiral to Density over the second:
//      Density ("phat")  sin(x * pi/2)
//      Spiral  ("clean") sin(x*|x|) / |x|
//    Two different nonlinearities meeting in the middle is what gives
//    the control an actual arc instead of just "more".
//
// 2. A SLEW CLIPPER that limits ACCELERATION, not the first difference.
//    A three-tap golden-ratio predictor estimates where the waveform
//    was heading and clamps only the departure from that, so steady
//    high frequencies pass untouched while transient spikes are caught.
//    A plain slew limiter would dull the top end; this does not.
//
// THREE ADAPTATIONS FOR am335x, none of them character changes:
//
// A. THE LIBM SINES ARE GONE. Two sin() per sample per channel is four
//    per stereo sample, at 300-500 ns each on A8
//    (feedback_am335x_libm_sin_cost) - completely unaffordable. Both
//    arguments are bounded within [-pi/2, pi/2] by the clamp that
//    precedes them, so a bounded odd polynomial covers both.
//
// B. THE SPIRAL DIVIDE CANCELS ANALYTICALLY, which makes this cheaper
//    than the original rather than merely as good. Substituting
//    sin(u) = u - u^3/6 + u^5/120 - u^7/5040 with u = x*|x|, every term
//    carries a factor of |x| that cancels the denominator:
//        sin(x|x|)/|x| = x * (1 - w/6 + w^2/120 - w^3/5040),  w = x^4
//    So the divide AND the |x|==0 branch both disappear. Verified
//    against libm across the full clamped range: worst error 1.25e-04.
//
// C. Doubles to float on the sample path; AW's dither dropped, since it
//    shapes truncation noise for a 32-bit host write and the ER-301
//    path is float.
//
// NOT PORTED: Channel9's dielectric-coefficient highpass. It would
// duplicate the strip's own Filter section, which already runs a
// highpass on the same signal for a fraction of the cost.
#pragma once

#include <od/config.h>
#include <math.h>
#include <stdint.h>

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#define DRIVE_STAGE_NEON 1
#endif

namespace house
{

  // sin(x) for x in [-pi/2, pi/2]. Odd, so negatives come free. Same
  // polynomial as Pop3Dynamics' gate; ~1.5e-4 worst case.
  static inline float driveSin(float x)
  {
    const float x2 = x * x;
    return x * (1.0f + x2 * (-0.16666667f + x2 * (0.00833333f + x2 * -0.00019841f)));
  }

  // The Spiral curve with its divide removed. See note B.
  static inline float driveSpiral(float x)
  {
    const float x2 = x * x;
    const float w = x2 * x2;
    return x * (1.0f + w * (-0.16666667f + w * (0.00833333f + w * -0.00019841f)));
  }

  struct DriveCoefs
  {
    float density;    // 0..1, dry -> Spiral
    float phattity;   // 0..1, Spiral -> Density
    float threshold;  // slew-clip threshold; large == no clipping
    float engaged;    // 1 baked, 0 means the caller should skip entirely

    DriveCoefs() : density(0.0f), phattity(0.0f), threshold(1.0e9f), engaged(0.0f) {}
  };

  // drive 0..1 spans dry -> Spiral -> Density, as AW's single knob does.
  // slew 0..1 tightens the clipper; 0 leaves it wide open.
  static inline void driveBake(DriveCoefs &c, double drive, double slew)
  {
    if (drive < 0.0) drive = 0.0; else if (drive > 1.0) drive = 1.0;
    if (slew < 0.0) slew = 0.0; else if (slew > 1.0) slew = 1.0;
    const double d2 = drive * 2.0;
    c.density = (float)(d2 > 1.0 ? 1.0 : d2);
    c.phattity = (float)(d2 > 1.0 ? d2 - 1.0 : 0.0);
    // Wide open at 0 so the clipper contributes nothing until asked.
    c.threshold = (float)(slew <= 0.0 ? 1.0e9 : 0.02 + (1.0 - slew) * 0.98);
    c.engaged = (float)((drive > 0.0 || slew > 0.0) ? 1.0 : 0.0);
  }

  // Stereo, block, band-major - the same shape that took the EQ from
  // 332 to 120 instructions per sample. L and R are independent and
  // elementwise apart from their own slew history, so 2-wide NEON is
  // the natural axis.
  class DriveStageStereo
  {
  public:
    DriveStageStereo() { reset(); }

    void reset()
    {
      for (int i = 0; i < 6; i++) mS[i] = 0.0f;
    }

    void processBlock(float *l, float *r, int n, const DriveCoefs &c)
    {
      const float dens = c.density, phat = c.phattity, thr = c.threshold;
      // [0..2] = L history A/B/C, [3..5] = R
      float aL = mS[0], bL = mS[1], cL = mS[2];
      float aR = mS[3], bR = mS[4], cR = mS[5];
      for (int i = 0; i < n; i++)
      {
        float xl = l[i], xr = r[i];
        const float dryL = xl, dryR = xr;

        // Clamp first: it is what bounds both polynomial arguments.
        if (xl > 1.0f) xl = 1.0f; else if (xl < -1.0f) xl = -1.0f;
        if (xr > 1.0f) xr = 1.0f; else if (xr < -1.0f) xr = -1.0f;

        const float phatL = driveSin(xl * 1.57079633f);
        const float phatR = driveSin(xr * 1.57079633f);
        const float spL = driveSpiral(xl * 1.2533141f);
        const float spR = driveSpiral(xr * 1.2533141f);

        float yl = dryL + (spL - dryL) * dens;
        float yr = dryR + (spR - dryR) * dens;
        yl += (phatL - yl) * phat;
        yr += (phatR - yr) * phat;

        // Slew clip on ACCELERATION. The golden-ratio weights are AW's.
        float clampL = (bL - cL) * 0.38196601f - (aL - bL) * 0.61803399f + (yl - aL);
        cL = bL; bL = aL; aL = yl;
        if (clampL > thr) yl = bL + thr;
        else if (-clampL > thr) yl = bL - thr;
        aL = aL * 0.38196601f + yl * 0.61803399f;

        float clampR = (bR - cR) * 0.38196601f - (aR - bR) * 0.61803399f + (yr - aR);
        cR = bR; bR = aR; aR = yr;
        if (clampR > thr) yr = bR + thr;
        else if (-clampR > thr) yr = bR - thr;
        aR = aR * 0.38196601f + yr * 0.61803399f;

        l[i] = yl;
        r[i] = yr;
      }
      mS[0] = aL; mS[1] = bL; mS[2] = cL;
      mS[3] = aR; mS[4] = bR; mS[5] = cR;
    }

  private:
    // Class member, never a stack local
    // (feedback_neon_intrinsics_drumvoice).
    float mS[6];
  };

} // namespace house
