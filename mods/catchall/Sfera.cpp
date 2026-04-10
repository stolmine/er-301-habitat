#include "Sfera.h"
#include "SferaConfigs.h"
#include <od/config.h>
#include <hal/ops.h>
#include <math.h>
#include <string.h>

namespace stolmine
{

  // Cytomic SVF section -- stable under fast modulation
  struct SvfSection
  {
    float ic1eq, ic2eq; // state

    // Current parameters (slewed per-sample)
    float g, k;         // freq/Q coefficients
    float m0, m1, m2;   // output mix (LP/BP/HP blend)

    // Target parameters (set per-frame)
    float tg, tk, tm0, tm1, tm2;

    void reset()
    {
      ic1eq = ic2eq = 0.0f;
      g = tg = 0.1f;
      k = tk = 1.0f;
      m0 = tm0 = 0.0f;
      m1 = tm1 = 0.0f;
      m2 = tm2 = 1.0f; // default: lowpass
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

      // Soft-limit state to prevent blowup
      if (ic1eq > 4.0f) ic1eq = 4.0f;
      if (ic1eq < -4.0f) ic1eq = -4.0f;
      if (ic2eq > 4.0f) ic2eq = 4.0f;
      if (ic2eq < -4.0f) ic2eq = -4.0f;

      return m0 * v0 + m1 * v1 + m2 * v2;
    }

    void setFromPoleZero(float pAngle, float pRadius, float zAngle, float zRadius, float sr)
    {
      // Convert pole angle to frequency
      float freq = pAngle * sr / (2.0f * 3.14159f);
      if (freq < 20.0f) freq = 20.0f;
      if (freq > sr * 0.45f) freq = sr * 0.45f;

      // g = tan(pi * freq / sr)
      float w = 3.14159f * freq / sr;
      g = tg = w / (1.0f + w * w * 0.333f); // tan approx

      // Q from pole radius: Q ~ 1 / (2 * (1 - r))
      float q = 0.5f / (1.001f - pRadius);
      if (q < 0.5f) q = 0.5f;
      if (q > 20.0f) q = 20.0f;
      k = tk = 1.0f / q;

      // Determine output mix from zero position
      if (zRadius < 0.01f)
      {
        // No zeros: allpole (lowpass-ish)
        m0 = tm0 = 0.0f;
        m1 = tm1 = 0.0f;
        m2 = tm2 = 1.0f;
      }
      else if (zAngle > 2.5f) // zeros near Nyquist -> lowpass
      {
        m0 = tm0 = 0.0f;
        m1 = tm1 = 0.0f;
        m2 = tm2 = 1.0f;
      }
      else if (zAngle < 0.1f) // zeros near DC -> highpass
      {
        m0 = tm0 = 1.0f;
        m1 = tm1 = -k;
        m2 = tm2 = -1.0f;
      }
      else // zeros at mid frequency -> notch or bandpass
      {
        // Blend based on zero angle relative to pole angle
        float ratio = zAngle / (pAngle + 0.001f);
        if (ratio > 0.8f && ratio < 1.2f) // zeros near pole = notch
        {
          m0 = tm0 = 1.0f;
          m1 = tm1 = -k;
          m2 = tm2 = 0.0f;
        }
        else // bandpass
        {
          m0 = tm0 = 0.0f;
          m1 = tm1 = 1.0f;
          m2 = tm2 = 0.0f;
        }
      }
    }
  };

  struct Sfera::Internal
  {
    SvfSection svfs[kSferaMaxSections];
    int activeSections = 0;

    // Pre-baked SVF params per config
    struct SvfParams
    {
      float g, k, m0, m1, m2;
    };

    struct BakedConfig
    {
      SvfParams sections[kSferaMaxSections];
      int numSections;
      float gain;
      float poleAngle[kSferaMaxSections];
      float poleRadius[kSferaMaxSections];
      float zeroAngle[kSferaMaxSections];
      float zeroRadius[kSferaMaxSections];
    };

    static const int kMaxConfigs = 256;
    BakedConfig configs[kMaxConfigs];
    int numConfigs = 0;

    static const int kMaxCubes = 128;
    MorphCube cubes[kMaxCubes];
    int numCubes = 0;

    float curPoleAngle[kSferaMaxSections];
    float curPoleRadius[kSferaMaxSections];
    float curZeroAngle[kSferaMaxSections];
    float curZeroRadius[kSferaMaxSections];

    float sphereRotation = 0.0f;
    float slewRate = 0.02f; // ~50ms transition
    float envelope = 0.0f;  // RMS envelope for graphic

