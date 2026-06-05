// house::ChromeOxide
//
// Component-only atom (no od::Object, no Lua unit, no toc entry)
// per feedback_atoms_as_components. Per-line tape-rot building
// block for composition. Currently consumed by RotCoat as the
// per-FDN-line tape character stage.
//
// Lifted from AW ChromeOxide (Chris Johnson, MIT, ~/repos/
// airwindows/plugins/MacVST/ChromeOxide/source/). Identical
// math per feedback_identical_means_identical except for:
//   - Stereo split into mono per-instance (RotCoat needs 4 lines
//     x 2 sides = 8 ChromeOxideMono instances per RotCoat).
//   - fpd RNG replaced with per-instance xorshift32 (different
//     seed per line for decorrelation).
//   - Per-sample 32-bit dither dropped (per template).
//   - Block-rate scalars extracted into a separate Coefs struct
//     baked once per block by the consumer.
//   - Spiral-style sin() saturation on the high band inlined
//     rather than calling Spiral.h's helper (avoids function-
//     call cost in tight per-line inner loop).
//
// Tape-rot mechanism (per line, per sample):
//   1. Alternating-IIR highpass split (flip toggles between
//      iirA and iirB) -> separates high band from low band.
//   2. Glitch-modulated subtraction on the low band (driven by
//      |high band|^2 * glitch) -> nonlinear interaction.
//   3. Alternating-IIR lowpass smooth (flip toggles between
//      iirC and iirD) on the low band.
//   4. High-band noise-FM warble: a bias+xorshift32-noise index
//      `randy` selects between 5 sample-history slots
//      (secondSample..fifthSample) and the current sample via
//      a piecewise-linear branch chain. The bias drift across
//      the 5 slots IS the warble; `noise` adds wow.
//   5. Spiral-style sin() saturation on the warbled high band.
//   6. Per-band gain trim + sum back to a single sample.
//
// State per ChromeOxideMono instance: 8 doubles + 1 bool + 1
// uint32. ~72 bytes. Trivial.
//
// Per-sample cost: 4 one-pole IIR + 1 sin + 4-branch interp
// chain + xorshift32 + ~10 misc ops. ~30 FLOPs + 1 sin.

#pragma once

#include <math.h>
#include <stdint.h>

#ifndef SWIGLUA
// SWIG (during %include of RotCoat.h) should not see this
// component-only helper, otherwise it tries to generate Lua
// bindings for the helper class. The C++ compiler still sees
// it because every actual build path has SWIGLUA undefined
// (the SWIG wrapper compile undefs it via the %{}/atoms include
// pattern; other .cpp files never define it).
namespace house
{

  // Block-rate-baked coefficients for ChromeOxideMono. Compute
  // once per block from Mulch (or A/B params if exposed
  // separately) and pass to every per-sample process() call.
  struct ChromeOxideMonoCoefs
  {
    double iirAmount;
    double bias;
    double noise;
    double glitch;
    double indrive;
    double densityA;
    double bassGainTrim;
    double trebleGainTrim;
  };

  // Bake coefs from the two-param AW interface (A = Drive/intensity
  // shape, B = Output/bias shape). For RotCoat's single-knob Mulch
  // the caller passes A = B = Mulch.
  //
  // overallscale = current sampleRate / 44100.0 (matches AW's
  // overallscale).
  static inline void chromeOxideBakeCoefs(double A, double B,
                                           double overallscale,
                                           ChromeOxideMonoCoefs& out)
  {
    double bias = B / 1.31578947368421;
    double intensity = 0.9 + (A * A);
    out.iirAmount = pow(1.0 - (intensity / (10.0 + (bias * 4.0))), 2.0) / overallscale;
    // Clamp iirAmount to a stable one-pole range. AW's formula
    // assumes overallscale >= 1 (host sample rate >= 44.1k). When
    // a consumer (RotCoat) runs ChromeOxideMono at a REDUCED rate
    // by passing effective_overallscale < 1, the formula can
    // produce iirAmount > 1 which makes the IIR degenerate
    // (passes input straight through, no band-split). Clamp to
    // the stable region so the split keeps working at any rate.
    if (out.iirAmount > 0.9) out.iirAmount = 0.9;
    if (out.iirAmount < 0.001) out.iirAmount = 0.001;
    bias *= overallscale;
    double noise = (intensity / (1.0 + bias)) * overallscale;
    out.bias = bias;
    out.noise = noise;
    out.densityA = (intensity * 80.0) + 1.0;
    if (intensity > 1.0)
    {
      out.glitch = intensity - 1.0;
      out.indrive = intensity * intensity;
      out.bassGainTrim = 1.0 / (intensity * intensity);
      out.trebleGainTrim = (intensity + 1.0) / 2.0;
    }
    else
    {
      out.glitch = 0.0;
      out.indrive = 1.0;
      out.bassGainTrim = 1.0;
      out.trebleGainTrim = 1.0;
    }
  }

