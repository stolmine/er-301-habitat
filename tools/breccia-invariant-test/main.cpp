// Breccia Glitch=0 invariant harness.
//
// Compiles the REAL mods/spreadsheet/Breccia.cpp (via #include, against stub
// od:: headers) with the same optimization semantics as the shipping package
// (-O3 -ffast-math -fno-tree-vectorize) and measures, bit for bit, whether the
// unit's output with Glitch at 0 equals a plain slicer with no effect
// processing at all.
//
// The plain-slicer REFERENCE replicates only the transport (slice grid,
// pending-size adoption + boundary snap, permutation, splice crossfade, level)
// and reads the source directly: srcPos = perm[idx]*sliceIn + inSlice*ratio.
// No effect code exists in it, so bit-equality proves the invariant rather
// than arguing it by construction.
//
// The unit under test is constructed by placement-new into 0x3B-poisoned
// storage, the way a recycled hardware heap block would arrive, so any member
// the constructor fails to initialize shows up as a real value, not a
// convenient zero page.
//
// Scenarios:
//   1/2: Glitch 0 from birth (speed 1.0 and 0.7315) - full-run bit compare.
//   3/4: Glitch 0.8 for 8 s then 0 (speed 1.0 and 2.0) - bit compare of the
//        entire Glitch=0 tail, starting at its very first sample.
//   5:   Glitch 1e-30 (an almost-zero the block-rate bypass does NOT catch) -
//        full-run bit compare; rollFx must degenerate to identity.
//
// +0.0f and -0.0f compare equal (-ffast-math already makes zero sign
// unspecified).

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <new>

#include <od/config.h>
_GlobalConfig globalConfig;

#include <od/extras/Random.h>
static uint32_t sRandState = 0xC0FFEE11u;
float od::Random::generateFloat(float from, float to)
{
  sRandState = sRandState * 1664525u + 1013904223u;
  float w = (float)((sRandState >> 8) & 0xFFFFFFu) * (1.0f / 16777215.0f);
  return from + w * (to - from);
}

// The real unit under test. private->public is a test-only probe so the
// harness can (a) end the glitch pulse at a moment when a stale decimation
// factor is provably latched from an already-finished crush slice, and
// (b) report the constructor's uninitialized fields. Single TU, so the
// access-specifier redefinition is layout-consistent.
#define private public
#include "Breccia.cpp"
#undef private

// ---------------------------------------------------------------------------
// Plain slicer reference: transport only, zero effect processing.
// ---------------------------------------------------------------------------
struct RefSlicer
{
  od::Sample *pSample = nullptr;
  double pos = 0.0;
  double rateRatio = 1.0;
  int slices = 8;
  int pending = 0;
  int fxIdx = -1;
  int perm[1024];
  float fadeIn[512];
  float fadeOut[512];
  int xf = 0;
  long cornerHits = 0; // inSlice landed at/beyond sliceOut (idx clamp corner)

  RefSlicer(od::Sample *s)
  {
    pSample = s;
    if (s && s->mSampleRate > 0.0f)
      rateRatio = (double)s->mSampleRate / (double)globalConfig.sampleRate;
    for (int i = 0; i < 1024; i++) perm[i] = i;
    const float sr = globalConfig.sampleRate > 0.0f ? globalConfig.sampleRate : 48000.0f;
    int n = (int)(0.003f * sr);
    if (n < 4) n = 4;
    if (n > 512) n = 512;
    xf = n;
    for (int i = 0; i < n; i++)
    {
      const double w = (double)i / (double)n;
      fadeIn[i] = (float)sin(w * M_PI * 0.5);
      fadeOut[i] = (float)cos(w * M_PI * 0.5);
    }
  }

  float readSource(double srcPos) const
  {
    od::Sample *s = pSample;
    if (s == 0 || s->mSampleCount == 0) return 0.0f;
    const int n = (int)s->mSampleCount;
    const int ch = (int)s->mChannelCount;
    if (srcPos < 0.0) srcPos = 0.0;
    int i0 = (int)srcPos;
    if (i0 >= n) i0 = n - 1;
    int i1 = i0 + 1;
    if (i1 >= n) i1 = n - 1;
    const float fr = (float)(srcPos - (double)i0);
    float a = s->get(i0, 0);
    float b = s->get(i1, 0);
    if (ch > 1)
    {
      a = 0.5f * (a + s->get(i0, 1));
      b = 0.5f * (b + s->get(i1, 1));
    }
    return a + (b - a) * fr;
  }

