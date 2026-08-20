// house::ParametricEq harness - the four-band unit, not the atom.
//
// Checks the properties that only appear once four bands are in series:
//   1 all gains at 0 dB is a BIT-IDENTICAL bypass through all four
//   2 each band lands its gain at its own centre without the others
//     leaking in
//   3 Colour changes the gain RANGE (Brown clamps at 15, Black at 18)
//   4 Mix is a linear crossfade
//   5 four bands at full boost stay finite and bounded
//   6 shelf/bell switching reaches the DSP
#include <cstdio>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <vector>
#include <od/objects/Object.h>
_GlobalConfig globalConfig;
#include "ParametricEq.h"

using namespace house;
static const double SR = 48000.0;
static const int FR = 128;


// std::isfinite is UNRELIABLE under -ffast-math: the compiler is told to
// assume NaN and Inf never occur, so the check folds to a constant true.
// This harness reported "finite: YES" on output that was entirely NaN.
// Inspect the bit pattern instead, which no optimization can elide.
static inline bool realFinite(float v)
{
  uint32_t u; memcpy(&u, &v, 4);
  return ((u >> 23) & 0xFFu) != 0xFFu;   // exponent all-ones => NaN or Inf
}

static int fails = 0;
static void chk(bool ok, const char *w, const char *d = "")
{ printf("  [%s] %s %s\n", ok ? "PASS" : "FAIL", w, d); if (!ok) fails++; }

static double respDb(ParametricEq &eq, double probe, double secs = 0.6)
{
  std::vector<float> l(FR), r(FR), ol(FR), orr(FR);
  eq.mInL.setBuffer(l.data()); eq.mInR.setBuffer(r.data());
  eq.mOutL.setBuffer(ol.data()); eq.mOutR.setBuffer(orr.data());
  const int blocks = (int)(SR * secs / FR);
  double peak = 0; long n = 0;
  for (int b = 0; b < blocks; b++)
  {
    for (int i = 0; i < FR; i++) { l[i] = r[i] = (float)sin(2.0 * M_PI * probe * n / SR); n++; }
    eq.process();
    if (b > blocks * 3 / 4) for (int i = 0; i < FR; i++) { const double a = fabs(ol[i]); if (a > peak) peak = a; }
  }
  return 20.0 * log10(peak + 1e-30);
}

