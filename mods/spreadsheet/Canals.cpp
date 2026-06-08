// Canals - linked resonant filter (Three Sisters inspired)
// Custom SVF core with tanh-saturating integrators

#include "Canals.h"
#include "SistersSvf.h"
#include <od/config.h>
#include <hal/ops.h>
#include <string.h>

#include "stmlib/dsp/units.h"

namespace stolmine
{

  static const int kMaxFrameLength = 256;

  struct Canals::Internal
  {
    SistersSvf low1, low2;
    SistersSvf ctr1, ctr2;
    SistersSvf hi1, hi2;

    float lowOut[kMaxFrameLength];
    float ctrOut[kMaxFrameLength];
    float hiOut[kMaxFrameLength];

    float prevFundamental;
    float prevSpan;
    float prevQuality;
    float prevVoct;
    int prevMode;

    void Init()
    {
      low1.reset(); low2.reset();
      ctr1.reset(); ctr2.reset();
      hi1.reset();  hi2.reset();
      prevFundamental = -999.0f;
      prevSpan = -1.0f;
      prevQuality = -999.0f;
      prevVoct = -999.0f;
      prevMode = -1;
    }
  };

  Canals::Canals()
  {
    addInput(mIn);
    addInput(mVOct);
    addOutput(mOut);
    addParameter(mFundamental);
    addParameter(mSpan);
    addParameter(mQuality);
    addParameter(mOutput);
    addParameter(mMode);

    mpInternal = new Internal();
    mpInternal->Init();
  }

  Canals::~Canals()
  {
    delete mpInternal;
  }

  static inline float clampNorm(float hz)
  {
    float f = hz / 48000.0f;
    if (f < 0.001f) f = 0.001f;
    if (f > 0.499f) f = 0.499f;
    return f;
  }

