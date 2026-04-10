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

    // Lorenz state
    float lorX, lorY, lorZ;


    // SVF state (post-generator filter)
    float svfS1, svfS2;

    // Output ring buffer for phase space graphic
    float outputRing[256];
    int ringPos;

    // RNG
    uint32_t rngSeed;

    // Current algorithm (for graphic to detect switches)
    int currentAlgo;

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
      lorX = 0.1f; lorY = 0.0f; lorZ = 0.0f;
      svfS1 = 0.0f;
      svfS2 = 0.0f;
      memset(outputRing, 0, sizeof(outputRing));
      ringPos = 0;
      rngSeed = 42;
      currentAlgo = -1;
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

  int Rauschen::getCurrentAlgorithm()
  {
    return mpInternal->currentAlgo;
  }

  void Rauschen::process()
  {
    Internal &s = *mpInternal;
    float *voct = mVOct.buffer();
    float *out = mOut.buffer();

    int algo = CLAMP(0, 10, (int)(mAlgorithm.value() + 0.5f));
    s.currentAlgo = algo;
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
        // Decimation: exponential curve so midrange X is already audible
        int decFactor = 1 + (int)(px * px * 63.0f);
        s.whiteDecCounter++;
        if (s.whiteDecCounter >= decFactor)
        {
          s.whiteDecCounter = 0;
          s.whiteHeld = lcgBipolar(s.rngSeed);
        }
        sample = s.whiteHeld;
        // Bit crush: exponential curve (Y=0: full, Y=0.5: ~16 levels, Y=1: 2 levels)
        if (py > 0.01f)
        {
          float crush = py * py; // quadratic for more usable midrange
          float levels = 256.0f * (1.0f - crush) + 2.0f * crush;
          sample = floorf(sample * levels + 0.5f) / levels;
        }
        break;
      }

      case 1: // Pink -- X: tilt (white-to-brown), Y: color (spectral thinning)
      {
        float white = lcgBipolar(s.rngSeed);
        // 3-pole pink (Kellet method)
        s.pinkB[0] = 0.99765f * s.pinkB[0] + white * 0.0990460f;
        s.pinkB[1] = 0.96300f * s.pinkB[1] + white * 0.2965164f;
        s.pinkB[2] = 0.57000f * s.pinkB[2] + white * 1.0526913f;
        float pink = (s.pinkB[0] + s.pinkB[1] + s.pinkB[2] + white * 0.1848f) * 0.25f;
        // Extra integration for brown tilt (X=0: pink, X=1: brown)
        float tiltCoeff = 0.02f + px * px * 0.18f;
        s.pinkB[3] += (pink - s.pinkB[3]) * tiltCoeff;
        s.pinkB[4] += (s.pinkB[3] - s.pinkB[4]) * tiltCoeff;
        float brown = s.pinkB[4] * 4.0f;
        float base = pink * (1.0f - px) + brown * px;
        // Y: spectral thinning via sample-and-hold decimation
        // Y=0: full bandwidth, Y=1: hold for up to 64 samples
        int holdLen = 1 + (int)(py * py * 63.0f);
        s.whiteDecCounter++;
        if (s.whiteDecCounter >= holdLen)
        {
          s.whiteDecCounter = 0;
          s.pinkB[5] = base;
        }
        sample = s.pinkB[5];
        break;
      }

      case 2: // Dust
      {
        // Log-scaled density: X=0 -> 1 Hz, X=0.5 -> 100 Hz, X=1 -> 10000 Hz
        float density = powf(10.0f, px * 4.0f); // 1 to 10000
        float amp = 0.1f + py * 0.9f;
        if (lcgFloat(s.rngSeed) < density / sr)
          sample = lcgBipolar(s.rngSeed) * amp;
        else
          sample = 0.0f;
        break;
      }

      case 3: // Particle (dust -> resonant bandpass)
      {
        // Log-scaled density: X=0 sparse pings, X=1 dense cloud
        float density = 0.001f + px * px * 0.3f;
        float spread = py * py * 48.0f; // quadratic: more resolution at low spread
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

      case 4: // Crackle -- X: chaos (p), Y: energy injection
      {
        // X: 1.0 to 2.0 -- chaos parameter
        float p = 1.0f + px;
        // Y: energy constant (0.01 gentle to 0.3 aggressive)
        float energy = 0.01f + py * 0.29f;
        // SC-style crackle: abs() in state creates chaotic attractor
        float yy = fabsf(p * s.crackY1 - s.crackY2 - energy);
        // NaN/divergence recovery
        if (yy > 2.0f || yy != yy)
        {
          yy = 0.1f + lcgFloat(s.rngSeed) * 0.01f;
          s.crackY2 = 0.0f;
        }
        s.crackY2 = s.crackY1;
        s.crackY1 = yy;
        // Output centered around zero
        sample = yy * 2.0f - 1.0f;
        break;
      }

      case 5: // Logistic -- X: growth (r), Y: iteration rate (pitch)
      {
        // X=0 -> r=3.45 (onset of chaos), X=1 -> r=4.0 (full chaos)
        float rr = 3.45f + px * 0.55f;
        // Y: hold length. Y=0 every sample (noise), Y=1 hold ~512 samples (pitched)
        int holdLen = 1 + (int)(py * py * 511.0f);
        s.whiteDecCounter++;
        if (s.whiteDecCounter >= holdLen)
        {
          s.whiteDecCounter = 0;
          float target = rr * s.logX * (1.0f - s.logX);
          if (target <= 0.0f || target >= 1.0f || target != target)
            target = 0.5f + lcgFloat(s.rngSeed) * 0.01f;
          s.logX = target;
        }
        sample = s.logX * 2.0f - 1.0f;
        break;
      }

      case 6: // Henon -- X: chaos (a), Y: coupling (b)
      {
        // X: a = 1.15 to 1.45, linear -- spans periodic islands through full chaos
        float a = 1.15f + px * 0.30f;
        // Y: b = 0.0 to 0.6 -- wider range, 0 = 1D map, higher = stronger 2D coupling
        float b = py * 0.6f;
        float xn = 1.0f - a * s.henX * s.henX + s.henY;
        float yn = b * s.henX;
        s.henX = xn;
        s.henY = yn;
        // Fold if diverged (keeps energy, doesn't just reset)
        if (s.henX > 1.5f) s.henX = 3.0f - s.henX;
        if (s.henX < -1.5f) s.henX = -3.0f - s.henX;
        if (s.henX != s.henX)
        {
          s.henX = 0.1f + lcgFloat(s.rngSeed) * 0.01f;
          s.henY = 0.1f;
        }
        sample = s.henX * 0.65f;
        break;
      }

      case 7: // Clocked noise (S&H with interpolation)
      {
        // Log-scaled frequency: X=0 -> 0.5 Hz, X=0.5 -> ~22 Hz, X=1 -> 1000 Hz
        float freq = (0.5f * powf(2000.0f, px)) / sr;
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
        // Log-scaled density: X=0 -> 10 Hz, X=0.5 -> 316 Hz, X=1 -> 10000 Hz
        float density = 10.0f * powf(1000.0f, px);
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
        // X: amplitude chaos, Y: duration chaos
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

          // Perturb multiple breakpoints ahead for less periodicity
          for (int k = 1; k <= 3; k++)
          {
            int pertIdx = (s.gendyIndex + k) % N;

            // Amplitude: large steps, mirror-fold at boundaries
            float ampStep = lcgBipolar(s.rngSeed) * ampScale * 0.7f;
            // Occasional large jump (1 in 4 chance, Levy-like)
            if ((lcgNext(s.rngSeed) & 3) == 0)
              ampStep *= 2.5f;
            s.gendyAmp[pertIdx] += ampStep;
            if (s.gendyAmp[pertIdx] > 1.0f) s.gendyAmp[pertIdx] = 2.0f - s.gendyAmp[pertIdx];
            if (s.gendyAmp[pertIdx] < -1.0f) s.gendyAmp[pertIdx] = -2.0f - s.gendyAmp[pertIdx];

            // Duration: proportional perturbation (short stays short-ish, long stays long-ish)
            float durStep = s.gendyDur[pertIdx] * lcgBipolar(s.rngSeed) * durScale * 0.6f;
            s.gendyDur[pertIdx] += durStep;
            if (s.gendyDur[pertIdx] < 0.00005f) s.gendyDur[pertIdx] = 0.00005f; // ~20kHz
            if (s.gendyDur[pertIdx] > 0.1f) s.gendyDur[pertIdx] = 0.1f;         // ~10Hz
          }
        }
        break;
      }

      case 10: // Lorenz attractor
      {
        float sigma = 10.0f;
        float beta = 2.6667f; // 8/3
        // X: chaos -- rho 22-100, onset ~24.7
        float rho = 22.0f + px * 78.0f;
        // Y: speed via sub-stepping. Fixed small dt keeps integration accurate.
        // 1-20 sub-steps per sample: ~50Hz to ~1kHz at 48kHz
        float dt = 0.002f;
        int steps = 1 + (int)(py * 19.0f);
        for (int k = 0; k < steps; k++)
        {
          float dx = sigma * (s.lorY - s.lorX);
          float dy = s.lorX * (rho - s.lorZ) - s.lorY;
          float dz = s.lorX * s.lorY - beta * s.lorZ;
          s.lorX += dx * dt;
          s.lorY += dy * dt;
          s.lorZ += dz * dt;
        }
        if (s.lorX != s.lorX || s.lorX > 100.0f || s.lorX < -100.0f)
        {
          s.lorX = 0.1f + lcgFloat(s.rngSeed) * 0.01f;
          s.lorY = 0.0f; s.lorZ = 25.0f;
        }
        sample = s.lorX * 0.05f;
        break;
      }

      }

      // Post-generator SVF morph filter
      // 0=off, 0.01-0.08=LP, 0.08-0.17=LP>BP, 0.17-0.33=BP,
      // 0.33-0.42=BP>HP, 0.42-0.58=HP, 0.58-0.67=HP>NT, 0.67-1.0=Notch
      if (morph > 0.005f)
      {
        float hp_out = (sample - r * s.svfS1 - g * s.svfS1 - s.svfS2) * h;
        float bp_out = g * hp_out + s.svfS1;
        s.svfS1 = g * hp_out + bp_out;
        float lp_out = g * bp_out + s.svfS2;
        s.svfS2 = g * bp_out + lp_out;
        float notch = lp_out + hp_out;

        float m = morph;
        if (m < 0.08f)
        {
          // Pure LP
          sample = lp_out;
        }
        else if (m < 0.17f)
        {
          // LP -> BP transition
          float t = (m - 0.08f) / 0.09f;
          sample = lp_out * (1.0f - t) + bp_out * t;
        }
        else if (m < 0.33f)
        {
          // Pure BP
          sample = bp_out;
        }
        else if (m < 0.42f)
        {
          // BP -> HP transition
          float t = (m - 0.33f) / 0.09f;
          sample = bp_out * (1.0f - t) + hp_out * t;
        }
        else if (m < 0.58f)
        {
          // Pure HP
          sample = hp_out;
        }
        else if (m < 0.67f)
        {
          // HP -> Notch transition
          float t = (m - 0.58f) / 0.09f;
          sample = hp_out * (1.0f - t) + notch * t;
        }
        else
        {
          // Pure Notch
          sample = notch;
        }
      }

      sample *= level;

      // Write to ring buffer for viz
      s.outputRing[s.ringPos] = sample;
      s.ringPos = (s.ringPos + 1) & 255;

      out[i] = sample;
    }
  }

} // namespace stolmine
