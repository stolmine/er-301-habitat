#include "CodescanFilter.h"
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

  CodescanFilter::CodescanFilter()
  {
    addInput(mInput);
    addOutput(mOutput);
    addParameter(mScan);
    addParameter(mTaps);
    addParameter(mMix);
    memset(mDelayLine, 0, sizeof(mDelayLine));
  }

  CodescanFilter::~CodescanFilter()
  {
    if (mData)
    {
      delete[] mData;
      mData = nullptr;
    }
  }

  void CodescanFilter::loadData(const char *path)
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

  const char *CodescanFilter::getFilePath()
  {
    return mFilePath.c_str();
  }

  int CodescanFilter::getDataSize()
  {
    return mDataSize;
  }

  void CodescanFilter::process()
  {
    float *in = mInput.buffer();
    float *out = mOutput.buffer();

    if (!mData || mDataSize < kMaxFIRTaps)
    {
      for (int i = 0; i < FRAMELENGTH; i++)
        out[i] = in[i];
      return;
    }

    float scan = CLAMP(0.0f, 1.0f, mScan.value());
    int numTaps = CLAMP(4, kMaxFIRTaps, (int)(mTaps.value() + 0.5f));
    float mix = CLAMP(0.0f, 1.0f, mMix.value());

    // DC blocker coefficient: ~20Hz highpass at any sample rate
    float sr = globalConfig.sampleRate;
    float dcCoeff = 6.2832f * 20.0f / sr;

    // Read FIR kernel from data at scan position
    int maxOffset = mDataSize - numTaps;
    int scanOffset = (int)(scan * (float)maxOffset);
    if (scanOffset < 0) scanOffset = 0;
    if (scanOffset > maxOffset) scanOffset = maxOffset;

    // Build normalized kernel
    float kernel[kMaxFIRTaps];
    float normSum = 0.0f;
    for (int t = 0; t < numTaps; t++)
    {
      kernel[t] = (float)((signed char)mData[scanOffset + t]) / 127.0f;
      normSum += fabsf(kernel[t]);
    }
    // Normalize to prevent gain explosion
    if (normSum > 0.001f)
    {
      float invNorm = 1.0f / normSum;
      for (int t = 0; t < numTaps; t++)
        kernel[t] *= invNorm;
    }

    for (int i = 0; i < FRAMELENGTH; i++)
    {
      // Write to delay line
      mDelayLine[mWriteIdx] = in[i];
      mWriteIdx = (mWriteIdx + 1) % numTaps;

      // Convolve
      float wet = 0.0f;
      int readIdx = mWriteIdx;
      for (int t = 0; t < numTaps; t++)
      {
        readIdx--;
        if (readIdx < 0) readIdx = numTaps - 1;
        wet += mDelayLine[readIdx] * kernel[t];
      }

      float mixed = in[i] * (1.0f - mix) + wet * mix;

      // DC blocker on output
      mDCState += (mixed - mDCState) * dcCoeff;
      out[i] = mixed - mDCState;
    }
  }

} // namespace stolmine
