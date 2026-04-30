// JF — hex-voiced harmonically-coupled slope-engine voice. v1.
// See planning/just-friends.md + planning/jf-initial-pass.md.
//
// Phase 3a: 6-voice NEON via two jf::four::Voice instances (8 lanes,
// 6 active, 2 masked off in group 1). INTONE morph wires per-voice
// pitch multipliers across the lane bank. RAMP and CURVE arrive in
// Phase 3b / 3c.
//
// Per-mode trigger semantics (all voices share the same Mode setting;
// edge dispatch happens per-lane within the NEON Voice struct):
//   Cycle      — free-running phasor; rising-edge phase-resets
//   Transient  — AR slope; rising edge starts cycle, retriggers ignored
//                while active
//   Sustain    — gate-following ASR-without-S; rises while gate-high,
//                falls while gate-low

#include "JF.h"
#include "jf/voice.h"

#include <od/AudioThread.h>
#include <od/config.h>
#include <math.h>
#include <string.h>

namespace stolmine
{

  struct JF::Internal
  {
    // Two 4-lane NEON voices = 8 lanes; 6 active (1N..6N) on lanes
    // [g0:0..3, g1:0..1]. g1 lanes 2,3 masked off via gate=0 always.
    jf::four::Voice mVoiceG0;  // 1N, 2N, 3N, 4N
    jf::four::Voice mVoiceG1;  // 5N, 6N, _, _
    jf::four::CurveLut mCurveLut;  // 5 anchors × 256 entries, init in ctor

    // FM AC-coupling state for Sound range. Tech map: FM input is
    // AC-coupled in Sound for clean TZFM, DC-coupled in Shape. One-pole
    // HPF, ~5 Hz at 48 kHz: alpha = exp(-2*pi*5/48000) ≈ 0.99934.
    float mFmHpfX1 = 0.0f;
    float mFmHpfY1 = 0.0f;
  };

  JF::JF()
  {
    addInput(mVOct);
    addInput(mFM);
    addInput(mTrig1N);
    addOutput(mMix);
    addOutput(mOut1N);
    addOutput(mOut2N);
    addOutput(mOut3N);
    addOutput(mOut4N);
    addOutput(mOut5N);
    addOutput(mOut6N);
    addParameter(mTimeBias);
    addParameter(mIntone);
    addParameter(mRamp);
    addParameter(mCurve);
    addParameter(mFmDepth);
    addParameter(mOut);
    addOption(mRange);
    addOption(mMode);
    addOption(mOutMode);
    mRange.enableSerialization();
    mMode.enableSerialization();
    mOutMode.enableSerialization();

    mpInternal = new Internal();
  }

  JF::~JF()
  {
    delete mpInternal;
  }

  // TIME mapping per tech map.
  //   Sound: ~20 Hz to ~80 kHz across timeBias [0,1]      (~12 octaves)
  //   Shape: ~minutes to ~milliseconds across [0,1]       (~16 octaves)
  // V/Oct adds octaves on top exponentially.
  static inline float computeBaseFreq(float voctV, float timeBias, int range)
  {
    if (timeBias < 0.0f) timeBias = 0.0f;
    if (timeBias > 1.0f) timeBias = 1.0f;
    if (voctV < -10.0f) voctV = -10.0f;
    if (voctV >  10.0f) voctV =  10.0f;

    float baseFreq;
    if (range == 2)
      baseFreq = 20.0f * powf(4096.0f, timeBias);
    else
      baseFreq = (1.0f / 60.0f) * powf(60000.0f, timeBias);

    return baseFreq * powf(2.0f, voctV);
  }