  void process(const float *shuf, const float *spd, float *out, float levelParam, float sizeParam)
  {
    const float level = CLAMP(0.0f, 1.0f, levelParam);
    const int total = pSample ? (int)pSample->mSampleCount : 0;
    if (total < 8)
    {
      for (int i = 0; i < FRAMELENGTH; i++) out[i] = 0.0f;
      return;
    }
    const double loopOut = (double)total / rateRatio;

    const float sz = CLAMP(0.0f, 1.0f, sizeParam);
    int req = (int)(pow(1024.0 * 0.5, 1.0 - (double)sz) * 2.0 + 0.5);
    req = CLAMP(2, 1024, req);
    if (req != slices) pending = req;

    for (int i = 0; i < FRAMELENGTH; i++)
    {
      double sliceOut = loopOut / (double)slices;
      int idx = (int)(pos / sliceOut);
      if (idx >= slices) idx = slices - 1;
      if (idx < 0) idx = 0;

      if (idx != fxIdx)
      {
        if (pending && pending != slices)
        {
          const double oldSlice = sliceOut;
          slices = pending;
          pending = 0;
          sliceOut = loopOut / (double)slices;
          double k = (pos + 0.5 * oldSlice) / sliceOut;
          k = (double)(long)k;
          pos = k * sliceOut;
          if (pos >= loopOut || pos < 0.0) pos = 0.0;
          idx = (int)(pos / sliceOut);
          if (idx >= slices) idx = slices - 1;
          if (idx < 0) idx = 0;
        }
        fxIdx = idx;
      }

      const double inSlice = pos - (double)idx * sliceOut;
      const double sliceIn = sliceOut * rateRatio;
      const double base = (double)perm[idx] * sliceIn;

      if (inSlice >= sliceOut) cornerHits++;

      // Plain read: no stutter, no reverse, no scrub, no crush, no filter,
      // no chop, no step, no decimator.
      double u = inSlice * rateRatio;
      if (u < 0.0) u = 0.0;
      else if (u > sliceIn) u = sliceIn;
      float y = readSource(base + u);

      if (inSlice < (double)xf)
      {
        int pIdx = idx - 1;
        while (pIdx < 0) pIdx += slices;
        const double pb = (double)perm[pIdx] * sliceIn;
        const float yPrev = readSource(pb + (sliceIn + inSlice * rateRatio));
        const int k = (int)inSlice;
        y = yPrev * fadeOut[k] + y * fadeIn[k];
      }

      out[i] = y * level;

      double sp = (double)spd[i];
      if (sp > 64.0) sp = 64.0; else if (sp < -64.0) sp = -64.0;
      pos += sp;
      if (pos >= loopOut) pos -= loopOut * (double)(long)(pos / loopOut);
      if (pos < 0.0) pos += loopOut * (1.0 + (double)(long)(-pos / loopOut));
      if (pos < 0.0 || pos >= loopOut) pos = 0.0;
    }
  }
};

// ---------------------------------------------------------------------------

static bool feq(float a, float b)
{
  if (a == b) return true; // covers +0 vs -0
  return memcmp(&a, &b, 4) == 0;
}

struct Result { long total; long diffs; long firstDiff; float ua, ub; long corner; int ctorDecim; int latchedDecim; };

