// Spectral Freeze - phase-vocoder freeze unit for ER-301
//
// A third-party attribution line used to sit here and was removed 2026-08-13:
// the code was never derived from the work it named, so the credit was simply
// wrong. Do not restore it. The engine is an original phase-vocoder freeze,
// designed in planning/kryos-spectral-freeze.md. Any attribution added in
// future must describe what this code actually is.

#include "SpectralFreeze.h"
#include <od/config.h>
#include <hal/ops.h>
#include <math.h>
#include <string.h>

namespace stolmine
{

  // Fast atan2. Used ONCE PER FREEZE over kBins, never per sample, so ~0.01 rad
  // accuracy is ample and this keeps a libm dependency out of the package that
  // would otherwise exist for a single gesture.
  static inline float fastAtan2(float y, float x)
  {
    const float ax = x < 0.0f ? -x : x;
    const float ay = y < 0.0f ? -y : y;
    const float d = (ax > ay ? ax : ay);
    if (d <= 0.0f) return 0.0f;
    const float z = (ax > ay ? ay / ax : ax / ay);
    const float z2 = z * z;
    float a = z * (0.99997726f + z2 * (-0.33262347f + z2 * (0.19354346f +
              z2 * (-0.11643287f + z2 * (0.05265332f + z2 * -0.01172120f)))));
    if (ay > ax) a = 1.57079633f - a;
    if (x < 0.0f) a = 3.14159265f - a;
    if (y < 0.0f) a = -a;
    return a;
  }

  SpectralFreeze::SpectralFreeze()
  {
    addInput(mIn);
    addInput(mFreeze);
    addOutput(mOut);
    addParameter(mDepth);
    addParameter(mRate);
    addParameter(mOffset);
    addParameter(mEther);
    addParameter(mAttack);
    addParameter(mRelease);
    addParameter(mShift);
    addParameter(mMix);
    addOption(mMovement);
    // od::Option is NOT auto-serialized.
    mMovement.enableSerialization();

    for (int i = 0; i <= kSinLut; i++)
      mSinLutData[i] = (float)sin(2.0 * M_PI * (double)i / (double)kSinLut);

    mpSetup = pffft_new_setup(kFFT, PFFFT_REAL);
    mpFftIn = (float *)pffft_aligned_malloc(kFFT * sizeof(float));
    mpFftOut = (float *)pffft_aligned_malloc(kFFT * sizeof(float));
    mpFftWork = (float *)pffft_aligned_malloc(kFFT * sizeof(float));
    mpSpec = (float *)pffft_aligned_malloc(kFFT * sizeof(float));
    mpSpecPrev = (float *)pffft_aligned_malloc(kFFT * sizeof(float));
    mpSpecCur = (float *)pffft_aligned_malloc(kFFT * sizeof(float));

    mpWindow = new float[kFFT];
    mpInRing = new float[kFFT];
    mpOla = new float[kFFT];
    mpHist = new float[kHist * kBins];
    mpRotRe = new float[kBins];
    mpRotIm = new float[kBins];
    mpPhRe = new float[kBins];
    mpPhIm = new float[kBins];
    mpRotAng = new float[kBins];
    mpScore = new float[kBins];
    mpSorted = new float[kBins];
    mpGate = new float[kBins];
    mpMag = new float[kBins];

    mReady = mpSetup && mpFftIn && mpFftOut && mpFftWork && mpSpec &&
             mpSpecPrev && mpSpecCur && mpWindow && mpInRing && mpOla && mpHist;

    for (int i = 0; i < kFFT; i++)
      mpWindow[i] = (float)(0.5 * (1.0 - cos(2.0 * M_PI * (double)i / (double)kFFT)));
    memset(mpInRing, 0, kFFT * sizeof(float));
    memset(mpOla, 0, kFFT * sizeof(float));
    memset(mpHist, 0, kHist * kBins * sizeof(float));
    if (mpSpecPrev) memset(mpSpecPrev, 0, kFFT * sizeof(float));
    if (mpSpecCur) memset(mpSpecCur, 0, kFFT * sizeof(float));
    for (int k = 0; k < kBins; k++)
    {
      mpRotRe[k] = 1.0f; mpRotIm[k] = 0.0f;
      mpPhRe[k] = 1.0f; mpPhIm[k] = 0.0f;
      mpRotAng[k] = 0.0f;
      mpScore[k] = 0.0f; mpSorted[k] = 0.0f; mpGate[k] = 1.0f; mpMag[k] = 0.0f;
    }
  }