    void Init()
    {
      float sr = globalConfig.sampleRate;

      for (int i = 0; i < kSferaMaxSections; i++)
      {
        svfs[i].reset();
        curPoleAngle[i] = curPoleRadius[i] = 0;
        curZeroAngle[i] = curZeroRadius[i] = 0;
      }

      // Bake configs from pole/zero specs into SVF parameters
      numConfigs = 0;
      for (int c = 0; c < kNumConfigSpecs && numConfigs < kMaxConfigs; c++)
      {
        BakedConfig &bc = configs[numConfigs];
        const ConfigSpec &spec = kConfigSpecs[c];
        bc.numSections = spec.numSections;
        bc.gain = spec.gain;

        for (int s = 0; s < kSferaMaxSections; s++)
        {
          float pa = spec.poles[s].angle;
          float pr = spec.poles[s].radius;
          float za = spec.zeros[s].angle;
          float zr = spec.zeros[s].radius;

          bc.poleAngle[s] = pa;
          bc.poleRadius[s] = pr;
          bc.zeroAngle[s] = za;
          bc.zeroRadius[s] = zr;

          if (s < spec.numSections)
          {
            SvfSection tmp;
            tmp.setFromPoleZero(pa, pr, za, zr, sr);
            bc.sections[s] = {tmp.g, tmp.k, tmp.m0, tmp.m1, tmp.m2};
          }
          else
          {
            // Bypass: unity gain
            bc.sections[s] = {0.1f, 2.0f, 1.0f, 0.0f, 0.0f};
          }
        }
        numConfigs++;
      }

      // Cubes
      numCubes = 0;
      for (int i = 0; i < kNumCubeSpecs && numCubes < kMaxCubes; i++)
      {
        cubes[numCubes] = kCubes[i];
        numCubes++;
      }
      int genSeed = 7919;
      while (numCubes < kMaxCubes)
      {
        genSeed = genSeed * 1103515245 + 12345;
        cubes[numCubes].config[0] = ((genSeed >> 8) & 0x7FFF) % numConfigs;
        genSeed = genSeed * 1103515245 + 12345;
        cubes[numCubes].config[1] = ((genSeed >> 8) & 0x7FFF) % numConfigs;
        genSeed = genSeed * 1103515245 + 12345;
        cubes[numCubes].config[2] = ((genSeed >> 8) & 0x7FFF) % numConfigs;
        genSeed = genSeed * 1103515245 + 12345;
        cubes[numCubes].config[3] = ((genSeed >> 8) & 0x7FFF) % numConfigs;
        cubes[numCubes].name = "gen";
        numCubes++;
      }
    }
  };

  Sfera::Sfera()
  {
    addInput(mIn);
    addInput(mVOct);
    addOutput(mOut);
    addParameter(mConfig);
    addParameter(mParamX);
    addParameter(mParamY);
    addParameter(mCutoff);
    addParameter(mQScale);
    addParameter(mLevel);
    addParameter(mSpin);

    mpInternal = new Internal();
    mpInternal->Init();
  }

  Sfera::~Sfera()
  {
    delete mpInternal;
  }

  float Sfera::getPoleAngle(int idx)
  {
    if (idx < 0 || idx >= kSferaMaxSections) return 0;
    return mpInternal->curPoleAngle[idx];
  }
  float Sfera::getPoleRadius(int idx)
  {
    if (idx < 0 || idx >= kSferaMaxSections) return 0;
    return mpInternal->curPoleRadius[idx];
  }
  float Sfera::getZeroAngle(int idx)
  {
    if (idx < 0 || idx >= kSferaMaxSections) return 0;
    return mpInternal->curZeroAngle[idx];
  }
  float Sfera::getZeroRadius(int idx)
  {
    if (idx < 0 || idx >= kSferaMaxSections) return 0;
    return mpInternal->curZeroRadius[idx];
  }
  int Sfera::getActiveSections()
  {
    return mpInternal->activeSections;
  }
  float Sfera::getSphereRotation()
  {
    return mpInternal->sphereRotation;
  }
  int Sfera::getNumCubes()
  {
    return mpInternal->numCubes;
  }
  float Sfera::getParamX()
  {
    return CLAMP(0.0f, 1.0f, mParamX.value());
  }
  float Sfera::getParamY()
  {
    return CLAMP(0.0f, 1.0f, mParamY.value());
  }
  float Sfera::getEnvelope()
  {
    return mpInternal->envelope;
  }
  float Sfera::getSpin()
  {
    return mSpin.value();
  }

  static inline float lerp2d(float v00, float v10, float v01, float v11, float x, float y)
  {
    return v00 + (v10 - v00) * x + (v01 - v00) * y + (v00 - v10 - v01 + v11) * x * y;
  }

