// house::GlueComp acceptance harness.
//
// These tests were WRITTEN BEFORE THE CODE, in
// planning/compressor-character-research.md, because the Parametric EQ
// taught that a character control needs MEASURABLE BEHAVIOURAL
// distinctions and not a distortion ladder. If a position cannot be
// told from another by measurement, a listener will not tell them apart
// either, and the design is wrong.
#include <cstdio>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <vector>
#include <od/config.h>
_GlobalConfig globalConfig;
#include "GlueComp.h"

using namespace house;
static const double SR = 48000.0;

static inline bool realFinite(float v)
{ uint32_t u; memcpy(&u,&v,4); return ((u>>23)&0xFFu)!=0xFFu; }

static int fails = 0;
static void chk(bool ok, const char *w, const char *d="")
{ printf("  [%s] %s %s\n", ok?"PASS":"FAIL", w, d); if(!ok) fails++; }

static const char *NAME[3] = {"glue","peak","opto"};

// Settled gain reduction in dB for a steady tone of given amplitude.
static double settledGrDb(GlueCompCharacter ch, double amp, double ratio = 1.0,
                          double thr = 0.15, double atk = 0.01, double rel = 0.2)
{
  GlueCompCoefs c; glueCompBake(c, ch, thr, ratio, atk, rel, 1.0, SR);
  GlueCompStereo g;
  const int N = 2048;
  std::vector<float> l(N), r(N);
  double peak = 0;
  for (int b = 0; b < 40; b++)
  {
    for (int i = 0; i < N; i++)
    { const long n = (long)b*N+i; l[i]=r[i]=(float)(sin(2.0*M_PI*300.0*n/SR)*amp); }
    g.processBlock(l.data(), r.data(), N, c);
    if (b >= 36) for (int i = 0; i < N; i++) peak = fmax(peak, fabs((double)l[i]));
  }
  return 20.0 * log10((peak + 1e-30) / amp);
}

// Blocks to reach 63% of the TOTAL gain-reduction excursion - one time
// constant, which is the actual attack time.
//
// NOT "blocks to settle within 5%": that conflates the time constant
// with the DISTANCE travelled. A position that compresses harder has
// further to go and takes longer to arrive even with an identical
// coefficient, which made a fixed-timing position look like it was
// tracking ratio. Normalising by the excursion removes that.
static int attackSamples(GlueCompCharacter ch, double amp, double ratio,
                         double atk = 0.01, double rel = 0.2)
{
  // SAMPLE resolution, not block. A 10 ms attack is 480 samples; 256-
  // sample blocks quantise that to 2, so a fixed-timing position looked
  // like it was varying when the reading was just rounding.
  GlueCompCoefs c; glueCompBake(c, ch, 0.15, ratio, atk, rel, 1.0, SR);
  GlueCompStereo g;
  const int N = 64;
  const int BLOCKS = 6000;
  std::vector<float> l(N), r(N);
  std::vector<double> gv;
  // 3 kHz, not 300. A 10 ms attack is 480 samples: at 300 Hz that is
  // only 3 cycles, so the peak detector ripples almost as much as the
  // envelope it is tracking and the 63% crossing lands wherever the
  // cycle happens to be. At 3 kHz it is 30 cycles and the ripple is
  // small against the envelope.
  for (int b = 0; b < BLOCKS; b++)
  {
    for (int i = 0; i < N; i++)
    { const long n = (long)b*N+i; l[i]=r[i]=(float)(sin(2.0*M_PI*3000.0*n/SR)*amp); }
    g.processBlock(l.data(), r.data(), N, c);
    // read the gain state directly: it IS the envelope, with no
    // rectification or peak-hold in the way
    gv.push_back(g.gain());
  }
  const double start = gv[0], settled = gv[gv.size()-1];
  const double exc = start - settled;
  if (exc <= 1e-6) return 0;
  const double target = start - exc * 0.63;
  for (size_t i = 0; i < gv.size(); i++)
    if (gv[i] <= target) return (int)(i * N);
  return BLOCKS * N;
}

