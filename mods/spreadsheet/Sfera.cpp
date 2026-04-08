#include "Sfera.h"
#include "SferaConfigs.h"
#include <od/config.h>
#include <hal/ops.h>
#include <math.h>
#include <string.h>

namespace stolmine
{

  static inline float softClip(float x)
  {
    if (x > 4.0f) return 4.0f;
    if (x < -4.0f) return -4.0f;
    return x;
  }

  struct BiquadSection
  {
    float s1, s2;
    // Current coefficients (slewed per-sample toward target)
    float b0, b1, b2, a1, a2;
    // Target coefficients (set per-frame from interpolation)
    float tb0, tb1, tb2, ta1, ta2;

    void reset()
    {
      s1 = s2 = 0.0f;
      b0 = tb0 = 1.0f;
      b1 = tb1 = 0.0f;
      b2 = tb2 = 0.0f;
      a1 = ta1 = 0.0f;
      a2 = ta2 = 0.0f;
    }

    // Slew current coefficients toward target
    inline void slew(float rate)
    {
      b0 += (tb0 - b0) * rate;
      b1 += (tb1 - b1) * rate;
      b2 += (tb2 - b2) * rate;
      a1 += (ta1 - a1) * rate;
      a2 += (ta2 - a2) * rate;
    }

    inline float process(float x)
    {
      float y = b0 * x + s1;
      s1 = softClip(b1 * x - a1 * y + s2);
      s2 = softClip(b2 * x - a2 * y);
      return y;
    }
  };

  struct Sfera::Internal
  {
    BiquadSection biquads[kSferaMaxSections];
    int activeSections = 0;

    // Pre-baked configs (coefficients computed at init from pole/zero specs)
    static const int kMaxConfigs = 256;
    FilterConfig configs[kMaxConfigs];
    int numConfigs = 0;

    // Cube table
    static const int kMaxCubes = 128;
    MorphCube cubes[kMaxCubes];
    int numCubes = 0;

    // Current interpolated state (for viz)
    float curPoleAngle[kSferaMaxSections];
    float curPoleRadius[kSferaMaxSections];
    float curZeroAngle[kSferaMaxSections];
    float curZeroRadius[kSferaMaxSections];

    float sphereRotation = 0.0f;

    // Slew rate: controls how fast coefficients approach target
    // Higher = faster (1.0 = instant, 0.01 = very slow)
    float slewRate = 0.05f; // ~20ms at 48kHz

