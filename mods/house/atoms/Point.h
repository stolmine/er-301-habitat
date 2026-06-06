// house::Point
//
// Component-only atom (no od::Object, no Lua unit, no toc entry)
// per feedback_atoms_as_components. Helper class encapsulating the
// AW Point transient-designer math (Chris Johnson, MIT). Ported
// verbatim from ~/repos/airwindows/plugins/MacVST/Point/source/
// PointProc.cpp.
//
// AW Point identity: two leaky integrators per side ("nib" faster,
// "nob" slower) with fpFlip alternation between A/B pairs. The
// instantaneous transient gain is nib/nob — when a transient
// arrives, nib responds faster than nob, ratio spikes above 1,
// the sample gets multiplied by that ratio, transient amplified.
// On flat material, nib and nob converge, ratio → 1 (identity).
//
// The fpFlip alternation between two integrator PAIRS (A and B)
// avoids smearing the leading edge of a transient — each pair
// effectively samples every other input, then the ratio is
// computed from whichever pair was just updated (freshest data).
//
// Per side state: 5 doubles (nibA, nobA, nibB, nobB, prev factor)
// + 1 bool. Stateless w.r.t. external context — all coefficients
// passed in.
//
// Used by house::Carriage as the transient-injection element of
// the dynamics chain, driven by an engagement-scaled bias on the
// nobDiv coefficient (boost mode = nob slower than nib = ratio
// peaks above 1 on transients).

#pragma once

#include <math.h>
#include <string.h>

#ifndef SWIGLUA
namespace house
{

  // Block-rate baked coefficients. Compute once per process()
  // call, pass into per-sample process() to avoid recomputing.
  struct PointCoefs
  {
    double nibDiv;      // 1 / pow(C+0.2, 7) / overallscale — nib (fast) leak rate
    double nobDiv;      // baseline nob (slow) leak rate
    double oneOverNibDivPlusOne;  // precomputed 1 / (1 + 1/nibDiv) — replaces per-sample divide
    double oneOverNibDiv;         // precomputed 1 / nibDiv — replaces per-sample divide
    double oneOverNobDivPlusOne;  // precomputed 1 / (1 + 1/nobDiv) — replaces per-sample divide
    double oneOverNobDiv;         // precomputed 1 / nobDiv — replaces per-sample divide
  };

  // Bake block-rate coefficients from user inputs.
  //
  // cKnob: time scale, range typical [0.0, 1.0]. AW formula:
  //        nibDiv = 1 / pow(cKnob + 0.2, 7) / overallscale.
  //        Low cKnob → large nibDiv → slow integrator.
  //        High cKnob → small nibDiv → fast integrator.
  //
  // bBoost: positive boost amount, range typical [0.0, 0.4].
  //         Maps to AW's B-knob boost mode (B in (0.5, 1.0]).
  //         The AW formula `nobDiv = nibDiv / (1.001 - ((B*2)-1))`
  //         with B = 0.5 + bBoost becomes:
  //         nobDiv = nibDiv / (1.001 - 2*bBoost).
  //         At bBoost=0: nobDiv = nibDiv (identity, ratio always 1).
  //         At bBoost=0.4: nobDiv ≈ 5 × nibDiv (strong boost).
  //
  // sampleRate: host sample rate (typically 48000).
  static inline void pointBakeCoefs(double cKnob, double bBoost,
                                    double sampleRate, PointCoefs &c)
  {
    double overallscale = sampleRate / 44100.0;
    double base = cKnob + 0.2;
    // b⁷ via three intermediate values: b², b⁴, then b⁴·b²·b = b⁷.
    // Four muls total, exact integer power (vs libm pow() at ~80
    // cycles + dependency on platform double-precision libm).
    double b2 = base * base;
    double b4 = b2 * b2;
    double basePow = b4 * b2 * base;     // = b⁷
    c.nibDiv = 1.0 / basePow / overallscale;
    // Guard against bBoost out of band
    if (bBoost < 0.0) bBoost = 0.0;
    if (bBoost > 0.49) bBoost = 0.49;  // 2*bBoost stays < 1.001
    c.nobDiv = c.nibDiv / (1.001 - 2.0 * bBoost);

    // Precompute reciprocals for hot loop (per
    // feedback_cortex_a8_no_double_in_hot_loops — eliminate
    // per-sample divides).
    c.oneOverNibDiv = 1.0 / c.nibDiv;
    c.oneOverNibDivPlusOne = 1.0 / (1.0 + c.oneOverNibDiv);
    c.oneOverNobDiv = 1.0 / c.nobDiv;
    c.oneOverNobDivPlusOne = 1.0 / (1.0 + c.oneOverNobDiv);
  }

  class PointMono
  {
  public:
    PointMono() { reset(); }

    void reset()
    {
      mNibA = mNobA = mNibB = mNobB = 0.0;
      mLastFactor = 1.0;  // identity start (no boost until integrators wind up)
      mFpFlip = true;
    }

    // Per-sample transient designer. Returns inputSample * nib/nob.
    //
    // The fpFlip alternation between A and B pairs is preserved
    // verbatim from AW. Each call updates ONE pair (whichever is
    // selected by mFpFlip), uses the just-updated pair's ratio as
    // the transient gain, and flips for next sample. This avoids
    // smearing the leading edge of transients.
    inline double process(double inputSample, const PointCoefs &c)
    {
      double absolute = fabs(inputSample);
      double factor;
      if (mFpFlip)
      {
        mNibA = (mNibA + absolute * c.oneOverNibDiv) * c.oneOverNibDivPlusOne;
        mNobA = (mNobA + absolute * c.oneOverNobDiv) * c.oneOverNobDivPlusOne;
        factor = (mNobA > 1.0e-30) ? (mNibA / mNobA) : mLastFactor;
      }
      else
      {
        mNibB = (mNibB + absolute * c.oneOverNibDiv) * c.oneOverNibDivPlusOne;
        mNobB = (mNobB + absolute * c.oneOverNobDiv) * c.oneOverNobDivPlusOne;
        factor = (mNobB > 1.0e-30) ? (mNibB / mNobB) : mLastFactor;
      }
      mLastFactor = factor;
      mFpFlip = !mFpFlip;
      return inputSample * factor;
    }

  private:
    double mNibA, mNobA, mNibB, mNobB;
    double mLastFactor;  // carries previous valid factor when nob denormal-flushed
    bool   mFpFlip;
  };

} // namespace house
#endif // !SWIGLUA
