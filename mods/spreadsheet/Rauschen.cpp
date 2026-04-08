#include "Rauschen.h"
#include <od/config.h>
#include <hal/ops.h>
#include <math.h>
#include <string.h>

namespace stolmine
{

  // --- Utility ---

  static inline uint32_t lcgNext(uint32_t &seed)
  {
    seed = seed * 1103515245u + 12345u;
    return seed;
  }

  static inline float lcgFloat(uint32_t &seed)
  {
    return (float)((lcgNext(seed) >> 8) & 0x7FFF) / 32767.0f;
  }

  static inline float lcgBipolar(uint32_t &seed)
  {
    return lcgFloat(seed) * 2.0f - 1.0f;
  }

  // --- Internal state ---

  struct Rauschen::Internal
  {
    // Pink noise (Kellet 3-pole + 3 extra for brown blend)
    float pinkB[6];

    // Crackle state
    float crackY1, crackY2;

    // Logistic map state
    float logX;

    // Henon map state
    float henX, henY;

    // White noise decimation state
    float whiteHeld = 0.0f;
    int whiteDecCounter = 0;

    // Velvet noise state
    int velvetCounter;
    int velvetImpulsePos;
    float velvetImpulseSign;

    // Gendy state (16 breakpoints)
    static const int kGendyPoints = 16;
    float gendyAmp[kGendyPoints];
    float gendyDur[kGendyPoints];
    int gendyIndex;
    float gendyPhase;

    // SVF state (post-generator filter)
    float svfS1, svfS2;

    // Output ring buffer for phase space graphic
    float outputRing[256];
    int ringPos;

    // RNG
    uint32_t rngSeed;

    void Init()
    {
      memset(pinkB, 0, sizeof(pinkB));
      whiteHeld = 0.0f;
      whiteDecCounter = 0;
      crackY1 = 0.1f;
      crackY2 = 0.0f;
      logX = 0.5f;
      henX = 0.1f;
      henY = 0.1f;
      velvetCounter = 0;
      velvetImpulsePos = -1;
      velvetImpulseSign = 1.0f;
      gendyIndex = 0;
      gendyPhase = 0.0f;
      for (int i = 0; i < kGendyPoints; i++)
      {
        gendyAmp[i] = 0.0f;
        gendyDur[i] = 0.002f; // ~2ms per segment default
      }
      svfS1 = 0.0f;
      svfS2 = 0.0f;
      memset(outputRing, 0, sizeof(outputRing));
      ringPos = 0;
      rngSeed = 42;
    }
  };

  Rauschen::Rauschen()
  {
    addInput(mVOct);
    addOutput(mOut);
    addParameter(mAlgorithm);
    addParameter(mParamX);
    addParameter(mParamY);
    addParameter(mFilterFreq);
    addParameter(mFilterQ);
    addParameter(mFilterMorph);
    addParameter(mLevel);

    mpInternal = new Internal();
    mpInternal->Init();
  }

  Rauschen::~Rauschen()
  {
    delete mpInternal;
  }

  float Rauschen::getOutputSample(int idx)
  {
    if (idx < 0 || idx > 255) return 0.0f;
    return mpInternal->outputRing[(mpInternal->ringPos + idx) & 255];
  }