  void Sfera::process()
  {
    Internal &s = *mpInternal;
    float *in = mIn.buffer();
    float *voct = mVOct.buffer();
    float *out = mOut.buffer();

    int config = CLAMP(0, s.numCubes - 1, (int)(mConfig.value() + 0.5f));
    float px = CLAMP(0.0f, 1.0f, mParamX.value());
    float py = CLAMP(0.0f, 1.0f, mParamY.value());
    float cutoff = CLAMP(20.0f, 20000.0f, mCutoff.value());
    float qScale = CLAMP(0.25f, 4.0f, mQScale.value());
    float level = CLAMP(0.0f, 2.0f, mLevel.value());

    float sr = globalConfig.sampleRate;
    float voctPitch = voct[0] * 10.0f;
    float cutoffRatio = cutoff / 1000.0f * powf(2.0f, voctPitch);

    // Look up corners
    const Internal::BakedConfig &c00 = s.configs[CLAMP(0, s.numConfigs - 1, s.cubes[config].config[0])];
    const Internal::BakedConfig &c10 = s.configs[CLAMP(0, s.numConfigs - 1, s.cubes[config].config[1])];
    const Internal::BakedConfig &c01 = s.configs[CLAMP(0, s.numConfigs - 1, s.cubes[config].config[2])];
    const Internal::BakedConfig &c11 = s.configs[CLAMP(0, s.numConfigs - 1, s.cubes[config].config[3])];

    int nSections = c00.numSections;
    if (c10.numSections > nSections) nSections = c10.numSections;
    if (c01.numSections > nSections) nSections = c01.numSections;
    if (c11.numSections > nSections) nSections = c11.numSections;
    if (nSections > kSferaMaxSections) nSections = kSferaMaxSections;
    s.activeSections = nSections;

    float gain = lerp2d(c00.gain, c10.gain, c01.gain, c11.gain, px, py);

    // Set SVF target parameters from interpolated configs
    for (int i = 0; i < kSferaMaxSections; i++)
    {
      if (i < nSections)
      {
        float ig = lerp2d(c00.sections[i].g, c10.sections[i].g, c01.sections[i].g, c11.sections[i].g, px, py);
        float ik = lerp2d(c00.sections[i].k, c10.sections[i].k, c01.sections[i].k, c11.sections[i].k, px, py);
        float im0 = lerp2d(c00.sections[i].m0, c10.sections[i].m0, c01.sections[i].m0, c11.sections[i].m0, px, py);
        float im1 = lerp2d(c00.sections[i].m1, c10.sections[i].m1, c01.sections[i].m1, c11.sections[i].m1, px, py);
        float im2 = lerp2d(c00.sections[i].m2, c10.sections[i].m2, c01.sections[i].m2, c11.sections[i].m2, px, py);

        // Apply cutoff scaling to g (frequency)
        ig *= cutoffRatio;
        if (ig > 10.0f) ig = 10.0f; // stability limit

        // Apply Q scale to k (damping = 1/Q)
        ik /= qScale;
        if (ik < 0.05f) ik = 0.05f; // don't let Q go infinite

        s.svfs[i].tg = ig;
        s.svfs[i].tk = ik;
        s.svfs[i].tm0 = im0;
        s.svfs[i].tm1 = im1;
        s.svfs[i].tm2 = im2;
      }
      else
      {
        // Bypass section
        s.svfs[i].tg = 0.1f;
        s.svfs[i].tk = 2.0f;
        s.svfs[i].tm0 = 1.0f;
        s.svfs[i].tm1 = 0.0f;
        s.svfs[i].tm2 = 0.0f;
      }
    }

    // Update viz
    for (int i = 0; i < nSections; i++)
    {
      s.curPoleAngle[i] = lerp2d(c00.poleAngle[i], c10.poleAngle[i], c01.poleAngle[i], c11.poleAngle[i], px, py);
      s.curPoleRadius[i] = lerp2d(c00.poleRadius[i], c10.poleRadius[i], c01.poleRadius[i], c11.poleRadius[i], px, py);
      s.curZeroAngle[i] = lerp2d(c00.zeroAngle[i], c10.zeroAngle[i], c01.zeroAngle[i], c11.zeroAngle[i], px, py);
      s.curZeroRadius[i] = lerp2d(c00.zeroRadius[i], c10.zeroRadius[i], c01.zeroRadius[i], c11.zeroRadius[i], px, py);
    }
    for (int i = nSections; i < kSferaMaxSections; i++)
      s.curPoleAngle[i] = s.curPoleRadius[i] = s.curZeroAngle[i] = s.curZeroRadius[i] = 0;

    s.sphereRotation = (float)config * 6.28318f / (float)s.numCubes + px * 0.26f - py * 0.26f;

    // Envelope follower coefficients (fast attack ~1ms, slow release ~150ms)
    float envAttack = 1.0f - expf(-1.0f / (sr * 0.001f));
    float envRelease = 1.0f - expf(-1.0f / (sr * 0.150f));
    float env = s.envelope;

    // Process audio
    for (int i = 0; i < FRAMELENGTH; i++)
    {
      float x = in[i];

      for (int j = 0; j < nSections; j++)
      {
        s.svfs[j].slew(s.slewRate);
        x = s.svfs[j].process(x);
      }

      // Emergency recovery
      if (x != x || x > 8.0f || x < -8.0f)
      {
        for (int j = 0; j < kSferaMaxSections; j++)
          s.svfs[j].ic1eq = s.svfs[j].ic2eq = 0.0f;
        x = 0.0f;
      }

      float y = x * gain * level;
      out[i] = y;

      // RMS envelope on output
      float abs_y = y < 0 ? -y : y;
      float coeff = abs_y > env ? envAttack : envRelease;
      env += (abs_y - env) * coeff;
    }
    s.envelope = env;
  }

} // namespace stolmine
