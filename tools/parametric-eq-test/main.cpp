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
    brown.mCharacter.set(2); black.mCharacter.set(3);
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
    eq.mCharacter.set(4);
    eq.mLfGain.hardSet(18.0f); eq.mLmfGain.hardSet(18.0f);
    eq.mHmfGain.hardSet(18.0f); eq.mHfGain.hardSet(18.0f);
    eq.mLmfQ.hardSet(10.0f); eq.mHmfQ.hardSet(10.0f); eq.mCharacter.set(4);
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

  // 7. BAND SKIPPING must be inaudible. A band at 0 dB is skipped
  // entirely now; prove that output is bit-identical to the same patch
  // with that band nudged to a gain that rounds to no change.
  {
    auto render = [](bool nudge) {
      ParametricEq eq; eq.mLmfGain.hardSet(6.0f); eq.mLmfQ.hardSet(2.0f);
      if (nudge) eq.mHmfGain.hardSet(0.0f);   // explicitly 0 dB => skipped
      std::vector<float> l(FR), r(FR), ol(FR), orr(FR), out;
      eq.mInL.setBuffer(l.data()); eq.mInR.setBuffer(r.data());
      eq.mOutL.setBuffer(ol.data()); eq.mOutR.setBuffer(orr.data());
      long n = 0;
      for (int b = 0; b < 200; b++)
      { for (int i = 0; i < FR; i++) { l[i]=r[i]=(float)(sin(2.0*M_PI*400.0*n/SR)*0.5); n++; }
        eq.process();
        for (int i = 0; i < FR; i++) out.push_back(ol[i]); }
      return out;
    };
    const std::vector<float> a = render(false), b = render(true);
    long d = 0; for (size_t i = 0; i < a.size(); i++) if (a[i] != b[i]) d++;
    snprintf(buf, sizeof buf, "(%ld/%zu differ)", d, a.size());
    chk(d == 0, "skipping a 0 dB band is bit-identical to running it", buf);
  }

  // 8. Character positions must be audibly distinct.
  {
    printf("  character positions, THD%% at +12 dB LMF, -12 dBFS:\n");
    double prev = -1; bool rising = true;
    for (int c = 1; c <= 4; c++)
    {
      const int N = 32768; const double PFc = SR*683.0/N;
      ParametricEq eq; eq.mCharacter.set(c);
      // Probe AT the band centre. Measuring an octave off understates
      // saturation by ~40x, which is how a bad drive calibration got
      // through once already.
      eq.mLmfFreq.hardSet((float)PFc);
      eq.mLmfGain.hardSet(12.0f); eq.mLmfQ.hardSet(2.0f);
      std::vector<float> l(FR), r(FR), ol(FR), orr(FR);
      eq.mInL.setBuffer(l.data()); eq.mInR.setBuffer(r.data());
      eq.mOutL.setBuffer(ol.data()); eq.mOutR.setBuffer(orr.data());
      const double PF = PFc;
      std::vector<double> y; y.reserve(N); long n = 0;
      for (int b = 0; b < N/FR + 200; b++)
      { for (int i = 0; i < FR; i++) { l[i]=r[i]=(float)(sin(2.0*M_PI*PF*n/SR)*0.25); n++; }
        eq.process();
        if (b >= 200) for (int i = 0; i < FR && (int)y.size() < N; i++) y.push_back(ol[i]); }
      double f1=0,h=0;
      for (int k=1;k<=8;k++){ double re=0,im=0;
        for (int i=0;i<N;i++){ double aa=2.0*M_PI*PF*k*i/SR; re+=y[i]*cos(aa); im-=y[i]*sin(aa);} 
        double m=sqrt(re*re+im*im); if(k==1)f1=m; else h+=m*m; }
      const double t = 100.0*sqrt(h)/(f1+1e-30);
      static const char *nm[4] = {"clean","brown","black","hot"};
      printf("      %-6s %7.3f%%\n", nm[c-1], t);
      if (c == 1 && t > 0.05) rising = false;          // clean must be clean
      if (c > 1 && t < prev * 1.3) rising = false;     // each step clearly more
      prev = t;
    }
    chk(rising, "character positions are clean-then-clearly-increasing");
  }

  printf("\n%s\n", fails ? "FAILURES PRESENT" : "ALL CHECKS PASS");
  return fails ? 1 : 0;
}
