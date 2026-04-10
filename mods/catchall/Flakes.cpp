#include "Flakes.h"
#include <od/config.h>
#include <hal/ops.h>
#include <math.h>
#include <string.h>
#include <new>

namespace stolmine
{

  static inline float fast_tanh(float x)
  {
    if (x < -4.0f) return -1.0f;
    if (x >  4.0f) return  1.0f;
    float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
  }

  // int16 buffer helpers (same pattern as Pecto/Petrichor)
  static inline void bufWrite(int16_t *buf, int idx, float val)
  {
    float clamped = val;
    if (clamped > 1.0f) clamped = 1.0f;
    if (clamped < -1.0f) clamped = -1.0f;
    buf[idx] = (int16_t)(clamped * 32767.0f);
  }

  static inline float bufRead(const int16_t *buf, int idx)
  {
    return (float)buf[idx] * (1.0f / 32767.0f);
  }

  static inline float bufReadInterp(const int16_t *buf, float pos, int maxDelay)
  {
    int idx0 = (int)pos;
    float frac = pos - (float)idx0;
    if (idx0 < 0) idx0 += maxDelay;
    if (idx0 >= maxDelay) idx0 -= maxDelay;
    int idx1 = idx0 + 1;
    if (idx1 >= maxDelay) idx1 = 0;
    return bufRead(buf, idx0) + (bufRead(buf, idx1) - bufRead(buf, idx0)) * frac;
  }

  // Simple LCG random (0-1 float)
  static inline float lcgRand(uint32_t &seed)
  {
    seed = seed * 1103515245u + 12345u;
    return (float)((seed >> 8) & 0x7FFF) / 32767.0f;
  }

  struct Flakes::Internal
  {
    int16_t *buffer = 0;
    int bufferSize = 0;
    int writeIndex = 0;

    // Freeze state
    bool frozen = false;

    // Feedback LP filter
    float fbLpState = 0.0f;

    // Self-modulation engine
    float envFollower = 0.0f;
    bool envWasAbove = false;
    uint32_t rngSeed = 42;

    // 4 modulation targets: delay, cutoff, feedback, scatter
    // Each has a current random value and a decay envelope
    float modValue[4];
    float modEnv[4];

    // Warble LFO
    float lfoPhase = 0.0f;

    // Noise filter state (one-pole LP for pink-ish)
    float noiseFilterState = 0.0f;

    void Init()
    {
      writeIndex = 0;
      frozen = false;
      fbLpState = 0.0f;
      envFollower = 0.0f;
      envWasAbove = false;
      rngSeed = 42;
      lfoPhase = 0.0f;
      noiseFilterState = 0.0f;
      for (int i = 0; i < 4; i++)
      {
        modValue[i] = 0.0f;
        modEnv[i] = 0.0f;
      }
    }
  };

  Flakes::Flakes()
  {
    addInput(mIn);
    addInput(mFreeze);
    addOutput(mOut);
    addParameter(mDepth);
    addParameter(mDelay);
    addParameter(mWarble);
    addParameter(mNoise);
    addParameter(mDryWet);

    mpInternal = new Internal();
    mpInternal->Init();
  }

  Flakes::~Flakes()
  {
    if (mpInternal->buffer)
      delete[] mpInternal->buffer;
    delete mpInternal;
  }

  float Flakes::allocateTimeUpTo(float seconds)
  {
    Internal &s = *mpInternal;
    if (s.buffer)
    {
      delete[] s.buffer;
      s.buffer = 0;
      s.bufferSize = 0;
    }
    int samples = (int)(seconds * globalConfig.sampleRate);
    if (samples < 256) samples = 256;
    s.buffer = new (std::nothrow) int16_t[samples];
    if (s.buffer)
    {
      memset(s.buffer, 0, samples * sizeof(int16_t));
      s.bufferSize = samples;
      s.writeIndex = 0;
    }
    return s.bufferSize * globalConfig.samplePeriod;
  }