// Runs one scenario. If pulse: hold glitchHi until a stale decim > 1 is
// latched in mFx[0] from an ALREADY-FINISHED crush slice (or 12 s cap), then
// drop to glitchLo and bit-compare the entire tail from its first sample.
// Otherwise: hold glitchLo from birth and compare everything.
static Result runScenario(float speed, bool pulse, float glitchHi,
                          float glitchLo, double secLo,
                          od::Sample *sample)
{
  // Poisoned construction: any field the ctor path misses keeps 0x3B3B3B3B.
  alignas(64) static unsigned char storage[sizeof(stolmine::Breccia) + 64];
  memset(storage, 0x3B, sizeof(storage));
  stolmine::Breccia *unit = new (storage) stolmine::Breccia();
  unit->setSample(sample);

  RefSlicer ref(sample);

  static float shufBuf[128];
  static float spdBuf[128];
  static float outBuf[128];
  static float refBuf[128];
  for (int i = 0; i < 128; i++) { shufBuf[i] = 0.0f; spdBuf[i] = speed; }
  unit->mShuffle.setBuffer(shufBuf);
  unit->mSpeed.setBuffer(spdBuf);
  unit->mOutput.setBuffer(outBuf);

  const long framesHiMax = (long)(12.0 * 48000.0 / 128.0);
  const long framesLo = (long)(secLo * 48000.0 / 128.0);

  Result r = {0, 0, -1, 0, 0, 0, unit->mFx[0].decim, 1};

  // Phase 1 (pulse only): run at glitchHi until a crush slice has come and
  // GONE, leaving its decimation factor latched. Post-fix this never happens
  // (identityFx restores decim=1 on every roll), so the 12 s cap applies.
  if (pulse)
  {
    unit->mGlitch.hardSet(glitchHi);
    for (long f = 0; f < framesHiMax; f++)
    {
      unit->process();
      ref.process(shufBuf, spdBuf, refBuf, 0.5f, 0.5f);
      // decim in [2,25] can only have come from a real FX_CRUSH derive
      // (1 + (int)(q*24)), distinguishing the crush latch from ctor poison.
      if (unit->mFx[0].decim > 1 && unit->mFx[0].decim <= 25 &&
          unit->mFx[0].mode != 3 /*FX_CRUSH*/)
        break;
    }
    r.latchedDecim = unit->mFx[0].decim;
  }

  // Phase 2: Glitch at glitchLo; every sample from the very first block must
  // be bit-identical to the plain slicer.
  unit->mGlitch.hardSet(glitchLo);
  long sampleIdx = 0;
  for (long f = 0; f < framesLo; f++)
  {
    unit->process();
    ref.process(shufBuf, spdBuf, refBuf, 0.5f, 0.5f);
    for (int i = 0; i < 128; i++, sampleIdx++)
    {
      r.total++;
      if (!feq(outBuf[i], refBuf[i]))
      {
        if (r.firstDiff < 0) { r.firstDiff = sampleIdx; r.ua = outBuf[i]; r.ub = refBuf[i]; }
        r.diffs++;
      }
    }
  }
  r.corner = ref.cornerHits;
  unit->~Breccia();
  return r;
}

int main()
{
  // Deterministic 1 s mono source, full of distinctive nonzero values.
  static float data[48000];
  uint32_t s = 0xA5A5A5A5u;
  for (int i = 0; i < 48000; i++)
  {
    s = s * 1664525u + 1013904223u;
    data[i] = ((float)((s >> 8) & 0xFFFFFFu) * (1.0f / 16777215.0f)) * 1.8f - 0.9f;
  }
  od::Sample sample;
  sample.mpData = data;
  sample.mSampleCount = 48000;
  sample.mChannelCount = 1;
  sample.mSampleRate = 48000.0f;

  struct Case { const char *name; float speed; bool pulse; float gHi, gLo; double sLo; };
  Case cases[] = {
    {"glitch0 from birth, speed 1.0   ", 1.0f,    false, 0.0f, 0.0f,   6.0},
    {"glitch0 from birth, speed 0.7315", 0.7315f, false, 0.0f, 0.0f,   6.0},
    {"glitch 0.8 -> 0,    speed 1.0   ", 1.0f,    true,  0.8f, 0.0f,   6.0},
    {"glitch 0.8 -> 0,    speed 2.0   ", 2.0f,    true,  0.8f, 0.0f,   6.0},
    {"glitch 1e-30 held,  speed 1.0   ", 1.0f,    false, 0.0f, 1e-30f, 6.0},
  };

  int failures = 0;
  for (const Case &c : cases)
  {
    Result r = runScenario(c.speed, c.pulse, c.gHi, c.gLo, c.sLo, &sample);
    const bool pass = (r.diffs == 0);
    if (!pass) failures++;
    printf("[%s] %s: %ld/%ld samples differ", pass ? "PASS" : "FAIL",
           c.name, r.diffs, r.total);
    if (r.firstDiff >= 0)
      printf(" (first at %ld: unit=%.9g ref=%.9g)", r.firstDiff, r.ua, r.ub);
    if (r.corner) printf(" [corner hits: %ld]", r.corner);
    printf(" [ctor decim=%d, decim at glitch-off=%d]", r.ctorDecim, r.latchedDecim);
    printf("\n");
  }
  printf(failures ? "\nINVARIANT VIOLATED (%d scenario(s))\n"
                  : "\nINVARIANT HOLDS: Glitch=0 output is bit-identical to the plain slicer.\n",
         failures);
  return failures ? 1 : 0;
}