  void Canals::process()
  {
    Internal &s = *mpInternal;

    float *in = mIn.buffer();
    float *voct = mVOct.buffer();
    float *out = mOut.buffer();
    float *outLow = mOutLow.buffer();
    float *outCentre = mOutCentre.buffer();
    float *outHigh = mOutHigh.buffer();

    float fundamental = mFundamental.value();
    float span = CLAMP(0.0f, 1.0f, mSpan.value());
    float quality = CLAMP(-1.0f, 1.0f, mQuality.value());
    float outputPos = CLAMP(0.0f, 3.0f, mOutput.value());
    int mode = CLAMP(0, 1, (int)(mMode.value() + 0.5f));

    float v = voct[0];

    // Reconfigure SVFs when parameters change
    if (fundamental != s.prevFundamental || span != s.prevSpan ||
        quality != s.prevQuality || mode != s.prevMode ||
        (v > s.prevVoct + 0.001f || v < s.prevVoct - 0.001f))
    {
      // Quality knob → damping (k = 1/Q). Two regimes:
      //
      //   quality ∈ [0.0, 0.9]: cubic ramp from Butterworth (q≈0.7)
      //   up to strong resonance (q≈50). Damping stays positive.
      //
      //   quality ∈ [0.9, 1.0]: linearly cross damping through zero
      //   into slight negative (-0.0008). At negative damping the
      //   filter self-oscillates; the in-loop tanh on s1 bounds the
      //   growth into a stable limit cycle (Issue #3). The crossover
      //   at quality=0.9 corresponds to ~3 o'clock on the knob,
      //   matching the hardware where Q ≈ 14-20 lives.
      //
      // Anti-resonance (quality < 0) handled via antiRes scalar below;
      // main q stays at Butterworth for the resonance path.
      float damping;
      if (quality < 0.0f)
      {
        damping = 1.0f / 0.7071f;  // Butterworth (anti-res does the spectral shaping)
      }
      else if (quality < 0.9f)
      {
        float t = quality * (1.0f / 0.9f);  // normalize to [0, 1]
        float qMag = 0.7071f + t * t * t * 49.3f;  // q ∈ [0.7071, 50]
        damping = 1.0f / qMag;
      }
      else
      {
        // Top decile: damping ramps from r≈0.02 (q≈50) through 0 into
        // -0.075. Self-oscillation regime sits in roughly the top 8%
        // (damping crosses zero at quality ≈ 0.917). At full CW the
        // negative damping is matched to hardware-observed limit-cycle
        // amplitude (~0.22 peak on CENTRE at noon FREQ, ≈ -13 dBFS) —
        // see planning/canals-phase0c-findings + canals_model.py
        // damping sweep that calibrated this value.
        float t = (quality - 0.9f) * 10.0f;  // 0..1 in [0.9, 1.0]
        damping = 0.02f * (1.0f - t) + (-0.075f) * t;
      }

      float totalSemitones = v * 120.0f + fundamental;
      float freqHz = 261.63f * stmlib::SemitonesToRatio(totalSemitones);
      freqHz = CLAMP(20.0f, 20000.0f, freqHz);

      float spanSemitones = span * 48.0f;
      float lowHz = CLAMP(20.0f, 20000.0f,
                          freqHz * stmlib::SemitonesToRatio(-spanSemitones));
      float highHz = CLAMP(20.0f, 20000.0f,
                           freqHz * stmlib::SemitonesToRatio(+spanSemitones));

      float lowF = clampNorm(lowHz);
      float highF = clampNorm(highHz);
      float ctrF = clampNorm(freqHz);

      // Issue #2 (Q placement): real Three Sisters has resonance on
      // SVF1 only for LOW/HIGH; SVF2 is a fixed non-resonant Butterworth
      // 2-pole. CENTRE keeps Q on BOTH stages (dual-resonance — the
      // defining behavior of CENTRE).
      //
      // Note: using setFreq(freq, damping) directly here rather than
      // setFreqQ so the resonant stages can receive negative damping
      // in the top-decile self-oscillation regime.
      const float kButterDamp = 1.0f / 0.7071f;  // ~1.414 (Butterworth)
      if (mode == 0)
      {
        // Crossover: LOW at lowF, CENTRE spans lowF->highF, HIGH at highF
        s.low1.setFreq(lowF, damping);
        s.low2.setFreq(lowF, kButterDamp);   // FIXED: was q
        s.ctr1.setFreq(lowF, damping);
        s.ctr2.setFreq(highF, damping);       // CENTRE dual-resonance stays
        s.hi1.setFreq(highF, damping);
        s.hi2.setFreq(highF, kButterDamp);   // FIXED: was q
      }
      else
      {
        // Formant: each block converges to its own FREQ
        s.low1.setFreq(lowF, damping);
        s.low2.setFreq(lowF, kButterDamp);   // FIXED
        s.ctr1.setFreq(ctrF, damping);
        s.ctr2.setFreq(ctrF, damping);        // CENTRE dual-resonance stays
        s.hi1.setFreq(highF, damping);
        s.hi2.setFreq(highF, kButterDamp);   // FIXED
      }

      s.prevFundamental = fundamental;
      s.prevSpan = span;
      s.prevQuality = quality;
      s.prevVoct = v;
      s.prevMode = mode;
    }

    float antiRes = (quality < 0.0f) ? -quality : 0.0f;

    float *lowOut = s.lowOut;
    float *ctrOut = s.ctrOut;
    float *hiOut = s.hiOut;

    // Per-sample SVF processing + topology-correct anti-resonance.
    //
    // Issue #5 fix: anti-res taps the genuine complementary SVF output
    // (LOW = SVF1.hp, HIGH = SVF1.lp, CENTRE = SVF1.lp + SVF2.hp) and
    // mixes additively against the main output. The old generic
    // dry-minus-out approximation produced wrong notch phase + depth.
    //
    // Issue #8 fix (FORMANT HIGH): SVF1 stays HP-first (was LP→HP);
    // SVF2 then takes LP for the formant output. Mirrors real Three
    // Sisters HP-first cascade convention.
    for (int i = 0; i < FRAMELENGTH; i++)
    {
      float x = in[i];

      if (mode == 0)
      {
        // Crossover: LOW = SVF1.lp→SVF2.lp; CENTRE = SVF1.hp→SVF2.lp;
        // HIGH = SVF1.hp→SVF2.hp.
        auto lo1 = s.low1.process(x);
        auto lo2 = s.low2.process(lo1.lp);
        lowOut[i] = lo2.lp + antiRes * lo1.hp;

        auto ct1 = s.ctr1.process(x);
        auto ct2 = s.ctr2.process(ct1.hp);
        ctrOut[i] = ct2.lp + antiRes * (ct1.lp + ct2.hp);

        auto hi1 = s.hi1.process(x);
        auto hi2 = s.hi2.process(hi1.hp);
        hiOut[i] = hi2.hp + antiRes * hi1.lp;
      }
      else
      {
        // Formant: LOW = SVF1.lp→SVF2.lp then take hp for the upper-
        // formant slope (kept same as biome — Issue #8 only flagged
        // HIGH formant); CENTRE same routing as XOVER; HIGH = SVF1.hp
        // (FIXED) → SVF2.lp.
        auto lo1 = s.low1.process(x);
        auto lo2 = s.low2.process(lo1.lp);
        lowOut[i] = lo2.hp + antiRes * lo1.hp;

        auto ct1 = s.ctr1.process(x);
        auto ct2 = s.ctr2.process(ct1.hp);
        ctrOut[i] = ct2.lp + antiRes * (ct1.lp + ct2.hp);

        auto hi1 = s.hi1.process(x);
        auto hi2 = s.hi2.process(hi1.hp);   // FIXED: was hi1.lp
        hiOut[i] = hi2.lp + antiRes * hi1.lp;
      }
    }

    // Output crossfade for Out 1 (chain auto-wire): LOW → CENTRE →
    // HIGH → ALL. Out 2/3/4 always carry the unmixed per-block taps
    // (multi-output).
    //
    // pos ∈ [2, 3] morphs HIGH (alone) → full ALL (= LOW+CENTRE+HIGH
    // unweighted sum, the hardware-accurate rail-sum). At pos=3 the
    // sum can reach 3× per-block peak; we apply fastTanh on the
    // mixed output to emulate the hardware ALL output's rail-clip
    // distortion character (the model-author's "rail-sum, not core"
    // note in three_sisters_svf.c).
    float pos = outputPos;
    float wL, wC, wH;
    if (pos <= 1.0f)
    {
      wL = 1.0f - pos;
      wC = pos;
      wH = 0.0f;
    }
    else if (pos <= 2.0f)
    {
      wL = 0.0f;
      wC = 2.0f - pos;
      wH = pos - 1.0f;
    }
    else
    {
      // HIGH stays at unity; LOW+CENTRE fade in to reach full ALL sum.
      float t = pos - 2.0f;
      wL = t;
      wC = t;
      wH = 1.0f;
    }

    for (int i = 0; i < FRAMELENGTH; i++)
    {
      // Per-block taps always live (multi-output sub-outs 2-4).
      // Clamp NaN/extreme values defensively.
      float vL = lowOut[i];
      float vC = ctrOut[i];
      float vH = hiOut[i];
      if (vL != vL || vL > 10.0f || vL < -10.0f) vL = 0.0f;
      if (vC != vC || vC > 10.0f || vC < -10.0f) vC = 0.0f;
      if (vH != vH || vH > 10.0f || vH < -10.0f) vH = 0.0f;
      outLow[i] = vL;
      outCentre[i] = vC;
      outHigh[i] = vH;
      // Sub-out 1: fader-morphed sum, soft-clipped via fastTanh
      // (rail-sum saturation emulating hardware ALL output character).
      // At low fader positions (single-block content) the input is
      // bounded ≤1 and tanh is near-transparent. At pos=3 (3× sum)
      // tanh asymptotes near ±1.73, producing the characteristic
      // rail distortion.
      float mix = vL * wL + vC * wC + vH * wH;
      out[i] = SistersSvf::fastTanh(mix);
    }
  }

} // namespace stolmine
