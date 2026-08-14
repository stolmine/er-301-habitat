#include "Sediment.h"
#include <od/config.h>
#include <hal/ops.h>
#include <math.h>
#include <string.h>

#include "pffft.h"

namespace stolmine
{

  Sediment::Sediment()
  {
    addInput(mTrigger);
    addOutput(mOutput);
    addParameter(mSort);
    addParameter(mLevel);
    addOption(mDirection);
    addOption(mLoop);

    mpSetup = pffft_new_setup(kFFT, PFFFT_REAL);

    mpFftIn = (float *)pffft_aligned_malloc(kFFT * sizeof(float));
    mpFftOut = (float *)pffft_aligned_malloc(kFFT * sizeof(float));
    mpFftWork = (float *)pffft_aligned_malloc(kFFT * sizeof(float));
    mpSorted = (float *)pffft_aligned_malloc(kFFT * sizeof(float));

    mpWindow = new float[kFFT];
    mpOla = new float[kFFT];
    mpKey = new float[kBins];
    mpCount = new int[kBuckets];
    mpOrder = new int[kBins];
    mpBucket = new int[kBins];

    // Hann. Built once at construction on the app thread, so the libm here is
    // free - and note this is DOUBLE sin, which is the variant that is safe
    // from a package .so on am335x (feedback_package_trig_lut is sinf/cosf).
    for (int i = 0; i < kFFT; i++)
      mpWindow[i] = (float)(0.5 * (1.0 - cos(2.0 * M_PI * (double)i / (double)kFFT)));

    memset(mpOla, 0, kFFT * sizeof(float));
    memset(mpKey, 0, kBins * sizeof(float));

    // od::Option is NOT auto-serialized; without this the menu choices are lost
    // on quicksave (Option.h defaults mIsSerializationNeeded to false).
    mDirection.enableSerialization();
    mLoop.enableSerialization();

    mReady = (mpSetup && mpFftIn && mpFftOut && mpFftWork && mpSorted &&
              mpWindow && mpOla && mpKey && mpCount && mpOrder && mpBucket);
  }

  Sediment::~Sediment()
  {
    if (mpSetup) pffft_destroy_setup(mpSetup);
    if (mpFftIn) pffft_aligned_free(mpFftIn);
    if (mpFftOut) pffft_aligned_free(mpFftOut);
    if (mpFftWork) pffft_aligned_free(mpFftWork);
    if (mpSorted) pffft_aligned_free(mpSorted);
    delete[] mpWindow;
    delete[] mpOla;
    delete[] mpKey;
    delete[] mpCount;
    delete[] mpOrder;
    delete[] mpBucket;
  }

  void Sediment::setSample(od::Sample *sample)
  {
    // A plain bool write is the race-safe minimum against the audio thread;
    // the next trigger clears the rest of the transport state.
    mPlaying = false;
    Base::setSample(sample);
    mRatio = 1.0;
    if (sample && sample->mSampleRate > 0.0f)
      mRatio = (double)sample->mSampleRate / (double)globalConfig.sampleRate;
  }

  // Start playback from the top. The read position starts BEFORE the file so
  // the overlap-add is fully populated by the time source sample 0 is emitted;
  // otherwise every trigger fades the attack in over ~13 ms (measured peak
  // envelope 0.011, 0.159, 0.465, 0.790, 0.984, 1.000 across the first hops).
  // Cost is kFFT-kHop samples of trigger latency, about 16 ms, and no CPU.
  void Sediment::restart()
  {
    mReadPos = -(double)(kFFT - kHop) * mRatio;
    mPlaying = true;
    memset(mpOla, 0, kFFT * sizeof(float));
    mOlaWrite = 0;
    mHopPhase = 0;
  }