  SpectralFreeze::~SpectralFreeze()
  {
    if (mpSetup) pffft_destroy_setup(mpSetup);
    if (mpFftIn) pffft_aligned_free(mpFftIn);
    if (mpFftOut) pffft_aligned_free(mpFftOut);
    if (mpFftWork) pffft_aligned_free(mpFftWork);
    if (mpSpec) pffft_aligned_free(mpSpec);
    if (mpSpecPrev) pffft_aligned_free(mpSpecPrev);
    if (mpSpecCur) pffft_aligned_free(mpSpecCur);
    delete[] mpWindow; delete[] mpInRing; delete[] mpOla; delete[] mpHist;
    delete[] mpRotRe; delete[] mpRotIm; delete[] mpPhRe; delete[] mpPhIm;
    delete[] mpRotAng; delete[] mpScore; delete[] mpSorted; delete[] mpGate;
    delete[] mpMag;
  }

  // Forward transform of the newest kFFT input samples. The transform runs on
  // EVERY hop, frozen or not, so mpSpecCur/mpSpecPrev stay warm and the NEXT
  // freeze captures the moment it was asked for rather than whenever a buffer
  // happened to fill.
  //
  // The magnitude history, though, is only written while NOT frozen. That is
  // load-bearing: the history ring is what the frozen voice plays from, so
  // letting analysis keep writing means the capture is overwritten within kHist
  // hops (170 ms) and the freeze collapses to silence the instant the input
  // stops. Measured exactly that way by tools/spectral-freeze-test before this guard -
  // sustain went to precisely 0.0 with the input cut, which is the one thing a
  // freeze must never do.
  void SpectralFreeze::doAnalysis()
  {
    for (int i = 0; i < kFFT; i++)
    {
      const int p = (mInWrite + i) & (kFFT - 1);
      mpFftIn[i] = mpInRing[p] * mpWindow[i];
    }
    memcpy(mpSpecPrev, mpSpecCur, kFFT * sizeof(float));
    pffft_transform_ordered(mpSetup, mpFftIn, mpSpecCur, mpFftWork, PFFFT_FORWARD);

    // pffft real layout: out[0] = DC, out[1] = Nyquist, then interleaved re/im
    // for bins 1..kBins-1.
    if (mFrozen) return;   // hold the captured window, see above

    float *h = mpHist + mHistWrite * kBins;
    h[0] = mpSpecCur[0] < 0.0f ? -mpSpecCur[0] : mpSpecCur[0];
    for (int k = 1; k < kBins; k++)
    {
      const float re = mpSpecCur[k * 2], im = mpSpecCur[k * 2 + 1];
      h[k] = sqrtf(re * re + im * im);
    }
    mHistWrite = (mHistWrite + 1) % kHist;
    if (mHistFill < kHist) mHistFill++;
  }

