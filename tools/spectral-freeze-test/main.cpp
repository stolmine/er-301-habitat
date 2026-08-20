// Spectral Freeze phase-vocoder - functional harness.
//
// Compiles the REAL mods/biome/SpectralFreeze.cpp against stub od:: headers with the
// shipping optimization semantics (-O3 -ffast-math -fno-tree-vectorize) and
// drives it with synthetic audio, asserting the properties the design claims
// rather than eyeballing a waveform.
//
// The unit is placement-new'd into 0x3B-poisoned storage, the way a recycled
// hardware heap block arrives, so any member the constructor forgets shows up
// as a real value instead of a convenient zero page. That is the trick that
// caught Breccia's uninitialized `decim`.
//
// Checks:
//   1 finite output, always, in every configuration
//   2 silence in = silence out, and no output before any freeze
//   3 a freeze SUSTAINS after the input is cut (the whole point)
//   4 the phase accumulator does not drift over a long hold
//   5 Ether reduces the surviving partial count monotonically
//   6 all five Movement modes produce distinct output
//   7 release actually decays to silence

#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cmath>
#include <new>
#include <vector>
#include <algorithm>
#include <od/config.h>

_GlobalConfig globalConfig;

#include "SpectralFreeze.cpp"

using namespace stolmine;

// std::isfinite is UNRELIABLE under -ffast-math: the compiler is told to
// assume NaN and Inf never occur, so the check folds to a constant true.
// This harness reported "finite: YES" on output that was entirely NaN.
// Inspect the bit pattern instead, which no optimization can elide.
static inline bool realFinite(float v)
{
  uint32_t u; memcpy(&u, &v, 4);
  return ((u >> 23) & 0xFFu) != 0xFFu;   // exponent all-ones => NaN or Inf
}
static const int FR = 128;
static const float SR = 48000.0f;

struct Rig
{
  alignas(16) unsigned char storage[sizeof(SpectralFreeze) + 64];
  SpectralFreeze *u = nullptr;
  float in[FR], gate[FR], out[FR];
  Rig()
  {
    memset(storage, 0x3B, sizeof(storage));
    u = new (storage) SpectralFreeze();
    u->mIn.setBuffer(in); u->mFreeze.setBuffer(gate); u->mOut.setBuffer(out);
  }
  ~Rig() { u->~SpectralFreeze(); }
};

// Harmonic tone plus a broadband bed, so Ether has both steady partials and
// non-tonal content to sort between.
static float src(long n)
{
  const double t = (double)n / SR;
  double v = 0.0;
  for (int k = 1; k <= 9; k++) v += sin(2.0 * M_PI * 220.0 * k * t + k) / k;
  v *= 0.22;
  v += 0.08 * sin(2.0 * M_PI * 3313.0 * t) * sin(2.0 * M_PI * 71.0 * t);
  return (float)v;
}

// Runs `blocks` frames. gateOn drives the freeze inlet; inputOn cuts the source.
// Returns RMS of the LAST quarter and whether anything non-finite appeared.
struct Res { double rms, peak; bool finite; std::vector<float> tail; };
static Res run(Rig &r, int blocks, bool gateOn, bool inputOn, long &n)
{
  Res res{0, 0, true, {}};
  double acc = 0; long cnt = 0;
  const int from = blocks * 3 / 4;
  for (int b = 0; b < blocks; b++)
  {
    for (int i = 0; i < FR; i++)
    {
      r.in[i] = inputOn ? src(n++) : (n++, 0.0f);
      r.gate[i] = gateOn ? 1.0f : 0.0f;
    }
    r.u->process();
    for (int i = 0; i < FR; i++)
    {
      const float y = r.out[i];
      if (!realFinite(y)) res.finite = false;
      const float a = y < 0 ? -y : y;
      if (a > res.peak) res.peak = a;
      if (b >= from) { acc += (double)y * y; cnt++; if (res.tail.size() < 8192) res.tail.push_back(y); }
    }
  }
  res.rms = cnt ? sqrt(acc / (double)cnt) : 0.0;
  return res;
}

static int partials(const std::vector<float> &x)
{
  const int N = 4096;
  if ((int)x.size() < N) return -1;
  std::vector<double> re(N / 2 + 1, 0.0), im(N / 2 + 1, 0.0);
  for (int k = 1; k <= N / 2; k++)
  {
    double sr_ = 0, si = 0;
    for (int i = 0; i < N; i++)
    {
      const double w = 0.5 * (1.0 - cos(2.0 * M_PI * i / N));
      const double a = 2.0 * M_PI * k * i / N;
      sr_ += x[i] * w * cos(a); si -= x[i] * w * sin(a);
    }
    re[k] = sr_; im[k] = si;
  }
  std::vector<double> m(N / 2 + 1);
  double mx = 0;
  for (int k = 1; k <= N / 2; k++) { m[k] = sqrt(re[k] * re[k] + im[k] * im[k]); if (m[k] > mx) mx = m[k]; }
  if (mx <= 0) return 0;
  int c = 0;
  for (int k = 2; k < N / 2; k++)
    if (m[k] > m[k - 1] && m[k] > m[k + 1] && m[k] > mx * 0.05) c++;
  return c;
}



