// Offline CPU bench + A/B harness for anamnesis::Anamnesis (DSP) and its
// all-over viz (buildFieldFrame + AnamFieldGraphic::draw). Native proxy:
// relative/structural costs only -- am335x libm/caches differ (see the
// feedback_f64_count_poor_cpu_proxy rail); pair with objdump call counts.
//
// Build (from repo root):
//   g++ -Os -o /tmp/anam_bench_Os tools/house-bench/anam_bench.cpp \
//       -Itools/house-bench/stub -Imods/anamnesis -Imods -std=gnu++11 -lm
//   g++ -O3 -ffast-math -o /tmp/anam_bench_O3 ... (same)
//
// Env:
//   SECONDS_AUDIO (default 30)   simulated seconds per DSP segment
//   MODE (1 tape, 2 stretch)     CLOCK (0..1, default 1)
//   DENSITY / DIFFUSION / MIX / REGEN / MOD (0..1)
//   DUMP=path                    write outL as raw float32 (A/B)
//   SKIP_VIZ=1                   DSP only

#include <od/config.h>
#include "atoms/Anamnesis.h"
#include "AnamFieldGraphic.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <vector>

namespace od { ConfigData globalConfig; }

static double now_s()
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (double)ts.tv_sec + 1e-9 * (double)ts.tv_nsec;
}

static float envf(const char *k, float d)
{
  const char *v = getenv(k);
  return v ? (float)atof(v) : d;
}

int main()
{
  const int sr = (int)envf("SR", 48000.0f);
  const int fr = 128;
  od::globalConfig.sampleRate = sr;
  od::globalConfig.frameLength = fr;

  anamnesis::Anamnesis *op = new anamnesis::Anamnesis();
  op->mMode.set((int)envf("MODE", 1.0f));
  op->mClock.hardSet(envf("CLOCK", 1.0f));
  op->mDensity.hardSet(envf("DENSITY", 0.5f));
  op->mDiffusion.hardSet(envf("DIFFUSION", 0.6f));
  op->mMix.hardSet(envf("MIX", 0.4f));
  op->mRegen.hardSet(envf("REGEN", 0.0f));
  op->mMod.hardSet(envf("MOD", 0.3f));

  const int secs = (int)envf("SECONDS_AUDIO", 30.0f);
  const int nb = secs * sr / fr;
  float *iL = op->mInL.buffer(), *iR = op->mInR.buffer();
  float *oL = op->mOutL.buffer();

  uint32_t s = 0x12345u;
  auto nz = [&]() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (float)(int32_t)s * 4.6566129e-10f; };

  std::vector<float> dump;
  const char *dumpPath = getenv("DUMP");

  // ---- segment 1: DSP process() only ----
  // (input regenerated per block; identical sequence across runs)
  double t0 = now_s();
  double acc = 0.0;
  for (int b = 0; b < nb; b++)
  {
    for (int i = 0; i < fr; i++)
    {
      int n = b * fr + i;
      float x = nz() * 0.3f + ((n % 4000 == 0) ? 0.8f : 0.0f);
      iL[i] = x; iR[i] = x * 0.7f;
    }
    op->process();
    if (dumpPath)
      for (int i = 0; i < fr; i++) dump.push_back(oL[i]);
    acc += oL[0];
  }
  double dspT = now_s() - t0;
  printf("DSP  : %7.1f ms for %ds audio -> %5.2f%% realtime  (sink %.3f)\n",
         dspT * 1e3, secs, 100.0 * dspT / secs, acc);

  if (dumpPath)
  {
    FILE *f = fopen(dumpPath, "wb");
    fwrite(dump.data(), sizeof(float), dump.size(), f);
    fclose(f);
    printf("dumped %zu samples to %s\n", dump.size(), dumpPath);
  }

  if (envf("SKIP_VIZ", 0.0f) > 0.5f) { delete op; return 0; }

  // ---- segment 2: DSP + per-block field build (isolates buildFieldFrame) ----
  t0 = now_s();
  for (int b = 0; b < nb; b++)
  {
    for (int i = 0; i < fr; i++) { float x = nz() * 0.3f; iL[i] = x; iR[i] = x * 0.7f; }
    op->process();
    op->ensureFieldFrame(); // phase advanced per block -> one build per block
  }
  double fieldT = now_s() - t0 - dspT;
  printf("FIELD: %7.3f us per buildFieldFrame (%d builds)\n", 1e6 * fieldT / nb, nb);

  // ---- segment 3: full draw path, 6 plies, ~53 fps (every 7th block) ----
  od::FrameBuffer fb;
  anamnesis::AnamFieldGraphic *g[anamnesis::field::kVizPlies];
  for (int i = 0; i < anamnesis::field::kVizPlies; i++)
  {
    g[i] = new anamnesis::AnamFieldGraphic(i * 43, 0, 42, 64);
    g[i]->follow(op);
    g[i]->setCanvas(i, anamnesis::field::kVizPlies);
  }
  int frames = 0;
  double drawT = 0.0;
  const char *fbDumpPath = getenv("FBDUMP");
  FILE *fbf = fbDumpPath ? fopen(fbDumpPath, "wb") : 0;
  for (int b = 0; b < nb; b++)
  {
    for (int i = 0; i < fr; i++) { float x = nz() * 0.3f; iL[i] = x; iR[i] = x * 0.7f; }
    op->process();
    if (b % 7 == 0)
    {
      memset(fb.mPlane, 0, sizeof(fb.mPlane)); // clean slate -> comparable planes
      double d0 = now_s();
      for (int i = 0; i < anamnesis::field::kVizPlies; i++) g[i]->draw(fb);
      drawT += now_s() - d0;
      frames++;
      if (fbf) fwrite(fb.mPlane, 1, sizeof(fb.mPlane), fbf);
    }
  }
  if (fbf) { fclose(fbf); printf("dumped %d frame planes to %s\n", frames, fbDumpPath); }
  printf("DRAW : %7.3f us per 6-ply frame (%d frames, includes field build)\n",
         1e6 * drawT / frames, frames);
  printf("       at 53 fps that is %5.2f%% of one core\n", 100.0 * (drawT / frames) * 53.0);

  for (int i = 0; i < anamnesis::field::kVizPlies; i++) delete g[i];
  delete op;
  return 0;
}