  // Freeze. The per-bin rotation is the RATIO between the two most recent
  // complex frames - rot = cur * conj(prev), normalized - which needs no atan2
  // at all. The angle is stored only so pitch shifting can rescale it.
  //
  // Measured against the obvious alternative (rotate every bin at its nominal
  // bin-centre rate, storing nothing): bin-centre gives 3.6x the envelope
  // wobble, 0.615 against 0.172, because adjacent bins of one partial then turn
  // at slightly wrong relative rates and beat against each other. That beating
  // is the classic phasey-freeze artifact. The true rate costs one float per
  // bin, once.
  void SpectralFreeze::capture()
  {
    for (int k = 1; k < kBins; k++)
    {
      const float cr = mpSpecCur[k * 2], ci = mpSpecCur[k * 2 + 1];
      const float pr = mpSpecPrev[k * 2], pi = mpSpecPrev[k * 2 + 1];
      const float rr = cr * pr + ci * pi;      // cur * conj(prev)
      const float ri = ci * pr - cr * pi;
      const float m = sqrtf(rr * rr + ri * ri);
      if (m > 1e-20f)
      {
        const float inv = 1.0f / m;
        mpRotRe[k] = rr * inv; mpRotIm[k] = ri * inv;
        mpRotAng[k] = fastAtan2(ri * inv, rr * inv);
      }
      else
      {
        mpRotRe[k] = 1.0f; mpRotIm[k] = 0.0f; mpRotAng[k] = 0.0f;
      }
      mpPhRe[k] = 1.0f; mpPhIm[k] = 0.0f;
    }
    mpRotRe[0] = 1.0f; mpRotIm[0] = 0.0f; mpRotAng[0] = 0.0f;
    mpPhRe[0] = 1.0f; mpPhIm[0] = 0.0f;

    // Steadiness scored across the captured window, ONCE, not per sample.
    // Score is steadiness WEIGHTED BY LEVEL. Steadiness alone measured
    // completely inert: 492 of 513 bins are noise floor and score inside a band
    // 0.039 wide, so a threshold spends its entire travel re-sorting inaudible
    // bins and never reaches the loud ones. "Strongest harmonics" is in the
    // spec, and the level term is what delivers it.
    const int n = mHistFill > 0 ? mHistFill : 1;
    for (int k = 0; k < kBins; k++)
    {
      float sum = 0.0f, sum2 = 0.0f;
      for (int f = 0; f < n; f++)
      {
        const float v = mpHist[f * kBins + k];
        sum += v; sum2 += v * v;
      }
      const float mean = sum / (float)n;
      float var = sum2 / (float)n - mean * mean;
      if (var < 0.0f) var = 0.0f;
      const float sd = sqrtf(var);
      mpScore[k] = (mean / (mean + sd + 1e-20f)) * mean;
    }
    // Sort a copy ascending so the Ether threshold is an O(1) lookup at block
    // rate instead of a selection pass every time. Insertion sort is fine:
    // kBins is 512 and this runs once per freeze gesture.
    memcpy(mpSorted, mpScore, kBins * sizeof(float));
    for (int i = 1; i < kBins; i++)
    {
      const float v = mpSorted[i];
      int j = i - 1;
      while (j >= 0 && mpSorted[j] > v) { mpSorted[j + 1] = mpSorted[j]; j--; }
      mpSorted[j + 1] = v;
    }
    mLastEther = -1.0f;    // force a gate rebuild
    mLastShift = 1e9f;     // and a rotation rebuild
    mHaveCapture = true;
    mPos = 0.0;
    mDir = 1.0f;
  }

  // Ether. Keeps the top N bins by score, N swept GEOMETRICALLY so the control
  // has resolution where it matters: the difference between 6 partials and 3 is
  // enormous, the difference between 400 and 380 is nothing.
  void SpectralFreeze::rebuildGate(float ether)
  {
    if (ether >= 1.0f)
    {
      for (int k = 0; k < kBins; k++) mpGate[k] = 1.0f;
      return;
    }
    int n = (int)(2.0f * powf((float)kBins * 0.5f, ether) + 0.5f);
    if (n < 2) n = 2;
    if (n > kBins) n = kBins;
    const float thr = mpSorted[kBins - n];
    for (int k = 0; k < kBins; k++) mpGate[k] = (mpScore[k] >= thr) ? 1.0f : 0.0f;
    // One-bin skirt, so a surviving partial keeps its window shape and stays a
    // tone instead of degenerating into a click train.
    float prev = mpGate[0];
    for (int k = 1; k < kBins - 1; k++)
    {
      const float cur = mpGate[k];
      if (cur == 0.0f && (prev > 0.5f || mpGate[k + 1] > 0.5f)) mpGate[k] = 0.5f;
      prev = cur;
    }
  }

