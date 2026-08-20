// house::StftFrontEnd measurement harness.
//
// The property that matters is PERFECT RECONSTRUCTION: forward then
// inverse with no spectral edit must return the input, delayed by one
// window, at unity gain. Everything a spectral unit does is a
// perturbation of that, so if it is wrong every consumer inherits a
// gain error or a comb.
//
// Checks:
//   1 reconstruction is unity-gain and sample-accurate after warmup
//   2 the COLA constant is DERIVED correctly (compare to the 1.5
//     literal the originals hard-coded)
//   3 reconstruction holds for tones, noise and transients alike
//   4 the pffft real layout is what the comments claim (DC and Nyquist
//     share slot 0) - verified with a DC probe and a Nyquist probe
//   5 output ring is properly consumed: no residue, no drift over a
//     long run
//   6 a spectral edit in the middle actually reaches the output
#include <cstdio>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <vector>
#include <od/config.h>
_GlobalConfig globalConfig;
#include "StftFrontEnd.h"

using namespace stolmine;
static const int N = StftFrontEnd::kFFT;


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

// Run `in` through a pass-through STFT and return the output.
static std::vector<float> passthru(const std::vector<float> &in)
{
  StftFrontEnd f;
  std::vector<float> spec(N), out(in.size());
  for (size_t i = 0; i < in.size(); i++)
  {
    f.push(in[i]);
    out[i] = f.consume();
    if (f.tick())
    {
      f.gather();
      f.forward(spec.data());
      f.inverseAndOverlap(spec.data());
    }
  }
  return out;
}

