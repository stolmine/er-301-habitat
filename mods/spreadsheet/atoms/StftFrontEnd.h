// stolmine::StftFrontEnd
//
// Component atom: the analysis/resynthesis scaffolding shared by every
// spectral unit - Hann window, pffft setup, overlap-add ring, hop
// counter, COLA normalization. Component-only per
// feedback_atoms_as_components.
//
// It deliberately owns NOTHING about what a unit does to a spectrum.
// The seam is exactly forward() -> (your code) -> inverse(), because
// that is the only place the two existing implementations differ.
//
// EXTRACTED FROM TWO SHIPPING UNITS, not designed from scratch.
// mods/biome/SpectralFreeze.cpp and mods/spreadsheet/Sediment.cpp had
// independently arrived at identical scaffolding:
//   - kFFT 1024, kHop = kFFT/4, kBins = kFFT/2
//   - the same Hann build, in double, at construction
//   - the same COLA constant 1/(kFFT * 1.5)
//   - the same ring advance, (write + kHop) & (kFFT - 1)
// Only the middle differed: Sediment permutes bins and reads from a
// sample, SpectralFreeze regenerates phase from a magnitude history
// and reads from a live input.
//
// PACKAGE: spreadsheet, chosen the way the ledger item directs - the
// package the first consumers land in. Palimpsest and Anneal are both
// spreadsheet units and Sediment already lives here. Assay targets
// biome and will need this vendored across, exactly as pffft already
// is in four packages; that is the known cost and it is cheaper than a
// cross-package build dependency.
//
// A THIRD implementation exists and was checked: zaum's STFTSpectral.h
// (933 lines) is the most developed spectral code in the tree, and it
// independently uses N=1024 with hop=256 - the same geometry as both
// units extracted here. Three implementations agreeing is why the
// geometry below is fixed rather than parameterized. STFTSpectral is
// NOT a front end though; it is one algorithm (Bloom/Space/Diffuse/
// Distance) with the transform embedded, so it is not a donor.
//
// WHY 4x OVERLAP. Hop = N/4 is the point where summed squared Hann
// windows are a constant 1.5, so analysis-window * synthesis-window
// overlap-add reconstructs exactly. Change the hop and that constant
// is wrong; the atom derives it rather than hard-coding a literal, and
// tools/stft-frontend-test measures reconstruction rather than
// trusting the arithmetic.
//
// TWO TRAPS CARRIED FORWARD FROM THE ORIGINALS:
//
// 1. pffft's `work` argument must NOT be null. Passing null makes it
//    use the STACK, which at N=1024 is 4 KB against a 2048-byte audio
//    task stack. Heap, always. Both originals carry this warning.
// 2. pffft's real layout is out[0] = DC, out[1] = Nyquist, then
//    interleaved re/im for bins 1..kBins-1. Slot 0 is two real values
//    sharing one complex slot, so anything that treats it as a normal
//    bin will swap DC against Nyquist.
#pragma once

#include <od/config.h>
#include <math.h>
#include <string.h>
#include <stdint.h>
#include "pffft.h"

namespace stolmine
{

  class StftFrontEnd
  {
  public:
    static const int kFFT = 1024;
    static const int kHop = kFFT / 4;
    static const int kBins = kFFT / 2;   // complex slots; slot 0 packs DC + Nyquist

    StftFrontEnd()
    {
      mpSetup = pffft_new_setup(kFFT, PFFFT_REAL);
      mpWork = (float *)pffft_aligned_malloc(kFFT * sizeof(float));
      mpTime = (float *)pffft_aligned_malloc(kFFT * sizeof(float));
      mpScratch = (float *)pffft_aligned_malloc(kFFT * sizeof(float));
      mpWindow = new float[kFFT];
      mpIn = new float[kFFT];
      mpOla = new float[kFFT];

      mReady = mpSetup && mpWork && mpTime && mpScratch &&
               mpWindow && mpIn && mpOla;
      if (!mReady) return;

      // Built with DOUBLE cos at construction, never runtime cosf:
      // single-precision trig from a package .so miscomputes on am335x
      // (feedback_package_trig_lut).
      for (int i = 0; i < kFFT; i++)
        mpWindow[i] = (float)(0.5 * (1.0 - cos(2.0 * M_PI * (double)i / (double)kFFT)));

      // COLA gain for periodic Hann at this overlap: sum of w^2 over
      // the overlapping frames. DERIVED, not the literal 1.5, so that
      // changing kHop cannot silently leave a wrong constant behind.
      double cola = 0.0;
      for (int i = 0; i < kFFT; i += kHop)
        cola += (double)mpWindow[i] * (double)mpWindow[i];
      // pffft is unscaled: BACKWARD(FORWARD(x)) == kFFT * x. Fold both
      // that and the COLA sum into one multiply.
      mNorm = (float)(1.0 / ((double)kFFT * cola));

      clear();
    }

