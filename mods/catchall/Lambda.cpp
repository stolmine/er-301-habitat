#include "Lambda.h"
#include <od/config.h>
#include <hal/ops.h>
#include <math.h>
#include <string.h>

namespace stolmine
{

  // --- LCG PRNG ---
  static inline uint32_t lcgNext(uint32_t &seed)
  {
    seed = seed * 1103515245u + 12345u;
    return seed;
  }

  static inline float lcgFloat(uint32_t &rng)
  {
    return (float)((lcgNext(rng) >> 8) & 0x7FFF) / 32767.0f;
  }

  static inline float lcgBipolar(uint32_t &rng)
  {
    return lcgFloat(rng) * 2.0f - 1.0f;
  }

  // --- Cytomic SVF section (from Sfera) ---
  struct SvfSection
  {
    float ic1eq, ic2eq;
    float g, k, m0, m1, m2;
    float tg, tk, tm0, tm1, tm2;

    void reset()
    {
      ic1eq = ic2eq = 0.0f;
      g = tg = 0.1f;
      k = tk = 1.0f;
      m0 = tm0 = 0.0f;
      m1 = tm1 = 0.0f;
      m2 = tm2 = 1.0f;
    }

    inline void slew(float rate)
    {
      g += (tg - g) * rate;
      k += (tk - k) * rate;
      m0 += (tm0 - m0) * rate;
      m1 += (tm1 - m1) * rate;
      m2 += (tm2 - m2) * rate;
    }

    inline float process(float v0)
    {
      float a1 = 1.0f / (1.0f + g * (g + k));
      float a2 = g * a1;
      float a3 = g * a2;

      float v3 = v0 - ic2eq;
      float v1 = a1 * ic1eq + a2 * v3;
      float v2 = ic2eq + a2 * ic1eq + a3 * v3;

      ic1eq = 2.0f * v1 - ic1eq;
      ic2eq = 2.0f * v2 - ic2eq;

      if (ic1eq > 4.0f) ic1eq = 4.0f;
      if (ic1eq < -4.0f) ic1eq = -4.0f;
      if (ic2eq > 4.0f) ic2eq = 4.0f;
      if (ic2eq < -4.0f) ic2eq = -4.0f;

      return m0 * v0 + m1 * v1 + m2 * v2;
    }

    void setFromPoleZero(float pAngle, float pRadius, float zAngle, float zRadius, float sr)
    {
      float freq = pAngle * sr / (2.0f * 3.14159f);
      if (freq < 20.0f) freq = 20.0f;
      if (freq > sr * 0.45f) freq = sr * 0.45f;

      float w = 3.14159f * freq / sr;
      g = tg = w / (1.0f + w * w * 0.333f);

      float q = 0.5f / (1.001f - pRadius);
      if (q < 0.5f) q = 0.5f;
      if (q > 20.0f) q = 20.0f;
      k = tk = 1.0f / q;

      if (zRadius < 0.01f)
      {
        m0 = tm0 = 0.0f; m1 = tm1 = 0.0f; m2 = tm2 = 1.0f;
      }
      else if (zAngle > 2.5f)
      {
        m0 = tm0 = 0.0f; m1 = tm1 = 0.0f; m2 = tm2 = 1.0f;
      }
      else if (zAngle < 0.1f)
      {
        m0 = tm0 = 1.0f; m1 = tm1 = -k; m2 = tm2 = -1.0f;
      }
      else
      {
        float ratio = zAngle / (pAngle + 0.001f);
        if (ratio > 0.8f && ratio < 1.2f)
        {
          m0 = tm0 = 1.0f; m1 = tm1 = -k; m2 = tm2 = 0.0f;
        }
        else
        {
          m0 = tm0 = 0.0f; m1 = tm1 = 1.0f; m2 = tm2 = 0.0f;
        }
      }
    }
  };

  // --- Internal state ---
  struct Lambda::Internal
  {
    float wavetable[kLambdaFrames][kLambdaWaveSize];

