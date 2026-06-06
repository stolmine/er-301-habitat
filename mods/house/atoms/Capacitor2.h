// house::Capacitor2
//
// Component atom for the Capacitor2 LP filter (signal-voltage-
// modulated, gearbox-sequenced 3-pole IIR). Component-only per
// feedback_atoms_as_components — exposed as both a per-sample
// helper class (Capacitor2Mono, for monolithic-chain consumers
// like Filament) AND an od::Object wrapper (house::Capacitor2,
// for future Lua-graph instantiation).
//
// SOURCE: AW Capacitor2 (Chris Johnson, MIT). LOWPASS SIDE
// PRESERVED VERBATIM; highpass side dropped (Filament focuses
// on synth-filter LP character; HP is left for a future
// Capacitor2-based chain unit if needed).
//
// THE SIGNAL-FM TRICK:
//   dielectricScale = fabs(1 - input * dynamicFmGain / nonLin)
//   lowpassAmount   = baseAmount * dielectricScale  (clamped)
//
// The filter's per-sample IIR coefficient is modulated by the
// signal voltage itself. Asymmetric per-sample behavior preserved
// from AW: negative-going input opens the filter (dielectricScale
// > 1), positive-going input closes it (dielectricScale < 1).
//
// ENVELOPE-BOOSTED FM (deviation from raw AW, added 0.1.0.25):
// At typical ER-301 audio levels (peaks ~0.3-0.5), AW's raw
// dielectricScale only modulates by ~10-20% which feels subtle.
// A per-side envelope tracker (instant attack, ~10ms release)
// adds a dynamic gain that scales with input energy:
//   dynamicFmGain = baseFmGain + envState * envBoost
// This amplifies the per-sample modulation DURING transients
// (envState pops up) and settles back during quiet passages.
// The asymmetric character (positive vs negative half-cycles)
// is preserved — only the depth scales.
//
// dielectricScale is hard-clamped to 2.0 to prevent IIR
// instability at high Cutoff + high FM + hot signal (lowpassAmount
// = baseAmount * dielectricScale must stay <= 1.0 for invLowpass
// to stay positive; at default Cutoff=0.6 (baseAmount=0.36) the
// 2.0 cap keeps lowpassAmount = 0.72 which is comfortably under 1).
//
// THE GEARBOX:
//   Switch dispatches on `count` (cycles 0..5), each case using
//   a DIFFERENT trio of IIR LP cells (A→B→D, A→C→E, A→B→F,
//   A→C→D, A→B→E, A→C→F). This progressively steepens the
//   filter while minimizing the artifacts that would come from
//   a straight 6-cell cascade. AW's clever pattern; preserve
//   verbatim per feedback_identical_means_identical.
//
// CORTEX-A8 HOT LOOP (per feedback_cortex_a8_no_double_in_hot_loops):
//   - All state double; no float→double cast tax in hot loop
//   - AW's two per-sample divides REPLACED with block-rate
//     reciprocal precomputes (invNonLin + oneOverSpeedPlusOne)
//   - No transcendentals per sample (block-rate pow + cbrt only)
//   - Pure double FMA throughout the inner loop
//
// AW DEFAULTS NOTE (per feedback_aw_param_default_subtle):
// AW Capacitor2 defaults (A=1.0, B=0, C=1.0, D=1.0) result in
// effective BYPASS — lowpassAmount=1.0 means the IIR passes
// input through unchanged. Consumer (Filament) MUST remap user-
// facing knobs so defaults produce audible filter character.
// See Filament.h for the remap.

#pragma once

#include <od/config.h>
#include <od/objects/Object.h>
#include <math.h>
#include <string.h>

#include "Spiral.h"   // spiralFastSaturate, used as per-state limiter in the gearbox

namespace house
{

  // ----- Block-rate coefs baked once per process() call. -----
  struct Capacitor2Coefs
  {
    double lowpassChase;          // target lowpass amount this block (= A² from caller)
    double oneOverSpeedPlusOne;   // 1.0 / (lowpassSpeed + 1.0), replaces AW per-sample divide
    double lowpassSpeed;          // smoothing pole rate for cutoff knob
    double invNonLin;             // 1.0 / nonLin, replaces AW per-sample divide
    double nonLinTrim;            // 1.5 / cbrt(nonLin), output gain compensation
    double baseFmGain;            // base FM depth, no envelope contribution (= 1 at FM=0, 3 at FM=1)
    double envBoost;              // envelope-driven FM depth boost (= 0 at FM=0, 5 at FM=1)
  };