  // One analysis/synthesis hop: read kFFT samples at the playhead, transform,
  // permute the bins, transform back, and overlap-add kFFT samples into the
  // output ring. Called once every kHop output samples.
  void Sediment::doHop()
  {
    od::Sample *pSample = mpSample;

    // ---- read + window -------------------------------------------------
    if (pSample == 0 || pSample->mSampleCount == 0)
    {
      memset(mpFftIn, 0, kFFT * sizeof(float));
    }
    else
    {
      const int n = (int)pSample->mSampleCount;
      const int ch = (int)pSample->mChannelCount;
      const bool loop = (mLoop.value() == 2);
      for (int i = 0; i < kFFT; i++)
      {
        // Interpolated read: the file rate need not match the engine rate, and
        // the pool does not resample on load. Without this a 44.1k file plays
        // 8.84% sharp, about +1.5 semitones - the built-in heads all correct
        // for this (LoopHead's mSpeedAdjustment).
        double p = mReadPos + (double)i * mRatio;
        float s = 0.0f;
        if (loop && n > 0)
        {
          // Wrap INSIDE the gather. Letting the window run off the end and
          // zero-pad instead makes the loop point a measured ~13 ms fade to
          // -39 dB and back, because the frame faithfully renders "file, then
          // silence" and then the start fades in from a cold overlap-add.
          p = fmod(p, (double)n);
          if (p < 0.0) p += (double)n;
        }
        const int i0 = (int)floor(p);
        const float fr = (float)(p - (double)i0);
        int a = i0, b = i0 + 1;
        if (loop && n > 0)
        {
          if (a >= n) a -= n;
          if (b >= n) b -= n;
        }
        if (a >= 0 && a < n && b >= 0 && b < n)
        {
          float sa = pSample->get(a, 0);
          float sb = pSample->get(b, 0);
          if (ch > 1)
          {
            sa = 0.5f * (sa + pSample->get(a, 1));
            sb = 0.5f * (sb + pSample->get(b, 1));
          }
          s = sa + (sb - sa) * fr;
        }
        mpFftIn[i] = s * mpWindow[i];
      }
    }

    pffft_transform_ordered(mpSetup, mpFftIn, mpFftOut, mpFftWork, PFFFT_FORWARD);

    // ---- the sort ------------------------------------------------------
    // Verified pffft real layout: out[0] = DC, out[1] = Nyquist, then
    // interleaved re/im for bins 1..kBins-1. Slot 0 is left strictly alone -
    // permuting it would swap DC against Nyquist, which is meaningless.
    memcpy(mpSorted, mpFftOut, kFFT * sizeof(float));

    const float macro = CLAMP(0.0f, 1.0f, mSort.value());
    if (macro > 0.0f)
    {
      // Key is magnitude-SQUARED. sqrt is monotonic, so this gives the identical
      // ordering with no transcendental anywhere in the hot path.
      float peak = 0.0f;
      for (int k = 1; k < kBins; k++)
      {
        const float re = mpFftOut[k * 2];
        const float im = mpFftOut[k * 2 + 1];
        const float key = re * re + im * im;
        mpKey[k] = key;
        if (key > peak) peak = key;
      }

      if (peak > 0.0f)
      {
        // Bins BELOW the threshold are eligible, so loud partials stay put and
        // anchor the spectrum while the floor between them melts - the behavior
        // that reads as pixel sorting rather than as noise.
        //
        // The two laws are DECOUPLED deliberately. A single `peak*macro*macro`
        // threshold measured 86.7% of bins eligible at macro=0.01 and 96% at
        // 0.1, so the multi-anchor regime that is the entire point existed only
        // below macro~0.05, where the crossfade rendered it at -45 dB and under
        // - i.e. inaudible. The knob was silence, then one global crossfade.
        // A dB-linear threshold sweeps the anchor count smoothly across the
        // whole travel; the crossfade saturates early so the character is
        // audible while anchors still exist.
        const float thresh = peak * powf(10.0f, -6.0f * (1.0f - macro));
        const float xfade = (3.0f * macro > 1.0f) ? 1.0f : (3.0f * macro);
        const bool loudFirst = (mDirection.value() == 2);

        int k = 1;
        while (k < kBins)
        {
          if (mpKey[k] > thresh) { k++; continue; }

          // span = maximal run of eligible bins
          int lo = k;
          while (k < kBins && mpKey[k] <= thresh) k++;
          int hi = k; // exclusive
          const int span = hi - lo;
          if (span < 2) continue;

          // Counting sort over the span, bucketed by the float bit pattern.
          // Both the clear and the prefix sum are bounded to the buckets this
          // span actually occupies. Clearing/scanning all kBuckets per span
          // costs ~680 KB of memset per hop on a noisy frame with ~170 spans,
          // which is the worst case and exactly when there is least headroom.
          int bmin = kBuckets - 1, bmax = 0;
          for (int j = lo; j < hi; j++)
          {
            unsigned int u;
            memcpy(&u, &mpKey[j], sizeof(u));
            int b = (int)(u >> kBucketShift);
            if (b >= kBuckets) b = kBuckets - 1;
            mpBucket[j] = b;
            if (b < bmin) bmin = b;
            if (b > bmax) bmax = b;
          }
          for (int b = bmin; b <= bmax; b++) mpCount[b] = 0;
          for (int j = lo; j < hi; j++) mpCount[mpBucket[j]]++;

          // prefix sum -> starting offset per bucket
          int running = 0;
          if (loudFirst)
          {
            for (int b = bmax; b >= bmin; b--)
            {
              const int c = mpCount[b];
              mpCount[b] = running;
              running += c;
            }
          }
          else
          {
            for (int b = bmin; b <= bmax; b++)
            {
              const int c = mpCount[b];
              mpCount[b] = running;
              running += c;
            }
          }
          // scatter: mpOrder[i] is the source bin for destination slot lo+i
          for (int j = lo; j < hi; j++)
            mpOrder[mpCount[mpBucket[j]]++] = j;

          // Write the permuted complex pairs, crossfaded against the original
          // by the macro. A crossfade rather than a hard swap is what makes
          // this a continuous morph instead of an on/off gimmick.
          for (int i = 0; i < span; i++)
          {
            const int dst = lo + i;
            const int src = mpOrder[i];
            const float aRe = mpFftOut[dst * 2];
            const float aIm = mpFftOut[dst * 2 + 1];
            const float bRe = mpFftOut[src * 2];
            const float bIm = mpFftOut[src * 2 + 1];
            mpSorted[dst * 2] = aRe + (bRe - aRe) * xfade;
            mpSorted[dst * 2 + 1] = aIm + (bIm - aIm) * xfade;
          }
        }
      }
    }

    // ---- inverse + overlap-add ----------------------------------------
    pffft_transform_ordered(mpSetup, mpSorted, mpFftIn, mpFftWork, PFFFT_BACKWARD);

    // pffft is unscaled (BACKWARD(FORWARD(x)) = N*x), and 4x-overlapped Hann
    // analysis + Hann synthesis sums to 1.5, so fold both into one constant.
    const float norm = 1.0f / ((float)kFFT * 1.5f);
    for (int i = 0; i < kFFT; i++)
    {
      const int p = (mOlaWrite + i) & (kFFT - 1);
      mpOla[p] += mpFftIn[i] * mpWindow[i] * norm;
    }

    if (mpSample) mReadPos += (double)kHop * mRatio;
  }

