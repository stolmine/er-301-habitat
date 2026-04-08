#include "Sfera.h"
#include "SferaConfigs.h"
#include <od/config.h>
#include <hal/ops.h>
#include <math.h>

namespace stolmine
{

  static inline float softClip(float x)
  {
    if (x > 4.0f) return 4.0f;
    if (x < -4.0f) return -4.0f;
    return x;
  }

  // Fast cosine (5th order, same as Rauschen fast_sinf shifted by pi/2)
  static inline float fast_cosf(float x)
  {
    x += 1.5708f; // shift by pi/2
    float n = floorf(x * 0.31831f + 0.5f);
    float xr = x - n * 3.14159f;
    float x2 = xr * xr;
    float r = xr * (1.0f + x2 * (-0.16605f + x2 * 0.00761f));
    return ((int)n & 1) ? -r : r;
  }

  struct BiquadSection
  {
    float s1, s2;           // transposed direct form II state
    float b0, b1, b2, a1, a2; // coefficients

    void reset()
    {
      s1 = 0.0f;
      s2 = 0.0f;
      b0 = 1.0f; b1 = 0.0f; b2 = 0.0f;
      a1 = 0.0f; a2 = 0.0f;
    }

    inline float process(float x)
    {
      float y = b0 * x + s1;
      s1 = softClip(b1 * x - a1 * y + s2);
      s2 = softClip(b2 * x - a2 * y);
      return y;
    }

    void setFromPoleZero(float pAngle, float pRadius, float zAngle, float zRadius)
    {
      // Pole pair -> denominator
      a1 = -2.0f * pRadius * fast_cosf(pAngle);
      a2 = pRadius * pRadius;
      // Zero pair -> numerator
      if (zRadius > 0.001f)
      {
        b0 = 1.0f;
        b1 = -2.0f * zRadius * fast_cosf(zAngle);
        b2 = zRadius * zRadius;
      }
      else
      {
        // No zeros: allpole
        b0 = 1.0f;
        b1 = 0.0f;
        b2 = 0.0f;
      }
    }
  };

  struct Sfera::Internal
  {
    BiquadSection biquads[kMaxBiquads];
    int activeSections = 0;

    // Current interpolated state (for graphic to read)
    float curPoleAngle[kMaxBiquads];
    float curPoleRadius[kMaxBiquads];
    float curZeroAngle[kMaxBiquads];
    float curZeroRadius[kMaxBiquads];

    // Sphere rotation
    float sphereRotation = 0.0f;

    // Runtime cube table (hand-curated + generated)
    static const int kMaxCubes = 128;
    MorphCube cubes[kMaxCubes];
    int numCubes = 0;

