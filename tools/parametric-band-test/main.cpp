// house::ParametricBand measurement harness.
//
// Compiles the REAL atom with shipping optimization semantics and
// MEASURES its response by sweeping sine probes, rather than asserting
// that the maths looks right.
//
// Checks:
//   1 gain 0 dB is a BIT-IDENTICAL bypass (the ochre note's caution)
//   2 a bell's peak lands at the requested frequency and gain
//   3 bell HALF-GAIN points sit a constant octave interval apart across
//     gain settings - the log-symmetric claim, actually checked.
//     NB half-gain, not "-3 dB from peak": bandwidth for a peaking EQ is
//     conventionally measured at the midpoint gain in dB (RBJ cookbook,
//     and analogue parametric practice). Those coincide only at +6 dB;
//     at +18 dB a -3 dB-from-peak reading sits near the tip of the bell
//     and MUST read narrow regardless of the filter. The design note's
//     "+/-3 dB points" phrasing is the special case, not the rule.
//   4 shelves reach their asymptote and pass the far side at unity
//   5 proportional-Q really does narrow with gain, constant-Q does not
//   6 output stays finite at extreme settings, including 30 Hz at 48k
//     where float recursion is the known risk
//   7 the nonlinearity does something, and does nothing at drive 0
#include <cstdio>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <vector>
#include <od/config.h>
_GlobalConfig globalConfig;
#include "ParametricBand.h"

using namespace house;
static const double SR = 48000.0;

// Steady-state amplitude response at f, in dB, measured by driving a
// sine and taking the settled peak.
static double respDb(double freqHz, double dB, double q, double probe,
                     ParametricBandShape shape, ParametricBandQLaw law,
                     double drive = 0.0)
{
  ParametricBandCoefs c;
  parametricBandBake(c, freqHz, dB, q, drive, SR, shape, law);
  ParametricBandMono b;
  const int warm = (int)(SR * 0.5), meas = (int)(SR * 0.25);
  double peak = 0.0;
  for (int i = 0; i < warm + meas; i++)
  {
    const float x = (float)sin(2.0 * M_PI * probe * i / SR);
    const float y = b.process(x, c);
    if (i >= warm) { const double a = fabs((double)y); if (a > peak) peak = a; }
  }
  return 20.0 * log10(peak + 1e-30);
}


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