    void Init()
    {
      for (int i = 0; i < kSferaMaxSections; i++)
      {
        biquads[i].reset();
        curPoleAngle[i] = curPoleRadius[i] = 0;
        curZeroAngle[i] = curZeroRadius[i] = 0;
      }

      // Pre-bake coefficients from pole/zero specs
      numConfigs = 0;
      for (int c = 0; c < kNumConfigSpecs && numConfigs < kMaxConfigs; c++)
      {
        FilterConfig &fc = configs[numConfigs];
        const ConfigSpec &spec = kConfigSpecs[c];
        fc.numSections = spec.numSections;
        fc.gain = spec.gain;

        for (int s = 0; s < kSferaMaxSections; s++)
        {
          float pa = spec.poles[s].angle;
          float pr = spec.poles[s].radius;
          float za = spec.zeros[s].angle;
          float zr = spec.zeros[s].radius;

          fc.poleAngle[s] = pa;
          fc.poleRadius[s] = pr;
          fc.zeroAngle[s] = za;
          fc.zeroRadius[s] = zr;

          if (s < spec.numSections)
            fc.sections[s] = pzToCoeffs(pa, pr, za, zr);
          else
            fc.sections[s] = bypassCoeffs();
        }
        numConfigs++;
      }

      // Copy hand-curated cubes
      numCubes = 0;
      for (int i = 0; i < kNumCubeSpecs && numCubes < kMaxCubes; i++)
      {
        cubes[numCubes] = kCubes[i];
        numCubes++;
      }

      // Generate remaining cubes
      int genSeed = 7919;
      while (numCubes < kMaxCubes)
      {
        genSeed = genSeed * 1103515245 + 12345;
        int c0 = ((genSeed >> 8) & 0x7FFF) % numConfigs;
        genSeed = genSeed * 1103515245 + 12345;
        int c1 = ((genSeed >> 8) & 0x7FFF) % numConfigs;
        genSeed = genSeed * 1103515245 + 12345;
        int c2 = ((genSeed >> 8) & 0x7FFF) % numConfigs;
        genSeed = genSeed * 1103515245 + 12345;
        int c3 = ((genSeed >> 8) & 0x7FFF) % numConfigs;
        cubes[numCubes].config[0] = c0;
        cubes[numCubes].config[1] = c1;
        cubes[numCubes].config[2] = c2;
        cubes[numCubes].config[3] = c3;
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
    float qScale = CLAMP(0.5f, 1.5f, mQScale.value());
    float level = CLAMP(0.0f, 2.0f, mLevel.value());

    // Cutoff ratio (1.0 = no shift from config's designed frequency)
    float voctPitch = voct[0] * 10.0f;
    float cutoffRatio = cutoff / 1000.0f * powf(2.0f, voctPitch);

    // Look up cube corners
    const MorphCube &cube = s.cubes[config];
    const FilterConfig &c00 = s.configs[CLAMP(0, s.numConfigs - 1, cube.config[0])];
    const FilterConfig &c10 = s.configs[CLAMP(0, s.numConfigs - 1, cube.config[1])];
    const FilterConfig &c01 = s.configs[CLAMP(0, s.numConfigs - 1, cube.config[2])];
    const FilterConfig &c11 = s.configs[CLAMP(0, s.numConfigs - 1, cube.config[3])];

    // Max sections across corners
    int nSections = c00.numSections;
    if (c10.numSections > nSections) nSections = c10.numSections;
    if (c01.numSections > nSections) nSections = c01.numSections;
    if (c11.numSections > nSections) nSections = c11.numSections;
    if (nSections > kSferaMaxSections) nSections = kSferaMaxSections;
    s.activeSections = nSections;

    float gain = lerp2d(c00.gain, c10.gain, c01.gain, c11.gain, px, py);

    // Interpolate biquad COEFFICIENTS directly (not pole/zero positions)
    // Then apply cutoff scaling to the interpolated a1 coefficients
    for (int i = 0; i < kSferaMaxSections; i++)
    {
      if (i < nSections)
      {
        // Bilinear interpolate each coefficient
        float ib0 = lerp2d(c00.sections[i].b0, c10.sections[i].b0, c01.sections[i].b0, c11.sections[i].b0, px, py);
        float ib1 = lerp2d(c00.sections[i].b1, c10.sections[i].b1, c01.sections[i].b1, c11.sections[i].b1, px, py);
        float ib2 = lerp2d(c00.sections[i].b2, c10.sections[i].b2, c01.sections[i].b2, c11.sections[i].b2, px, py);
        float ia1 = lerp2d(c00.sections[i].a1, c10.sections[i].a1, c01.sections[i].a1, c11.sections[i].a1, px, py);
        float ia2 = lerp2d(c00.sections[i].a2, c10.sections[i].a2, c01.sections[i].a2, c11.sections[i].a2, px, py);

        // Apply Q scale to pole radius (a2 = r^2, so scale a2; a1 = -2r*cos, scale by sqrt)
        ia2 *= qScale * qScale;
        ia1 *= qScale;
        // Stability: ensure a2 < 1
        if (ia2 > 0.998f) ia2 = 0.998f;

        s.biquads[i].tb0 = ib0;
        s.biquads[i].tb1 = ib1;
        s.biquads[i].tb2 = ib2;
        s.biquads[i].ta1 = ia1;
        s.biquads[i].ta2 = ia2;
      }
      else
      {
        // Bypass
        s.biquads[i].tb0 = 1.0f;
        s.biquads[i].tb1 = 0.0f;
        s.biquads[i].tb2 = 0.0f;
        s.biquads[i].ta1 = 0.0f;
        s.biquads[i].ta2 = 0.0f;
      }
    }

    // Update viz pole/zero data (interpolate positions for display)
    for (int i = 0; i < nSections; i++)
    {
      s.curPoleAngle[i] = lerp2d(c00.poleAngle[i], c10.poleAngle[i], c01.poleAngle[i], c11.poleAngle[i], px, py);
      s.curPoleRadius[i] = lerp2d(c00.poleRadius[i], c10.poleRadius[i], c01.poleRadius[i], c11.poleRadius[i], px, py);
      s.curZeroAngle[i] = lerp2d(c00.zeroAngle[i], c10.zeroAngle[i], c01.zeroAngle[i], c11.zeroAngle[i], px, py);
      s.curZeroRadius[i] = lerp2d(c00.zeroRadius[i], c10.zeroRadius[i], c01.zeroRadius[i], c11.zeroRadius[i], px, py);
    }
    for (int i = nSections; i < kSferaMaxSections; i++)
    {
      s.curPoleAngle[i] = s.curPoleRadius[i] = 0;
      s.curZeroAngle[i] = s.curZeroRadius[i] = 0;
    }

    s.sphereRotation = (float)config * 6.28318f / (float)s.numCubes + px * 0.26f - py * 0.26f;

    // Process audio with per-sample coefficient slew
    for (int i = 0; i < FRAMELENGTH; i++)
    {
      float x = in[i];

      for (int j = 0; j < nSections; j++)
      {
        s.biquads[j].slew(s.slewRate);
        x = s.biquads[j].process(x);
      }

      // Emergency recovery
      if (x != x || x > 8.0f || x < -8.0f)
      {
        for (int j = 0; j < kSferaMaxSections; j++)
          s.biquads[j].s1 = s.biquads[j].s2 = 0.0f;
        x = 0.0f;
      }

      out[i] = x * gain * level;
    }
  }

} // namespace stolmine
