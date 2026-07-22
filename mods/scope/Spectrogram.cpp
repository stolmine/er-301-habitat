#include "Spectrogram.h"
#include "pffft.h"
#include <od/config.h>
#include <string.h>
#include <math.h>

namespace scope_unit
{

  struct Spectrogram::Internal
  {
    PFFFT_Setup *fftSetup;
    float *fftIn;
    float *fftOut;
    float *fftWork;
    float hannWindow[256];
    float fftPeak[128];
    float fftRms[128];
    float ringBuf[256];
    int ringPos;
    int fftFrameCount;
    bool fftReady;

    void Init()
    {
      fftSetup = 0;
      fftIn = 0;
      fftOut = 0;
      fftWork = 0;
      fftReady = false;
      memset(fftPeak, 0, sizeof(fftPeak));
      memset(fftRms, 0, sizeof(fftRms));
      memset(ringBuf, 0, sizeof(ringBuf));
      ringPos = 0;
      fftFrameCount = 0;
    }

    void Cleanup()
    {
      if (fftSetup) { pffft_destroy_setup(fftSetup); fftSetup = 0; }
      if (fftIn) { pffft_aligned_free(fftIn); fftIn = 0; }
      if (fftOut) { pffft_aligned_free(fftOut); fftOut = 0; }
      if (fftWork) { pffft_aligned_free(fftWork); fftWork = 0; }
    }
  };

  Spectrogram::Spectrogram()
  {
    addInput(mInL);
    addInput(mInR);
    addOutput(mOutL);
    addOutput(mOutR);

    mpInternal = new Internal();
    mpInternal->Init();
  }

  Spectrogram::~Spectrogram()
  {
    mpInternal->Cleanup();
    delete mpInternal;
  }

  float Spectrogram::getFFTPeak(int bin)
  {
    if (bin < 0 || bin > 127) return 0.0f;
    return mpInternal->fftPeak[bin];
  }

  float Spectrogram::getFFTRms(int bin)
  {
    if (bin < 0 || bin > 127) return 0.0f;
    return mpInternal->fftRms[bin];
  }

  void Spectrogram::process()
  {
    Internal &s = *mpInternal;
    float *inL = mInL.buffer();
    float *inR = mInR.buffer();
    float *outL = mOutL.buffer();
    float *outR = mOutR.buffer();

    // Stereo passthrough
    memcpy(outL, inL, FRAMELENGTH * sizeof(float));
    memcpy(outR, inR, FRAMELENGTH * sizeof(float));

    // Mono sum into ring buffer for FFT analysis
    for (int i = 0; i < FRAMELENGTH; i++)
    {
      s.ringBuf[s.ringPos] = (inL[i] + inR[i]) * 0.5f;
      s.ringPos = (s.ringPos + 1) & 255;
    }

    // Lazy FFT init
    if (!s.fftReady)
    {
      s.fftSetup = pffft_new_setup(256, PFFFT_REAL);
      s.fftIn = (float *)pffft_aligned_malloc(256 * sizeof(float));
      s.fftOut = (float *)pffft_aligned_malloc(256 * sizeof(float));
      s.fftWork = (float *)pffft_aligned_malloc(256 * sizeof(float));
      if (s.fftSetup && s.fftIn && s.fftOut && s.fftWork)
      {
        for (int k = 0; k < 256; k++)
          s.hannWindow[k] = 0.5f * (1.0f - cosf(2.0f * 3.14159f * (float)k / 255.0f));
        s.fftReady = true;
      }
    }

    // FFT: compute every 4 frames (~12 FFTs/sec at 48kHz/128)
    s.fftFrameCount++;
    if (s.fftFrameCount >= 4 && s.fftReady)
    {
      s.fftFrameCount = 0;
      for (int k = 0; k < 256; k++)
        s.fftIn[k] = s.ringBuf[(s.ringPos + k) & 255] * s.hannWindow[k];

      pffft_transform_ordered(s.fftSetup, s.fftIn, s.fftOut, s.fftWork, PFFFT_FORWARD);

      float peakDecay = 0.92f;
      float rmsSmooth = 0.3f;
      for (int k = 0; k < 128; k++)
      {
        float re = s.fftOut[k * 2];
        float im = s.fftOut[k * 2 + 1];
        float mag = sqrtf(re * re + im * im) / 256.0f;
        if (mag > s.fftPeak[k])
          s.fftPeak[k] = mag;
        else
          s.fftPeak[k] *= peakDecay;
        s.fftRms[k] += (mag - s.fftRms[k]) * rmsSmooth;
      }
    }
  }

} // namespace scope_unit
