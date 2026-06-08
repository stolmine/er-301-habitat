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

  struct Canals::Internal
  {
    SistersSvf low1, low2;
    SistersSvf ctr1, ctr2;
    SistersSvf hi1, hi2;

    void Init()
    {
      low1.reset(); low2.reset();
      ctr1.reset(); ctr2.reset();
      hi1.reset();  hi2.reset();
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
    // PHASE 1 — Per-sample audio-rate parameter modulation.
    //
    // The previous frame-rate change-detection-gated coefficient bake
    // capped modulation bandwidth at framerate/2 (~375 Hz at 64-sample
    // frames) — fatal for audio-rate FM and self-patching scenarios.
    //
    // This version reads V/Oct per-sample and recomputes SVF
    // coefficients per-sample. Block-rate work stays outside the
    // inner loop; per-sample work happens once per audio frame.
    //
    // Matches the ER-301 LadderFilter convention:
    // mods/core/objects/filters/LadderFilter.cpp. See
    // planning/canals-audio-rate-mod.md for the design.

    Internal &s = *mpInternal;

    float *in = mIn.buffer();
    float *voct = mVOct.buffer();
    float *out = mOut.buffer();
    float *outLow = mOutLow.buffer();
    float *outCentre = mOutCentre.buffer();
    float *outHigh = mOutHigh.buffer();

    // === Block-rate parameter sampling ===
    // These values come from ParameterAdapters which are control-rate
    // by design. No bandwidth gained by reading them per-sample.
    float fundamental = mFundamental.value();
    float span = CLAMP(0.0f, 1.0f, mSpan.value());
    float quality = CLAMP(-1.0f, 1.0f, mQuality.value());
    float outputPos = CLAMP(0.0f, 3.0f, mOutput.value());
    int mode = CLAMP(0, 1, (int)(mMode.value() + 0.5f));

    // === Block-rate derived constants ===

    // Quality knob → damping (k = 1/Q):
    //   quality < 0:    Butterworth (anti-res handled separately)
    //   quality < 0.9:  cubic ramp Butter → q≈50 (still positive damping)
    //   quality ≥ 0.9:  cross zero into -0.5 (top decile = self-osc edge)
    float damping;
    if (quality < 0.0f)
    {
      damping = 1.0f / 0.7071f;
    }
    else if (quality < 0.9f)
    {
      float t = quality * (1.0f / 0.9f);
      float qMag = 0.7071f + t * t * t * 49.3f;
      damping = 1.0f / qMag;
    }
    else
    {
      float t = (quality - 0.9f) * 10.0f;
      damping = 0.02f * (1.0f - t) + (-0.5f) * t;
    }

    float antiRes = (quality < 0.0f) ? -quality : 0.0f;

    // Span multiplier (precomputed; per-sample lowHz/highHz are then
    // freqHz × invSpanMult and freqHz × spanMult, no per-sample
    // SemitonesToRatio for span).
    float spanSemitones = span * 48.0f;
    float spanMult = stmlib::SemitonesToRatio(spanSemitones);
    float invSpanMult = 1.0f / spanMult;

    // Fader weights for Out 1 morph (block-rate; outputPos doesn't
    // change per-sample at this iteration — could be made per-sample
    // later for fader-mod use cases).
    float pos = outputPos;
    float wL, wC, wH;
    if (pos <= 1.0f)
    {
      wL = 1.0f - pos; wC = pos; wH = 0.0f;
    }
    else if (pos <= 2.0f)
    {
      wL = 0.0f; wC = 2.0f - pos; wH = pos - 1.0f;
    }
    else
    {
      float t = pos - 2.0f;
      wL = t; wC = t; wH = 1.0f;
    }

    const float kButterDamp = 1.0f / 0.7071f;
    const float kLowGain = 2.0f;
    const float kHighGain = 1.8f;
    const float kInvSampleRate = 1.0f / 48000.0f;

    // === Per-sample loop ===
    // Everything inside this loop is per-sample. Includes:
    //   - V/Oct read → cutoff derivation
    //   - Frequency-compensated damping calculation
    //   - setFreq() on all 6 SVFs
    //   - SVF processing
    //   - NaN clamp + per-block output writes
    //   - Fader mix → tanh → main Out write
    for (int i = 0; i < FRAMELENGTH; i++)
    {
      // Denormal flush — perpetual noise floor for self-osc bootstrap.
      float x = in[i];
      if (fabsf(x) < 1.18e-23f) x = 1.18e-17f;

      // ---- Per-sample cutoff derivation ----
      float v = voct[i];                       // ← was voct[0]; now per-sample
      float totalSemis = v * 120.0f + fundamental;
      float freqHz = 261.63f * stmlib::SemitonesToRatio(totalSemis);
      if (freqHz < 20.0f) freqHz = 20.0f;
      if (freqHz > 20000.0f) freqHz = 20000.0f;

      float lowHz = freqHz * invSpanMult;
      float highHz = freqHz * spanMult;
      if (lowHz < 20.0f) lowHz = 20.0f;
      if (highHz > 20000.0f) highHz = 20000.0f;

      float lowF = lowHz * kInvSampleRate;
      if (lowF < 0.001f) lowF = 0.001f;
      if (lowF > 0.499f) lowF = 0.499f;
      float ctrF = freqHz * kInvSampleRate;
      if (ctrF < 0.001f) ctrF = 0.001f;
      if (ctrF > 0.499f) ctrF = 0.499f;
      float highF = highHz * kInvSampleRate;
      if (highF < 0.001f) highF = 0.001f;
      if (highF > 0.499f) highF = 0.499f;

      // ---- Frequency-compensated damping per resonant stage ----
      // damping × (ctrF / f_stage), clamped at 4× to prevent runaway
      // at extreme SPAN. CENTRE (at ctrF) gets no compensation.
      float ratioLow = ctrF / lowF;
      if (ratioLow > 4.0f) ratioLow = 4.0f;
      float ratioHigh = ctrF / highF;
      if (ratioHigh > 4.0f) ratioHigh = 4.0f;
      float dampLowF = damping * ratioLow;
      float dampHighF = damping * ratioHigh;

      // ---- Configure SVFs (per-sample) + process audio ----
      SistersSvf::Output lo1, lo2, ct1, ct2, hi1, hi2;
      float vL_raw, vC_raw, vH_raw;
      if (mode == 0)
      {
        // Crossover: CENTRE SVF1 at lowF (single peak at lowF
        // matches hardware spectral character).
        s.low1.setFreq(lowF, dampLowF);
        s.low2.setFreq(lowF, kButterDamp);
        s.ctr1.setFreq(lowF, dampLowF);
        s.ctr2.setFreq(highF, kButterDamp);
        s.hi1.setFreq(highF, dampHighF);
        s.hi2.setFreq(highF, kButterDamp);

        lo1 = s.low1.process(x);
        lo2 = s.low2.process(lo1.lp);
        ct1 = s.ctr1.process(x);
        ct2 = s.ctr2.process(ct1.hp);
        hi1 = s.hi1.process(x);
        hi2 = s.hi2.process(hi1.hp);

        vL_raw = (lo2.lp + antiRes * lo1.hp) * kLowGain;
        vC_raw = ct2.lp + antiRes * (ct1.lp + ct2.hp);
        vH_raw = (hi2.hp + antiRes * hi1.lp) * kHighGain;
      }
      else
      {
        // Formant: blocks converge to their own FREQ.
        // CENTRE both stages at ctrF (ctrF/ctrF = 1, no comp).
        s.low1.setFreq(lowF, dampLowF);
        s.low2.setFreq(lowF, kButterDamp);
        s.ctr1.setFreq(ctrF, damping);
        s.ctr2.setFreq(ctrF, kButterDamp);
        s.hi1.setFreq(highF, dampHighF);
        s.hi2.setFreq(highF, kButterDamp);

        lo1 = s.low1.process(x);
        lo2 = s.low2.process(lo1.lp);
        ct1 = s.ctr1.process(x);
        ct2 = s.ctr2.process(ct1.hp);
        hi1 = s.hi1.process(x);
        hi2 = s.hi2.process(hi1.hp);

        vL_raw = (lo2.hp + antiRes * lo1.hp) * kLowGain;
        vC_raw = ct2.lp + antiRes * (ct1.lp + ct2.hp);
        vH_raw = (hi2.lp + antiRes * hi1.lp) * kHighGain;
      }

      // ---- NaN clamp + per-block writes ----
      float vL = vL_raw, vC = vC_raw, vH = vH_raw;
      if (vL != vL || vL > 10.0f || vL < -10.0f) vL = 0.0f;
      if (vC != vC || vC > 10.0f || vC < -10.0f) vC = 0.0f;
      if (vH != vH || vH > 10.0f || vH < -10.0f) vH = 0.0f;
      outLow[i] = vL;
      outCentre[i] = vC;
      outHigh[i] = vH;

      // ---- Out 1: fader mix → fastTanh (rail-clip emulation) ----
      float mix = vL * wL + vC * wC + vH * wH;
      out[i] = SistersSvf::fastTanh(mix);
    }
  }

} // namespace stolmine
