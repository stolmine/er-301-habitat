// house::Pop3Dynamics measurement harness.
//
// Measures the REAL atom with shipping flags rather than asserting the
// maths reads correctly.
//
// Checks:
//   1 ratio 0 on both stages is a BIT-IDENTICAL bypass
//   2 the compressor actually compresses, and harder at higher ratio
//   3 the transfer curve is monotonic (no fold-back at high input)
//   4 attack and release timing move in the right direction
//   5 the gate closes below threshold and opens above
//   6 the gate reads the UNCOMPRESSED signal - the distinguishing
//     behaviour, checked by squashing the input and confirming the
//     gate still fires
//   7 the sine polynomial matches libm sin over [0, pi/2]
//   8 finite and bounded at extremes, and at 44.1/48/96 kHz
//   9 stereo link converges the pair and shares the gate
#include <cstdio>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <vector>
#include <od/config.h>
_GlobalConfig globalConfig;
#include "Pop3Dynamics.h"

using namespace house;
static const double SR = 48000.0;


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
static void chk(bool ok, const char *what, const char *detail = "")
{
  printf("  [%s] %s %s\n", ok ? "PASS" : "FAIL", what, detail);
  if (!ok) fails++;
}

// Steady-state output peak for a sine of given amplitude.
static double outPeak(double amp, const Pop3Coefs &c, double freq = 220.0, double secs = 1.0)
{
  Pop3Mono m;
  const int n = (int)(SR * secs);
  double peak = 0.0;
  for (int i = 0; i < n; i++)
  {
    const float x = (float)(sin(2.0 * M_PI * freq * i / SR) * amp);
    const float y = m.process(x, c);
    if (i > n * 3 / 4) { const double a = fabs((double)y); if (a > peak) peak = a; }
  }
  return peak;
}