static int fails = 0;
static void check(bool ok, const char *what, const char *detail = "")
{
  printf("  [%s] %s %s\n", ok ? "PASS" : "FAIL", what, detail);
  if (!ok) fails++;
}

int main()
{
  globalConfig.frameLength = FR;
  globalConfig.sampleRate = SR;
  char buf[256];

  // 1 + 2: no freeze, dry only. Mix defaults to 0.5 so dry passes at sqrt(0.5).
  {
    Rig r; long n = 0;
    r.u->mMix.hardSet(1.0f);           // wet only: nothing should come out
    Res a = run(r, 400, false, true, n);
    check(a.finite, "output finite with no freeze");
    snprintf(buf, sizeof buf, "(peak %.2e)", a.peak);
    check(a.peak < 1e-6, "wet path silent before any freeze", buf);
  }

  // 3: freeze, then CUT the input. A real freeze keeps sounding.
  {
    Rig r; long n = 0;
    r.u->mMix.hardSet(1.0f);
    r.u->mAttack.hardSet(0.0f);
    r.u->mRate.hardSet(0.0f);
    run(r, 200, false, true, n);        // fill history
    Res hot = run(r, 200, true, true, n);
    Res held = run(r, 400, true, false, n);   // input cut, gate still high
    check(hot.finite && held.finite, "output finite while frozen");
    snprintf(buf, sizeof buf, "(rms %.4f -> %.4f after input cut)", hot.rms, held.rms);
    check(held.rms > hot.rms * 0.25 && held.rms > 1e-4, "freeze SUSTAINS with no input", buf);

    // 4: phase accumulator must not drift over a long hold.
    Res late = run(r, 2000, true, false, n);
    snprintf(buf, sizeof buf, "(rms %.4f vs %.4f, ratio %.2f)", late.rms, held.rms,
             held.rms > 0 ? late.rms / held.rms : 0.0);
    check(late.finite && late.rms > held.rms * 0.3 && late.rms < held.rms * 3.0,
          "no drift or blow-up over ~5 s hold", buf);

    // 7: release decays to silence.
    Res rel = run(r, 600, false, false, n);
    snprintf(buf, sizeof buf, "(rms %.2e)", rel.rms);
    check(rel.rms < held.rms * 0.05, "release decays to silence", buf);
  }

  // 5: Ether reduces the surviving partial count.
  {
    printf("  Ether sweep:\n");
    int prev = 1 << 30; bool mono = true;
    for (float e : {1.0f, 0.6f, 0.3f, 0.1f})
    {
      Rig r; long n = 0;
      r.u->mMix.hardSet(1.0f); r.u->mRate.hardSet(0.0f); r.u->mAttack.hardSet(0.0f);
      r.u->mEther.hardSet(e);
      run(r, 200, false, true, n);
      run(r, 100, true, true, n);
      Res h = run(r, 200, true, false, n);
      const int p = partials(h.tail);
      printf("      ether %.1f -> %3d partials, rms %.4f\n", e, p, h.rms);
      if (p > prev) mono = false;
      prev = p;
    }
    check(mono, "Ether reduces partial count monotonically");
  }

  // 6: the five Movement modes must actually differ.
  //
  // Rate matters here. At 0.5 the position advances 0.045 frames/hop, so
  // crossing 31 frames takes ~1400 blocks; inside a shorter window Alternating
  // never reaches a boundary and is CORRECTLY identical to Forwards. Run at
  // full rate so every mode actually exercises its behaviour - otherwise the
  // test reports a DSP bug that is really just an under-powered stimulus.
  {
    std::vector<std::vector<float>> outs;
    for (int mode = 1; mode <= 5; mode++)
    {
      Rig r; long n = 0;
      r.u->mMix.hardSet(1.0f); r.u->mAttack.hardSet(0.0f);
      r.u->mRate.hardSet(1.0f); r.u->mDepth.hardSet(1.0f);
      r.u->mMovement.set(mode);
      run(r, 300, false, true, n);
      run(r, 100, true, true, n);
      Res h = run(r, 900, true, false, n);
      if (!h.finite) { check(false, "movement mode finite"); }
      outs.push_back(h.tail);
    }
    int same = 0;
    for (size_t i = 0; i < outs.size(); i++)
      for (size_t j = i + 1; j < outs.size(); j++)
      {
        const size_t N = std::min(outs[i].size(), outs[j].size());
        if (!N) continue;
        double d = 0;
        for (size_t k = 0; k < N; k++) { const double e = outs[i][k] - outs[j][k]; d += e * e; }
        if (sqrt(d / N) < 1e-7) same++;
      }
    snprintf(buf, sizeof buf, "(%d identical pairs of 10)", same);
    check(same == 0, "all 5 Movement modes produce distinct output", buf);
  }

  printf("\n%s\n", fails ? "FAILURES PRESENT" : "ALL CHECKS PASS");
  return fails ? 1 : 0;
}