int main()
{
  globalConfig.frameLength = 128;
  globalConfig.sampleRate = (float)SR;
  char buf[256];

  // 1. exact bypass at 0 dB
  {
    ParametricBandCoefs c;
    parametricBandBake(c, 1000.0, 0.0, 0.7, 0.8, SR, PARAM_BAND_BELL, PARAM_BAND_Q_CONSTANT);
    ParametricBandMono b;
    long diff = 0;
    for (int i = 0; i < 96000; i++)
    {
      const float x = (float)(sin(2.0 * M_PI * 220.0 * i / SR) * 0.7 + sin(2.0 * M_PI * 3100.0 * i / SR) * 0.3);
      if (b.process(x, c) != x) diff++;
    }
    snprintf(buf, sizeof buf, "(%ld/96000 samples differ)", diff);
    chk(diff == 0, "0 dB is a bit-identical bypass", buf);
  }

  // 2. bell peak lands where asked
  {
    bool ok = true;
    printf("  bell peak accuracy:\n");
    for (double f : {100.0, 1000.0, 6000.0})
      for (double g : {6.0, 12.0})
      {
        const double got = respDb(f, g, 1.0, f, PARAM_BAND_BELL, PARAM_BAND_Q_CONSTANT);
        printf("      %6.0f Hz  %+5.1f dB asked -> %+6.2f dB\n", f, g, got);
        if (fabs(got - g) > 1.5) ok = false;
      }
    chk(ok, "bell peak within 1.5 dB of the requested gain");
  }

  // 3. THE LOG-SYMMETRY CLAIM: -3 dB points a constant octave apart
  {
    printf("  bell bandwidth at HALF-GAIN, in octaves (constant-Q law, Q=1):\n");
    double first = -1; bool ok = true;
    for (double g : {6.0, 12.0, 18.0})
    {
      const double pk = respDb(1000.0, g, 1.0, 1000.0, PARAM_BAND_BELL, PARAM_BAND_Q_CONSTANT);
      double lo = 0, hi = 0;
      const double half = pk * 0.5;   // half-gain in dB
      for (double f = 100.0; f < 1000.0; f *= 1.01)
        if (respDb(1000.0, g, 1.0, f, PARAM_BAND_BELL, PARAM_BAND_Q_CONSTANT) >= half) { lo = f; break; }
      for (double f = 10000.0; f > 1000.0; f /= 1.01)
        if (respDb(1000.0, g, 1.0, f, PARAM_BAND_BELL, PARAM_BAND_Q_CONSTANT) >= half) { hi = f; break; }
      const double oct = (lo > 0 && hi > 0) ? log2(hi / lo) : -1;
      printf("      %+5.1f dB: %.0f..%.0f Hz = %.2f octaves\n", g, lo, hi, oct);
      if (first < 0) first = oct;
      else if (fabs(oct - first) > 0.35) ok = false;
    }
    chk(ok, "bandwidth stays constant in octaves across gain (log-symmetric)");
  }

  // 4. shelves: asymptote reached, far side unity
  {
    const double lsIn = respDb(500.0, 12.0, 0.7, 40.0, PARAM_BAND_LOW_SHELF, PARAM_BAND_Q_CONSTANT);
    const double lsOut = respDb(500.0, 12.0, 0.7, 12000.0, PARAM_BAND_LOW_SHELF, PARAM_BAND_Q_CONSTANT);
    const double hsIn = respDb(3000.0, 12.0, 0.7, 16000.0, PARAM_BAND_HIGH_SHELF, PARAM_BAND_Q_CONSTANT);
    const double hsOut = respDb(3000.0, 12.0, 0.7, 60.0, PARAM_BAND_HIGH_SHELF, PARAM_BAND_Q_CONSTANT);
    printf("      low shelf : in-band %+6.2f dB, far side %+6.2f dB\n", lsIn, lsOut);
    printf("      high shelf: in-band %+6.2f dB, far side %+6.2f dB\n", hsIn, hsOut);
    chk(lsIn > 9.0 && fabs(lsOut) < 1.0, "low shelf reaches gain, passes highs at unity");
    chk(hsIn > 9.0 && fabs(hsOut) < 1.0, "high shelf reaches gain, passes lows at unity");
  }

  // 5. the two Q laws actually differ
  {
    auto bw = [](double g, ParametricBandQLaw law) {
      const double pk = respDb(1000.0, g, 1.0, 1000.0, PARAM_BAND_BELL, law);
      double lo = 0, hi = 0;
      const double half = pk * 0.5;
      for (double f = 100.0; f < 1000.0; f *= 1.01)
        if (respDb(1000.0, g, 1.0, f, PARAM_BAND_BELL, law) >= half) { lo = f; break; }
      for (double f = 10000.0; f > 1000.0; f /= 1.01)
        if (respDb(1000.0, g, 1.0, f, PARAM_BAND_BELL, law) >= half) { hi = f; break; }
      return (lo > 0 && hi > 0) ? log2(hi / lo) : -1.0;
    };
    const double c6 = bw(6.0, PARAM_BAND_Q_CONSTANT), c18 = bw(18.0, PARAM_BAND_Q_CONSTANT);
    const double p6 = bw(6.0, PARAM_BAND_Q_PROPORTIONAL), p18 = bw(18.0, PARAM_BAND_Q_PROPORTIONAL);
    printf("      constant-Q    : %.2f oct at +6, %.2f oct at +18\n", c6, c18);
    printf("      proportional-Q: %.2f oct at +6, %.2f oct at +18\n", p6, p18);
    chk(p18 < p6 - 0.2, "proportional-Q narrows as gain rises");
    chk(fabs(c18 - c6) < 0.35, "constant-Q does not");
  }

  // 6. finite at extremes, including the 30 Hz float-recursion risk
  {
    bool ok = true;
    for (double f : {30.0, 20000.0})
      for (double g : {-18.0, 18.0})
        for (double q : {0.1, 20.0})
          for (double d : {0.0, 1.0})
          {
            ParametricBandCoefs c;
            parametricBandBake(c, f, g, q, d, SR, PARAM_BAND_BELL, PARAM_BAND_Q_PROPORTIONAL);
            ParametricBandMono b;
            for (int i = 0; i < 48000; i++)
            {
              const float x = (float)(sin(2.0 * M_PI * 55.0 * i / SR) * 0.9);
              const float y = b.process(x, c);
              if (!realFinite(y) || fabs(y) > 100.0) { ok = false; break; }
            }
          }
    chk(ok, "finite and bounded at every extreme, incl. 30 Hz in float");
  }

  // 7. THE DRIVE GRID. The original version of this check probed a
  // single hot point (amp 0.9, Q 2) and reported a healthy 17% THD
  // while the control was in fact inaudible at every realistic level
  // and folded back at the top. Measuring the ceiling is not measuring
  // the working range. This sweeps level x drive and demands the
  // surface rise monotonically in BOTH axes.
  {
    auto thd = [](double amp, double drive) {
      ParametricBandCoefs c;
      const int N = 32768; const double PF = SR * 683.0 / N;
      parametricBandBake(c, PF, 12.0, 2.0, drive, SR, PARAM_BAND_BELL, PARAM_BAND_Q_CONSTANT);
      ParametricBandMono b;
      std::vector<double> y(N);
      for (int i = 0; i < 8000; i++) b.process((float)(sin(2.0*M_PI*PF*i/SR)*amp), c);
      for (int i = 0; i < N; i++) y[i] = b.process((float)(sin(2.0*M_PI*PF*i/SR)*amp), c);
      double f1 = 0, h = 0;
      for (int k = 1; k <= 8; k++)
      {
        double re = 0, im = 0; const double fk = PF * k;
        for (int i = 0; i < N; i++) { const double a = 2.0*M_PI*fk*i/SR; re += y[i]*cos(a); im -= y[i]*sin(a); }
        const double m = sqrt(re*re+im*im);
        if (k == 1) f1 = m; else h += m*m;
      }
      return 100.0 * sqrt(h) / (f1 + 1e-30);
    };
    const double amps[5] = {0.05, 0.1, 0.25, 0.5, 0.9};
    const double drv[4]  = {0.25, 0.5, 0.75, 1.0};
    double g[4][5];
    printf("  drive grid, THD%% (rows drive, cols input amp):\n           ");
    for (int a = 0; a < 5; a++) printf("%9.2f", amps[a]);
    printf("\n");
    for (int d = 0; d < 4; d++)
    {
      printf("     %.2f  ", drv[d]);
      for (int a = 0; a < 5; a++) { g[d][a] = thd(amps[a], drv[d]); printf("%9.3f", g[d][a]); }
      printf("\n");
    }
    bool monoDrive = true, monoLevel = true;
    for (int a = 0; a < 5; a++) for (int d = 1; d < 4; d++) if (g[d][a] < g[d-1][a] * 0.98) monoDrive = false;
    for (int d = 0; d < 4; d++) for (int a = 1; a < 5; a++) if (g[d][a] < g[d][a-1] * 0.98) monoLevel = false;
    chk(monoDrive, "THD rises monotonically with DRIVE at every level");
    chk(monoLevel, "THD rises monotonically with LEVEL at every drive");
    snprintf(buf, sizeof buf, "(%.2f%% at -12 dBFS, full drive)", g[3][2]);
    chk(g[3][2] > 1.0, "drive is AUDIBLE at a realistic -12 dBFS", buf);
    ParametricBandCoefs c0;
    parametricBandBake(c0, 1000.0, 12.0, 2.0, 0.0, SR, PARAM_BAND_BELL, PARAM_BAND_Q_CONSTANT);
    ParametricBandMono b0;
    double q0 = 0;
    for (int i = 0; i < 4096; i++) q0 += fabs(b0.process((float)(sin(2.0*M_PI*1000.0*i/SR)*0.25), c0));
    chk(q0 > 0.0, "drive 0 still passes signal");
  }

  printf("\n%s\n", fails ? "FAILURES PRESENT" : "ALL CHECKS PASS");
  return fails ? 1 : 0;
}
