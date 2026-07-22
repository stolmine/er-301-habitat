#include "Spectrogram.h"
#include "pffft.h"
#include <od/config.h>
#include <string.h>
#include <math.h>

namespace scope_unit
{

  // Overlap-averaging Spectrogram with a switchable FFT size.
  //   ply 1     : 256-pt, single window (the original behaviour)
  //   ply 2, 3  : 256-pt + overlap-average of the last 2/3 spectra
  //   ply 4, 6  : 512-pt (half the bin width) + overlap-average of 4/6 spectra
  // A 256-pt and a 512-pt pffft setup are both allocated; buffers are sized 512.
  static const int kMaxN = 512;
  static const int kMaxBins = kMaxN / 2;   // 256
  static const int kMaxAvg = 6;            // deepest overlap window

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
    int ringPos;                       // 0..kMaxN-1, masked
    int fftHopCount;                   // frames since last FFT
    bool fftReady;
    // rolling history of the last kMaxAvg magnitude spectra (for overlap-averaging)
    float magHist[kMaxAvg][kMaxBins];
    int histWrite;                     // next slot to write
    int histFilled;                    // how many valid entries (<= kMaxAvg)
    int curSize;                       // FFT size currently reported (256/512)

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
      memset(magHist, 0, sizeof(magHist));
      ringPos = 0;
      fftHopCount = 0;
      histWrite = 0;
      histFilled = 0;
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
    addParameter(mPly);

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

    // Resolve ply -> FFT size + overlap window count.
    int ply = (int)(mPly.value() + 0.5f);
    if (ply < 1) ply = 1;
    if (ply > kMaxAvg) ply = kMaxAvg;
    int fftSize = (ply <= 3) ? 256 : 512;
    int avgN = ply;                     // number of spectra to average (1 = no averaging)
    int nb = fftSize / 2;               // bins
    // FFT hop (in frames):
    //   ply 1  -> every 4 frames (the original base rate; single window, unchanged look).
    //   ply>=2 -> overlapping windows for the averaging: 256-pt every 1 frame (50%),
    //             512-pt every 2 frames (50%).
    int hopFrames;
    if (avgN <= 1)
      hopFrames = 4;
    else
    {
      hopFrames = fftSize / 2 / FRAMELENGTH;
      if (hopFrames < 1) hopFrames = 1;
    }

    // If the size changed, reset the rolling history (bins/scale differ).
    if (fftSize != s.curSize)
    {
      s.curSize = fftSize;
      s.histFilled = 0;
      s.histWrite = 0;
      memset(s.fftPeak, 0, sizeof(s.fftPeak));
      memset(s.fftRms, 0, sizeof(s.fftRms));
    }

    s.fftHopCount++;
    if (s.fftHopCount < hopFrames)
      return;
    s.fftHopCount = 0;

    // Window the newest fftSize samples out of the ring.
    const float *win = (fftSize == 256) ? s.hann256 : s.hann512;
    PFFFT_Setup *setup = (fftSize == 256) ? s.fftSetup256 : s.fftSetup512;
    int start = (s.ringPos - fftSize) & (kMaxN - 1);
    for (int k = 0; k < fftSize; k++)
      s.fftIn[k] = s.ringBuf[(start + k) & (kMaxN - 1)] * win[k];

    pffft_transform_ordered(setup, s.fftIn, s.fftOut, s.fftWork, PFFFT_FORWARD);

    // Magnitude spectrum -> push into the rolling history.
    float invSize = 1.0f / (float)fftSize;
    float *cur = s.magHist[s.histWrite];
    for (int k = 0; k < nb; k++)
    {
      float re = s.fftOut[k * 2];
      float im = s.fftOut[k * 2 + 1];
      cur[k] = sqrtf(re * re + im * im) * invSize;
    }
    s.histWrite = (s.histWrite + 1) % kMaxAvg;
    if (s.histFilled < kMaxAvg) s.histFilled++;

    // Overlap-average the last min(avgN, filled) spectra, then feed peak/rms.
    int use = avgN < s.histFilled ? avgN : s.histFilled;
    if (use < 1) use = 1;
    float invUse = 1.0f / (float)use;
    float peakDecay = 0.92f;
    float rmsSmooth = 0.3f;
    for (int k = 0; k < nb; k++)
    {
      float acc = 0.0f;
      for (int j = 1; j <= use; j++)
      {
        int idx = (s.histWrite - j + kMaxAvg) % kMaxAvg;
        acc += s.magHist[idx][k];
      }
      float mag = acc * invUse;
      if (mag > s.fftPeak[k])
        s.fftPeak[k] = mag;
      else
        s.fftPeak[k] *= peakDecay;
      s.fftRms[k] += (mag - s.fftRms[k]) * rmsSmooth;
    }
  }

} // namespace scope_unit