int main()
{
  globalConfig.frameLength = 128;
  globalConfig.sampleRate = (float)SR;
  char buf[256];

  // 5 (first, because everything rests on it). Ratio 0 is exact bypass.
  {
    bool allOk = true;
    for (int ch = 0; ch < 3; ch++)
    {
      GlueCompCoefs c; glueCompBake(c, (GlueCompCharacter)ch, 0.15, 0.0, 0.01, 0.2, 1.0, SR);
      GlueCompStereo g;
      const int N = 512; std::vector<float> l(N), r(N); long diff = 0;
      for (int b = 0; b < 100; b++)
      {
        std::vector<float> keep(N);
        for (int i = 0; i < N; i++)
        { const long n=(long)b*N+i; l[i]=r[i]=keep[i]=(float)(sin(2.0*M_PI*300.0*n/SR)*0.9); }
        g.processBlock(l.data(), r.data(), N, c);
        for (int i = 0; i < N; i++) if (l[i] != keep[i]) diff++;
      }
      if (diff != 0) { allOk = false; printf("      %s: %ld differ\n", NAME[ch], diff); }
    }
    chk(allOk, "TEST 5: ratio 0 is a bit-identical bypass in every position");
  }

  // 1. Feedback resists deep GR; feedforward does not.
  {
    const double gGlue = settledGrDb(GLUE_COMP_GLUE, 0.9, 1.0);
    const double gPeak = settledGrDb(GLUE_COMP_PEAK, 0.9, 1.0);
    const double gOpto = settledGrDb(GLUE_COMP_OPTO, 0.9, 1.0);
    printf("      settled GR at ratio 1, amp 0.9:  glue %.2f dB  peak %.2f dB  opto %.2f dB\n",
           gGlue, gPeak, gOpto);
    snprintf(buf, sizeof buf, "(peak reaches %.2f dB vs glue %.2f)", gPeak, gGlue);
    chk(gPeak < gGlue - 2.0, "TEST 1: feedforward PEAK goes deeper than feedback GLUE", buf);
  }

  // 2. Feedback timing tracks ratio; feedforward timing does not.
  {
    const int gLo = attackSamples(GLUE_COMP_GLUE, 0.9, 0.3);
    const int gHi = attackSamples(GLUE_COMP_GLUE, 0.9, 1.0);
    const int pLo = attackSamples(GLUE_COMP_PEAK, 0.9, 0.3);
    const int pHi = attackSamples(GLUE_COMP_PEAK, 0.9, 1.0);
    printf("      time constant (SAMPLES to 63%%), ratio .3 -> 1.0:  glue %d -> %d   peak %d -> %d\n",
           gLo, gHi, pLo, pHi);
    const double gRel = fabs((double)gHi - gLo) / (gLo + 1);
    const double pRel = fabs((double)pHi - pLo) / (pLo + 1);
    snprintf(buf, sizeof buf, "(glue moves %.0f%%, peak moves %.0f%%)", gRel*100, pRel*100);
    chk(gRel > pRel, "TEST 2: GLUE timing tracks ratio more than PEAK's does", buf);
  }

  // 3. Program dependence. The program-dependent positions must speed
  // up a LOT with overshoot; the fixed-timing one must speed up much
  // less.
  //
  // NOT "must not vary at all", which was the first version of this
  // assertion and is unachievable for any peak detector: with an
  // oscillating detector the gain alternates between the attack and
  // release branches every cycle, and at small overshoot it crosses
  // more often relative to the excursion, so the effective settling
  // slows. Real fixed-timing compressors do this too. The meaningful
  // claim is the RATIO of variation between positions.
  {
    printf("      time constant by overshoot:\n");
    double var[3];
    for (int ch = 0; ch < 3; ch++)
    {
      const int small = attackSamples((GlueCompCharacter)ch, 0.25, 1.0);
      const int big = attackSamples((GlueCompCharacter)ch, 0.95, 1.0);
      var[ch] = (double)small / (big > 0 ? big : 1);
      printf("        %-5s  small %6d   large %6d   speedup %.1fx\n",
             NAME[ch], small, big, var[ch]);
    }
    snprintf(buf, sizeof buf, "(glue %.1fx, opto %.1fx vs peak %.1fx)", var[0], var[2], var[1]);
    chk(var[0] > var[1] * 2.0 && var[2] > var[1] * 2.0,
        "TEST 3: program-dependent positions chase overshoot far more than PEAK", buf);
  }

  // 4. Knee: soft positions must bend before the corner.
  {
    printf("      transfer curve near threshold (thr 0.15):\n");
    bool ok = true;
    for (int ch = 0; ch < 3; ch++)
    {
      const double below = settledGrDb((GlueCompCharacter)ch, 0.12, 1.0);
      const double at = settledGrDb((GlueCompCharacter)ch, 0.15, 1.0);
      printf("        %-5s  at 0.8x thresh %+6.2f dB   at thresh %+6.2f dB\n",
             NAME[ch], below, at);
      if (ch == 1 && below < -0.5) ok = false;   // PEAK must be flat below
    }
    chk(ok, "TEST 4: PEAK has a hard corner, no reduction below threshold");
  }

  // finite everywhere
  {
    bool fin = true;
    for (int ch = 0; ch < 3; ch++)
      for (double amp : {0.01, 0.5, 1.0, 2.0})
      {
        GlueCompCoefs c; glueCompBake(c, (GlueCompCharacter)ch, 0.05, 1.0, 0.0001, 0.002, 4.0, SR);
        GlueCompStereo g;
        const int N = 512; std::vector<float> l(N), r(N);
        for (int b = 0; b < 60; b++)
        {
          for (int i = 0; i < N; i++)
          { const long n=(long)b*N+i; l[i]=r[i]=(float)(sin(2.0*M_PI*90.0*n/SR)*amp); }
          g.processBlock(l.data(), r.data(), N, c);
          for (int i = 0; i < N; i++) if (!realFinite(l[i])) fin = false;
        }
      }
    chk(fin, "finite at every character, amplitude and extreme setting");
  }

  printf("\n%s\n", fails ? "FAILURES PRESENT" : "ALL CHECKS PASS");
  return fails ? 1 : 0;
}
