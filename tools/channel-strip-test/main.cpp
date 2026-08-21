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
    S secs[5] = {{"Dynamics",1},{"Filter",2},{"EQ",3},{"Drive",4},{"Out",6}};
    for (auto &sec : secs)
    {
      Rig g; long d; double rms; bool fin;
      if (sec.which == 1) { g.s.mDynEngage.set(1); g.s.mDynAmount.hardSet(1.0f); g.s.mDynThresh.hardSet(0.2f); }
      if (sec.which == 2) { g.s.mFilterEngage.set(1); g.s.mHpFreq.hardSet(500.0f); }
      if (sec.which == 3) { g.s.mEqEngage.set(1); g.s.mEqMidGain.hardSet(12.0f); g.s.mEqMidFreq.hardSet(220.0f); }
      if (sec.which == 4) { g.s.mDriveEngage.set(1); g.s.mDrive.hardSet(0.8f); }
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

  // 3b. the Filter section parked at its extremes must also be free.
  // Both sides self-skip, so an engaged-but-unused Filter costs nothing
  // and colours nothing.
  {
    Rig g; long d; double rms; bool fin;
    g.s.mFilterEngage.set(1);   // HP at 20 Hz, LP at 20 kHz - both parked
    g.run(400, d, rms, fin);
    snprintf(buf, sizeof buf, "(%ld differ with Filter engaged but parked)", d);
    chk(d == 0, "engaging Filter at its extremes is still bit-identical", buf);
  }

  // 3c. Drive engaged with both controls at zero must also be free.
  {
    Rig g; long d; double rms; bool fin;
    g.s.mDriveEngage.set(1);
    g.run(400, d, rms, fin);
    snprintf(buf, sizeof buf, "(%ld differ with Drive engaged at zero)", d);
    chk(d == 0, "engaging Drive at zero is still bit-identical", buf);
  }

  // 3d. The Drive knob must have an ARC, not just "more": dry to Spiral
  // over the first half, Spiral to Density over the second. Two
  // different curves meeting in the middle, so THD should rise
  // monotonically and the halves should not be the same curve.
  {
    printf("  drive arc, THD%% at -6 dBFS:\n");
    double prev = -1; bool rising = true;
    for (double dv : {0.0, 0.25, 0.5, 0.75, 1.0})
    {
      ChannelStrip s; std::vector<float> l(FR), r(FR), ol(FR), orr(FR);
      s.mInL.setBuffer(l.data()); s.mInR.setBuffer(r.data());
      s.mOutL.setBuffer(ol.data()); s.mOutR.setBuffer(orr.data());
      s.mDriveEngage.set(1); s.mDrive.hardSet((float)dv);
      const int N = 32768; const double PF = SR*683.0/N;
      std::vector<double> y; y.reserve(N); long n = 0;
      for (int b = 0; b < N/FR + 40; b++)
      { for (int i = 0; i < FR; i++) { l[i]=r[i]=(float)(sin(2.0*M_PI*PF*n/SR)*0.5); n++; }
        s.process();
        if (b >= 40) for (int i = 0; i < FR && (int)y.size() < N; i++) y.push_back(ol[i]); }
      double f1=0,h=0;
      for (int k=1;k<=8;k++){ double re=0,im=0;
        for (int i=0;i<N;i++){ double a=2.0*M_PI*PF*k*i/SR; re+=y[i]*cos(a); im-=y[i]*sin(a);} 
        double m=sqrt(re*re+im*im); if(k==1)f1=m; else h+=m*m; }
      const double t = 100.0*sqrt(h)/(f1+1e-30);
      printf("      drive %.2f -> %7.3f%%\n", dv, t);
      if (dv > 0.0 && t < prev) rising = false;
      prev = t;
    }
    chk(rising, "Drive rises monotonically across its arc");
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