  // ----- Coef baker. Call once per process() with user-facing
  // lowpass (0..1, will be squared internally per AW) + nonLin
  // (1..7, controls FM strength — see Filament's FM-knob remap)
  // + baseFmGain (1..3, depth scaling) + envBoost (0..5, envelope-
  // driven dynamic depth amplification).
  // -----
  static inline void capacitor2BakeCoefs(double lowpassKnob,
                                          double nonLinValue,
                                          double baseFmGain,
                                          double envBoost,
                                          double prevLowpassChase,
                                          Capacitor2Coefs &out)
  {
    if (lowpassKnob < 0.0) lowpassKnob = 0.0;
    if (lowpassKnob > 1.0) lowpassKnob = 1.0;
    out.lowpassChase = lowpassKnob * lowpassKnob;  // AW: pow(A, 2)
    // Cutoff smoothing speed depends on knob movement (faster
    // when knob is moving, slower when stable — AW's perceptual
    // zipper-noise mitigation, preserved verbatim).
    double knobDelta = fabs(prevLowpassChase - out.lowpassChase);
    out.lowpassSpeed = 300.0 / (knobDelta + 1.0);
    out.oneOverSpeedPlusOne = 1.0 / (out.lowpassSpeed + 1.0);
    if (nonLinValue < 1.0) nonLinValue = 1.0;
    out.invNonLin = 1.0 / nonLinValue;
    // nonLinTrim is block-rate; cbrt is fine here.
    out.nonLinTrim = 1.5 / cbrt(nonLinValue);
    out.baseFmGain = baseFmGain;
    out.envBoost = envBoost;
  }

  // ----- Per-sample helper class. Wraps the gearbox state +
  // baseAmount smoothing for one channel. Filament instantiates
  // two (L + R). -----
  class Capacitor2Mono
  {
  public:
    Capacitor2Mono()
    {
      lowpassA = lowpassB = lowpassC = lowpassD = lowpassE = lowpassF = 0.0;
      lowpassBaseAmount = 0.0;
      count = 0;
      envState = 0.0;
      smoothedIn = 0.0;
    }

    void reset()
    {
      lowpassA = lowpassB = lowpassC = lowpassD = lowpassE = lowpassF = 0.0;
      lowpassBaseAmount = 0.0;
      count = 0;
      envState = 0.0;
      smoothedIn = 0.0;
    }

    // Process one sample. coefs are baked once per block by the
    // caller. Returns post-filter sample (caller multiplies by
    // nonLinTrim if desired for level matching).
    // Original single-argument process: envelope tracks the same
    // signal as the filter input. Kept for backwards compat with
    // the standalone Capacitor2 Object wrapper.
    inline double process(double in, const Capacitor2Coefs &c)
    {
      return process(in, in, c);
    }

