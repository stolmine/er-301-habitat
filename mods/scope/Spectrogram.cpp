#include "Spectrogram.h"
#include "pffft.h"
#include <od/config.h>
#include <string.h>
#include <math.h>

namespace scope_unit
{

  // Single-window Spectrogram with a switchable FFT size (256 or 512). The wider ply
  // units select 512 so the extra display columns are backed by real bins. Same analysis
  // otherwise (Hann, every-4-frames rate, per-bin peak-decay + smoothed RMS).
  static const int kMaxN = 512;
  static const int kMaxBins = kMaxN / 2;   // 256

  struct Spectrogram::Internal
  {
    PFFFT_Setup *fftSetup256;
    PFFFT_Setup *fftSetup512;
    float *fftIn;
    float *fftOut;
    float *fftWork;
    float hann256[256];
    float hann512[512];
    float fftPeak[kMaxBins];
    float fftRms[kMaxBins];
    float ringBuf[kMaxN];
    int ringPos;
    int fftFrameCount;
    bool fftReady;
    int curSize;

    void Init()
    {
      fftSetup256 = 0;
      fftSetup512 = 0;
      fftIn = 0;
      fftOut = 0;
      fftWork = 0;
      fftReady = false;
      memset(fftPeak, 0, sizeof(fftPeak));
      memset(fftRms, 0, sizeof(fftRms));
      memset(ringBuf, 0, sizeof(ringBuf));
      ringPos = 0;
      fftFrameCount = 0;
      curSize = 256;
    }

    void Cleanup()
    {
      if (fftSetup256) { pffft_destroy_setup(fftSetup256); fftSetup256 = 0; }
      if (fftSetup512) { pffft_destroy_setup(fftSetup512); fftSetup512 = 0; }
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
    addParameter(mFFTSize);

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
    if (bin < 0 || bin >= kMaxBins) return 0.0f;
    return mpInternal->fftPeak[bin];
  }

  float Spectrogram::getFFTRms(int bin)
  {
    if (bin < 0 || bin >= kMaxBins) return 0.0f;
    return mpInternal->fftRms[bin];
  }

  int Spectrogram::getFFTSize()
  {
    return mpInternal->curSize;
  }

  // Greatest-average-energy bin, parabolic-interpolated for sub-bin frequency.
  static int findPeakBin(const float *rms, int nb, float &fracOut)
  {
    int best = 1;                    // skip DC (bin 0)
    float bestV = 0.0f;
    for (int k = 1; k < nb; k++)
      if (rms[k] > bestV) { bestV = rms[k]; best = k; }
    fracOut = 0.0f;
    if (best > 0 && best < nb - 1)
    {
      float a = rms[best - 1], b = rms[best], c = rms[best + 1];
      float denom = (a - 2.0f * b + c);
      if (denom < -1e-12f || denom > 1e-12f)
      {
        float d = 0.5f * (a - c) / denom;   // -0.5..0.5 offset
        if (d > -1.0f && d < 1.0f) fracOut = d;
      }
    }
    return best;
  }

  float Spectrogram::getPeakHz()
  {
    Internal &s = *mpInternal;
    int nb = s.curSize / 2;
    float frac;
    int bin = findPeakBin(s.fftRms, nb, frac);
    float binHz = (float)globalConfig.sampleRate / (float)s.curSize;
    return ((float)bin + frac) * binHz;
  }

  float Spectrogram::getPeakDb()
  {
    Internal &s = *mpInternal;
    int nb = s.curSize / 2;
    float frac;
    int bin = findPeakBin(s.fftRms, nb, frac);
    float mag = s.fftRms[bin];
    return 20.0f * log10f(mag + 1e-10f);
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

    // Mono sum into the 512-sample ring buffer for FFT analysis.
    for (int i = 0; i < FRAMELENGTH; i++)
    {
      s.ringBuf[s.ringPos] = (inL[i] + inR[i]) * 0.5f;
      s.ringPos = (s.ringPos + 1) & (kMaxN - 1);
    }

    // Lazy FFT init: both 256 and 512 setups + Hann windows, buffers sized 512.
    if (!s.fftReady)
    {
      s.fftSetup256 = pffft_new_setup(256, PFFFT_REAL);
      s.fftSetup512 = pffft_new_setup(512, PFFFT_REAL);
      s.fftIn = (float *)pffft_aligned_malloc(kMaxN * sizeof(float));
      s.fftOut = (float *)pffft_aligned_malloc(kMaxN * sizeof(float));
      s.fftWork = (float *)pffft_aligned_malloc(kMaxN * sizeof(float));
      if (s.fftSetup256 && s.fftSetup512 && s.fftIn && s.fftOut && s.fftWork)
      {
        for (int k = 0; k < 256; k++)
          s.hann256[k] = 0.5f * (1.0f - cosf(2.0f * 3.14159f * (float)k / 255.0f));
        for (int k = 0; k < 512; k++)
          s.hann512[k] = 0.5f * (1.0f - cosf(2.0f * 3.14159f * (float)k / 511.0f));
        s.fftReady = true;
      }
    }
    if (!s.fftReady)
      return;

    // FFT size from the parameter (256 or 512). Reset peak/rms on a change.
    int fftSize = ((int)(mFFTSize.value() + 0.5f) >= 512) ? 512 : 256;
    int nb = fftSize / 2;
    if (fftSize != s.curSize)
    {
      s.curSize = fftSize;
      memset(s.fftPeak, 0, sizeof(s.fftPeak));
      memset(s.fftRms, 0, sizeof(s.fftRms));
    }

    // FFT: compute every 4 frames (the original rate; single window).
    s.fftFrameCount++;
    if (s.fftFrameCount >= 4)
    {
      s.fftFrameCount = 0;
      const float *win = (fftSize == 256) ? s.hann256 : s.hann512;
      PFFFT_Setup *setup = (fftSize == 256) ? s.fftSetup256 : s.fftSetup512;
      int start = (s.ringPos - fftSize) & (kMaxN - 1);
      for (int k = 0; k < fftSize; k++)
        s.fftIn[k] = s.ringBuf[(start + k) & (kMaxN - 1)] * win[k];

      pffft_transform_ordered(setup, s.fftIn, s.fftOut, s.fftWork, PFFFT_FORWARD);

      float invSize = 1.0f / (float)fftSize;
      float peakDecay = 0.92f;
      float rmsSmooth = 0.3f;
      for (int k = 0; k < nb; k++)
      {
        float re = s.fftOut[k * 2];
        float im = s.fftOut[k * 2 + 1];
        float mag = sqrtf(re * re + im * im) * invSize;
        if (mag > s.fftPeak[k])
          s.fftPeak[k] = mag;
        else
          s.fftPeak[k] *= peakDecay;
        s.fftRms[k] += (mag - s.fftRms[k]) * rmsSmooth;
      }
    }
  }

} // namespace scope_unit
