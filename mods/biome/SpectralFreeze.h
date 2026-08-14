#pragma once

#include <od/objects/Object.h>
#include <od/config.h>
#include <hal/ops.h>
#include <stdint.h>
#include "pffft.h"

namespace stolmine
{

  // Spectral Freeze - phase-vocoder freeze.
  //
  // Captures a HISTORY WINDOW of FFT frames, not a single frame, and
  // resynthesizes from it with regenerated phase. A single-frame freeze is
  // completely static; the history plus the Movement control is what turns it
  // into a sustaining texture. Design, and the offline measurements that fixed
  // its parameters: planning/kryos-spectral-freeze.md.
  class SpectralFreeze : public od::Object
  {
  public:
    SpectralFreeze();
    virtual ~SpectralFreeze();

#ifndef SWIGLUA
    virtual void process();

    od::Inlet mIn{"In"};
    // Gate: high freezes and plays, low releases. Threshold is 0.5, never 0.0 -
    // a zero threshold trips on fuzz and DC (feedback_comparator_gate_threshold).
    od::Inlet mFreeze{"Freeze"};
    od::Outlet mOut{"Out"};

    od::Parameter mDepth{"Depth", 1.0f};    // 0..1 -> 1..kHist frames
    od::Parameter mRate{"Rate", 0.25f};     // 0..1, geometric
    od::Parameter mOffset{"Offset", 0.0f};  // 0..1, position back in time
    // Transient rejection. 1 = keep everything (the default, matching the
    // reference), falling toward a handful of the strongest steady partials.
    od::Parameter mEther{"Ether", 1.0f};
    od::Parameter mAttack{"Attack", 0.05f};
    od::Parameter mRelease{"Release", 0.2f};
    od::Parameter mShift{"Shift", 0.0f};    // semitones
    od::Parameter mMix{"Mix", 0.5f};

    // 1 Forwards, 2 Backwards, 3 Alternating, 4 Random walk, 5 Random skip.
    // od::Option values are 1-based; 0 means UNKNOWN and is never used.
    od::Option mMovement{"Movement", 1};
#endif

  private:
    // STFT geometry, lifted from Sediment which already runs this shape on
    // am335x. Hop = N/4 gives 4x overlap, where the sum of squared Hann windows
    // is a constant 1.5 (the COLA normalization folded into doSynth).
    static const int kFFT = 1024;
    static const int kHop = kFFT / 4;
    static const int kBins = kFFT / 2;   // complex slots; slot 0 packs DC + Nyquist

    // Frames of MAGNITUDE history. The reference reaches 128, which as complex
    // spectra would be 512 KB against a 256 KB L2. 32 magnitude-only frames is
    // 64 KB and about 170 ms at 48k. Magnitude-only is the mechanism rather
    // than a compromise: phase is regenerated from a per-bin rotation, and that
    // is exactly what lets the loop wrap interpolate instead of splice.
    static const int kHist = 32;

    // Sine table for the rotation rebuild. Never runtime sinf: single-precision
    // trig from a package .so miscomputes on am335x (feedback_package_trig_lut).
    static const int kSinLut = 1024;

    void doAnalysis();
    void doSynth();
    void capture();
    void rebuildRotation(float semis);
    void rebuildGate(float ether);

    PFFFT_Setup *mpSetup = 0;

    // pffft wants aligned buffers, and its `work` argument must NOT be NULL:
    // passing NULL makes it use the STACK, which at N=1024 is 4 KB against a
    // 2048-byte audio task stack. Heap, always.
    float *mpFftIn = 0;
    float *mpFftOut = 0;
    float *mpFftWork = 0;
    float *mpSpec = 0;        // synthesis spectrum, kFFT

    float *mpWindow = 0;      // Hann, kFFT
    float *mpInRing = 0;      // analysis input ring, kFFT
    float *mpOla = 0;         // overlap-add ring, kFFT

    float *mpHist = 0;        // kHist * kBins magnitudes
    // The two most recent COMPLEX frames. Only two, not the whole history: the
    // per-bin rotation is derived from the ratio between consecutive frames at
    // the instant of capture, so keeping 8 KB here buys the phase model without
    // paying 512 KB to store complex history.
    float *mpSpecPrev = 0;    // kFFT
    float *mpSpecCur = 0;     // kFFT

    // Rotation as a UNIT COMPLEX, not an angle, so advancing the phase is one
    // complex multiply per bin per hop with no trig in the loop at all.
    float *mpRotRe = 0, *mpRotIm = 0;
    float *mpPhRe = 0, *mpPhIm = 0;
    // The rotation ANGLE is kept as well, purely so pitch shifting can remap
    // it: angle scales linearly with the shift ratio, a unit complex does not.
    float *mpRotAng = 0;

    float *mpScore = 0;       // per-bin steadiness*level, computed at capture
    float *mpSorted = 0;      // kBins, mpScore sorted ascending, for the threshold
    float *mpGate = 0;        // per-bin Ether weight
    float *mpMag = 0;         // per-hop working magnitudes

    float mSinLutData[kSinLut + 1];

    int mInWrite = 0;
    int mOlaWrite = 0;
    int mHopPhase = 0;
    int mHistWrite = 0;
    int mHistFill = 0;

    double mPos = 0.0;        // playback position within the history, in frames
    float mDir = 1.0f;        // for Alternating / Random walk
    uint32_t mLcg = 0x9E3779B9u;
    int mSkipCount = 0;

    // Energy-preserving normalization gain. Smoothed across hops, because
    // Ether and Shift are swept live and a jump in this would zipper.
    float mNormGain = 1.0f;
    float mEnv = 0.0f;        // attack/release envelope on the frozen voice
    bool mFrozen = false;
    bool mHaveCapture = false;
    float mLastShift = 1e9f;
    float mLastEther = -1.0f;
    bool mReady = false;
  };

} // namespace stolmine