  void Flakes::process()
  {
    Internal &s = *mpInternal;
    if (!s.buffer || s.bufferSize < 256) return;

    float *in = mIn.buffer();
    float *freeze = mFreeze.buffer();
    float *out = mOut.buffer();

    float depth = CLAMP(0.0f, 1.0f, mDepth.value());
    float delayParam = CLAMP(0.0f, 1.0f, mDelay.value());
    float warble = CLAMP(0.0f, 1.0f, mWarble.value());
    float noise = CLAMP(0.0f, 1.0f, mNoise.value());
    float dryWet = CLAMP(0.0f, 1.0f, mDryWet.value());

    float sr = globalConfig.sampleRate;
    int maxDelay = s.bufferSize;

    // Base delay in samples (Delay param maps 0-1 to 1ms - full buffer)
    float baseDelaySamples = (0.001f + delayParam * ((float)maxDelay / sr - 0.001f)) * sr;
    if (baseDelaySamples < 1.0f) baseDelaySamples = 1.0f;
    if (baseDelaySamples > (float)(maxDelay - 1)) baseDelaySamples = (float)(maxDelay - 1);

    // Feedback LP: more depth = darker (cutoff from 12kHz down to 800Hz)
    float fbLpFreq = 12000.0f * (1.0f - depth * 0.93f);
    float fbLpCoeff = fbLpFreq / sr;
    if (fbLpCoeff > 1.0f) fbLpCoeff = 1.0f;

    // Envelope follower coefficients
    float envAttack = 1.0f - expf(-1.0f / (0.003f * sr));
    float envRelease = 1.0f - expf(-1.0f / (0.01f * sr));

    // Modulation envelope decay (~300ms)
    float modDecay = 1.0f - expf(-1.0f / (0.3f * sr));

    // Warble LFO rate (0.1 - 2 Hz)
    float lfoRate = (0.1f + warble * 1.9f) / sr;

    // Noise LP for pink-ish color
    float noiseLpCoeff = 3000.0f / sr;

    // Amplitude threshold for self-mod trigger
    float threshold = 0.05f + (1.0f - depth) * 0.2f;

    int16_t *buf = s.buffer;

    for (int i = 0; i < FRAMELENGTH; i++)
    {
      float dry = in[i];

      // Freeze gate (per-sample)
      s.frozen = freeze[i] > 0.5f;

      // Read from buffer with delay + modulation
      // Warble LFO
      s.lfoPhase += lfoRate;
      if (s.lfoPhase > 1.0f) s.lfoPhase -= 1.0f;
      float lfoVal = sinf(s.lfoPhase * 2.0f * 3.14159f);
      float warbleMod = lfoVal * warble * baseDelaySamples * 0.02f;

      // Self-modulation: delay time wobble (target 0)
      float selfDelayMod = s.modValue[0] * s.modEnv[0] * depth * baseDelaySamples * 0.05f;

      float readDelay = baseDelaySamples + warbleMod + selfDelayMod;
      if (readDelay < 1.0f) readDelay = 1.0f;
      if (readDelay > (float)(maxDelay - 1)) readDelay = (float)(maxDelay - 1);

      float readPos = (float)s.writeIndex - readDelay;
      if (readPos < 0.0f) readPos += (float)maxDelay;

      float wet = bufReadInterp(buf, readPos, maxDelay);

      // Self-modulation: feedback amount variation (target 2)
      float fbMod = 1.0f + s.modValue[2] * s.modEnv[2] * depth * 0.3f;
      float fb = wet * depth * fbMod;

      // Feedback LP filter (depth-dependent darkening)
      s.fbLpState += (fb - s.fbLpState) * fbLpCoeff;
      fb = s.fbLpState;

      // Self-modulation: filter cutoff modulation (target 1)
      // Apply extra LP smoothing when self-mod fires
      float extraSmooth = s.modValue[1] * s.modEnv[1] * depth * 0.5f;
      if (extraSmooth > 0.01f)
      {
        float extraCoeff = fbLpCoeff * (1.0f - extraSmooth * 0.9f);
        if (extraCoeff < 0.001f) extraCoeff = 0.001f;
        fb = s.fbLpState + (fb - s.fbLpState) * extraCoeff;
      }

      // Soft clip feedback
      if (fb > 1.5f || fb < -1.5f)
        fb = fast_tanh(fb * 0.67f) * 1.5f;

      // Noise injection
      float n = 0.0f;
      if (noise > 0.001f)
      {
        float white = lcgRand(s.rngSeed) * 2.0f - 1.0f;
        s.noiseFilterState += (white - s.noiseFilterState) * noiseLpCoeff;
        n = s.noiseFilterState * noise * 0.3f;
      }

      // Write to buffer (input + feedback + noise) -- skip if frozen
      if (!s.frozen)
        bufWrite(buf, s.writeIndex, dry + fb + n);

      s.writeIndex++;
      if (s.writeIndex >= maxDelay) s.writeIndex = 0;

      // Envelope follower on wet signal (drives self-modulation)
      float absWet = fabsf(wet);
      if (absWet > s.envFollower)
        s.envFollower += (absWet - s.envFollower) * envAttack;
      else
        s.envFollower += (absWet - s.envFollower) * envRelease;

      // Self-modulation trigger: fire on rising threshold crossing
      bool envAbove = s.envFollower > threshold;
      if (envAbove && !s.envWasAbove)
      {
        // Fire new random modulation event on all 4 targets
        for (int t = 0; t < 4; t++)
        {
          s.modValue[t] = lcgRand(s.rngSeed) * 2.0f - 1.0f;
          s.modEnv[t] = 1.0f;
        }
      }
      s.envWasAbove = envAbove;

      // Decay modulation envelopes
      for (int t = 0; t < 4; t++)
        s.modEnv[t] -= s.modEnv[t] * modDecay;

      // Dry/wet mix
      float mixed = dry * (1.0f - dryWet) + wet * dryWet;

      // Output limiter
      out[i] = fast_tanh(mixed);
    }
  }

} // namespace stolmine
