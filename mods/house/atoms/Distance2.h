// house::Distance2
//
// Component-only atom (no od::Object, no Lua unit, no toc entry)
// per feedback_atoms_as_components. Helper class encapsulating the
// AW Distance2 air-absorption + slew-cascade math (Chris Johnson,
// MIT). Ported from ~/repos/airwindows/plugins/MacVST/Distance2/
// source/Distance2Proc.cpp.
//
// AW Distance2 identity: cascade of slew clampers with
// progressively-slacker thresholds (golden-ratio spaced), plus a
// pre-cascade "Loud-style" first-derivative offset, plus a
// post-cascade 2-tap IIR for the far-distance squish. The result
// is a level-dependent air-absorption + small-delay feel — drums
// "go huge" without an actual reverb.
//
// AW uses 13 stages (thresholds A..M). Carriage uses 7 stages
// (A..G) — enough to evoke distance without the heaviest CPU
// cost or the deepest darkening. The choice was design-time:
// Carriage's air axis is a counterweight to dynamics, not a
// maximum-distance shaper. Promote stage count if the character
// audition demands it.
//
// Per side state: 7 cascade samples (lastSampleA..G) + 2 IIR
// samples (lastSample, thirdSample) = 9 doubles. Coefficients
// baked block-rate via distance2BakeCoefs().
//
// Used by house::Carriage as the orthogonal-tone element of the
// dynamics chain, modulated by engagement so the signal feels
// MORE distant when flat material is being aggressively
// reshaped, LESS distant when natural dynamics are riding through.

#pragma once

#include <math.h>
#include <string.h>

#ifndef SWIGLUA
namespace house
{

  static constexpr int kDist2Stages = 7;

  // Golden-ratio-spaced thresholds (AW A..G; M was the 13th).
  // Each successive stage is ~1.1× slacker than the last, giving
  // progressively-larger allowable Δ as the cascade deepens.
  static constexpr double kDist2BaseThresholds[kDist2Stages] = {
    0.618033988749894,   // A
    0.679837387624884,   // B
    0.747821126387373,   // C
    0.822603239026110,   // D
    0.904863562928721,   // E
    0.995349919221593,   // F
    1.094884911143752    // G
  };

  // Block-rate baked coefficients.
  struct Distance2Coefs
  {
    double thresholds[kDist2Stages];  // base × softslew⁻¹
    double softslew;
    double offsetScale;
    double secondfilter;              // IIR feedback coef
    double thirdfilter;               // IIR feedback coef
    double oneOverSecondfilterPlusOne;
    double oneOverThirdfilterPlusOne;
    double levelcorrect;
    double oneOverSoftslew;
    double wet;
  };

  // aKnob: Distance amount [0, 1]. AW cubic curve.
  // bKnob: Filter darken [0, 1]. Fixed by caller for Carriage.
  // wetKnob: Wet level [0, 1].
  // sampleRate: host sample rate.
  static inline void distance2BakeCoefs(double aKnob, double bKnob,
                                        double wetKnob, double sampleRate,
                                        Distance2Coefs &c)
  {
    double overallscale = sampleRate / 44100.0;
    double aCubed = aKnob * aKnob * aKnob;
    c.softslew = (aCubed * 24.0 + 0.6) * overallscale;
    double filter = c.softslew * bKnob;
    c.secondfilter = filter * (1.0 / 3.0);
    c.thirdfilter = filter * 0.2;
    c.offsetScale = aKnob * 0.1618;
    c.levelcorrect = 1.0 + (filter * (1.0 / 12.0)) * aKnob;
    c.wet = wetKnob;
    if (c.wet < 0.0) c.wet = 0.0;
    if (c.wet > 1.0) c.wet = 1.0;

    // Cascade thresholds: AW divides each by overallscale
    // (so threshold scales DOWN as SR rises). We additionally
    // skip multiplying by softslew because AW pre-scales the
    // sample by softslew BEFORE clamping; we'll keep that step
    // verbatim and use the raw thresholds.
    double invOverall = 1.0 / overallscale;
    for (int i = 0; i < kDist2Stages; i++) {
      c.thresholds[i] = kDist2BaseThresholds[i] * invOverall;
    }

    c.oneOverSoftslew = 1.0 / c.softslew;
    c.oneOverSecondfilterPlusOne = 1.0 / (c.secondfilter + 1.0);
    c.oneOverThirdfilterPlusOne = 1.0 / (c.thirdfilter + 1.0);
  }

  class Distance2Mono
  {
  public:
    Distance2Mono() { reset(); }

    void reset()
    {
      memset(mCascade, 0, sizeof(mCascade));
      mLastSample = 0.0;
      mThirdSample = 0.0;
    }

    // Per-sample air absorption. Returns wet-mixed sample.
    // wetOverride: if ≥ 0, used instead of c.wet (Carriage drives
    // this from engagement × airKnob per sample without needing
    // to rebake the full coefs struct). Pass -1 to use c.wet.
    inline double process(double inputSample, const Distance2Coefs &c,
                          double wetOverride = -1.0)
    {
      double wet = (wetOverride >= 0.0) ? wetOverride : c.wet;
      double drySample = inputSample;

      // (a) "Loud-style" offset air compression — signal-dependent
      // DC offset proportional to first derivative.
      double offset = c.offsetScale - (mLastSample - inputSample);
      inputSample += offset * c.offsetScale;

      // (b) Scale into clamp domain (×wet first to control depth,
      // then ×softslew to scale into the threshold range).
      inputSample *= wet;
      inputSample *= c.softslew;

      // (c) 7-stage slew cascade. Each stage clamps Δ against its
      // own threshold; deeper stages allow more Δ.
      for (int i = 0; i < kDist2Stages; i++) {
        double clamp = inputSample - mCascade[i];
        if (clamp > c.thresholds[i]) inputSample = mCascade[i] + c.thresholds[i];
        else if (-clamp > c.thresholds[i]) inputSample = mCascade[i] - c.thresholds[i];
      }

      // (d) Shift cascade memory upward (G←F, ..., B←A, A←drySample).
      // Storing the RAW dry input at the head, as AW does.
      for (int i = kDist2Stages - 1; i > 0; i--) {
        mCascade[i] = mCascade[i - 1];
      }
      mCascade[0] = drySample;

      // (e) Scale back out + remove offset.
      inputSample *= c.levelcorrect;
      inputSample *= c.oneOverSoftslew;
      inputSample -= offset * c.offsetScale;

      // (f) 2-tap IIR "for superdistant stuff" — AW pattern, kept verbatim.
      inputSample = (inputSample + mThirdSample * c.thirdfilter) * c.oneOverThirdfilterPlusOne;
      inputSample = (inputSample + mLastSample * c.secondfilter) * c.oneOverSecondfilterPlusOne;
      mThirdSample = mLastSample;
      mLastSample = inputSample;

      // (g) Re-apply levelcorrect.
      inputSample *= c.levelcorrect;

      // (h) Wet/dry mix. AW does this with branch on wet != 1.0; we
      // skip the branch and always mix (cheap at wet=1.0 — passes
      // inputSample through exactly).
      double outSample = inputSample * wet + drySample * (1.0 - wet);
      return outSample;
    }

  private:
    double mCascade[kDist2Stages];  // lastSampleA..G
    double mLastSample;             // IIR memory
    double mThirdSample;            // IIR pre-pre memory
  };

} // namespace house
#endif // !SWIGLUA