    ~StftFrontEnd()
    {
      if (mpSetup) pffft_destroy_setup(mpSetup);
      if (mpWork) pffft_aligned_free(mpWork);
      if (mpTime) pffft_aligned_free(mpTime);
      if (mpScratch) pffft_aligned_free(mpScratch);
      delete[] mpWindow;
      delete[] mpIn;
      delete[] mpOla;
    }

    bool ready() const { return mReady; }
    float norm() const { return mNorm; }
    const float *window() const { return mpWindow; }

    void clear()
    {
      if (!mReady) return;
      memset(mpIn, 0, kFFT * sizeof(float));
      memset(mpOla, 0, kFFT * sizeof(float));
      memset(mpTime, 0, kFFT * sizeof(float));
      mInWrite = 0;
      mOlaWrite = 0;
      mHopPhase = 0;
    }

    // ---- input side ----

    // Push one live sample into the analysis ring.
    inline void push(float x)
    {
      mpIn[mInWrite] = x;
      mInWrite = (mInWrite + 1) & (kFFT - 1);
    }

    // Window the newest kFFT samples of the ring into the internal
    // time buffer, ready for forward().
    void gather()
    {
      for (int i = 0; i < kFFT; i++)
      {
        const int p = (mInWrite + i) & (kFFT - 1);
        mpTime[i] = mpIn[p] * mpWindow[i];
      }
    }

    // For consumers that supply their own time-domain frame - Sediment
    // reads from a sample with its own fractional position rather than
    // from a live ring. `src` must be kFFT samples; the window is
    // applied here so a caller cannot forget it.
    void gatherFrom(const float *src)
    {
      for (int i = 0; i < kFFT; i++) mpTime[i] = src[i] * mpWindow[i];
    }

    // ---- the transform pair; consumer work goes between them ----

    // Forward transform of the gathered frame into `spec` (kFFT floats,
    // pffft ordered layout).
    void forward(float *spec)
    {
      pffft_transform_ordered(mpSetup, mpTime, spec, mpWork, PFFFT_FORWARD);
    }

    // Inverse `spec`, window again, and overlap-add into the output
    // ring with COLA normalization applied.
    void inverseAndOverlap(const float *spec)
    {
      pffft_transform_ordered(mpSetup, (float *)spec, mpScratch, mpWork, PFFFT_BACKWARD);
      for (int i = 0; i < kFFT; i++)
      {
        const int q = (mOlaWrite + i) & (kFFT - 1);
        mpOla[q] += mpScratch[i] * mpWindow[i] * mNorm;
      }
    }

    // ---- output side ----

    // Pop the current output sample and clear its slot, so the ring is
    // clean by the time the frame four hops from now adds into it.
    inline float consume()
    {
      const int q = (mOlaWrite + mHopPhase) & (kFFT - 1);
      const float y = mpOla[q];
      mpOla[q] = 0.0f;
      return y;
    }

    // Advance one sample. Returns true when a hop boundary is crossed,
    // which is when the consumer should gather/forward/process/inverse.
    inline bool tick()
    {
      mHopPhase++;
      if (mHopPhase < kHop) return false;
      mHopPhase = 0;
      mOlaWrite = (mOlaWrite + kHop) & (kFFT - 1);
      return true;
    }

    inline int hopPhase() const { return mHopPhase; }

  private:
    PFFFT_Setup *mpSetup = 0;
    // pffft wants aligned buffers, and `work` must NOT be null: passing
    // null makes it use the stack, 4 KB at N=1024 against a 2048-byte
    // audio task stack.
    float *mpWork = 0;
    float *mpTime = 0;      // windowed time-domain frame
    float *mpScratch = 0;   // inverse output before overlap-add
    float *mpWindow = 0;
    float *mpIn = 0;        // analysis input ring
    float *mpOla = 0;       // overlap-add output ring

    float mNorm = 0.0f;
    int mInWrite = 0;
    int mOlaWrite = 0;
    int mHopPhase = 0;
    bool mReady = false;
  };

} // namespace stolmine