    // Two-argument process: decouples envelope SOURCE from filter
    // INPUT. Filament uses this to track envelope on the post-Console
    // pre-feedback signal so APF feedback doesn't dampen the
    // envelope-driven dielectric boost.
    inline double process(double in, double envSrc, const Capacitor2Coefs &c)
    {
      // Asymmetric envelope follower with proper attack + release —
      // mimics a moderate mix-bus compressor's "auto" detector
      // (G-comp territory): ~20ms attack TC + ~250ms release TC.
      // Tracks signal LEVEL rather than instantaneous peaks:
      //   - Fast transients (< 20ms): envState rises partway,
      //     never reaches peak
      //   - Sustained content (> 100ms): envState tracks RMS-like
      //   - Multiple closely-spaced transients accumulate
      //   - Slow release → "musical recovery" feel, not pumping
      // Result: filter opens in response to overall signal energy,
      // settles gradually — "sun through clouds" rather than
      // sample-by-sample twitching.
      const double attackAlpha  = 0.999;     // ~20ms TC at 48k
      const double releaseAlpha = 0.99992;   // ~250ms TC at 48k
      double absEnv = fabs(envSrc);
      double alpha = (absEnv > envState) ? attackAlpha : releaseAlpha;
      envState = envState * alpha + absEnv * (1.0 - alpha);

      // Smooth the SIGNED input that drives dielectricScale. Without
      // this, the per-sample audio-rate sign swings cause the filter
      // coefficient to modulate at audio rates → audible high-end
      // scratch at high Cutoff + high FM. One-pole LP at ~2 kHz
      // (alpha 0.25) tames the audio-rate scratch while preserving
      // the asymmetric character (sign of smoothedIn still flips
      // direction of modulation, just at sub-audio rates).
      smoothedIn = smoothedIn * 0.75 + in * 0.25;

      // Dynamic FM gain: base depth plus envelope-driven boost.
      double dynamicFmGain = c.baseFmGain + envState * c.envBoost;

      // Dielectric scale: signal-voltage modulation of cutoff.
      // AW: fabs(2.0 - ((in + nonLin)/nonLin)) → algebraically =
      //     fabs(1.0 - in/nonLin) = fabs(1.0 - in * invNonLin).
      // Now uses smoothedIn (not raw in) to kill audio-rate scratch,
      // with envelope-boosted dynamicFmGain folded in. One mul vs
      // AW's per-sample divide.
      double dielectricScale = fabs(1.0 - smoothedIn * dynamicFmGain * c.invNonLin);
      // Hard clamp keeps lowpassAmount = baseAmount * dielectricScale
      // <= 1.0 across reasonable Cutoff settings (prevents IIR
      // instability: invLowpass = 1 - lowpassAmount must stay >= 0).
      if (dielectricScale > 2.0) dielectricScale = 2.0;

      // Cutoff smoothing (block-rate * sample modulation).
      // AW: lowpassBaseAmount = (lowpassBaseAmount*speed + chase) / (speed+1)
      // Replaced /(speed+1) with precomputed *oneOverSpeedPlusOne.
      lowpassBaseAmount = (lowpassBaseAmount * c.lowpassSpeed + c.lowpassChase)
                          * c.oneOverSpeedPlusOne;
      double lowpassAmount = lowpassBaseAmount * dielectricScale;
      double invLowpass = 1.0 - lowpassAmount;
      // NO clamp here — instead we let spiralFastSaturate on each IIR
      // state below catch what would otherwise be runaway. When
      // lowpassAmount > 1 (high Cutoff + transient dielectric peak)
      // → invLowpass goes negative → IIR pole < 0. Without the
      // per-state spiral, this oscillates to ±inf. WITH the spiral,
      // the state is bounded ±1 each sample, so the "oscillation"
      // becomes saturation distortion — runaway converts into
      // tone, per feedback_spiral_feedback_governor.

      // Gearbox: 3-stage IIR LP dispatch per `count` (0..5).
      // Each case uses different cell trio for artifact reduction
      // (AW pattern preserved verbatim).
      //
      // EACH STATE UPDATE IS SPIRAL-LIMITED. When the filter would
      // be unstable (lowpassAmount > 1 → IIR pole goes negative),
      // the state would normally grow exponentially. With Spiral
      // bounding it to ±1 each sample, the "runaway" becomes
      // saturation distortion — runaway converts into tone.
      // The 3 cascaded stages amplify the effect: signal gathers
      // progressive harmonic content as it passes through. At
      // normal levels Spiral is near-identity (sin(x) ≈ x for
      // small x) so the filter behaves like AW's original.
      count++;
      if (count > 5) count = 0;
      switch (count)
      {
        case 0:
          lowpassA = (lowpassA * invLowpass) + (in * lowpassAmount); lowpassA = spiralFastSaturate(lowpassA, 1.0); in = lowpassA;
          lowpassB = (lowpassB * invLowpass) + (in * lowpassAmount); lowpassB = spiralFastSaturate(lowpassB, 1.0); in = lowpassB;
          lowpassD = (lowpassD * invLowpass) + (in * lowpassAmount); lowpassD = spiralFastSaturate(lowpassD, 1.0); in = lowpassD;
          break;
        case 1:
          lowpassA = (lowpassA * invLowpass) + (in * lowpassAmount); lowpassA = spiralFastSaturate(lowpassA, 1.0); in = lowpassA;
          lowpassC = (lowpassC * invLowpass) + (in * lowpassAmount); lowpassC = spiralFastSaturate(lowpassC, 1.0); in = lowpassC;
          lowpassE = (lowpassE * invLowpass) + (in * lowpassAmount); lowpassE = spiralFastSaturate(lowpassE, 1.0); in = lowpassE;
          break;
        case 2:
          lowpassA = (lowpassA * invLowpass) + (in * lowpassAmount); lowpassA = spiralFastSaturate(lowpassA, 1.0); in = lowpassA;
          lowpassB = (lowpassB * invLowpass) + (in * lowpassAmount); lowpassB = spiralFastSaturate(lowpassB, 1.0); in = lowpassB;
          lowpassF = (lowpassF * invLowpass) + (in * lowpassAmount); lowpassF = spiralFastSaturate(lowpassF, 1.0); in = lowpassF;
          break;
        case 3:
          lowpassA = (lowpassA * invLowpass) + (in * lowpassAmount); lowpassA = spiralFastSaturate(lowpassA, 1.0); in = lowpassA;
          lowpassC = (lowpassC * invLowpass) + (in * lowpassAmount); lowpassC = spiralFastSaturate(lowpassC, 1.0); in = lowpassC;
          lowpassD = (lowpassD * invLowpass) + (in * lowpassAmount); lowpassD = spiralFastSaturate(lowpassD, 1.0); in = lowpassD;
          break;
        case 4:
          lowpassA = (lowpassA * invLowpass) + (in * lowpassAmount); lowpassA = spiralFastSaturate(lowpassA, 1.0); in = lowpassA;
          lowpassB = (lowpassB * invLowpass) + (in * lowpassAmount); lowpassB = spiralFastSaturate(lowpassB, 1.0); in = lowpassB;
          lowpassE = (lowpassE * invLowpass) + (in * lowpassAmount); lowpassE = spiralFastSaturate(lowpassE, 1.0); in = lowpassE;
          break;
        case 5:
          lowpassA = (lowpassA * invLowpass) + (in * lowpassAmount); lowpassA = spiralFastSaturate(lowpassA, 1.0); in = lowpassA;
          lowpassC = (lowpassC * invLowpass) + (in * lowpassAmount); lowpassC = spiralFastSaturate(lowpassC, 1.0); in = lowpassC;
          lowpassF = (lowpassF * invLowpass) + (in * lowpassAmount); lowpassF = spiralFastSaturate(lowpassF, 1.0); in = lowpassF;
          break;
      }

      return in;
    }

