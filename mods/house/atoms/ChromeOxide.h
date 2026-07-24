// house::ChromeOxide
//
// Stereo tape-rot atom (od::Object instantiable from Lua via
// libhouse.ChromeOxide()). Component atom -- no Lua unit + no
// toc entry per feedback_atoms_as_components; consumed by chain
// units like TickerTape.
//
// SOURCE: AW ChromeOxide (Chris Johnson, MIT, ~/repos/airwindows/
// plugins/MacVST/ChromeOxide/source/). Identical math per
// feedback_identical_means_identical except:
//   - Stereo split into two mono per-sample helpers (one per
//     side) with different RNG seeds for L/R noise decorrelation
//   - fpd RNG replaced by per-instance xorshift32 (deterministic,
//     seeded differently per side)
//   - Per-sample 32-bit dither dropped (per template)
//   - High-band sin() saturator replaced with spiralFastSaturate
//     (5th-order Taylor poly, ~20x faster than libm sin, 0.45%
//     curve error inaudible for a saturator)
//
// PARAMETERS (AW-faithful naming):
//   Drive  (A) -> intensity = 0.9 + Drive^2 ; range [0.9, 1.9]
//   Output (B) -> bias = Output / 1.31578 ; range [0, 0.76]
//
// TAPE-ROT MECHANISM (per side, per sample):
//   1. Alternating-IIR highpass split (flip toggles A/B):
//      separates high band (inputSample) from low band (bassSample)
//   2. Glitch-modulated subtraction on bass:
//      bassSample -= input * (|input|*glitch)^2
//   3. Alternating-IIR lowpass smooth on bass (flip toggles C/D)
//   4. High-band noise-FM warble: bias + xorshift32-noise drives
//      a 5-slot piecewise-linear interp into sample history
//   5. spiralFastSaturate on the warbled high band
//   6. Per-band gain trim + recombine
//
// RATE COMPATIBILITY: ChromeOxide's IIR coefficients ARE
// sample-rate-dependent (the AW formula bakes 1/overallscale).
// Running at HOST RATE in TickerTape this is correct -- the IIR
// behaves as AW intended. **DO NOT** wrap ChromeOxide inside a
// reduced-rate domain (e.g. inside an undersample shell) without
// adjusting the overallscale passed to bakeCoefs. This was the
// RotCoat trap.
//
// State per ChromeOxide Object: 2 x Mono instances (~144 B) +
// inlets/outlets/parameters. Trivial memory.

#pragma once

#include <od/config.h>
#include <od/objects/Object.h>
#include <math.h>
#include <stdint.h>

#include "Spiral.h"

namespace house
{

  class ChromeOxide : public od::Object
  {
  public:
    ChromeOxide()
    {
      addInput(mInL);
      addInput(mInR);
      addOutput(mOutL);
      addOutput(mOutR);
      addParameter(mDrive);
      addParameter(mOutput);

      // Decorrelate L/R noise RNG seeds.
      mMonoL.seed(0xA1B2C3D5u);
      mMonoR.seed(0xD5C3B2A1u);
    }

    virtual ~ChromeOxide() {}

#ifndef SWIGLUA

    // ----- Nested helpers (hidden from SWIG via the surrounding
    // #ifndef SWIGLUA; visible to C++ compilation when SWIGLUA
    // is undefined per the standard %{}/atoms include pattern).
    // Scoped to ChromeOxide to avoid polluting the house
    // namespace.

    struct Coefs
    {
      // float: read every sample; baked once per block (float precision ample for gains/
      // amounts). Keeping them float avoids per-sample float<->double casts in the hot path.
      float iirAmount;
      float bias;
      float noise;
      float glitch;
      float indrive;
      float densityA;
      float bassGainTrim;
      float trebleGainTrim;
    };

