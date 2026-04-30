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

  void JF::process()
  {
    const int frames = FRAMELENGTH;

    float *vOctBuf = mVOct.buffer();
    float *trigBuf = mTrig1N.buffer();

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

    // Block-rate base frequency. Per-sample V/Oct + FM in Phase 4.
    const float voctV = vOctBuf[0];
    const float baseFreq = computeBaseFreq(voctV, timeBias, range);
    const float invSr = 1.0f / globalConfig.sampleRate;

    // Per-voice phase increments (NEON across the 4 lanes).
    // g0 lanes = voices 1..4; g1 lanes = voices 5,6 + masked.
    const float incs[6] = {
      baseFreq * intoneMult(1, intonePos) * invSr,
      baseFreq * intoneMult(2, intonePos) * invSr,
      baseFreq * intoneMult(3, intonePos) * invSr,
      baseFreq * intoneMult(4, intonePos) * invSr,
      baseFreq * intoneMult(5, intonePos) * invSr,
      baseFreq * intoneMult(6, intonePos) * invSr
    };

    const float32x4_t incG0 = jf::four::make_4(incs[0], incs[1], incs[2], incs[3]);
    // Lanes 2,3 of g1 are masked-voice; inc value irrelevant but keep
    // small/finite to avoid NaN propagation.
    const float32x4_t incG1 = jf::four::make_4(incs[4], incs[5], 0.0f, 0.0f);

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

      // Voice processing — 4 lanes per group, mode shared.
      auto pG0 = vG0.process(incG0, gate, mode);
      auto pG1 = vG1.process(incG1, vandq_u32(gate, g1Mask), mode);

      // Waveshape: triangle for Cycle/Transient, linear for Sustain.
      // (Sustain phase is already a 0..1 trapezoid level; no fold.)
      float32x4_t shapedG0, shapedG1;
      if (mode == jf::four::kSustain)
      {
        shapedG0 = pG0;
        shapedG1 = pG1;
      }
      else
      {
        shapedG0 = jf::four::triangle(pG0);
        shapedG1 = jf::four::triangle(pG1);
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

      // Per-voice sub-out writes.
      float voices[8];
      vst1q_f32(voices,     shapedG0);
      vst1q_f32(voices + 4, shapedG1);

      outs[0][i] = voices[0];  // 1N
      outs[1][i] = voices[1];  // 2N
      outs[2][i] = voices[2];  // 3N
      outs[3][i] = voices[3];  // 4N
      outs[4][i] = voices[4];  // 5N
      outs[5][i] = voices[5];  // 6N

      // MIX = sum of all 6 voices, scaled to avoid clipping.
      // Phase 4 swaps in the proper combiner (tanh in Sound, max-of-
      // index-scaled in Shape).
      const float sum = jf::four::sum_lanes(shapedG0)
                      + voices[4] + voices[5];
      mixBuf[i] = sum * (1.0f / 6.0f);
    }
  }

} // namespace stolmine