int main()
{
  globalConfig.frameLength = 128;
  globalConfig.sampleRate = 48000.0f;
  char buf[256];

  {
    StftFrontEnd f;
    chk(f.ready(), "allocates cleanly");
    // 2. the derived COLA constant against the hard-coded literal
    const float lit = 1.0f / ((float)N * 1.5f);
    snprintf(buf, sizeof buf, "(derived %.9g vs literal 1/(N*1.5) %.9g)", f.norm(), lit);
    chk(fabs(f.norm() - lit) / lit < 1e-6, "derived COLA norm matches the originals' literal", buf);
  }

  // 1 + 3. reconstruction on three signal types
  {
    const int L = N * 12;
    struct Case { const char *name; std::vector<float> sig; };
    std::vector<Case> cases;
    {
      std::vector<float> s(L);
      for (int i = 0; i < L; i++) s[i] = (float)(sin(2.0 * M_PI * 440.0 * i / 48000.0) * 0.7);
      cases.push_back({"440 Hz tone", s});
    }
    {
      std::vector<float> s(L); uint32_t r = 12345u;
      for (int i = 0; i < L; i++) { r = r * 1103515245u + 12345u; s[i] = ((float)((r >> 16) & 0xFFFF) / 32767.5f - 1.0f) * 0.5f; }
      cases.push_back({"white noise", s});
    }
    {
      std::vector<float> s(L, 0.0f);
      for (int i = 0; i < L; i += N + 137) if (i < L) s[i] = 0.9f;
      cases.push_back({"impulses", s});
    }
    printf("  perfect reconstruction (delay %d samples):\n", N - StftFrontEnd::kHop);
    bool ok = true;
    for (auto &c : cases)
    {
      const std::vector<float> out = passthru(c.sig);
      // Find the delay by best correlation, then measure error past warmup.
      int bestD = 0; double bestC = -1e18;
      for (int d = 0; d <= N; d++)
      {
        double acc = 0;
        for (int i = N * 4; i < (int)c.sig.size() - N; i++) acc += (double)out[i] * c.sig[i - d];
        if (acc > bestC) { bestC = acc; bestD = d; }
      }
      double num = 0, den = 0, peak = 0;
      for (int i = N * 6; i < (int)c.sig.size() - N; i++)
      {
        const double e = (double)out[i] - c.sig[i - bestD];
        num += e * e; den += (double)c.sig[i - bestD] * c.sig[i - bestD];
        if (fabs(e) > peak) peak = fabs(e);
      }
      const double errDb = 10.0 * log10((num / (den + 1e-30)) + 1e-30);
      printf("      %-12s delay %4d  residual %7.1f dB  peak err %.2e\n", c.name, bestD, errDb, peak);
      if (errDb > -100.0) ok = false;
    }
    chk(ok, "reconstruction residual below -100 dB on tone, noise and transients");
  }

  // 4. pffft real layout: DC and Nyquist share slot 0
  {
    StftFrontEnd f;
    std::vector<float> spec(N), dc(N, 0.5f), nyq(N);
    for (int i = 0; i < N; i++) nyq[i] = (i & 1) ? -0.5f : 0.5f;
    f.gatherFrom(dc.data()); f.forward(spec.data());
    const float dc0 = spec[0], dc1 = spec[1];
    f.gatherFrom(nyq.data()); f.forward(spec.data());
    const float ny0 = spec[0], ny1 = spec[1];
    printf("      DC probe      -> slot0[0]=%10.2f  slot0[1]=%10.2f\n", dc0, dc1);
    printf("      Nyquist probe -> slot0[0]=%10.2f  slot0[1]=%10.2f\n", ny0, ny1);
    chk(fabs(dc0) > fabs(dc1) * 100.0, "slot0[0] carries DC");
    chk(fabs(ny1) > fabs(ny0) * 100.0, "slot0[1] carries Nyquist");
  }

  // 5. no residue or drift over a long run.
  // The probe frequency must fit a WHOLE number of cycles into the
  // measurement window or RMS depends on start phase: 220 Hz gives
  // 18.773 cycles per 4096 samples, whose 1/(2*pi*cycles) artifact is
  // 0.073 dB - larger than any drift worth detecting, and it read as a
  // 0.043 dB failure before this was fixed.
  {
    const int L = N * 200;
    const double PF = 48000.0 * 19.0 / (N * 4);   // exactly 19 cycles/window
    std::vector<float> s(L);
    for (int i = 0; i < L; i++) s[i] = (float)(sin(2.0 * M_PI * PF * i / 48000.0) * 0.6);
    const std::vector<float> out = passthru(s);
    double early = 0, late = 0;
    for (int i = N * 8; i < N * 12; i++) early += (double)out[i] * out[i];
    for (int i = L - N * 5; i < L - N; i++) late += (double)out[i] * out[i];
    const double drift = 10.0 * log10((late / (early + 1e-30)) + 1e-30);
    snprintf(buf, sizeof buf, "(level drift %+.4f dB over 200 frames)", drift);
    chk(fabs(drift) < 0.01, "no level drift over a long run", buf);
    bool fin = true;
    for (float v : out) if (!realFinite(v)) fin = false;
    chk(fin, "output stays finite");
  }

  // 6. a spectral edit reaches the output
  {
    const int L = N * 12;
    std::vector<float> s(L);
    for (int i = 0; i < L; i++) s[i] = (float)(sin(2.0 * M_PI * 440.0 * i / 48000.0) * 0.7);
    StftFrontEnd f; std::vector<float> spec(N), out(L);
    for (int i = 0; i < L; i++)
    {
      f.push(s[i]); out[i] = f.consume();
      if (f.tick())
      {
        f.gather(); f.forward(spec.data());
        for (int k = 0; k < N; k++) spec[k] *= 0.5f;   // -6 dB in the spectral domain
        f.inverseAndOverlap(spec.data());
      }
    }
    double a = 0, b = 0;
    const std::vector<float> ref = passthru(s);
    for (int i = N * 6; i < L - N; i++) { a += (double)out[i] * out[i]; b += (double)ref[i] * ref[i]; }
    const double db = 10.0 * log10((a / (b + 1e-30)) + 1e-30);
    snprintf(buf, sizeof buf, "(halving every bin gave %+.3f dB)", db);
    chk(fabs(db + 6.0206) < 0.01, "a spectral edit reaches the output at the right level", buf);
  }

  printf("\n%s\n", fails ? "FAILURES PRESENT" : "ALL CHECKS PASS");
  return fails ? 1 : 0;
}
