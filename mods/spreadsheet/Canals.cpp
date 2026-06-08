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
    addOutput(mOutLow);     // FIXED: was missing — sub-out picker silently failed
    addOutput(mOutCentre);  // FIXED
    addOutput(mOutHigh);    // FIXED
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
        // -0.5. Self-oscillation regime sits in roughly the top 8%
        // (damping crosses zero at quality ≈ 0.917). At full CW the
        // negative damping drives self-osc to amplitudes where the
        // summed ALL output reaches the rail-clip region.
        //
        // Re-calibrated from -0.075 to -0.5 after capturing self-osc
        // internally on the ER-301 (not via MOTU input) — the original
        // calibration was based on MOTU-attenuated levels and produced
        // self-osc ~18 dB too quiet vs real hardware. See
        // planning/refs/three-sisters-hardware/internal/.
        float t = (quality - 0.9f) * 10.0f;  // 0..1 in [0.9, 1.0]
        damping = 0.02f * (1.0f - t) + (-0.5f) * t;
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

      // Resonance placement: SVF1 resonant, SVF2 Butterworth on ALL
      // three blocks. Single-resonant CENTRE matches hardware spectral
      // analysis (single peak at lowF, not dual peaks).
      //
      // FREQUENCY-COMPENSATED DAMPING: hardware self-osc amplitude is
      // frequency-flat (OTA voltage rails); our SVF self-osc would
      // scale with cutoff (g coefficient). To match the hardware
      // "even resonance across blocks" feel, scale negative damping by
      // (f_ref/f). Lower frequency → more negative damping → larger
      // limit cycle. f_ref = ctrF (the geometric mean), so CENTRE
      // gets damping unchanged.
      //
      // Calibrated against internal hardware captures: damping factor
      // (f_ref/f) brings LOW/CTR/HIGH within 2% of hardware self-osc
      // amplitudes when combined with the per-block output gain below.
      const float kButterDamp = 1.0f / 0.7071f;
      const float fRef = ctrF;
      // Guard against pathologically small frequencies (would amplify
      // damping to runaway). Clamp ratio to a reasonable maximum.
      auto compDamp = [&](float f) {
        float ratio = (f > 0.001f) ? (fRef / f) : 4.0f;
        if (ratio > 4.0f) ratio = 4.0f;  // ~2 octaves of span span
        return damping * ratio;
      };

      if (mode == 0)
      {
        // Crossover: CENTRE SVF1 at lowF (matches hardware spectral
        // peak alignment). low1, ctr1 share lowF damping; hi1 at highF.
        float dampLowF = compDamp(lowF);
        float dampHighF = compDamp(highF);
        s.low1.setFreq(lowF, dampLowF);
        s.low2.setFreq(lowF, kButterDamp);
        s.ctr1.setFreq(lowF, dampLowF);
        s.ctr2.setFreq(highF, kButterDamp);
        s.hi1.setFreq(highF, dampHighF);
        s.hi2.setFreq(highF, kButterDamp);
      }
      else
      {
        // Formant: blocks at their own FREQ. CENTRE both stages at ctrF.
        float dampLowF = compDamp(lowF);
        float dampCtrF = compDamp(ctrF);  // = damping (no compensation since fRef==ctrF)
        float dampHighF = compDamp(highF);
        s.low1.setFreq(lowF, dampLowF);
        s.low2.setFreq(lowF, kButterDamp);
        s.ctr1.setFreq(ctrF, dampCtrF);
        s.ctr2.setFreq(ctrF, kButterDamp);
        s.hi1.setFreq(highF, dampHighF);
        s.hi2.setFreq(highF, kButterDamp);
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

    // Denormal flush as self-osc seed source. Without this, a perfectly
    // silent input + zero integrator states stays at exact zero forever
    // (negative damping has nothing to amplify). Replacing sub-denormal
    // values with a small fixed seed gives the resonant stages perpetual
    // noise to bootstrap into self-oscillation. Matches the AW house-
    // atom pattern. Seed level (1.18e-17) is ~-340 dBFS — inaudible
    // but well above any FTZ threshold.
    {
      float *src = in;
      for (int i = 0; i < FRAMELENGTH; i++)
      {
        if (fabsf(src[i]) < 1.18e-23f) src[i] = 1.18e-17f;
      }
    }

    // Per-sample SVF processing + topology-correct anti-resonance.
    //
    // Per-block POST-GAIN compensation: LOW and HIGH go through dual-LP
    // and dual-HP cascades respectively, both attenuating ~6 dB at fc.
    // CENTRE's HP→LP cascade attenuates only ~3 dB. To match hardware's
    // "even resonance across blocks" feel, LOW and HIGH outputs are
    // boosted to compensate. Calibrated against internal hardware
    // captures (LOW×2.0, HIGH×1.8 brings all three within 2% of
    // measured self-osc amplitudes).
    const float kLowGain = 2.0f;
    const float kHighGain = 1.8f;

    for (int i = 0; i < FRAMELENGTH; i++)
    {
      float x = in[i];

      if (mode == 0)
      {
        // Crossover: LOW = SVF1.lp→SVF2.lp; CENTRE = SVF1.hp→SVF2.lp;
        // HIGH = SVF1.hp→SVF2.hp.
        auto lo1 = s.low1.process(x);
        auto lo2 = s.low2.process(lo1.lp);
        lowOut[i] = (lo2.lp + antiRes * lo1.hp) * kLowGain;

        auto ct1 = s.ctr1.process(x);
        auto ct2 = s.ctr2.process(ct1.hp);
        ctrOut[i] = ct2.lp + antiRes * (ct1.lp + ct2.hp);

        auto hi1 = s.hi1.process(x);
        auto hi2 = s.hi2.process(hi1.hp);
        hiOut[i] = (hi2.hp + antiRes * hi1.lp) * kHighGain;
      }
      else
      {
        // Formant: per Issue #8 fix on HIGH stage order.
        auto lo1 = s.low1.process(x);
        auto lo2 = s.low2.process(lo1.lp);
        lowOut[i] = (lo2.hp + antiRes * lo1.hp) * kLowGain;

        auto ct1 = s.ctr1.process(x);
        auto ct2 = s.ctr2.process(ct1.hp);
        ctrOut[i] = ct2.lp + antiRes * (ct1.lp + ct2.hp);

        auto hi1 = s.hi1.process(x);
        auto hi2 = s.hi2.process(hi1.hp);
        hiOut[i] = (hi2.lp + antiRes * hi1.lp) * kHighGain;
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
      // Per-block taps always live (multi-output sub-outs 3-5).
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
      float mix = vL * wL + vC * wC + vH * wH;
      out[i] = SistersSvf::fastTanh(mix);
    }
  }

} // namespace stolmine
