// JF — hex-voiced harmonically-coupled slope-engine voice. v1.
// See planning/just-friends.md + planning/jf-initial-pass.md.
//
// Phase 2: scalar single-voice slope engine for IDENTITY (1N) across
// all 6 base cells (Range x Mode = Sound/Shape x Transient/Sustain/Cycle).
// MIX = voice 1N (only active voice). 2N..6N remain silent until
// Phase 3 SIMD-izes via vendored polygon DSP.
//
// Per-mode trigger semantics:
//   Cycle      — free-running phasor; rising-edge phase-resets (hard-sync)
//   Transient  — AR slope; rising edge starts cycle, retriggers ignored
//                while active
//   Sustain    — gate-following; rises while gate-high, falls while
//                gate-low (clamped at extremes)

#include "JF.h"

#include <od/AudioThread.h>
#include <od/config.h>
#include <math.h>
#include <string.h>

namespace stolmine
{

  struct JF::Internal
  {
    // Voice 1N state (scalar Phase 2; one of six in Phase 3+)
    float phase = 0.0f;        // [0, 1) for Cycle/Transient; [0, 1] clamped for Sustain
    bool active = false;       // Transient: AR cycle currently running
    bool sustaining = false;   // Sustain: gate currently high (rising)
    bool prevGate = false;     // edge detection latch
    float prevTrigSample = 0.0f;
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
  // Returns per-sample phase increment (frequency / sample-rate).
  static inline float computeIncrement(float voctV, float timeBias, int range)
  {
    // Clamp inputs to safe ranges.
    if (timeBias < 0.0f) timeBias = 0.0f;
    if (timeBias > 1.0f) timeBias = 1.0f;
    if (voctV < -10.0f) voctV = -10.0f;
    if (voctV >  10.0f) voctV =  10.0f;

    float baseFreq;
    if (range == 2)
    {
      // Sound: 20 Hz at timeBias=0 → ~81920 Hz at timeBias=1 (12 octaves)
      baseFreq = 20.0f * powf(4096.0f, timeBias);
    }
    else
    {
      // Shape: ~1/60 Hz (~minute slope) at timeBias=0 → ~1000 Hz at timeBias=1
      baseFreq = (1.0f / 60.0f) * powf(60000.0f, timeBias);
    }

    float freq = baseFreq * powf(2.0f, voctV);
    float inc = freq / globalConfig.sampleRate;
    if (inc < 0.0f) inc = 0.0f;
    if (inc > 0.5f) inc = 0.5f;  // Nyquist guard
    return inc;
  }

  void JF::process()
  {
    const int frames = FRAMELENGTH;

    float *vOctBuf = mVOct.buffer();
    float *trigBuf = mTrig1N.buffer();

    float *mixBuf  = mMix.buffer();
    float *out1N   = mOut1N.buffer();
    float *out2N   = mOut2N.buffer();
    float *out3N   = mOut3N.buffer();
    float *out4N   = mOut4N.buffer();
    float *out5N   = mOut5N.buffer();
    float *out6N   = mOut6N.buffer();

    // Voices 2N..6N silent in Phase 2.
    const size_t bytes = frames * sizeof(float);
    memset(out2N, 0, bytes);
    memset(out3N, 0, bytes);
    memset(out4N, 0, bytes);
    memset(out5N, 0, bytes);
    memset(out6N, 0, bytes);

    const int range = mRange.value();   // 1=Shape, 2=Sound
    const int mode  = mMode.value();    // 1=Transient, 2=Sustain, 3=Cycle
    const float timeBias = mTimeBias.value();

    // Block-rate frequency: read V/Oct at sample 0. Per-sample V/Oct
    // tracking arrives in Phase 4 with FM (and proper per-sample inc).
    const float voctV = vOctBuf[0];
    const float inc = computeIncrement(voctV, timeBias, range);

    Internal &s = *mpInternal;

    for (int i = 0; i < frames; i++)
    {
      const float trigS = trigBuf[i];
      // Gate threshold = 0.5 per feedback_comparator_gate_threshold.
      const bool gateNow = (trigS > 0.5f);
      const bool risingEdge  =  gateNow && !s.prevGate;
      const bool fallingEdge = !gateNow &&  s.prevGate;
      s.prevGate = gateNow;

      float p = s.phase;

      if (mode == 3)
      {
        // Cycle: free-running; rising edge hard-syncs phase to 0.
        p += inc;
        if (p >= 1.0f) p -= 1.0f;
        if (risingEdge) p = 0.0f;
      }
      else if (mode == 1)
      {
        // Transient: AR. Rising edge starts cycle if not active.
        // Retriggers mid-slope ignored.
        if (risingEdge && !s.active)
        {
          s.active = true;
          p = 0.0f;
        }
        if (s.active)
        {
          p += inc;
          if (p >= 1.0f)
          {
            p = 0.0f;
            s.active = false;
          }
        }
        else
        {
          p = 0.0f;
        }
      }
      else
      {
        // Sustain: gate-following. Rise while gate-high, fall while gate-low.
        // Trapezoid width follows gate duration (per tech map Sound/Sustain).
        if (risingEdge)  s.sustaining = true;
        if (fallingEdge) s.sustaining = false;

        if (s.sustaining)
        {
          p += inc;
          if (p > 1.0f) p = 1.0f;
        }
        else
        {
          p -= inc;
          if (p < 0.0f) p = 0.0f;
        }
      }

      s.phase = p;

      // Phase-2 shape: triangle (rise 0→1 over phase 0..0.5,
      // fall 1→0 over phase 0.5..1). RAMP/CURVE in Phase 3.
      // Sustain mode runs the phase 0..1 directly so output tracks
      // the slope (no triangle fold).
      float shaped;
      if (mode == 2)
      {
        shaped = p;  // Sustain: linear AR-without-S envelope
      }
      else
      {
        shaped = (p < 0.5f) ? (p * 2.0f) : (2.0f - p * 2.0f);
      }

      // Range determines polarity per tech map:
      //   Sound: bipolar (±5V on hardware → ±1 normalized)
      //   Shape: unipolar (0..8V on hardware → 0..1 normalized)
      const float voiceOut = (range == 2) ? (shaped * 2.0f - 1.0f) : shaped;

      out1N[i] = voiceOut;
      mixBuf[i] = voiceOut;  // Phase 2: MIX = single active voice
    }
  }

} // namespace stolmine