    struct SvfParams { float g, k, m0, m1, m2; };
    struct FilterConfig
    {
      SvfParams sections[kLambdaMaxSections];
      int numSections;
      float gain;
    };
    FilterConfig filterBank[kLambdaFrames];

    SvfSection svfs[kLambdaMaxSections];
    float phase;
    float vizWave[kLambdaWaveSize];
    float envelope;
    int lastSeed;

    // DC blocker state
    float dcX, dcY;

    void Init()
    {
      phase = 0.0f;
      envelope = 0.0f;
      lastSeed = -1;
      dcX = dcY = 0.0f;
      memset(wavetable, 0, sizeof(wavetable));
      memset(vizWave, 0, sizeof(vizWave));
      memset(filterBank, 0, sizeof(filterBank));
      for (int i = 0; i < kLambdaMaxSections; i++)
        svfs[i].reset();
    }

    void generateFromSeed(int seed)
    {
      float sr = globalConfig.sampleRate;
      uint32_t rng = (uint32_t)seed * 2654435761u + 1u;

      // --- Generate harmonic DNA ---
      static const int kHarmonics = 16;
      float baseAmp[kHarmonics];
      float basePhase[kHarmonics];

      for (int h = 0; h < kHarmonics; h++)
      {
        // 1/(1+h) rolloff weighted by random factor
        float rolloff = 1.0f / (1.0f + (float)h);
        baseAmp[h] = lcgFloat(rng) * rolloff;
        basePhase[h] = lcgFloat(rng) * 6.28318f;
      }

      // --- Generate wavetable frames ---
      for (int f = 0; f < kLambdaFrames; f++)
      {
        uint32_t frameRng = (uint32_t)seed * 48271u + (uint32_t)f * 16807u + 1u;
        float morphPos = (float)f / (float)(kLambdaFrames - 1);

        float frameAmp[kHarmonics];
        for (int h = 0; h < kHarmonics; h++)
        {
          float walk = lcgBipolar(frameRng) * 0.25f;
          // Odd/even harmonic bias shifts across scan
          float oddEvenBias = (h % 2 == 0) ? morphPos : (1.0f - morphPos);
          frameAmp[h] = baseAmp[h] * (0.7f + 0.3f * oddEvenBias) + walk * baseAmp[h];
          if (frameAmp[h] < 0.0f) frameAmp[h] = 0.0f;
        }

        // Render via additive synthesis
        float peak = 0.0001f;
        float dcSum = 0.0f;
        for (int s = 0; s < kLambdaWaveSize; s++)
        {
          float t = (float)s / (float)kLambdaWaveSize * 6.28318f;
          float val = 0.0f;
          for (int h = 0; h < kHarmonics; h++)
            val += frameAmp[h] * sinf((float)(h + 1) * t + basePhase[h]);
          wavetable[f][s] = val;
          dcSum += val;
          float absVal = val < 0 ? -val : val;
          if (absVal > peak) peak = absVal;
        }

        // Remove DC and normalize
        float dcOffset = dcSum / (float)kLambdaWaveSize;
        float invPeak = 1.0f / peak;
        for (int s = 0; s < kLambdaWaveSize; s++)
          wavetable[f][s] = (wavetable[f][s] - dcOffset) * invPeak;
      }

      // --- Generate filter bank ---
      uint32_t filterSeedRng = (uint32_t)seed * 1664525u + 7u;
      int numSections = 1 + (int)(lcgNext(filterSeedRng) % kLambdaMaxSections);

      for (int f = 0; f < kLambdaFrames; f++)
      {
        uint32_t fRng = (uint32_t)seed * 69069u + (uint32_t)f * 48271u + 13u;
        FilterConfig &fc = filterBank[f];
        fc.numSections = numSections;

        float gainProduct = 1.0f;
        for (int s = 0; s < numSections; s++)
        {
          // Pole angle: 20Hz - 8kHz mapped to 0.0026 - 1.047 radians at 48kHz
          float pAngle = 0.02f + lcgFloat(fRng) * 1.03f;
          float pRadius = 0.3f + lcgFloat(fRng) * 0.62f;

          // Zero type
          float zeroType = lcgFloat(fRng);
          float zAngle = 0.0f, zRadius = 0.0f;
          if (zeroType < 0.30f)
          {
            // Allpole - no zeros
          }
          else if (zeroType < 0.55f)
          {
            // LP: zeros at Nyquist
            zAngle = 3.14159f;
            zRadius = 0.9f;
          }
          else if (zeroType < 0.70f)
          {
            // HP: zeros at DC
            zAngle = 0.02f;
            zRadius = 0.9f;
          }
          else
          {
            // Notch near pole
            zAngle = pAngle * (0.8f + lcgFloat(fRng) * 0.4f);
            zRadius = 0.8f + lcgFloat(fRng) * 0.19f;
          }

          SvfSection tmp;
          tmp.setFromPoleZero(pAngle, pRadius, zAngle, zRadius, sr);
          fc.sections[s] = {tmp.g, tmp.k, tmp.m0, tmp.m1, tmp.m2};

          // Gain compensation per section
          float sectionGain = 1.0f / (1.0f + pRadius * pRadius * 2.0f);
          gainProduct *= sectionGain;
        }
        fc.gain = gainProduct;

        // Bypass unused sections
        for (int s = numSections; s < kLambdaMaxSections; s++)
          fc.sections[s] = {0.1f, 2.0f, 1.0f, 0.0f, 0.0f};
      }
    }
  };