  // Pitch. A partial that sat at bin k/rho now sits at bin k and turns rho times
  // as fast, so BOTH the magnitude and the rotation have to move or the two
  // disagree and the result smears. Angle scales linearly where a unit complex
  // does not, which is the only reason the angle is kept; the complex form is
  // rebuilt from the table. Runs only when the control actually moves.
  void SpectralFreeze::rebuildRotation(float semis)
  {
    const float rho = powf(2.0f, semis * (1.0f / 12.0f));
    const float inv = 1.0f / rho;
    for (int k = 0; k < kBins; k++)
    {
      const float src = (float)k * inv;
      float ang = 0.0f;
      // Out of range goes SILENT rather than clamping to the edge bin: a clamp
      // holds the last value across the whole out-of-range span.
      if (src < (float)(kBins - 1) && src >= 0.0f)
      {
        const int i0 = (int)src;
        const float fr = src - (float)i0;
        ang = (mpRotAng[i0] + (mpRotAng[i0 + 1] - mpRotAng[i0]) * fr) * rho;
      }
      float t = ang * (float)(1.0 / (2.0 * M_PI));
      t -= (float)(int)t;
      if (t < 0.0f) t += 1.0f;
      const float fp = t * (float)kSinLut;
      int si = (int)fp;
      if (si < 0) si = 0; else if (si >= kSinLut) si = kSinLut - 1;
      const float sf = fp - (float)si;
      const float s = mSinLutData[si] + (mSinLutData[si + 1] - mSinLutData[si]) * sf;
      const int ci = (si + kSinLut / 4) & (kSinLut - 1);
      const int ci2 = (ci + 1) & (kSinLut - 1);
      const float c = mSinLutData[ci] + (mSinLutData[ci2] - mSinLutData[ci]) * sf;
      mpRotRe[k] = c; mpRotIm[k] = s;
      mpPhRe[k] = 1.0f; mpPhIm[k] = 0.0f;
    }
  }