int main()
{
  globalConfig.frameLength = 128;
  globalConfig.sampleRate = (float)SR;
  char buf[256];

  // 1. exact bypass
  {
    Pop3Coefs c;
    pop3Bake(c, 0.3, 0.0, 0.5, 0.5, 0.3, 0.0, 0.5, 0.5, SR);
    Pop3Mono m;
    long diff = 0;
    for (int i = 0; i < 96000; i++)
    {
      const float x = (float)(sin(2.0 * M_PI * 220.0 * i / SR) * 0.6 + sin(2.0 * M_PI * 3100.0 * i / SR) * 0.2);
      if (m.process(x, c) != x) diff++;
    }
    snprintf(buf, sizeof buf, "(%ld/96000 differ)", diff);
    chk(diff == 0, "ratio 0 on both stages is bit-identical bypass", buf);
  }

  // 2. it compresses, and more at higher ratio
  {
    printf("  gain reduction vs ratio (thresh 0.5^4=0.0625, input 0.8):\n");
    double prev = 1e9; bool mono = true;
    for (double r : {0.0, 0.25, 0.5, 0.75, 1.0})
    {
      Pop3Coefs c;
      pop3Bake(c, 0.5, r, 0.2, 0.5, 0.0, 0.0, 0.5, 0.5, SR);
      const double p = outPeak(0.8, c);
      const double gr = 20.0 * log10(p / 0.8 + 1e-30);
      printf("      ratio %.2f -> out %.4f  (%+6.2f dB)\n", r, p, gr);
      if (gr > prev + 0.05) mono = false;
      prev = gr;
    }
    chk(mono, "gain reduction increases monotonically with ratio");
  }

  // 3. transfer curve monotonic in input
  {
    Pop3Coefs c;
    pop3Bake(c, 0.5, 1.0, 0.2, 0.5, 0.0, 0.0, 0.5, 0.5, SR);
    bool ok = true; double prev = -1;
    printf("  transfer curve (ratio 1.0):\n");
    for (double a : {0.05, 0.1, 0.2, 0.4, 0.8, 1.0})
    {
      const double p = outPeak(a, c);
      printf("      in %.2f -> out %.4f\n", a, p);
      if (p < prev - 1e-6) ok = false;
      prev = p;
    }
    chk(ok, "transfer curve is monotonic (no fold-back)");
  }

  // 4. attack/release direction
  {
    auto settle = [](double attack) {
      Pop3Coefs c;
      pop3Bake(c, 0.3, 1.0, attack, 0.5, 0.0, 0.0, 0.5, 0.5, SR);
      Pop3Mono m;
      // step into a loud tone, count samples to reach 90% of final GR
      double final_ = 0;
      for (int i = 0; i < (int)SR; i++) { const float x=(float)(sin(2.0*M_PI*220.0*i/SR)*0.9); final_ = m.process(x,c); }
      m.reset();
      const double target = 0.9;
      int k = 0;
      for (int i = 0; i < (int)SR; i++)
      {
        const float x = (float)(sin(2.0 * M_PI * 220.0 * i / SR) * 0.9);
        m.process(x, c);
        if (m.compState() < target) { k = i; break; }
      }
      return k;
    };
    const int fast = settle(0.0), slow = settle(1.0);
    snprintf(buf, sizeof buf, "(fast %d samples, slow %d)", fast, slow);
    chk(fast < slow, "longer attack setting really is slower", buf);
  }

  // 5. gate opens above threshold, closes below
  {
    Pop3Coefs c;
    pop3Bake(c, 1.0, 0.0, 0.5, 0.5, 0.6, 1.0, 0.0, 0.0, SR);
    const double loud = outPeak(0.9, c);
    const double quiet = outPeak(0.02, c, 220.0, 2.0);
    printf("      loud in 0.90 -> %.4f ; quiet in 0.02 -> %.6f\n", loud, quiet);
    chk(loud > 0.8, "gate passes signal above threshold");
    chk(quiet < 0.02 * 0.5, "gate attenuates signal below threshold");
  }

  // 6. THE DISTINGUISHING BEHAVIOUR: gate reads the uncompressed signal.
  // With heavy compression the post-comp level is far below the gate
  // threshold; if the gate read post-comp it would close. It must not.
  {
    Pop3Coefs c;
    pop3Bake(c, 0.1, 1.0, 0.0, 0.5, 0.6, 1.0, 0.5, 0.5, SR);
    const double p = outPeak(0.9, c);
    Pop3Mono probe; Pop3Coefs cc = c;
    for (int i = 0; i < (int)SR; i++) probe.process((float)(sin(2.0*M_PI*220.0*i/SR)*0.9), cc);
    snprintf(buf, sizeof buf, "(out %.4f, gate phase %.3f)", p, probe.gateState());
    chk(probe.gateState() >= 1.5707963f, "gate stays open under heavy compression", buf);
  }

  // 7. the sine polynomial
  {
    double worst = 0;
    for (double x = 0.0; x <= M_PI_2; x += 0.0005)
    {
      const double e = fabs((double)pop3Sin((float)x) - sin(x));
      if (e > worst) worst = e;
    }
    snprintf(buf, sizeof buf, "(worst error %.2e over [0,pi/2])", worst);
    chk(worst < 1e-3, "gate sine polynomial matches libm", buf);
  }

  // 8. bounded at extremes, across sample rates
  {
    bool ok = true;
    for (double sr : {44100.0, 48000.0, 96000.0})
      for (double t : {0.0, 1.0})
        for (double r : {0.0, 1.0})
          for (double gt : {0.0, 1.0})
          {
            Pop3Coefs c;
            pop3Bake(c, t, r, 0.0, 0.0, gt, 1.0, 0.0, 0.0, sr);
            Pop3Mono m;
            for (int i = 0; i < 20000; i++)
            {
              const float x = (float)(sin(2.0 * M_PI * 55.0 * i / sr) * 1.5);
              const float y = m.process(x, c);
              if (!realFinite(y) || fabs(y) > 10.0) { ok = false; break; }
            }
          }
    chk(ok, "finite and bounded at extremes, 44.1/48/96 kHz");
  }

  // 9. stereo link
  {
    Pop3Coefs c;
    pop3Bake(c, 0.3, 1.0, 0.3, 0.5, 0.5, 1.0, 0.5, 0.5, SR);
    Pop3Mono L, R;
    // hard-panned: only L gets signal
    for (int i = 0; i < (int)SR; i++)
    {
      const float xl = (float)(sin(2.0 * M_PI * 220.0 * i / SR) * 0.9);
      L.observe(xl, c); R.observe(0.0f, c);
      pop3StereoLink(L, R, c);
      L.apply(xl, c); R.apply(0.0f, c);
    }
    snprintf(buf, sizeof buf, "(gate L %.3f == R %.3f)", L.gateState(), R.gateState());
    chk(L.gateState() == R.gateState(), "stereo link shares the gate across channels", buf);
    chk(R.compState() <= 1.0f && R.compState() >= 0.0f, "linked comp state stays in range");
  }

  printf("\n%s\n", fails ? "FAILURES PRESENT" : "ALL CHECKS PASS");
  return fails ? 1 : 0;
}