    void Init()
    {
      for (int i = 0; i < kMaxBiquads; i++)
      {
        biquads[i].reset();
        curPoleAngle[i] = 0.0f;
        curPoleRadius[i] = 0.0f;
        curZeroAngle[i] = 0.0f;
        curZeroRadius[i] = 0.0f;
      }
      activeSections = 0;
      sphereRotation = 0.0f;

      // Copy hand-curated cubes
      numCubes = 0;
      for (int i = 0; i < kNumCubes && numCubes < kMaxCubes; i++)
      {
        cubes[numCubes] = kCubes[i];
        numCubes++;
      }

      // Generate remaining cubes by permuting configs at different frequency offsets
      // Each generated cube takes two existing configs and morphs between them
      int genSeed = 7919;
      while (numCubes < kMaxCubes)
      {
        genSeed = genSeed * 1103515245 + 12345;
        int c0 = (genSeed >> 8) % kNumConfigs;
        genSeed = genSeed * 1103515245 + 12345;
        int c1 = (genSeed >> 8) % kNumConfigs;
        genSeed = genSeed * 1103515245 + 12345;
        int c2 = (genSeed >> 8) % kNumConfigs;
        genSeed = genSeed * 1103515245 + 12345;
        int c3 = (genSeed >> 8) % kNumConfigs;

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
    if (idx < 0 || idx >= kMaxBiquads) return 0;
    return mpInternal->curPoleAngle[idx];
  }

  float Sfera::getPoleRadius(int idx)
  {
    if (idx < 0 || idx >= kMaxBiquads) return 0;
    return mpInternal->curPoleRadius[idx];
  }

  float Sfera::getZeroAngle(int idx)
  {
    if (idx < 0 || idx >= kMaxBiquads) return 0;
    return mpInternal->curZeroAngle[idx];
  }

  float Sfera::getZeroRadius(int idx)
  {
    if (idx < 0 || idx >= kMaxBiquads) return 0;
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

  // Bilinear interpolation
  static inline float lerp2d(float v00, float v10, float v01, float v11, float x, float y)
  {
    float a = v00 + (v10 - v00) * x;
    float b = v01 + (v11 - v01) * x;
    return a + (b - a) * y;
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

    float sr = globalConfig.sampleRate;

    // V/Oct
    float voctPitch = voct[0] * 10.0f;
    float cutoffRatio = cutoff / 1000.0f * powf(2.0f, voctPitch);

    // Look up the 4 corner configs
    const MorphCube &cube = s.cubes[config];
    const FilterConfig &c00 = kConfigs[CLAMP(0, kNumConfigs - 1, cube.config[0])];
    const FilterConfig &c10 = kConfigs[CLAMP(0, kNumConfigs - 1, cube.config[1])];
    const FilterConfig &c01 = kConfigs[CLAMP(0, kNumConfigs - 1, cube.config[2])];
    const FilterConfig &c11 = kConfigs[CLAMP(0, kNumConfigs - 1, cube.config[3])];

    // Determine active section count (max of all 4 corners)
    int nSections = c00.numSections;
    if (c10.numSections > nSections) nSections = c10.numSections;
    if (c01.numSections > nSections) nSections = c01.numSections;
    if (c11.numSections > nSections) nSections = c11.numSections;
    if (nSections > kMaxBiquads) nSections = kMaxBiquads;
    s.activeSections = nSections;

    // Interpolated gain
    float gain = lerp2d(c00.gain, c10.gain, c01.gain, c11.gain, px, py);

    // Interpolate pole/zero positions and compute biquad coefficients
    for (int i = 0; i < nSections; i++)
    {
      float pAngle = lerp2d(c00.poles[i].angle, c10.poles[i].angle,
                             c01.poles[i].angle, c11.poles[i].angle, px, py);
      float pRadius = lerp2d(c00.poles[i].radius, c10.poles[i].radius,
                              c01.poles[i].radius, c11.poles[i].radius, px, py);
      float zAngle = lerp2d(c00.zeros[i].angle, c10.zeros[i].angle,
                             c01.zeros[i].angle, c11.zeros[i].angle, px, py);
      float zRadius = lerp2d(c00.zeros[i].radius, c10.zeros[i].radius,
                              c01.zeros[i].radius, c11.zeros[i].radius, px, py);

      // Apply cutoff offset (scale angles)
      pAngle *= cutoffRatio;
      zAngle *= cutoffRatio;

      // Clamp angles to valid range
      float maxAngle = kPi * 0.99f;
      if (pAngle > maxAngle) pAngle = maxAngle;
      if (pAngle < 0.001f) pAngle = 0.001f;
      if (zAngle > maxAngle) zAngle = maxAngle;
      if (zAngle < 0.001f) zAngle = 0.001f;

      // Apply Q scale to pole radius
      pRadius *= qScale;
      if (pRadius > 0.999f) pRadius = 0.999f;
      if (pRadius < 0.0f) pRadius = 0.0f;

      // Store for graphic
      s.curPoleAngle[i] = pAngle;
      s.curPoleRadius[i] = pRadius;
      s.curZeroAngle[i] = zAngle;
      s.curZeroRadius[i] = zRadius;

      s.biquads[i].setFromPoleZero(pAngle, pRadius, zAngle, zRadius);
    }

    // Reset inactive sections
    for (int i = nSections; i < kMaxBiquads; i++)
    {
      s.biquads[i].reset();
      s.curPoleAngle[i] = 0;
      s.curPoleRadius[i] = 0;
      s.curZeroAngle[i] = 0;
      s.curZeroRadius[i] = 0;
    }

    // Update sphere rotation
    s.sphereRotation = (float)config * 6.28318f / (float)s.numCubes
                      + px * 0.26f - py * 0.26f;

    // Process audio
    for (int i = 0; i < FRAMELENGTH; i++)
    {
      float x = in[i];

      // Cascade biquad sections
      for (int j = 0; j < nSections; j++)
        x = s.biquads[j].process(x);

      // Emergency recovery: if output explodes, reset all states
      if (x != x || x > 8.0f || x < -8.0f)
      {
        for (int j = 0; j < nSections; j++)
        {
          s.biquads[j].s1 = 0.0f;
          s.biquads[j].s2 = 0.0f;
        }
        x = 0.0f;
      }

      out[i] = x * gain * level;
    }
  }

} // namespace stolmine
