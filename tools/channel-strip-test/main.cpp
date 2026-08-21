// house::ChannelStrip skeleton harness.
//
// The gating test for the whole strip: EVERYTHING BYPASSED MUST BE
// BIT-IDENTICAL TO THE INPUT. If that does not hold, no section's
// behaviour can be attributed to that section, because the chain
// itself is colouring. Prove it before building any section.
//
// Then: each section engaged alone actually does something, and true
// bypass really is a skip rather than unity gain.
#include <cstdio>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <vector>
#include <od/objects/Object.h>
_GlobalConfig globalConfig;
#include "ChannelStrip.h"

using namespace house;
static const double SR = 48000.0;
static const int FR = 128;

static inline bool realFinite(float v)
{ uint32_t u; memcpy(&u, &v, 4); return ((u >> 23) & 0xFFu) != 0xFFu; }

static int fails = 0;
static void chk(bool ok, const char *w, const char *d = "")
{ printf("  [%s] %s %s\n", ok ? "PASS" : "FAIL", w, d); if (!ok) fails++; }

struct Rig
{
  ChannelStrip s;
  std::vector<float> l, r, ol, orr;
  Rig() : l(FR), r(FR), ol(FR), orr(FR)
  {
    s.mInL.setBuffer(l.data()); s.mInR.setBuffer(r.data());
    s.mOutL.setBuffer(ol.data()); s.mOutR.setBuffer(orr.data());
  }
  // Returns (differing samples, output rms). in == out is the null test.
  void run(int blocks, long &diff, double &rms, bool &finite)
  {
    long n = 0; double acc = 0; long cnt = 0; diff = 0; finite = true;
    for (int b = 0; b < blocks; b++)
    {
      for (int i = 0; i < FR; i++)
      { l[i] = r[i] = (float)(sin(2.0*M_PI*220.0*n/SR)*0.5 + sin(2.0*M_PI*3300.0*n/SR)*0.2); n++; }
      std::vector<float> keep = l;
      s.process();
      for (int i = 0; i < FR; i++)
      {
        if (!realFinite(ol[i])) finite = false;
        if (ol[i] != keep[i]) diff++;
        acc += (double)ol[i]*ol[i]; cnt++;
      }
    }
    rms = cnt ? sqrt(acc/cnt) : 0.0;
  }
};

int main()
{
  globalConfig.frameLength = FR;
  globalConfig.sampleRate = (float)SR;
  char buf[256];

  // 1. THE GATING TEST
  {
    Rig g; long d; double rms; bool fin;
    g.run(400, d, rms, fin);
    snprintf(buf, sizeof buf, "(%ld/%d samples differ)", d, 400*FR);
    chk(d == 0, "ALL SECTIONS BYPASSED is bit-identical to input", buf);
    chk(fin, "output finite when fully bypassed");
  }

  // 2. each implemented section, engaged alone, must change something
  {
    struct S { const char *name; int which; };
    S secs[3] = {{"Dynamics",1},{"EQ",3},{"Out",6}};
    for (auto &sec : secs)
    {
      Rig g; long d; double rms; bool fin;
      if (sec.which == 1) { g.s.mDynEngage.set(1); g.s.mDynAmount.hardSet(1.0f); g.s.mDynThresh.hardSet(0.2f); }
      if (sec.which == 3) { g.s.mEqEngage.set(1); g.s.mEqMidGain.hardSet(12.0f); g.s.mEqMidFreq.hardSet(220.0f); }
      if (sec.which == 6) { g.s.mOutEngage.set(1); g.s.mOutLevel.hardSet(0.5f); }
      g.run(400, d, rms, fin);
      snprintf(buf, sizeof buf, "(%s: %ld samples changed, rms %.4f)", sec.name, d, rms);
      chk(d > 0 && fin, "engaged section changes the signal", buf);
    }
  }

  // 3. an ENGAGED section at neutral settings should still be near-unity,
  // so engaging is not itself a tone change.
  {
    Rig g; long d; double rms; bool fin;
    g.s.mEqEngage.set(1);   // all EQ gains at 0 dB
    g.run(400, d, rms, fin);
    snprintf(buf, sizeof buf, "(%ld differ with EQ engaged but flat)", d);
    chk(d == 0, "engaging EQ at flat gains is still bit-identical", buf);
  }

  // 4. Out level is exact below the clip knee
  {
    Rig g; long d; double rms; bool fin;
    g.s.mOutEngage.set(1); g.s.mOutLevel.hardSet(1.0f);
    g.run(400, d, rms, fin);
    snprintf(buf, sizeof buf, "(%ld differ at unity level)", d);
    chk(d == 0, "Out at unity level is bit-identical", buf);
  }

  // 5. everything at once stays finite and bounded
  {
    Rig g; long d; double rms; bool fin;
    g.s.mDynEngage.set(1); g.s.mEqEngage.set(1); g.s.mOutEngage.set(1);
    g.s.mDynAmount.hardSet(1.0f); g.s.mDynThresh.hardSet(0.1f);
    g.s.mGateAmount.hardSet(1.0f); g.s.mGateThresh.hardSet(0.3f);
    g.s.mEqLowGain.hardSet(15.0f); g.s.mEqMidGain.hardSet(15.0f);
    g.s.mEqHighGain.hardSet(15.0f); g.s.mOutLevel.hardSet(4.0f);
    g.run(400, d, rms, fin);
    snprintf(buf, sizeof buf, "(rms %.4f)", rms);
    chk(fin && rms <= 1.01, "all sections hot stays finite and clipped", buf);
  }

  printf("\n%s\n", fails ? "FAILURES PRESENT" : "ALL CHECKS PASS");
  return fails ? 1 : 0;
}