    static void bakeCoefs(double A, double B, double overallscale, Coefs& out)
    {
      double bias = B / 1.31578947368421;
      double intensity = 0.9 + (A * A);
      out.iirAmount = pow(1.0 - (intensity / (10.0 + (bias * 4.0))), 2.0) / overallscale;
      // Clamp iirAmount to stable one-pole range. AW's formula
      // assumes overallscale >= 1; if a consumer runs at reduced
      // rate with effective_overallscale < 1, the formula can
      // produce iirAmount > 1 = degenerate IIR. Clamp.
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

    class Mono
    {
    public:
      Mono()
      {
        iirA = iirB = iirC = iirD = 0.0;
        secondSample = thirdSample = fourthSample = fifthSample = 0.0;
        flip = false;
        fpd = 0x9E3779B9u;
      }

      void seed(uint32_t s) { fpd = (s == 0u) ? 0x9E3779B9u : s; }

      void reset()
      {
        iirA = iirB = iirC = iirD = 0.0;
        secondSample = thirdSample = fourthSample = fifthSample = 0.0;
        flip = false;
      }

      float process(float in, const Coefs& c)
      {
        float low, high;
        processSplit(in, c, low, high);
        return low + high;
      }

      void processSplit(float in, const Coefs& c,
                        float& outLow, float& outHigh)
      {
        float inputSample = in * c.indrive;
        float bassSample = inputSample;

        if (flip)
        {
          iirA = (iirA * (1.0f - c.iirAmount)) + (inputSample * c.iirAmount);
          inputSample -= iirA;
        }
        else
        {
          iirB = (iirB * (1.0f - c.iirAmount)) + (inputSample * c.iirAmount);
          inputSample -= iirB;
        }
        // inputSample now holds the high-band component.

        bassSample -= (inputSample * (fabs(inputSample) * c.glitch) * (fabs(inputSample) * c.glitch));

        if (flip)
        {
          iirC = (iirC * (1.0f - c.iirAmount)) + (bassSample * c.iirAmount);
          bassSample = iirC;
        }
        else
        {
          iirD = (iirD * (1.0f - c.iirAmount)) + (bassSample * c.iirAmount);
          bassSample = iirD;
        }

        flip = !flip;

        // Noise-FM warble via 5-slot piecewise interp.
        float bridgerectifier = inputSample;
        float randy = c.bias + ((float)nextRand() / (float)0x7FFFFFFF) * c.noise;
        if ((randy >= 0.0f) && (randy < 1.0f))
          bridgerectifier = (inputSample * randy) + (secondSample * (1.0f - randy));
        else if ((randy >= 1.0f) && (randy < 2.0f))
          bridgerectifier = (secondSample * (randy - 1.0f)) + (thirdSample * (2.0f - randy));
        else if ((randy >= 2.0f) && (randy < 3.0f))
          bridgerectifier = (thirdSample * (randy - 2.0f)) + (fourthSample * (3.0f - randy));
        else if ((randy >= 3.0f) && (randy < 4.0f))
          bridgerectifier = (fourthSample * (randy - 3.0f)) + (fifthSample * (4.0f - randy));
        // Shift sample history.
        fifthSample = fourthSample;
        fourthSample = thirdSample;
        thirdSample = secondSample;
        secondSample = inputSample;
        inputSample = bridgerectifier;

        // spiralFastSaturate (5th-order Taylor poly) on the high
        // band. ~20x faster than libm sin, inaudible curve error.
        inputSample = spiralFastSaturate(inputSample, c.densityA);

        outHigh = inputSample * c.trebleGainTrim;
        outLow = bassSample * c.bassGainTrim;
      }

    private:
      uint32_t nextRand()
      {
        fpd ^= fpd << 13;
        fpd ^= fpd >> 17;
        fpd ^= fpd << 5;
        return fpd & 0x7FFFFFFFu;
      }

      float iirA, iirB, iirC, iirD;
      float secondSample, thirdSample, fourthSample, fifthSample;
      bool flip;
      uint32_t fpd;
    };

    // ----- Object members -----
    od::Inlet     mInL{"In L"};
    od::Inlet     mInR{"In R"};
    od::Outlet    mOutL{"Out L"};
    od::Outlet    mOutR{"Out R"};
    od::Parameter mDrive{"Drive", 0.5f};
    od::Parameter mOutput{"Output", 0.5f};

    virtual void process()
    {
      float *in1 = mInL.buffer();
      float *in2 = mInR.buffer();
      float *out1 = mOutL.buffer();
      float *out2 = mOutR.buffer();

      double A = (double)mDrive.value();
      double B = (double)mOutput.value();
      if (A < 0.0) A = 0.0; if (A > 1.0) A = 1.0;
      if (B < 0.0) B = 0.0; if (B > 1.0) B = 1.0;

      double overallscale = 1.0;
      overallscale /= 44100.0;
      overallscale *= (double)globalConfig.sampleRate;

      Coefs cox;
      bakeCoefs(A, B, overallscale, cox);

      int sampleFrames = FRAMELENGTH;
      while (--sampleFrames >= 0)
      {
        float inL = *in1;
        float inR = *in2;
        if (fabsf(inL) < 1.18e-23f) inL = 1.18e-17f;
        if (fabsf(inR) < 1.18e-23f) inR = 1.18e-17f;

        *out1 = mMonoL.process(inL, cox);
        *out2 = mMonoR.process(inR, cox);
        in1++; in2++; out1++; out2++;
      }
    }

  private:
    Mono mMonoL;
    Mono mMonoR;
#endif
  };

} // namespace house
