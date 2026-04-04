#include "CodescanOsc.h"
#include <od/config.h>
#include <hal/ops.h>
#include <math.h>
#include <string.h>

#ifdef __arm__
#include <od/extras/FileReader.h>
#else
#include <stdio.h>
#include <stdlib.h>
#endif

namespace stolmine
{

  CodescanOsc::CodescanOsc()
  {
    addInput(mVOct);
    addInput(mSync);
    addOutput(mOutput);
    addParameter(mScan);
    addParameter(mFundamental);
  }

  CodescanOsc::~CodescanOsc()
  {
    if (mData)
    {
      delete[] mData;
      mData = nullptr;
    }
  }

  void CodescanOsc::loadData(const char *path)
  {
    if (mData)
    {
      delete[] mData;
      mData = nullptr;
      mDataSize = 0;
    }

    mFilePath = path ? path : "";

#ifdef __arm__
    od::FileReader reader;
    if (!reader.open(mFilePath))
      return;

    uint32_t size = reader.getSizeInBytes();
    if (size == 0)
    {
      reader.close();
      return;
    }

    mData = new (std::nothrow) unsigned char[size];
    if (mData)
    {
      mDataSize = (int)reader.readBytes(mData, size);
    }
    reader.close();
#else
    FILE *f = fopen(path, "rb");
    if (!f)
      return;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size <= 0)
    {
      fclose(f);
      return;
    }

    mData = new (std::nothrow) unsigned char[size];
    if (mData)
    {
      mDataSize = (int)fread(mData, 1, size, f);
    }
    fclose(f);
#endif
  }

  const char *CodescanOsc::getFilePath()
  {
    return mFilePath.c_str();
  }

  int CodescanOsc::getDataSize()
  {
    return mDataSize;
  }

  void CodescanOsc::process()
  {
    float *voct = mVOct.buffer();
    float *sync = mSync.buffer();
    float *out = mOutput.buffer();

    if (!mData || mDataSize < 256)
    {
      for (int i = 0; i < FRAMELENGTH; i++)
        out[i] = 0.0f;
      return;
    }

    float sr = globalConfig.sampleRate;
    float f0 = CLAMP(0.1f, sr * 0.49f, mFundamental.value());
    float scan = CLAMP(0.0f, 1.0f, mScan.value());

    // DC blocker coefficient: ~20Hz highpass at any sample rate
    float dcCoeff = 6.2832f * 20.0f / sr; // 2*pi*fc/sr

    // Scan position: where in the data to read the waveform cycle
    // Use a 256-byte window as one waveform cycle
    int windowSize = 256;
    int maxOffset = mDataSize - windowSize;
    int scanOffset = (int)(scan * (float)maxOffset);
    if (scanOffset < 0) scanOffset = 0;
    if (scanOffset > maxOffset) scanOffset = maxOffset;

    for (int i = 0; i < FRAMELENGTH; i++)
    {
      // Sync on rising edge
      bool syncHigh = sync[i] > 0.5f;
      if (syncHigh && !mSyncWasHigh)
        mPhase = 0.0f;
      mSyncWasHigh = syncHigh;

      // V/Oct pitch: 0.1V per octave from ConstantOffset in 10Vpp range
      float pitch = voct[i] * 10.0f;
      float freq = f0 * powf(2.0f, pitch);

      // Phase accumulator (0-1 maps to one window cycle)
      mPhase += freq / sr;
      mPhase -= floorf(mPhase);

      // Read from data with linear interpolation
      float pos = mPhase * (float)(windowSize - 1);
      int idx0 = (int)pos;
      float frac = pos - (float)idx0;
      int idx1 = idx0 + 1;
      if (idx1 >= windowSize) idx1 = 0;

      // Signed 8-bit: interpret byte as signed, normalize to -1/+1
      float s0 = (float)((signed char)mData[scanOffset + idx0]) / 127.0f;
      float s1 = (float)((signed char)mData[scanOffset + idx1]) / 127.0f;
      float raw = s0 + (s1 - s0) * frac;

      // DC blocker: subtract tracked DC offset
      mDCState += (raw - mDCState) * dcCoeff;
      out[i] = raw - mDCState;
    }
  }

} // namespace stolmine