  void Sediment::process()
  {
    float *trig = mTrigger.buffer();
    float *out = mOutput.buffer();

    const float level = CLAMP(0.0f, 1.0f, mLevel.value());
    const int total = mpSample ? (int)mpSample->mSampleCount : 0;
    const bool loop = (mLoop.value() == 2);

    for (int i = 0; i < FRAMELENGTH; i++)
    {
      // Threshold is 0.5, not 0.0 - a 0.0 threshold trips on fuzz and DC
      // (feedback_comparator_gate_threshold).
      const bool high = trig[i] > 0.5f;
      if (high && !mTrigWasHigh && mReady && total > 0)
        restart();
      mTrigWasHigh = high;

      if (mHopPhase == 0)
      {
        // Nothing attached means nothing to do - do not burn two FFTs per hop
        // synthesizing silence.
        if (!mReady || total == 0) mPlaying = false;
        else if (mPlaying && !loop && mReadPos >= (double)total)
          mPlaying = false;
        if (mPlaying) doHop();
      }

      float y = 0.0f;
      if (mPlaying)
      {
        const int p = (mOlaWrite + mHopPhase) & (kFFT - 1);
        y = mpOla[p];
        mpOla[p] = 0.0f; // consume, so the slot is clean for the frame 4 hops out
        // Display position. Clamped because the pre-roll starts negative.
        int disp = (int)mReadPos;
        if (disp < 0) disp = 0;
        else if (disp >= total) disp = total > 0 ? total - 1 : 0;
        mCurrentIndex = disp;
      }

      out[i] = y * level;

      mHopPhase++;
      if (mHopPhase >= kHop)
      {
        mHopPhase = 0;
        mOlaWrite = (mOlaWrite + kHop) & (kFFT - 1);
      }
    }
  }

} // namespace stolmine