  // INTONE morph: continuous CCW (-1) → noon (0) → CW (+1).
  // Returns per-voice frequency multiplier (1.0 = same as IDENTITY).
  //   CW (+1):  voice n → n   (overtone series 1:2:3:4:5:6)
  //   noon (0): voice n → 1 + (n-1)*0.005   (slight detune spread)
  //   CCW (-1): voice n → (7-n)/6   (undertone — 1N=1, 6N=1/6;
  //                                  inter-voice ratios 6:5:4:3:2:1)
  // n is 1-indexed (1..6). voice 1 (IDENTITY) is always 1.0.
  static inline float intoneMult(int n, float pos)
  {
    if (n == 1) return 1.0f;
    if (pos > 1.0f) pos = 1.0f;
    if (pos < -1.0f) pos = -1.0f;

    const float overtone   = (float)n;
    const float noonDetune = 1.0f + (float)(n - 1) * 0.005f;
    const float undertone  = (float)(7 - n) / 6.0f;

    if (pos >= 0.0f)
      return noonDetune + (overtone - noonDetune) * pos;
    else
      return undertone + (noonDetune - undertone) * (1.0f + pos);
  }

  // no-tree-vectorize keeps GCC from auto-vectorizing the per-sample
  // output buffer writes into NEON quad-D stores with `:64` alignment
  // hints, which trap on Cortex-A8 when the buffer pointer arithmetic
  // (outs[N] + i) doesn't land on an 8-byte boundary. Same pattern as
  // Pecto's copyFloatArray helper. Our own NEON intrinsics inside
  // jf::four are unaffected — they're explicit, not auto-emitted.
  __attribute__((optimize("no-tree-vectorize")))
  void JF::process()
  {
    const int frames = FRAMELENGTH;

    float *vOctBuf = mVOct.buffer();
    float *trigBuf = mTrig1N.buffer();
    float *fmBuf   = mFM.buffer();

    float *mixBuf  = mMix.buffer();
    float *outs[6] = {
      mOut1N.buffer(),
      mOut2N.buffer(),
      mOut3N.buffer(),
      mOut4N.buffer(),
      mOut5N.buffer(),
      mOut6N.buffer()
    };

    const int range = mRange.value();   // 1=Shape, 2=Sound
    const int mode  = mMode.value();    // 1=Transient, 2=Sustain, 3=Cycle
    const float timeBias = mTimeBias.value();
    const float intonePos = mIntone.value();
    const float rampPos = mRamp.value();  // -1..+1 bipolar
    const float curvePos = mCurve.value(); // -1..+1 bipolar
    const float fmDepth = mFmDepth.value(); // -1..+1 bipolar (signed)

    // RAMP threshold T in (eps, 1-eps). Clamp keeps invT / inv(1-T)
    // finite at extremes. T = 0.5 → symmetric triangle (matches Phase 3a
    // when RAMP at noon).
    float rampT = 0.5f + 0.49f * rampPos;
    if (rampT < 0.01f) rampT = 0.01f;
    if (rampT > 0.99f) rampT = 0.99f;
    const float32x4_t rampTv = vdupq_n_f32(rampT);
    const float32x4_t invRampTv = vdupq_n_f32(1.0f / rampT);
    const float32x4_t invOneMinusRampTv = vdupq_n_f32(1.0f / (1.0f - rampT));

    // CURVE morph: continuous blend across 5 anchors.
    //   pos -1.0..-0.5: rect (0) → log (1)    morph = (pos + 1) / 0.5
    //   pos -0.5..  0 : log  (1) → lin (2)    morph = (pos + 0.5) / 0.5
    //   pos  0  ..+0.5: lin  (2) → exp (3)    morph = pos / 0.5
    //   pos +0.5..+1.0: exp  (3) → sine (4)   morph = (pos - 0.5) / 0.5
    int curveShape0, curveShape1;
    float curveMorph;
    if (curvePos < -0.5f)
    {
      curveShape0 = 0; curveShape1 = 1; curveMorph = (curvePos + 1.0f) / 0.5f;
    }
    else if (curvePos < 0.0f)
    {
      curveShape0 = 1; curveShape1 = 2; curveMorph = (curvePos + 0.5f) / 0.5f;
    }
    else if (curvePos < 0.5f)
    {
      curveShape0 = 2; curveShape1 = 3; curveMorph = curvePos / 0.5f;
    }
    else
    {
      curveShape0 = 3; curveShape1 = 4; curveMorph = (curvePos - 0.5f) / 0.5f;
    }
    if (curveMorph < 0.0f) curveMorph = 0.0f;
    if (curveMorph > 1.0f) curveMorph = 1.0f;
    const float32x4_t curveMorphV = vdupq_n_f32(curveMorph);

    // Block-rate base frequency. Per-sample V/Oct + FM in Phase 4.
    const float voctV = vOctBuf[0];
    const float baseFreq = computeBaseFreq(voctV, timeBias, range);
    const float invSr = 1.0f / globalConfig.sampleRate;

    // Per-voice base phase increments (NEON across the 4 lanes).
    // g0 lanes = voices 1..4; g1 lanes = voices 5,6 + masked.
    // FM modulates these per-sample inside the loop.
    const float baseIncs[6] = {
      baseFreq * intoneMult(1, intonePos) * invSr,
      baseFreq * intoneMult(2, intonePos) * invSr,
      baseFreq * intoneMult(3, intonePos) * invSr,
      baseFreq * intoneMult(4, intonePos) * invSr,
      baseFreq * intoneMult(5, intonePos) * invSr,
      baseFreq * intoneMult(6, intonePos) * invSr
    };

    const float32x4_t baseIncG0 = jf::four::make_4(baseIncs[0], baseIncs[1], baseIncs[2], baseIncs[3]);
    const float32x4_t baseIncG1 = jf::four::make_4(baseIncs[4], baseIncs[5], 0.0f, 0.0f);

    // FM dispatch:
    //   fmDepth >= 0 (CW):  linear FM to TIME (all voices equally → TZFM)
    //   fmDepth <  0 (CCW): linear FM to INTONE (per-voice index-weighted;
    //                       IDENTITY unaffected, 6N most-affected)
    //
    // Linear FM scale: Helicase pattern uses 100/sr for ~100Hz/V FM index
    // at +1 depth and +1 FM input. Same scale here for parity.
    const float fmScale = 100.0f * invSr;
    const bool fmToTime = (fmDepth >= 0.0f);
    const float fmAbs = fmDepth >= 0.0f ? fmDepth : -fmDepth;

    // Per-voice INTONE-FM weights (1-indexed): voice n weight = (n-1)/5.
    // Voice 1 IDENTITY weight = 0 (unaffected). Voice 6 weight = 1.
    const float32x4_t intoneFmWeightG0 =
        jf::four::make_4(0.0f, 1.0f/5.0f, 2.0f/5.0f, 3.0f/5.0f);
    const float32x4_t intoneFmWeightG1 =
        jf::four::make_4(4.0f/5.0f, 1.0f, 0.0f, 0.0f);

    // DC-blocker activation per range. Sound = AC-coupled, Shape = DC.
    // alpha tuned for ~5 Hz cutoff at 48 kHz.
    const bool fmAcCouple = (range == 2);
    const float hpfAlpha = 0.99934f;

    // Lane mask: g1 lanes 0,1 are real voices; lanes 2,3 are dummies.
    // Used to gate output.
    const uint32x4_t g1Mask = jf::four::make_mask(true, true, false, false);

    jf::four::Voice &vG0 = mpInternal->mVoiceG0;
    jf::four::Voice &vG1 = mpInternal->mVoiceG1;

    for (int i = 0; i < frames; i++)
    {
      // Phase 3a: single trigger inlet (1N) drives all 6 voices.
      // Phase 5 distributes per-voice triggers via cascade.
      const bool gateNow = (trigBuf[i] > 0.5f);
      const uint32x4_t gate = gateNow ? vdupq_n_u32(0xFFFFFFFFu) : vdupq_n_u32(0u);

      // FM input — AC-couple in Sound range, passthrough in Shape.
      float fmS = fmBuf[i];
      if (fmAcCouple)
      {
        // y[n] = x[n] - x[n-1] + alpha * y[n-1]
        const float y = fmS - mpInternal->mFmHpfX1 + hpfAlpha * mpInternal->mFmHpfY1;
        mpInternal->mFmHpfX1 = fmS;
        mpInternal->mFmHpfY1 = y;
        fmS = y;
      }

      // Compute per-lane FM phase increments.
      float32x4_t incG0, incG1;
      if (fmToTime)
      {
        // Linear FM to TIME — same fm offset added to every voice.
        // TZFM: when fm goes negative, inc + fmAmount can go negative,
        // but wrap_phase handles that via the floor trick.
        const float fmAmount = fmS * fmAbs * fmScale;
        const float32x4_t fmAmountV = vdupq_n_f32(fmAmount);
        incG0 = vaddq_f32(baseIncG0, fmAmountV);
        incG1 = vaddq_f32(baseIncG1, fmAmountV);
      }
      else
      {
        // Linear FM to INTONE — per-voice weighted.
        const float fmAmount = fmS * fmAbs * fmScale;
        const float32x4_t fmAmountV = vdupq_n_f32(fmAmount);
        incG0 = vaddq_f32(baseIncG0, vmulq_f32(fmAmountV, intoneFmWeightG0));
        incG1 = vaddq_f32(baseIncG1, vmulq_f32(fmAmountV, intoneFmWeightG1));
      }

      // Voice processing — 4 lanes per group, mode shared.
      auto pG0 = vG0.process(incG0, gate, mode);
      auto pG1 = vG1.process(incG1, vandq_u32(gate, g1Mask), mode);

      // Waveshape: RAMP-asymmetric stage progress → CURVE LUT bend, for
      // Cycle/Transient. Sustain feeds phase straight through (it's
      // already a 0..1 trapezoid level; CURVE is bypassed in Sustain
      // mode per tech map's interpretation).
      float32x4_t shapedG0, shapedG1;
      if (mode == jf::four::kSustain)
      {
        shapedG0 = pG0;
        shapedG1 = pG1;
      }
      else
      {
        auto rg0 = jf::four::ramp_triangle(pG0, rampTv, invRampTv, invOneMinusRampTv);
        auto rg1 = jf::four::ramp_triangle(pG1, rampTv, invRampTv, invOneMinusRampTv);
        shapedG0 = mpInternal->mCurveLut.lookup(rg0, curveShape0, curveShape1, curveMorphV);
        shapedG1 = mpInternal->mCurveLut.lookup(rg1, curveShape0, curveShape1, curveMorphV);
      }

      // Range polarity: Sound = bipolar (±1), Shape = unipolar (0..1).
      if (range == 2)
      {
        const auto two = vdupq_n_f32(2.0f);
        const auto one = vdupq_n_f32(1.0f);
        shapedG0 = vsubq_f32(vmulq_f32(shapedG0, two), one);
        shapedG1 = vsubq_f32(vmulq_f32(shapedG1, two), one);
      }

      // Mask off g1 lanes 2,3 in output (zero them).
      shapedG1 = vbslq_f32(g1Mask, shapedG1, vdupq_n_f32(0.0f));

      // Per-voice sub-out writes via vgetq_lane_f32 — keeps the NEON
      // values register-resident; storing to a stack-local voices[8]
      // array would emit `:64` hints that trap on Cortex-A8 (per
      // feedback_neon_intrinsics_drumvoice + feedback_neon_hint_surfaces).
      const float v1N = vgetq_lane_f32(shapedG0, 0);
      const float v2N = vgetq_lane_f32(shapedG0, 1);
      const float v3N = vgetq_lane_f32(shapedG0, 2);
      const float v4N = vgetq_lane_f32(shapedG0, 3);
      const float v5N = vgetq_lane_f32(shapedG1, 0);
      const float v6N = vgetq_lane_f32(shapedG1, 1);

      outs[0][i] = v1N;
      outs[1][i] = v2N;
      outs[2][i] = v3N;
      outs[3][i] = v4N;
      outs[4][i] = v5N;
      outs[5][i] = v6N;

      // MIX = sum of all 6 voices, scaled to avoid clipping.
      // Phase 4 swaps in the proper combiner (tanh in Sound, max-of-
      // index-scaled in Shape).
      mixBuf[i] = (v1N + v2N + v3N + v4N + v5N + v6N) * (1.0f / 6.0f);
    }
  }

} // namespace stolmine