  // --- Constructor / Destructor ---
  Lambda::Lambda()
  {
    addInput(mVOct);
    addOutput(mOut);
    addParameter(mSeed);
    addParameter(mScan);
    addParameter(mFundamental);
    addParameter(mCutoff);
    addParameter(mLevel);

    mpInternal = new Internal();
    mpInternal->Init();
  }

  Lambda::~Lambda()
  {
    delete mpInternal;
  }

  // --- SWIG-visible accessors ---
  float Lambda::getWaveformSample(int idx)
  {
    if (idx < 0 || idx >= kLambdaWaveSize) return 0.0f;
    return mpInternal->vizWave[idx];
  }

  int Lambda::getCurrentSeed()
  {
    return mpInternal->lastSeed;
  }

  float Lambda::getEnvelope()
  {
    return mpInternal->envelope;
  }

  // --- Process ---
  void Lambda::process()
  {
    Internal &s = *mpInternal;
    float *voct = mVOct.buffer();
    float *out = mOut.buffer();

    // Detect seed change
    int seed = CLAMP(0, 999, (int)(mSeed.value() + 0.5f));
    if (seed != s.lastSeed)
    {
      s.generateFromSeed(seed);
      s.lastSeed = seed;
      for (int i = 0; i < kLambdaMaxSections; i++)
        s.svfs[i].reset();
      s.dcX = s.dcY = 0.0f;
    }

    float sr = globalConfig.sampleRate;
    float f0 = CLAMP(0.1f, sr * 0.49f, mFundamental.value());
    float scan = CLAMP(0.0f, 1.0f, mScan.value());
    float cutoff = CLAMP(20.0f, 20000.0f, mCutoff.value());
    float level = CLAMP(0.0f, 1.0f, mLevel.value());

    // V/Oct (block rate)
    float pitch = voct[0] * 10.0f;
    float freq = f0 * powf(2.0f, pitch);
    if (freq > sr * 0.49f) freq = sr * 0.49f;
    float phaseInc = freq / sr;

    // Wavetable scan position
    float wavePos = scan * (float)(kLambdaFrames - 1);
    int waveIdx0 = (int)wavePos;
    int waveIdx1 = waveIdx0 + 1;
    if (waveIdx1 >= kLambdaFrames) waveIdx1 = kLambdaFrames - 1;
    float waveFrac = wavePos - (float)waveIdx0;

    // Filter scan position
    float filtPos = scan * (float)(kLambdaFrames - 1);
    int filtIdx0 = (int)filtPos;
    int filtIdx1 = filtIdx0 + 1;
    if (filtIdx1 >= kLambdaFrames) filtIdx1 = kLambdaFrames - 1;
    float filtFrac = filtPos - (float)filtIdx0;

    // Set SVF targets from interpolated filter
    Internal::FilterConfig &fc0 = s.filterBank[filtIdx0];
    Internal::FilterConfig &fc1 = s.filterBank[filtIdx1];
    int nSections = fc0.numSections;
    float cutoffRatio = cutoff / 1000.0f;

    for (int j = 0; j < nSections; j++)
    {
      float ig = fc0.sections[j].g * (1.0f - filtFrac) + fc1.sections[j].g * filtFrac;
      ig *= cutoffRatio;
      if (ig > 10.0f) ig = 10.0f;
      s.svfs[j].tg = ig;
      s.svfs[j].tk = fc0.sections[j].k * (1.0f - filtFrac) + fc1.sections[j].k * filtFrac;
      s.svfs[j].tm0 = fc0.sections[j].m0 * (1.0f - filtFrac) + fc1.sections[j].m0 * filtFrac;
      s.svfs[j].tm1 = fc0.sections[j].m1 * (1.0f - filtFrac) + fc1.sections[j].m1 * filtFrac;
      s.svfs[j].tm2 = fc0.sections[j].m2 * (1.0f - filtFrac) + fc1.sections[j].m2 * filtFrac;
    }

    float gain = fc0.gain * (1.0f - filtFrac) + fc1.gain * filtFrac;

    // Populate viz waveform (block rate)
    for (int i = 0; i < kLambdaWaveSize; i++)
      s.vizWave[i] = s.wavetable[waveIdx0][i] * (1.0f - waveFrac) +
                     s.wavetable[waveIdx1][i] * waveFrac;

    // Envelope follower coefficients
    float envAttack = 1.0f - expf(-1.0f / (sr * 0.001f));
    float envRelease = 1.0f - expf(-1.0f / (sr * 0.150f));
    float env = s.envelope;

    // DC blocker coefficient (~10Hz highpass)
    float dcCoeff = 1.0f - (6.28318f * 10.0f / sr);
    if (dcCoeff < 0.9f) dcCoeff = 0.9f;

    // Per-sample loop
    for (int i = 0; i < FRAMELENGTH; i++)
    {
      // Phase accumulator
      s.phase += phaseInc;
      s.phase -= floorf(s.phase);

      // Read wavetable with bilinear interpolation
      float pos = s.phase * (float)(kLambdaWaveSize);
      int si0 = (int)pos;
      float sfrac = pos - (float)si0;
      int si1 = (si0 + 1) & (kLambdaWaveSize - 1); // power-of-2 wrap
      si0 &= (kLambdaWaveSize - 1);

      float s0 = s.wavetable[waveIdx0][si0] * (1.0f - waveFrac) +
                 s.wavetable[waveIdx1][si0] * waveFrac;
      float s1 = s.wavetable[waveIdx0][si1] * (1.0f - waveFrac) +
                 s.wavetable[waveIdx1][si1] * waveFrac;
      float x = s0 + (s1 - s0) * sfrac;

      // SVF cascade
      for (int j = 0; j < nSections; j++)
      {
        s.svfs[j].slew(0.02f);
        x = s.svfs[j].process(x);
      }

      // Emergency recovery
      if (x != x || x > 8.0f || x < -8.0f)
      {
        for (int j = 0; j < kLambdaMaxSections; j++)
          s.svfs[j].ic1eq = s.svfs[j].ic2eq = 0.0f;
        x = 0.0f;
      }

      // DC blocker
      float dcOut = x - s.dcX + dcCoeff * s.dcY;
      s.dcX = x;
      s.dcY = dcOut;
      x = dcOut;

      float y = x * gain * level;
      out[i] = y;

      // Envelope follower
      float abs_y = y < 0 ? -y : y;
      float coeff = abs_y > env ? envAttack : envRelease;
      env += (abs_y - env) * coeff;
    }
    s.envelope = env;
  }

} // namespace stolmine
