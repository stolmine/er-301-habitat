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
      // Q mapping: 0.5 at quality=0, up to 100 at quality=1
      // Cubic curve for gentle low end, aggressive high end
      float q = 0.5f;
      if (quality >= 0.0f)
      {
        float t = quality * quality * quality;
        q = 0.5f + t * 99.5f;
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
      const float kButterQ = 0.7071f;
      if (mode == 0)
      {
        // Crossover: LOW at lowF, CENTRE spans lowF->highF, HIGH at highF
        s.low1.setFreqQ(lowF, q);
        s.low2.setFreqQ(lowF, kButterQ);   // FIXED: was q
        s.ctr1.setFreqQ(lowF, q);
        s.ctr2.setFreqQ(highF, q);          // CENTRE dual-resonance stays
        s.hi1.setFreqQ(highF, q);
        s.hi2.setFreqQ(highF, kButterQ);    // FIXED: was q
      }
      else
      {
        // Formant: each block converges to its own FREQ
        s.low1.setFreqQ(lowF, q);
        s.low2.setFreqQ(lowF, kButterQ);    // FIXED
        s.ctr1.setFreqQ(ctrF, q);
        s.ctr2.setFreqQ(ctrF, q);            // CENTRE dual-resonance stays
        s.hi1.setFreqQ(highF, q);
        s.hi2.setFreqQ(highF, kButterQ);    // FIXED
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

    // Output crossfade for Out 1 (chain auto-wire): LOW -> CENTRE -> HIGH -> ALL.
    // Out 2/3/4 always carry the unmixed per-block taps (multi-output).
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
      float t = pos - 2.0f;
      wL = t * 0.333f;
      wC = t * 0.333f;
      wH = (1.0f - t) + t * 0.333f;
    }

    for (int i = 0; i < FRAMELENGTH; i++)
    {
      // Per-block taps always live (multi-output sub-outs 2-4).
      // Clamp NaN/extreme values defensively (the existing biome
      // pattern; keeps faulty math from leaking out).
      float vL = lowOut[i];
      float vC = ctrOut[i];
      float vH = hiOut[i];
      if (vL != vL || vL > 10.0f || vL < -10.0f) vL = 0.0f;
      if (vC != vC || vC > 10.0f || vC < -10.0f) vC = 0.0f;
      if (vH != vH || vH > 10.0f || vH < -10.0f) vH = 0.0f;
      outLow[i] = vL;
      outCentre[i] = vC;
      outHigh[i] = vH;
      // Sub-out 1: fader-selected morph (auto-wire to chain).
      out[i] = vL * wL + vC * wC + vH * wH;
    }
  }

} // namespace stolmine