  void Rauschen::process()
  {
    Internal &s = *mpInternal;
    float *voct = mVOct.buffer();
    float *out = mOut.buffer();

    int algo = CLAMP(0, 9, (int)(mAlgorithm.value() + 0.5f));
    float px = CLAMP(0.0f, 1.0f, mParamX.value());
    float py = CLAMP(0.0f, 1.0f, mParamY.value());
    float filterFreq = CLAMP(20.0f, 20000.0f, mFilterFreq.value());
    float filterQ = CLAMP(0.5f, 20.0f, mFilterQ.value());
    float morph = CLAMP(0.0f, 1.0f, mFilterMorph.value());
    float level = CLAMP(0.0f, 1.0f, mLevel.value());

    float sr = globalConfig.sampleRate;

    // V/Oct pitch offset (block-rate)
    float voctPitch = voct[0] * 10.0f;
    float freqMod = filterFreq * powf(2.0f, voctPitch);
    freqMod = CLAMP(20.0f, sr * 0.49f, freqMod);

    // SVF coefficients (ZDF topology, same as Parfait)
    float fNorm = 3.14159f * freqMod / sr;
    float sinVal = sinf(fNorm);
    float cosVal = cosf(fNorm);
    float g = (cosVal > 1e-10f) ? sinVal / cosVal : 100.0f;
    float r = 1.0f / filterQ;
    float h = 1.0f / (1.0f + r * g + g * g);

    for (int i = 0; i < FRAMELENGTH; i++)
    {
      float sample = 0.0f;

      switch (algo)
      {
      case 0: // White -- X: decimation (sample rate reduction), Y: bit crush
      {
        // Decimation: hold sample for N frames (X=0: full rate, X=1: /64)
        int decFactor = 1 + (int)(px * 63.0f);
        s.whiteDecCounter++;
        if (s.whiteDecCounter >= decFactor)
        {
          s.whiteDecCounter = 0;
          s.whiteHeld = lcgBipolar(s.rngSeed);
        }
        sample = s.whiteHeld;
        // Bit crush: Y=0: full resolution, Y=1: 2 levels
        if (py > 0.01f)
        {
          float levels = 256.0f * (1.0f - py) + 2.0f * py; // 256 down to 2
          sample = floorf(sample * levels + 0.5f) / levels;
        }
        break;
      }

      case 1: // Pink -- X: tilt (white-to-brown), Y: resonance (feedback on integrators)
      {
        float white = lcgBipolar(s.rngSeed);
        // Resonance: feed output back into input (Y=0: none, Y=1: strong)
        float fbk = s.pinkB[5] * py * 0.8f;
        float excited = white + fbk;
        // 3-pole pink (fixed coefficients)
        s.pinkB[0] = 0.99765f * s.pinkB[0] + excited * 0.0990460f;
        s.pinkB[1] = 0.96300f * s.pinkB[1] + excited * 0.2965164f;
        s.pinkB[2] = 0.57000f * s.pinkB[2] + excited * 1.0526913f;
        float pink = (s.pinkB[0] + s.pinkB[1] + s.pinkB[2] + excited * 0.1848f) * 0.22f;
        // Extra integration stages for brown (X blends pink -> brown)
        s.pinkB[3] += (pink - s.pinkB[3]) * 0.02f;
        s.pinkB[4] += (s.pinkB[3] - s.pinkB[4]) * 0.02f;
        float brown = s.pinkB[4] * 10.0f;
        sample = pink * (1.0f - px) + brown * px;
        s.pinkB[5] = sample; // store for feedback
        break;
      }

      case 2: // Dust
      {
        float density = 1.0f + px * 9999.0f; // 1-10000 Hz
        float amp = 0.1f + py * 0.9f;
        if (lcgFloat(s.rngSeed) < density / sr)
          sample = lcgBipolar(s.rngSeed) * amp;
        else
          sample = 0.0f;
        break;
      }

      case 3: // Particle (dust -> resonant bandpass)
      {
        float density = 0.001f + px * 0.3f;
        float spread = py * 48.0f; // 0-48 semitones
        float pFreq = freqMod / sr;
        if (pFreq > 0.49f) pFreq = 0.49f;
        float pQ = filterQ;
        // Inline particle: random impulse -> SVF bandpass
        if (lcgFloat(s.rngSeed) < density)
        {
          float impulse = lcgBipolar(s.rngSeed);
          // Randomize frequency within spread range
          float freqRand = pFreq * powf(2.0f, lcgBipolar(s.rngSeed) * spread / 12.0f);
          if (freqRand > 0.49f) freqRand = 0.49f;
          if (freqRand < 0.001f) freqRand = 0.001f;
          // Quick one-shot SVF tick with the impulse
          float pg = 3.14159f * freqRand;
          float pr = 1.0f / pQ;
          float ph = 1.0f / (1.0f + pr * pg + pg * pg);
          float hp = (impulse - pr * s.svfS1 - pg * s.svfS1 - s.svfS2) * ph;
          float bp = pg * hp + s.svfS1;
          s.svfS1 = pg * hp + bp;
          s.svfS2 = pg * bp + s.svfS2;
          // Note: particle uses its own SVF excitation, post-filter is separate
          sample = bp * 2.0f;
        }
        else
        {
          // Decay existing resonance
          float hp = (0.0f - r * s.svfS1 - g * s.svfS1 - s.svfS2) * h;
          float bp = g * hp + s.svfS1;
          s.svfS1 = g * hp + bp;
          s.svfS2 = g * bp + s.svfS2;
          sample = bp * 2.0f;
        }
        // Skip the post-filter for particle (it has its own resonance)
        sample *= level;
        s.outputRing[s.ringPos] = sample;
        s.ringPos = (s.ringPos + 1) & 255;
        out[i] = sample;
        continue; // skip post-filter below
      }

      case 4: // Crackle -- X: chaos (p), Y: damping (attractor shape)
      {
        float p = 1.0f + px; // 1.0 to 2.0
        float damp = 0.05f * (1.0f - py * 0.95f); // Y=0: normal, Y=1: nearly undamped
        float yy = p * s.crackY1 - s.crackY2 - damp;
        if (yy > 1.0f) yy = 1.0f;
        if (yy < -1.0f) yy = -1.0f;
        s.crackY2 = s.crackY1;
        s.crackY1 = yy;
        sample = yy;
        break;
      }

      case 5: // Logistic -- X: growth (r), Y: slew (smooths between iterations)
      {
        float rr = 3.0f + px; // 3.0 to 4.0
        float target = rr * s.logX * (1.0f - s.logX);
        // Reseed if escaped
        if (target <= 0.0f || target >= 1.0f || target != target)
          target = 0.5f + lcgFloat(s.rngSeed) * 0.01f;
        // Slew: Y=0 instant (raw map), Y=1 heavy smoothing
        float slewCoeff = 1.0f - py * 0.99f;
        s.logX += (target - s.logX) * slewCoeff;
        sample = s.logX * 2.0f - 1.0f;
        break;
      }

      case 6: // Henon
      {
        float a = 1.0f + px * 0.4f;  // 1.0 to 1.4
        float b = 0.1f + py * 0.3f;  // 0.1 to 0.4
        float xn = 1.0f - a * s.henX * s.henX + s.henY;
        float yn = b * s.henX;
        s.henX = xn;
        s.henY = yn;
        // Clamp if diverged
        if (s.henX > 2.0f || s.henX < -2.0f || s.henX != s.henX)
        {
          s.henX = 0.1f + lcgFloat(s.rngSeed) * 0.01f;
          s.henY = 0.1f;
        }
        sample = s.henX * 0.5f; // scale to ~[-1,1]
        break;
      }

      case 7: // Clocked noise (S&H with interpolation)
      {
        // Simple clocked S&H: X = frequency, no polyBLEP for simplicity
        float freq = (0.5f + px * 999.5f) / sr; // 0.5-1000 Hz normalized
        s.gendyPhase += freq;
        if (s.gendyPhase >= 1.0f)
        {
          s.gendyPhase -= 1.0f;
          s.crackY2 = s.crackY1; // reuse crackle state as prev/next
          s.crackY1 = lcgBipolar(s.rngSeed);
        }
        // Crossfade stepped -> interpolated based on Y
        float stepped = s.crackY1;
        float interp = s.crackY2 + (s.crackY1 - s.crackY2) * s.gendyPhase;
        sample = stepped * (1.0f - py) + interp * py;
        break;
      }

      case 8: // Velvet -- X: density, Y: width (single click to wider pulse)
      {
        float density = 10.0f + px * 9990.0f; // 10-10000 Hz
        int windowSize = (int)(sr / density);
        if (windowSize < 1) windowSize = 1;
        // Pulse width: Y=0 single sample, Y=1 up to half the window
        int pulseWidth = 1 + (int)(py * (float)(windowSize / 2));

        if (s.velvetCounter == 0)
        {
          s.velvetImpulsePos = (int)(lcgFloat(s.rngSeed) * (float)(windowSize - pulseWidth));
          s.velvetImpulseSign = (lcgFloat(s.rngSeed) > 0.5f) ? 1.0f : -1.0f;
        }

        int dist = s.velvetCounter - s.velvetImpulsePos;
        if (dist >= 0 && dist < pulseWidth)
        {
          // Raised cosine pulse shape
          float t = (float)dist / (float)(pulseWidth > 1 ? pulseWidth - 1 : 1);
          float env = 0.5f * (1.0f - cosf(t * 2.0f * 3.14159f));
          if (pulseWidth == 1) env = 1.0f;
          sample = s.velvetImpulseSign * env;
        }
        else
          sample = 0.0f;

        s.velvetCounter++;
        if (s.velvetCounter >= windowSize)
          s.velvetCounter = 0;
        break;
      }

      case 9: // Gendy (Xenakis stochastic synthesis)
      {
        float ampScale = px;
        float durScale = py;
        int N = Internal::kGendyPoints;

        // Interpolate between current and next breakpoint
        int nextIdx = (s.gendyIndex + 1) % N;
        float t = s.gendyPhase;
        sample = s.gendyAmp[s.gendyIndex] * (1.0f - t) + s.gendyAmp[nextIdx] * t;

        // Advance phase
        float durSamples = s.gendyDur[s.gendyIndex] * sr;
        if (durSamples < 1.0f) durSamples = 1.0f;
        s.gendyPhase += 1.0f / durSamples;

        if (s.gendyPhase >= 1.0f)
        {
          s.gendyPhase -= 1.0f;
          s.gendyIndex = nextIdx;

          // Perturb the NEXT breakpoint
          int pertIdx = (s.gendyIndex + 1) % N;
          s.gendyAmp[pertIdx] += lcgBipolar(s.rngSeed) * ampScale * 0.3f;
          // Mirror at boundaries
          if (s.gendyAmp[pertIdx] > 1.0f) s.gendyAmp[pertIdx] = 2.0f - s.gendyAmp[pertIdx];
          if (s.gendyAmp[pertIdx] < -1.0f) s.gendyAmp[pertIdx] = -2.0f - s.gendyAmp[pertIdx];

          s.gendyDur[pertIdx] += lcgBipolar(s.rngSeed) * durScale * 0.001f;
          if (s.gendyDur[pertIdx] < 0.0002f) s.gendyDur[pertIdx] = 0.0002f;
          if (s.gendyDur[pertIdx] > 0.05f) s.gendyDur[pertIdx] = 0.05f;
        }
        break;
      }
      }

      // Post-generator SVF morph filter
      if (morph > 0.01f)
      {
        float hp = (sample - r * s.svfS1 - g * s.svfS1 - s.svfS2) * h;
        float bp = g * hp + s.svfS1;
        s.svfS1 = g * hp + bp;
        float lp = g * bp + s.svfS2;
        s.svfS2 = g * bp + lp;

        // Morph: 0.01-0.25=LP, 0.25-0.50=BP, 0.50-0.75=HP, 0.75-1.0=notch
        float m = (morph - 0.01f) / 0.99f;
        float lp_g, bp_g, hp_g;
        if (m < 0.333f)
        {
          float t = m * 3.0f;
          lp_g = 1.0f - t; bp_g = t; hp_g = 0.0f;
        }
        else if (m < 0.666f)
        {
          float t = (m - 0.333f) * 3.0f;
          lp_g = 0.0f; bp_g = 1.0f - t; hp_g = t;
        }
        else
        {
          float t = (m - 0.666f) * 3.0f;
          lp_g = t; bp_g = 0.0f; hp_g = 1.0f;
        }
        sample = lp * lp_g + bp * bp_g + hp * hp_g;
      }

      sample *= level;

      // Write to ring buffer for viz
      s.outputRing[s.ringPos] = sample;
      s.ringPos = (s.ringPos + 1) & 255;

      out[i] = sample;
    }
  }

} // namespace stolmine
