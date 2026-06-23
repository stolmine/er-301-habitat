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
    // for the upper internal step. lastIn split into per-block
    // copies so cross-frame interpolation remains coherent when the
    // per-block routing routes different signals into each block.
    float lastVoct;
    float lastInLow;
    float lastInCentre;
    float lastInHigh;
    // Quality is an audio-rate Inlet — carry the prior frame's last
    // sample for the 2× OS midpoint interp (same pattern as lastVoct).
    float lastQuality;
    // Span is audio-rate but one-pole slewed before the exponential
    // cutoff mapping. Each knob detent is GainBias-ramped over only one
    // block (~1.3 ms), and Span's 4-octave leverage turns that into a
    // fast cutoff jump that the resonant bands click on — worse at
    // higher cutoffs (bigger Hz jump per detent), at all Q. Slewing in
    // [0,1] = slewing in log-cutoff = constant oct/s, equalizing the fix
    // across the range. Safe now that the rail clamps are soft (Phase
    // 5d) — the earlier 5c slew only sounded worse because it swept
    // Span slowly THROUGH the old hard-clamp seams. Persists across
    // frames.
    float spanSlew;

    // Per-block post-routing input ring buffer for the overview ply
    // viz. Stored at decimated rate (one sample per ~8 cycles
    // captured in 256 entries) so the viz sees a meaningful waveform
    // at any pitch.
    static const int kInputRingSize = 256;
    float inputRing[3][kInputRingSize];
    int inputRingPos;
    int inputRingDecimCounter;
    int inputRingDecimRate;
    // true when the block is sourcing from ALL fallback (no per-block
    // patched signal). Drives the "ALL" overlay in the viz.
    bool curUsingAll[3];

    void Init()
    {
      low1.reset(); low2.reset();
      ctr1.reset(); ctr2.reset();
      hi1.reset();  hi2.reset();
      lastVoct = 0.0f;
      lastInLow = 0.0f;
      lastInCentre = 0.0f;
      lastInHigh = 0.0f;
      lastQuality = 0.0f;
      spanSlew = 0.25f;      // matches GainBias default bias
      memset(inputRing, 0, sizeof(inputRing));
      inputRingPos = 0;
      inputRingDecimCounter = 0;
      inputRingDecimRate = 8;
      curUsingAll[0] = true;
      curUsingAll[1] = true;
      curUsingAll[2] = true;
    }
  };

  Canals::Canals()
  {
    addInput(mIn);
    addInput(mLowIn);
    addInput(mCentreIn);
    addInput(mHighIn);
    addInput(mVOct);
    addInput(mSpan);        // audio-rate Inlet (was Parameter) — per-sample
    addInput(mQuality);     // audio-rate Inlet (was Parameter) — per-sample
    addOutput(mOut);
    addOutput(mOutLow);     // FIXED: was missing — sub-out picker silently failed
    addOutput(mOutCentre);  // FIXED
    addOutput(mOutHigh);    // FIXED
    addParameter(mFundamental);
    addParameter(mOutput);
    addParameter(mMode);
    // Routing options driven by Lua-side branch-state polling.
    // enableSerialization so quicksave round-trips the patched state
    // + the AllEnabled toggle.
    addOption(mAllEnabled);
    addOption(mLowPatched);
    addOption(mCentrePatched);
    addOption(mHighPatched);
    mAllEnabled.enableSerialization();
    mLowPatched.enableSerialization();
    mCentrePatched.enableSerialization();
    mHighPatched.enableSerialization();

    mpInternal = new Internal();
    mpInternal->Init();
  }

  float Canals::getBlockInputSample(int block, int idx)
  {
    if (block < 0 || block > 2) return 0.0f;
    if (idx < 0 || idx >= 256)  return 0.0f;
    return mpInternal->inputRing[block][(mpInternal->inputRingPos + idx) & 255];
  }

  bool Canals::isBlockUsingAll(int block)
  {
    if (block < 0 || block > 2) return false;
    return mpInternal->curUsingAll[block];
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

  // Interpolated semitones->ratio. stmlib's SemitonesToRatio truncates
  // the fractional LUT index (256 steps/semitone, no interpolation),
  // which audibly QUANTIZES the cutoff/span under audio-rate FM — the
  // native Sine Osc instead derives frequency with a smooth polynomial
  // exp (no table, no steps). We linearly interpolate stmlib's low
  // table to remove the stair-step while keeping its exact node values,
  // so the hardware-matched self-osc freq calibration is preserved.
  // Index clamped so hot audio-rate V/Oct can't read the LUT OOB
  // (stmlib's unsafe variant relied on the caller's freqHz clamp).
  static inline float semisToRatioSmooth(float semitones)
  {
    float pitch = semitones + 128.0f;
    if (pitch < 0.0f) pitch = 0.0f;
    if (pitch > 255.999f) pitch = 255.999f;
    int integral = (int)pitch;
    float fractional = pitch - (float)integral;
    float fidx = fractional * 256.0f;
    int i = (int)fidx;                 // 0..255
    float t = fidx - (float)i;
    const float *low = stmlib::lut_pitch_ratio_low;
    float lo = low[i] + (low[i + 1] - low[i]) * t;   // i+1 <= 256 (257-entry table)
    return stmlib::lut_pitch_ratio_high[integral] * lo;
  }

  // C1 soft clamps: identity until within `knee` of the limit, then a
  // quadratic ease that meets the limit with matched value AND slope.
  // Replaces hard min/max so a control sweeping THROUGH a limit produces
  // no derivative-break seam — the native ladder filter keeps its wide
  // cutoff sweeps seamless precisely because it has no in-band hard
  // clamps (just a single rail). See planning/canals-audio-rate-mod.md
  // Phase 5d.
  static inline float softCeil(float x, float hi, float knee)
  {
    float edge = hi - knee;
    if (x <= edge) return x;
    if (x >= hi + knee) return hi;
    float u = x - edge;                       // 0..2·knee
    return edge + u - u * u * (0.25f / knee);
  }
  static inline float softFloor(float x, float lo, float knee)
  {
    float edge = lo + knee;
    if (x >= edge) return x;
    if (x <= lo - knee) return lo;
    float u = edge - x;                       // 0..2·knee
    return edge - u + u * u * (0.25f / knee);
  }
  static inline float softClampF(float x, float lo, float hi,
                                 float kneeLo, float kneeHi)
  {
    return softCeil(softFloor(x, lo, kneeLo), hi, kneeHi);
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

    float *in       = mIn.buffer();
    float *lowIn    = mLowIn.buffer();
    float *centreIn = mCentreIn.buffer();
    float *highIn   = mHighIn.buffer();
    float *voct     = mVOct.buffer();
    float *spanBuf  = mSpan.buffer();     // audio-rate Span CV
    float *qualBuf  = mQuality.buffer();  // audio-rate Quality CV
    float *out       = mOut.buffer();
    float *outLow    = mOutLow.buffer();
    float *outCentre = mOutCentre.buffer();
    float *outHigh   = mOutHigh.buffer();

    // Per-block input routing (driven by Lua-side branch-state polling
    // + the AllEnabled global toggle). Block-rate snapshot of Options.
    bool allEn      = (mAllEnabled.value()    == 1);
    bool lowPatched = (mLowPatched.value()    == 2);
    bool ctrPatched = (mCentrePatched.value() == 2);
    bool hiPatched  = (mHighPatched.value()   == 2);

    // Routing-state mirror for the overview ply viz. A block is
    // "using ALL" when it's not patched and ALL is enabled; the
    // viz overlays the word "ALL" on the input scope in that case.
    Internal &sBlockState = *mpInternal;
    sBlockState.curUsingAll[0] = !lowPatched && allEn;
    sBlockState.curUsingAll[1] = !ctrPatched && allEn;
    sBlockState.curUsingAll[2] = !hiPatched && allEn;

    // === Block-rate parameter sampling ===
    // Span + Quality are NOT read here — they are audio-rate Inlets read
    // per-sample inside the loop and their derived constants (damping,
    // antiRes, spanMult) are computed per-sample inside innerStep. Only
    // the genuine block-rate controls are sampled here.
    float fundamental = mFundamental.value();
    float outputPos = CLAMP(0.0f, 3.0f, mOutput.value());
    int mode = CLAMP(0, 1, (int)(mMode.value() + 0.5f));

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

    // Span one-pole slew coefficient (~8 ms). Smooths the per-detent
    // cutoff transients (see spanSlew note). Tune kSpanSlewMs: shorter =
    // snappier knob, risk of pop creeping back at high cutoffs; longer =
    // silkier, more glide. Bandlimits Span CV to ~20 Hz — span audio-
    // rate FM is exotic; knob smoothness wins.
    const float kSpanSlewMs = 8.0f;
    float kSpanSlew = (globalConfig.samplePeriod * 1000.0f) / kSpanSlewMs;
    if (kSpanSlew > 1.0f) kSpanSlew = 1.0f;

    // Linear-interpolation upsample state — carry the last V/Oct
    // and input sample from the prior frame so the first output
    // sample can interpolate a midpoint between them.
    float prevV  = s.lastVoct;
    float prevXL = s.lastInLow;
    float prevXC = s.lastInCentre;
    float prevXH = s.lastInHigh;
    float prevQual = s.lastQuality;

    // Input ring-buffer decimation rate — hardcoded so the scope
    // timebase doesn't track Fundamental. User preference: keep the
    // ~10.7 ms scope window (matches what the adaptive rate produced
    // at Fundamental ≈ +16 semitones). At 48 kHz / decim 2, 256 ring
    // entries = 512 audio samples ≈ 10.7 ms.
    s.inputRingDecimRate = 2;

    // Internal-step helper: takes (v, span, quality, x) at the internal
    // sample time, configures all 6 SVFs, processes, returns raw
    // vL/vC/vH pre-NaN-clamp. Run twice per output sample (midpoint +
    // current). span/quality arrive per-sample (audio-rate Inlets), so
    // their derived constants (damping, antiRes, spanMult) are computed
    // here per internal step — which also gives them the 2× OS headroom
    // the cutoff already enjoys.
    auto innerStep = [&](float v, float span, float quality,
                         float xL, float xC, float xH,
                         float &vL, float &vC, float &vH)
    {
      // Quality → damping curve (per-sample; was block-rate).
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

      // Span → cutoff spread multiplier (per-sample; was block-rate).
      // Smooth (interpolated) ratio — see semisToRatioSmooth: the
      // truncated stmlib LUT quantizes this under audio-rate Span mod.
      float spanMult = semisToRatioSmooth(span * 48.0f);
      float invSpanMult = 1.0f / spanMult;

      // Cutoff derivation at this internal sample time.
      float totalSemis = v * 120.0f + fundamental;
      float freqHz = 261.63f * semisToRatioSmooth(totalSemis);
      float lowHz = freqHz * invSpanMult;
      float highHz = freqHz * spanMult;

      // Normalized cutoffs, each soft-clamped into the SVF-safe band
      // [~96 Hz, ~20 kHz] (at the 96 kHz internal rate). One C1 soft-knee
      // per band REPLACES the old hard Hz+normalized clamp cascade: when
      // a wide Span sweep drove the LOW band into the 96 Hz floor and the
      // HIGH band into the 20 kHz ceiling, each hard clamp was a
      // derivative-break "seam" that popped. The soft-knee eases into the
      // same rails. (semisToRatioSmooth already bounds freqHz, so the
      // products can't overflow before the clamp.) [lo,hi] preserve the
      // old effective range exactly; knees are well inside it.
      const float fLo = 0.001f;                  // ~96 Hz
      const float fHi = 20000.0f * kInvSR_OS;    // ~0.2083 = 20 kHz
      const float fKneeLo = 0.0006f;
      const float fKneeHi = 0.030f;
      float lowF  = softClampF(lowHz  * kInvSR_OS, fLo, fHi, fKneeLo, fKneeHi);
      float ctrF  = softClampF(freqHz * kInvSR_OS, fLo, fHi, fKneeLo, fKneeHi);
      float highF = softClampF(highHz * kInvSR_OS, fLo, fHi, fKneeLo, fKneeHi);

      // Frequency-compensated damping. Soft-knee the ratio ceiling too —
      // the old >4 hard clamp stepped resonance directly (the most
      // audible Span seam). knee=1 eases from ratio 3 up to 4.
      float ratioLow  = softCeil(ctrF / lowF,  4.0f, 1.0f);
      float ratioHigh = softCeil(ctrF / highF, 4.0f, 1.0f);
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

        lo1 = s.low1.process(xL);
        lo2 = s.low2.process(lo1.lp);
        ct1 = s.ctr1.process(xC);
        ct2 = s.ctr2.process(ct1.hp);
        hi1 = s.hi1.process(xH);
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

        lo1 = s.low1.process(xL);
        lo2 = s.low2.process(lo1.lp);
        ct1 = s.ctr1.process(xC);
        ct2 = s.ctr2.process(ct1.hp);
        hi1 = s.hi1.process(xH);
        hi2 = s.hi2.process(hi1.hp);

        vL = (lo2.hp + antiRes * lo1.hp) * kLowGain;
        vC = ct2.lp + antiRes * (ct1.lp + ct2.hp);
        vH = (hi2.lp + antiRes * hi1.lp) * kHighGain;
      }
    };

    // === Per-output-sample loop (2 internal steps each) ===
    for (int i = 0; i < FRAMELENGTH; i++)
    {
      // Per-block input selection (normalling).
      //   patched  → use per-block input directly
      //   else     → use ALL (if enabled) or silence
      float xAll = in[i];
      float xCurrL = lowPatched ? lowIn[i]    : (allEn ? xAll : 0.0f);
      float xCurrC = ctrPatched ? centreIn[i] : (allEn ? xAll : 0.0f);
      float xCurrH = hiPatched  ? highIn[i]   : (allEn ? xAll : 0.0f);

      // Decimated capture of post-routing per-block input for the
      // overview ply viz. Stored BEFORE denormal seeding so the
      // viz sees genuine silence rather than the tiny seed value.
      s.inputRingDecimCounter++;
      if (s.inputRingDecimCounter >= s.inputRingDecimRate) {
        s.inputRingDecimCounter = 0;
        s.inputRing[0][s.inputRingPos] = xCurrL;
        s.inputRing[1][s.inputRingPos] = xCurrC;
        s.inputRing[2][s.inputRingPos] = xCurrH;
        s.inputRingPos = (s.inputRingPos + 1) & 255;
      }

      // Denormal seed (per-block — keeps self-osc bootable on any
      // block whose input is genuinely silent).
      if (fabsf(xCurrL) < 1.18e-23f) xCurrL = 1.18e-17f;
      if (fabsf(xCurrC) < 1.18e-23f) xCurrC = 1.18e-17f;
      if (fabsf(xCurrH) < 1.18e-23f) xCurrH = 1.18e-17f;

      float vCurr = voct[i];
      // Quality per-sample (audio-rate Inlet). Span per-sample then
      // one-pole slewed (kSpanSlew) to smooth the per-detent cutoff
      // transients — the slewed value is already smooth, so it feeds
      // both 2× sub-steps directly (no midpoint interp needed).
      float qualCurr = CLAMP(-1.0f, 1.0f, qualBuf[i]);
      float spanTarget = CLAMP(0.0f, 1.0f, spanBuf[i]);
      s.spanSlew += (spanTarget - s.spanSlew) * kSpanSlew;
      float spanNow = s.spanSlew;

      // Internal sample A: midpoint between prev and current
      // (linear interp at the upsampled 96 kHz timeline). Per-block
      // interpolation keeps each block's continuity coherent even
      // as routing changes between blocks.
      float vMid = (prevV + vCurr) * 0.5f;
      float qualMid = (prevQual + qualCurr) * 0.5f;
      float xMidL = (prevXL + xCurrL) * 0.5f;
      float xMidC = (prevXC + xCurrC) * 0.5f;
      float xMidH = (prevXH + xCurrH) * 0.5f;
      if (fabsf(xMidL) < 1.18e-23f) xMidL = 1.18e-17f;
      if (fabsf(xMidC) < 1.18e-23f) xMidC = 1.18e-17f;
      if (fabsf(xMidH) < 1.18e-23f) xMidH = 1.18e-17f;

      float vL_a, vC_a, vH_a;
      innerStep(vMid, spanNow, qualMid, xMidL, xMidC, xMidH, vL_a, vC_a, vH_a);

      // Internal sample B: current sample
      float vL_b, vC_b, vH_b;
      innerStep(vCurr, spanNow, qualCurr, xCurrL, xCurrC, xCurrH, vL_b, vC_b, vH_b);

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

      prevV  = vCurr;
      prevQual = qualCurr;
      prevXL = xCurrL;
      prevXC = xCurrC;
      prevXH = xCurrH;
    }

    // Carry the last samples to the next frame so the first sample's
    // midpoint interpolation has a valid history.
    s.lastVoct      = prevV;
    s.lastQuality   = prevQual;
    // s.spanSlew persists in place (mutated each sample above).
    s.lastInLow     = prevXL;
    s.lastInCentre  = prevXC;
    s.lastInHigh    = prevXH;
  }

} // namespace stolmine
