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

    // 2× oversampling state: last V/Oct and input sample from the
    // previous frame, used to linear-interpolate the midpoint sample
    // for the upper internal step.
    float lastVoct;
    float lastIn;

    void Init()
    {
      low1.reset(); low2.reset();
      ctr1.reset(); ctr2.reset();
      hi1.reset();  hi2.reset();
      lastVoct = 0.0f;
      lastIn = 0.0f;
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
    // PHASE 2 — 2× oversampling for FM-clean cutoff modulation.
    //
    // Operates internally at 96 kHz (assuming host rate 48 kHz):
    // for each output sample we run TWO internal SVF steps with
    // separate cutoffs interpolated from V/Oct, then decimate
    // (average pair) back to the output buffer. Doubles cutoff
    // sideband and tanh-harmonic headroom before Nyquist folding —
    // cures the gurgling artifacts that frame-rate-recompute and
    // single-rate per-sample modes both produced at high FM rates.
    //
    // Per planning/canals-audio-rate-mod.md.

    Internal &s = *mpInternal;

    float *in = mIn.buffer();
    float *voct = mVOct.buffer();
    float *out = mOut.buffer();
    float *outLow = mOutLow.buffer();
    float *outCentre = mOutCentre.buffer();
    float *outHigh = mOutHigh.buffer();

    // === Block-rate parameter sampling ===
    float fundamental = mFundamental.value();
    float span = CLAMP(0.0f, 1.0f, mSpan.value());
    float quality = CLAMP(-1.0f, 1.0f, mQuality.value());
    float outputPos = CLAMP(0.0f, 3.0f, mOutput.value());
    int mode = CLAMP(0, 1, (int)(mMode.value() + 0.5f));

    // === Block-rate derived constants ===
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
      // Top decile: damping ramps from r≈0.02 (q≈50) through 0 into
      // -0.15. Calibrated for the pseudoSaturate curve (sharper knee,
      // larger limit cycles at given damping) — produces CTR self-osc
      // amplitude ~1.12 matching hardware while keeping 3rd harmonic
      // at -33 dB (vs hardware -31 dB).
      float t = (quality - 0.9f) * 10.0f;
      damping = 0.02f * (1.0f - t) + (-0.15f) * t;
    }

    float antiRes = (quality < 0.0f) ? -quality : 0.0f;

    float spanSemitones = span * 48.0f;
    float spanMult = stmlib::SemitonesToRatio(spanSemitones);
    float invSpanMult = 1.0f / spanMult;

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
    // Post-gain calibrated for the pseudoSaturate in-loop curve.
    // Pseudo's larger asymptote (K=2.5) and sharper knee produce
    // larger raw limit cycles than the prior Padé K=1.5 curve —
    // post-gain is now ATTENUATING (was amplifying) to land each
    // per-block output at hardware-measured amplitudes (LOW=0.68,
    // CTR=1.12, HIGH=0.65).
    const float kLowGain = 0.45f;
    const float kHighGain = 0.48f;
    // Internal rate is 96 kHz (= 2× host rate). All SVF cutoffs are
    // normalized against this rate, so g = tan(π·f/96k).
    const float kInvSR_OS = 1.0f / 96000.0f;

    // Linear-interpolation upsample state — carry the last V/Oct
    // and input sample from the prior frame so the first output
    // sample can interpolate a midpoint between them.
    float prevV = s.lastVoct;
    float prevX = s.lastIn;

    // Internal-step helper: takes (v, x) at the internal sample
    // time, configures all 6 SVFs, processes, returns raw vL/vC/vH
    // pre-NaN-clamp. Run twice per output sample (midpoint + current).
    auto innerStep = [&](float v, float x,
                         float &vL, float &vC, float &vH)
    {
      // Cutoff derivation at this internal sample time
      float totalSemis = v * 120.0f + fundamental;
      float freqHz = 261.63f * stmlib::SemitonesToRatio(totalSemis);
      if (freqHz < 20.0f) freqHz = 20.0f;
      if (freqHz > 20000.0f) freqHz = 20000.0f;
      float lowHz = freqHz * invSpanMult;
      float highHz = freqHz * spanMult;
      if (lowHz < 20.0f) lowHz = 20.0f;
      if (highHz > 20000.0f) highHz = 20000.0f;

      float lowF = lowHz * kInvSR_OS;
      if (lowF < 0.001f) lowF = 0.001f;
      if (lowF > 0.499f) lowF = 0.499f;
      float ctrF = freqHz * kInvSR_OS;
      if (ctrF < 0.001f) ctrF = 0.001f;
      if (ctrF > 0.499f) ctrF = 0.499f;
      float highF = highHz * kInvSR_OS;
      if (highF < 0.001f) highF = 0.001f;
      if (highF > 0.499f) highF = 0.499f;

      // Frequency-compensated damping
      float ratioLow = ctrF / lowF;
      if (ratioLow > 4.0f) ratioLow = 4.0f;
      float ratioHigh = ctrF / highF;
      if (ratioHigh > 4.0f) ratioHigh = 4.0f;
      float dampLowF = damping * ratioLow;
      float dampHighF = damping * ratioHigh;

      SistersSvf::Output lo1, lo2, ct1, ct2, hi1, hi2;
      if (mode == 0)
      {
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

        vL = (lo2.lp + antiRes * lo1.hp) * kLowGain;
        vC = ct2.lp + antiRes * (ct1.lp + ct2.hp);
        vH = (hi2.hp + antiRes * hi1.lp) * kHighGain;
      }
      else
      {
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

        vL = (lo2.hp + antiRes * lo1.hp) * kLowGain;
        vC = ct2.lp + antiRes * (ct1.lp + ct2.hp);
        vH = (hi2.lp + antiRes * hi1.lp) * kHighGain;
      }
    };

    // === Per-output-sample loop (2 internal steps each) ===
    for (int i = 0; i < FRAMELENGTH; i++)
    {
      float xCurr = in[i];
      if (fabsf(xCurr) < 1.18e-23f) xCurr = 1.18e-17f;
      float vCurr = voct[i];

      // Internal sample A: midpoint between prev and current
      // (linear interp at the upsampled 96 kHz timeline)
      float vMid = (prevV + vCurr) * 0.5f;
      float xMid = (prevX + xCurr) * 0.5f;
      if (fabsf(xMid) < 1.18e-23f) xMid = 1.18e-17f;

      float vL_a, vC_a, vH_a;
      innerStep(vMid, xMid, vL_a, vC_a, vH_a);

      // Internal sample B: current sample
      float vL_b, vC_b, vH_b;
      innerStep(vCurr, xCurr, vL_b, vC_b, vH_b);

      // Decimation: simple [1/2, 1/2] kernel = average of pair.
      // -3 dB at host-rate Fs/4 (= 12 kHz at 48k host). A sharper
      // half-band FIR would attenuate the internal-rate harmonics
      // further but the average is the cheapest decimator that
      // still removes the bulk of aliasing energy.
      float vL = (vL_a + vL_b) * 0.5f;
      float vC = (vC_a + vC_b) * 0.5f;
      float vH = (vH_a + vH_b) * 0.5f;

      // NaN clamp + per-block writes
      if (vL != vL || vL > 10.0f || vL < -10.0f) vL = 0.0f;
      if (vC != vC || vC > 10.0f || vC < -10.0f) vC = 0.0f;
      if (vH != vH || vH > 10.0f || vH < -10.0f) vH = 0.0f;
      outLow[i] = vL;
      outCentre[i] = vC;
      outHigh[i] = vH;

      // Out 1: fader mix → fastTanh
      float mix = vL * wL + vC * wC + vH * wH;
      out[i] = SistersSvf::fastTanh(mix);

      prevV = vCurr;
      prevX = xCurr;
    }

    // Carry the last samples to the next frame so the first sample's
    // midpoint interpolation has a valid history.
    s.lastVoct = prevV;
    s.lastIn = prevX;
  }

} // namespace stolmine