  private:
    double lowpassA, lowpassB, lowpassC, lowpassD, lowpassE, lowpassF;
    double lowpassBaseAmount;
    double envState;     // envelope follower for dynamic FM depth (fast attack, ~10ms release)
    double smoothedIn;   // one-pole LP on signed input (~2kHz) — kills audio-rate scratch
    int count;
  };

  // ----- Standalone od::Object wrapper (for Lua-graph use).
  // Filament doesn't instantiate this — it uses Capacitor2Mono
  // directly. Exposed for future Lua-composed chain units that
  // want Capacitor2 as a graph node. -----
  class Capacitor2 : public od::Object
  {
  public:
    Capacitor2()
    {
      addInput(mInL);
      addInput(mInR);
      addOutput(mOutL);
      addOutput(mOutR);
      addParameter(mCutoff);
      addParameter(mFM);

      mMonoL.reset();
      mMonoR.reset();
      mPrevLowpassChase = 0.0;
    }

    virtual ~Capacitor2() {}

#ifndef SWIGLUA
    od::Inlet     mInL{"In L"};
    od::Inlet     mInR{"In R"};
    od::Outlet    mOutL{"Out L"};
    od::Outlet    mOutR{"Out R"};
    od::Parameter mCutoff{"Cutoff", 0.6f};
    od::Parameter mFM{"FM", 0.5f};

    virtual void process()
    {
      float *in1 = mInL.buffer();
      float *in2 = mInR.buffer();
      float *out1 = mOutL.buffer();
      float *out2 = mOutR.buffer();

      double cutoffKnob = (double)mCutoff.value();
      double fmKnob = (double)mFM.value();
      if (fmKnob < 0.0) fmKnob = 0.0;
      if (fmKnob > 1.0) fmKnob = 1.0;
      // FM knob inverted: high FM = strong modulation (low nonLin).
      double nonLin = 1.0 + (1.0 - fmKnob) * 6.0;
      // Envelope-boosted FM depth.
      double baseFmGain = 1.0 + fmKnob * 2.0;
      double envBoost = fmKnob * 5.0;

      Capacitor2Coefs cox;
      capacitor2BakeCoefs(cutoffKnob, nonLin, baseFmGain, envBoost,
                          mPrevLowpassChase, cox);
      mPrevLowpassChase = cox.lowpassChase;

      int sampleFrames = FRAMELENGTH;
      while (--sampleFrames >= 0)
      {
        double inL = *in1;
        double inR = *in2;
        if (fabs(inL) < 1.18e-23) inL = 1.18e-17;
        if (fabs(inR) < 1.18e-23) inR = 1.18e-17;

        *out1 = (float)(mMonoL.process(inL, cox) * cox.nonLinTrim);
        *out2 = (float)(mMonoR.process(inR, cox) * cox.nonLinTrim);
        in1++; in2++; out1++; out2++;
      }
    }

  private:
    Capacitor2Mono mMonoL;
    Capacitor2Mono mMonoR;
    double mPrevLowpassChase;
#endif
  };

} // namespace house