  void SpectralFreeze::doSynth()
  {
    const float depthN = CLAMP(0.0f, 1.0f, mDepth.value());
    const float rateN = CLAMP(0.0f, 1.0f, mRate.value());
    const float offN = CLAMP(0.0f, 1.0f, mOffset.value());
    const int mode = mMovement.value();

    int depth = 1 + (int)(depthN * (float)(kHist - 1) + 0.5f);
    if (depth > mHistFill) depth = mHistFill;
    if (depth < 1) depth = 1;
    const double span = (double)(depth - 1);

    // Geometric rate: 0 stopped, else 0.002 up to 1.0 frames per hop. Linear
    // would spend nearly all its travel too fast to read as motion.
    const float rate = (rateN <= 0.0f) ? 0.0f : 0.002f * powf(500.0f, rateN);

    if (span > 0.0 && rate > 0.0f)
    {
      switch (mode)
      {
        case 2: mPos -= rate; break;
        case 3:
          mPos += rate * mDir;
          if (mPos >= span) { mPos = span; mDir = -1.0f; }
          else if (mPos <= 0.0) { mPos = 0.0; mDir = 1.0f; }
          break;
        case 4:
          mLcg = mLcg * 1103515245u + 12345u;
          if (((mLcg >> 16) & 0xFFFFu) < 4000u) mDir = -mDir;
          mPos += rate * mDir;
          break;
        case 5:
          if (--mSkipCount <= 0)
          {
            mLcg = mLcg * 1103515245u + 12345u;
            mPos = (double)(((mLcg >> 16) & 0xFFFFu) * (1.0f / 65535.0f)) * span;
            mSkipCount = 4 + (int)((mLcg >> 8) & 7u);
          }
          break;
        default: mPos += rate; break;
      }
      if (mode != 3)
      {
        while (mPos < 0.0) mPos += span;
        while (mPos >= span) mPos -= span;
      }
    }
    else mPos = 0.0;

    double p = mPos + (double)offN * span;
    if (span > 0.0) { while (p >= span) p -= span; while (p < 0.0) p += span; }
    else p = 0.0;

    // Newest frame sits at mHistWrite-1; f counts BACK from it. Reading between
    // two frames is what makes the wrap interpolated rather than a splice.
    const int f0 = (int)p;
    const int f1 = (f0 + 1 <= depth - 1) ? f0 + 1 : 0;
    const float a = (float)(p - (double)f0);
    const int base = mHistWrite - 1 + kHist * 2;
    const float *h0 = mpHist + ((base - f0) % kHist) * kBins;
    const float *h1 = mpHist + ((base - f1) % kHist) * kBins;

    const float shift = CLAMP(-48.0f, 24.0f, mShift.value());
    const bool shifted = (shift < -0.01f || shift > 0.01f);
    const float invRho = shifted ? powf(2.0f, -shift * (1.0f / 12.0f)) : 1.0f;

    // Build the frame, accumulating the energy BEFORE and AFTER gating and
    // shifting so the result can be renormalized to what the source actually
    // had.
    float eRef = 0.0f, eOut = 0.0f;
    for (int k = 0; k < kBins; k++)
    {
      const float raw = h0[k] + (h1[k] - h0[k]) * a;
      eRef += raw * raw;
      float m;
      if (!shifted) m = raw;
      else
      {
        const float src = (float)k * invRho;
        if (src >= (float)(kBins - 1) || src < 0.0f) m = 0.0f;
        else
        {
          const int i0 = (int)src;
          const float fr = src - (float)i0;
          const float v0 = h0[i0] + (h0[i0 + 1] - h0[i0]) * fr;
          const float v1 = h1[i0] + (h1[i0 + 1] - h1[i0]) * fr;
          m = v0 + (v1 - v0) * a;
        }
      }
      m *= mpGate[k];
      eOut += m * m;
      mpMag[k] = m;
    }

    // ENERGY PRESERVATION. Without it, Ether and Shift are both level controls
    // by accident: measured -4.1 dB at Ether 0.1, and an 11 dB SWING across the
    // Shift range (-7.2 dB at -24 ST, +4.2 dB at +24 ST) because a downshift
    // samples from the quieter top of the spectrum and drops content off the
    // end, while an upshift stretches loud low content over more bins.
    //
    // Restoring the source energy is also what makes the surviving partials
    // BLOOM as Ether comes down and fill the space the discarded transients
    // left, instead of simply getting quieter. That bloom is the behaviour
    // being aimed for, and it falls straight out of the normalization rather
    // than needing a separate mechanism.
    //
    // Capped, because at extreme settings eOut can be a rounding error and an
    // uncapped ratio would detonate. Smoothed, because both controls are swept
    // live and a step here would zipper.
    float ng = 1.0f;
    if (eOut > 1e-18f && eRef > 1e-18f)
    {
      ng = sqrtf(eRef / eOut);
      if (ng > 8.0f) ng = 8.0f;
      else if (ng < 0.125f) ng = 0.125f;
    }
    mNormGain += (ng - mNormGain) * 0.16f;   // ~30 ms at 187.5 hops/s

    // Advance the phase: one complex multiply per bin, no trig anywhere. The
    // Newton step pulls the accumulator back onto the unit circle - without it,
    // thousands of multiplies of not-quite-unit values drift to zero or blow up.
    mpSpec[0] = mpMag[0] * mNormGain;
    mpSpec[1] = 0.0f;
    for (int k = 1; k < kBins; k++)
    {
      const float pr = mpPhRe[k], pi = mpPhIm[k];
      const float rr = mpRotRe[k], ri = mpRotIm[k];
      float nr = pr * rr - pi * ri;
      float ni = pr * ri + pi * rr;
      const float corr = 1.5f - 0.5f * (nr * nr + ni * ni);
      nr *= corr; ni *= corr;
      mpPhRe[k] = nr; mpPhIm[k] = ni;
      const float m = mpMag[k] * mNormGain;
      mpSpec[k * 2] = m * nr;
      mpSpec[k * 2 + 1] = m * ni;
    }

    pffft_transform_ordered(mpSetup, mpSpec, mpFftIn, mpFftWork, PFFFT_BACKWARD);

    // pffft is unscaled (BACKWARD(FORWARD(x)) = N*x), and 4x-overlapped Hann
    // analysis plus Hann synthesis sums to 1.5, so fold both into one constant.
    const float norm = 1.0f / ((float)kFFT * 1.5f);
    for (int i = 0; i < kFFT; i++)
    {
      const int q = (mOlaWrite + i) & (kFFT - 1);
      mpOla[q] += mpFftIn[i] * mpWindow[i] * norm;
    }
  }

