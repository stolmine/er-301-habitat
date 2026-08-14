#pragma once

#include <od/objects/heads/TapeHead.h>
#include <od/config.h>

struct PFFFT_Setup;

namespace stolmine
{

  // Sediment - spectral sort. "Pixel sorting" applied to the spectrogram.
  //
  // Design: planning/spectral-sort-unit.md. Pixel sorting is mask -> spans ->
  // sort by key, and it is a PERMUTATION, so the per-frame histogram (and
  // therefore the energy) is preserved for free. The STFT magnitude matrix is
  // literally the image the algorithm wants.
  //
  // THIS IS THE MVP: frequency axis only. Sorting bins WITHIN a frame needs
  // only the current frame, so there is no analysis matrix here at all - the
  // rolling window in the design doc is purely a time-axis feature and lands
  // in the next phase.
  //
  // Inherits od::TapeHead for mpSample / mCurrentIndex and TapeHeadDisplay
  // compatibility, so the Lua side gets the standard waveform view. File
  // handling follows the built-in players (VariSpeed is the reference).
  class Sediment : public od::TapeHead
  {
  public:
    Sediment();
    virtual ~Sediment();

    // Stops playback when the attached sample changes. Without this, attaching
    // mid-play continues from a stale offset into the new file with the old
    // file's overlap-add tail still bleeding through.
    virtual void setSample(od::Sample *sample);

#ifndef SWIGLUA
    virtual void process();

    od::Inlet mTrigger{"Trigger"};
    od::Outlet mOutput{"Out"};

    // Sort is the macro: it opens the eligibility threshold AND raises
    // sortedness together, so 0 is bypass and 1 is fully sorted.
    od::Parameter mSort{"Sort", 0.0f};
    od::Parameter mLevel{"Level", 1.0f};

    // od::Option values are 1-based; 0 means UNKNOWN and must never be used.
    od::Option mDirection{"Direction", 1};  // 1 = quiet first, 2 = loud first
    od::Option mLoop{"Loop", 2};            // 1 = once, 2 = loop
#endif

  private:
    typedef od::TapeHead Base;

    // STFT geometry. Hop = N/4 gives 4x overlap, where the sum of squared Hann
    // windows is a constant 1.5 (the COLA normalization applied in doHop).
    static const int kFFT = 1024;
    static const int kHop = kFFT / 4;
    static const int kBins = kFFT / 2;  // complex slots; slot 0 packs DC + Nyquist

    // Counting-sort buckets. The bucket index is taken straight from the float
    // bit pattern of the key: for non-negative floats the bit pattern read as an
    // integer is MONOTONIC in the value, so a shift gives log-spaced buckets
    // with no logf and no divide. >>21 keeps sign + exponent + 2 mantissa bits,
    // which is 4 buckets per binade of magnitude-squared, about 3 dB of
    // magnitude resolution.
    static const int kBucketShift = 21;
    static const int kBuckets = 1024;

    void doHop();
    void restart();

    PFFFT_Setup *mpSetup = 0;

    // pffft wants aligned buffers, and its `work` argument must NOT be NULL:
    // passing NULL makes it use the STACK, which for N=1024 is 4 KB against a
    // 2048-byte audio task stack. Heap, always.
    float *mpFftIn = 0;
    float *mpFftOut = 0;
    float *mpFftWork = 0;
    float *mpSorted = 0;

    float *mpWindow = 0;   // Hann, kFFT
    float *mpOla = 0;      // overlap-add ring, kFFT
    float *mpKey = 0;      // magnitude-squared per bin, kBins

    // Class-member scratch, never stack-local, per feedback_neon_intrinsics_drumvoice.
    int *mpCount = 0;      // kBuckets
    int *mpOrder = 0;      // kBins, the permutation being built
    int *mpBucket = 0;     // kBins, cached bucket index per bin

    int mOlaWrite = 0;     // ring position of the next frame's start
    int mHopPhase = 0;     // output samples emitted since the last hop

    // Fractional read position, in SOURCE samples. Fractional because the file
    // rate need not match the engine rate; see mRatio.
    double mReadPos = 0.0;
    double mRatio = 1.0;   // source samples consumed per output sample

    bool mReady = false;   // all allocations succeeded
    bool mPlaying = false;
    bool mTrigWasHigh = false;
  };

} // namespace stolmine
