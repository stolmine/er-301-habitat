// Impasto attack/release override harness.
//
// THE BUG THIS EXISTS TO PREVENT RECURRING: the Attack and Release
// plies were wired in Lua, registered in C++, and NEVER READ BY THE
// DSP. setBandBias(i,3,..) and (i,4,..) wrote slots the sample path
// ignored, so turning either control did nothing while looking
// completely correct in the source. It survived an audit as "reported,
// not independently confirmed" - reading the code was not enough.
//
// So this measures ENVELOPE TIMING THROUGH THE AUDIO, not whether a
// parameter appears connected.
#include <cstdio>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <vector>
#include <od/objects/Object.h>
_GlobalConfig globalConfig;
#include "MultibandCompressor.cpp"

using namespace stolmine;
static const double SR = 48000.0;
static const int FR = 128;

static inline bool realFinite(float v)
{ uint32_t u; memcpy(&u,&v,4); return ((u>>23)&0xFFu)!=0xFFu; }

static int fails = 0;
static void chk(bool ok, const char *w, const char *d="")
{ printf("  [%s] %s %s\n", ok?"PASS":"FAIL", w, d); if(!ok) fails++; }

// Step a loud tone in and count samples until the output settles to
// within 5% of its final compressed level. That IS the attack time,
// observed rather than asserted.
struct Probe
{
  MultibandCompressor c;
  std::vector<float> in, sc, out;
  od::Parameter thr{"t", 0.15f}, rat{"r", 8.0f}, spd{"s", 0.3f};
  od::Parameter atk{"a", 0.0f}, rel{"e", 0.0f}, wgt{"w", 1.0f};
  Probe()
  {
    in.resize(FR); sc.resize(FR); out.resize(FR);
    c.mIn.setBuffer(in.data());
    c.mSidechain.setBuffer(sc.data());
    c.mOut.setBuffer(out.data());
    for (int b = 0; b < 3; b++)
    {
      c.setBandBias(b, 0, &thr);
      c.setBandBias(b, 1, &rat);
      c.setBandBias(b, 2, &spd);
      c.setBandBias(b, 3, &atk);
      c.setBandBias(b, 4, &rel);
      c.setBandBias(b, 5, &wgt);
    }
  }
  // Returns BLOCKS until the per-block peak envelope settles.
  //
  // Per-BLOCK peaks, not instantaneous samples: a sine passes through
  // every value each cycle, so an instantaneous test fires on the first
  // sample and reports 0 regardless of the timing. A compressor's
  // output starts loud and falls, so the measure is when the peak
  // envelope stops falling.
  int attackBlocks()
  {
    long n = 0;
    std::vector<double> pk;
    for (int b = 0; b < 900; b++)
    {
      for (int i = 0; i < FR; i++)
      { in[i] = (float)(sin(2.0*M_PI*300.0*n/SR)*0.9); sc[i]=in[i]; n++; }
      c.process();
      double p = 0;
      for (int i = 0; i < FR; i++) p = fmax(p, fabs((double)out[i]));
      pk.push_back(p);
    }
    // settled = mean of the last 50 blocks
    double settled = 0;
    for (size_t i = pk.size()-50; i < pk.size(); i++) settled += pk[i];
    settled /= 50.0;
    // first block within 5% of settled, having started above it
    for (size_t i = 4; i < pk.size(); i++)
      if (pk[i] <= settled * 1.05) return (int)i;
    return (int)pk.size();
  }
};

int main()
{
  globalConfig.frameLength = FR;
  globalConfig.sampleRate = (float)SR;
  char buf[256];

  // 1. Speed still drives timing when the overrides are at 0.
  {
    Probe fast, slow;
    fast.spd.mV = 0.9f;   // fast attack
    slow.spd.mV = 0.05f;  // slow attack
    const int f = fast.attackBlocks(), s = slow.attackBlocks();
    snprintf(buf, sizeof buf, "(speed 0.9 -> %d blocks, speed 0.05 -> %d)", f, s);
    chk(f < s, "Speed still drives attack when overrides are 0", buf);
  }

  // 2. THE FIX: with Speed held constant, the Attack override must
  // change the timing. Before the fix this was identical either way.
  {
    Probe a, b;
    a.spd.mV = 0.5f; b.spd.mV = 0.5f;
    a.atk.mV = 0.0f;      // follow Speed
    b.atk.mV = 0.05f;     // 50 ms, far slower than Speed 0.5 gives
    const int x = a.attackBlocks(), y = b.attackBlocks();
    snprintf(buf, sizeof buf, "(auto -> %d blocks, 50 ms override -> %d)", x, y);
    chk(y > x, "Attack OVERRIDE changes timing at constant Speed", buf);
  }

  // 3. Zero really means auto, not "zero seconds".
  {
    Probe a, b;
    a.spd.mV = 0.2f; a.atk.mV = 0.0f;
    b.spd.mV = 0.2f; b.atk.mV = 0.0f;
    const int x = a.attackBlocks(), y = b.attackBlocks();
    chk(x == y, "attack 0 is deterministic (follows Speed, not 0 s)");
    Probe c2; c2.spd.mV = 0.95f; c2.atk.mV = 0.0f;
    const int z = c2.attackBlocks();
    snprintf(buf, sizeof buf, "(speed .2 auto -> %d blocks, speed .95 auto -> %d)", x, z);
    chk(z != x, "attack 0 tracks Speed rather than pinning", buf);
  }

  // 4. output stays finite across the override range
  {
    bool fin = true;
    for (float av : {0.0f, 0.0001f, 0.01f, 0.1f})
      for (float rv : {0.0f, 0.001f, 0.5f, 1.5f})
      {
        Probe p; p.atk.mV = av; p.rel.mV = rv;
        long n = 0;
        for (int b = 0; b < 200; b++)
        {
          for (int i = 0; i < FR; i++) { p.in[i] = (float)(sin(2.0*M_PI*300.0*n/SR)*0.9); p.sc[i]=p.in[i]; n++; }
          p.c.process();
          for (int i = 0; i < FR; i++) if (!realFinite(p.out[i])) fin = false;
        }
      }
    chk(fin, "finite across the whole override range");
  }

  printf("\n%s\n", fails ? "FAILURES PRESENT" : "ALL CHECKS PASS");
  return fails ? 1 : 0;
}