  void SpectralFreeze::process()
  {
    float *in = mIn.buffer();
    float *gate = mFreeze.buffer();
    float *out = mOut.buffer();

    if (!mReady)
    {
      for (int i = 0; i < FRAMELENGTH; i++) out[i] = 0.0f;
      return;
    }

    const float mix = CLAMP(0.0f, 1.0f, mMix.value());
    // Equal-power, not linear: a linear crossfade dips about 3 dB at centre for
    // decorrelated sources, and a frozen spectrum is thoroughly decorrelated
    // from the live input (feedback_equal_power_drywet_crossfade).
    const float dryG = sqrtf(1.0f - mix);
    const float wetG = sqrtf(mix);

    const float ether = CLAMP(0.0f, 1.0f, mEther.value());
    if (mHaveCapture && ether != mLastEther) { rebuildGate(ether); mLastEther = ether; }
    const float shift = CLAMP(-48.0f, 24.0f, mShift.value());
    if (mHaveCapture && (shift < mLastShift - 0.01f || shift > mLastShift + 0.01f))
    { rebuildRotation(shift); mLastShift = shift; }

    const float sr = globalConfig.sampleRate > 0.0f ? globalConfig.sampleRate : 48000.0f;
    const float atkInc = 1.0f / (sr * (0.002f + CLAMP(0.0f, 1.0f, mAttack.value()) * 2.0f));
    const float relInc = 1.0f / (sr * (0.002f + CLAMP(0.0f, 1.0f, mRelease.value()) * 4.0f));

    for (int i = 0; i < FRAMELENGTH; i++)
    {
      const float x = in[i];
      mpInRing[mInWrite] = x;
      mInWrite = (mInWrite + 1) & (kFFT - 1);

      const bool wantFrozen = gate[i] > 0.5f;
      if (wantFrozen && !mFrozen) { capture(); mFrozen = true; }
      else if (!wantFrozen && mFrozen) mFrozen = false;

      mEnv += mFrozen ? atkInc : -relInc;
      if (mEnv > 1.0f) mEnv = 1.0f; else if (mEnv < 0.0f) mEnv = 0.0f;

      const int q = (mOlaWrite + mHopPhase) & (kFFT - 1);
      const float wet = mpOla[q];
      mpOla[q] = 0.0f;   // consume, so the slot is clean 4 hops from now

      out[i] = x * dryG + wet * mEnv * wetG;

      mHopPhase++;
      if (mHopPhase >= kHop)
      {
        mHopPhase = 0;
        mOlaWrite = (mOlaWrite + kHop) & (kFFT - 1);
        doAnalysis();
        if (mHaveCapture && mEnv > 0.0f) doSynth();
      }
    }
  }

} // namespace stolmine