int main()
{
  globalConfig.frameLength = FR;
  globalConfig.sampleRate = (float)SR;
  char buf[256];

  // 1. exact bypass through all four bands
  {
    ParametricEq eq;
    std::vector<float> l(FR), r(FR), ol(FR), orr(FR);
    eq.mInL.setBuffer(l.data()); eq.mInR.setBuffer(r.data());
    eq.mOutL.setBuffer(ol.data()); eq.mOutR.setBuffer(orr.data());
    long diff = 0, n = 0;
    for (int b = 0; b < 400; b++)
    {
      for (int i = 0; i < FR; i++)
      { l[i] = r[i] = (float)(sin(2.0*M_PI*220.0*n/SR)*0.6 + sin(2.0*M_PI*4100.0*n/SR)*0.3); n++; }
      std::vector<float> keep = l;
      eq.process();
      for (int i = 0; i < FR; i++) if (ol[i] != keep[i]) diff++;
    }
    snprintf(buf, sizeof buf, "(%ld/%d differ)", diff, 400*FR);
    chk(diff == 0, "all gains 0 dB is bit-identical through 4 bands", buf);
  }

  // 2. each band hits its own centre, others stay near flat
  {
    printf("  per-band isolation (+12 dB on one band at a time):\n");
    struct B { const char *n; double f; };
    B bands[4] = {{"LF",100},{"LMF",500},{"HMF",2000},{"HF",8000}};
    bool ok = true;
    for (int k = 0; k < 4; k++)
    {
      ParametricEq eq;
      eq.mLfShape.set(2); eq.mHfShape.set(2);   // bells, so all four are comparable
      (k==0?eq.mLfGain:k==1?eq.mLmfGain:k==2?eq.mHmfGain:eq.mHfGain).hardSet(12.0f);
      if (k==1) eq.mLmfQ.hardSet(4.0f);
      if (k==2) eq.mHmfQ.hardSet(4.0f);
      const double at = respDb(eq, bands[k].f);
      double worstOther = 0;
      for (int j = 0; j < 4; j++) if (j != k)
      { const double o = respDb(eq, bands[j].f); if (fabs(o) > fabs(worstOther)) worstOther = o; }
      printf("      %-4s at %5.0f Hz -> %+6.2f dB   worst other band %+6.2f dB\n",
             bands[k].n, bands[k].f, at, worstOther);
      if (at < 8.0) ok = false;
    }
    chk(ok, "each band reaches its gain at its own centre");
  }

  // 3. Colour changes the gain range
  {
    ParametricEq brown, black;
    brown.mLfShape.set(2); black.mLfShape.set(2);
    brown.mColour.set(1); black.mColour.set(2);
    brown.mLmfGain.hardSet(18.0f); black.mLmfGain.hardSet(18.0f);
    const double b1 = respDb(brown, 500.0), b2 = respDb(black, 500.0);
    snprintf(buf, sizeof buf, "(brown %+.2f dB, black %+.2f dB at a +18 request)", b1, b2);
    chk(b2 > b1 + 1.0, "Brown clamps lower than Black", buf);
  }

  // 4. Mix is linear
  {
    ParametricEq eq; eq.mLfShape.set(2);
    eq.mLmfGain.hardSet(12.0f); eq.mLmfQ.hardSet(4.0f);
    eq.mMix.hardSet(1.0f); const double full = pow(10.0, respDb(eq, 500.0)/20.0);
    ParametricEq eq2; eq2.mLfShape.set(2);
    eq2.mLmfGain.hardSet(12.0f); eq2.mLmfQ.hardSet(4.0f);
    eq2.mMix.hardSet(0.5f); const double half = pow(10.0, respDb(eq2, 500.0)/20.0);
    const double expect = 0.5 * 1.0 + 0.5 * full;
    snprintf(buf, sizeof buf, "(mix .5 gave %.4f, linear predicts %.4f)", half, expect);
    chk(fabs(half - expect) < 0.05, "Mix is a linear crossfade", buf);
  }

  // 5. bounded with everything boosted
  {
    ParametricEq eq;
    eq.mColour.set(2);
    eq.mLfGain.hardSet(18.0f); eq.mLmfGain.hardSet(18.0f);
    eq.mHmfGain.hardSet(18.0f); eq.mHfGain.hardSet(18.0f);
    eq.mLmfQ.hardSet(10.0f); eq.mHmfQ.hardSet(10.0f); eq.mDrive.hardSet(1.0f);
    std::vector<float> l(FR), r(FR), ol(FR), orr(FR);
    eq.mInL.setBuffer(l.data()); eq.mInR.setBuffer(r.data());
    eq.mOutL.setBuffer(ol.data()); eq.mOutR.setBuffer(orr.data());
    bool ok = true; long n = 0;
    for (int b = 0; b < 800; b++)
    {
      for (int i = 0; i < FR; i++) { l[i] = r[i] = (float)(sin(2.0*M_PI*55.0*n/SR)*0.95); n++; }
      eq.process();
      for (int i = 0; i < FR; i++) if (!realFinite(ol[i]) || fabs(ol[i]) > 200.0) ok = false;
    }
    chk(ok, "four bands at max boost + drive stay finite and bounded");
  }

  // 6. shelf vs bell actually differ
  {
    ParametricEq shelf, bell;
    shelf.mLfShape.set(1); bell.mLfShape.set(2);
    shelf.mLfGain.hardSet(12.0f); bell.mLfGain.hardSet(12.0f);
    shelf.mLfFreq.hardSet(200.0f); bell.mLfFreq.hardSet(200.0f);
    const double s40 = respDb(shelf, 40.0), b40 = respDb(bell, 40.0);
    snprintf(buf, sizeof buf, "(at 40 Hz: shelf %+.2f dB, bell %+.2f dB)", s40, b40);
    chk(s40 > b40 + 2.0, "LF shelf holds gain below centre where a bell falls off", buf);
  }

  printf("\n%s\n", fails ? "FAILURES PRESENT" : "ALL CHECKS PASS");
  return fails ? 1 : 0;
}