  class ChromeOxideMono
  {
  public:
    // seed must be non-zero (xorshift32 requirement). Use a
    // different seed per instance for decorrelation.
    ChromeOxideMono()
    {
      iirA = iirB = iirC = iirD = 0.0;
      secondSample = thirdSample = fourthSample = fifthSample = 0.0;
      flip = false;
      fpd = 0x9E3779B9u; // default seed; consumer should call seed()
                        // to set distinct seeds per instance for
                        // noise decorrelation
    }

    // Set the noise RNG seed. Must be non-zero (xorshift32
    // requirement). Use a different value per instance for
    // decorrelation across lines / sides.
    void seed(uint32_t s)
    {
      fpd = (s == 0u) ? 0x9E3779B9u : s;
    }

    void reset()
    {
      iirA = iirB = iirC = iirD = 0.0;
      secondSample = thirdSample = fourthSample = fifthSample = 0.0;
      flip = false;
      // do not reset fpd -- preserve decorrelation seed
    }

    // Process one sample. Returns the full ChromeOxide output
    // (low band saturated + high band noise-FM-warbled +
    // spiral-saturated, then recombined). When the consumer
    // wants ONLY the low band or ONLY the high band (e.g.
    // RotCoat's band-recirc flip), use processSplit() below.
    double process(double in, const ChromeOxideMonoCoefs& c)
    {
      double low, high;
      processSplit(in, c, low, high);
      return low + high;
    }

    // Split output: low band and high band returned separately.
    // Consumers (RotCoat) use this to pick which band recirculates
    // through the FDN feedback.
    void processSplit(double in, const ChromeOxideMonoCoefs& c,
                      double& outLow, double& outHigh)
    {
      double inputSample = in * c.indrive;
      double bassSample = inputSample;

      // Alternating highpass IIR split.
      if (flip)
      {
        iirA = (iirA * (1.0 - c.iirAmount)) + (inputSample * c.iirAmount);
        inputSample -= iirA;
      }
      else
      {
        iirB = (iirB * (1.0 - c.iirAmount)) + (inputSample * c.iirAmount);
        inputSample -= iirB;
      }
      // inputSample now holds the high-band component.

      // Glitch-modulated subtraction on the bass.
      bassSample -= (inputSample * (fabs(inputSample) * c.glitch) * (fabs(inputSample) * c.glitch));

      // Alternating lowpass IIR smooth on the bass.
      if (flip)
      {
        iirC = (iirC * (1.0 - c.iirAmount)) + (bassSample * c.iirAmount);
        bassSample = iirC;
      }
      else
      {
        iirD = (iirD * (1.0 - c.iirAmount)) + (bassSample * c.iirAmount);
        bassSample = iirD;
      }

      flip = !flip;

      // High-band noise-FM warble via 5-slot piecewise interp.
      double bridgerectifier = inputSample;
      double randy = c.bias + ((double)nextRand() / (double)0x7FFFFFFF) * c.noise;
      if ((randy >= 0.0) && (randy < 1.0))
        bridgerectifier = (inputSample * randy) + (secondSample * (1.0 - randy));
      else if ((randy >= 1.0) && (randy < 2.0))
        bridgerectifier = (secondSample * (randy - 1.0)) + (thirdSample * (2.0 - randy));
      else if ((randy >= 2.0) && (randy < 3.0))
        bridgerectifier = (thirdSample * (randy - 2.0)) + (fourthSample * (3.0 - randy));
      else if ((randy >= 3.0) && (randy < 4.0))
        bridgerectifier = (fourthSample * (randy - 3.0)) + (fifthSample * (4.0 - randy));
      // Shift sample history.
      fifthSample = fourthSample;
      fourthSample = thirdSample;
      thirdSample = secondSample;
      secondSample = inputSample;
      inputSample = bridgerectifier;

      // Spiral-style sin saturation on the high band (inlined).
      double br = fabs(inputSample) * c.densityA;
      if (br > 1.57079633) br = 1.57079633;
      br = sin(br);
      inputSample = (inputSample > 0.0) ? (br / c.densityA) : -(br / c.densityA);

      outHigh = inputSample * c.trebleGainTrim;
      outLow = bassSample * c.bassGainTrim;
    }

  private:
    uint32_t nextRand()
    {
      // Xorshift32 -- matches AW's fpd evolution per sample.
      fpd ^= fpd << 13;
      fpd ^= fpd >> 17;
      fpd ^= fpd << 5;
      return fpd & 0x7FFFFFFFu;
    }

    double iirA, iirB, iirC, iirD;
    double secondSample, thirdSample, fourthSample, fifthSample;
    bool flip;
    uint32_t fpd;
  };

} // namespace house
#endif // !SWIGLUA
