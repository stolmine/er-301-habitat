// Test stub providing the minimal od:: surface that Breccia.h/.cpp compile
// against: Sample, Inlet, Outlet, Parameter, Option, TapeHead. Faithful to the
// pieces of the real SDK API that Breccia actually touches (Sample::get is the
// real interleaved indexing; Head carries mpSample and mCurrentIndex).
#pragma once

#include <od/config.h>
#include <stdint.h>
#include <cstddef>

namespace od
{
  class Sample
  {
  public:
    float *mpData = nullptr;
    uint32_t mSampleCount = 0;
    uint32_t mChannelCount = 1;
    float mSampleRate = 48000.0f;

    inline float get(int i, int channel)
    {
      return mpData[i * mChannelCount + channel];
    }
  };

  class Inlet
  {
  public:
    Inlet(const char *name) : mName(name) {}
    float *buffer() { return mpBuffer; }
    void setBuffer(float *b) { mpBuffer = b; }
    const char *mName;
    float *mpBuffer = nullptr;
  };

  class Outlet
  {
  public:
    Outlet(const char *name) : mName(name) {}
    float *buffer() { return mpBuffer; }
    void setBuffer(float *b) { mpBuffer = b; }
    const char *mName;
    float *mpBuffer = nullptr;
  };

  class Parameter
  {
  public:
    Parameter(const char *name, float initial = 0.0f)
        : mName(name), mValue(initial) {}
    float value() { return mValue; }
    void hardSet(float v) { mValue = v; }
    const char *mName;
    float mValue;
  };

  class Option
  {
  public:
    Option(const char *name, int initial) : mName(name), mValue(initial) {}
    int value() { return mValue; }
    void set(int v) { mValue = v; }
    void enableSerialization() {}
    const char *mName;
    int mValue;
  };

  class TapeHead
  {
  public:
    TapeHead() {}
    virtual ~TapeHead() {}

    virtual void setSample(Sample *sample) { mpSample = sample; }

    void addInput(Inlet &) {}
    void addOutput(Outlet &) {}
    void addParameter(Parameter &) {}
    void addOption(Option &) {}

    Sample *mpSample = nullptr;
    int mCurrentIndex = 0;
  };
}
