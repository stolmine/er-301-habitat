// Test stub: the minimal od:: surface SpectralFreeze.h/.cpp compile against.
#pragma once
#include <od/config.h>
#include <stdint.h>
#include <cstddef>
namespace od
{
  class Inlet {
  public:
    Inlet(const char *n) : mName(n) {}
    float *buffer() { return mpBuffer; }
    void setBuffer(float *b) { mpBuffer = b; }
    const char *mName; float *mpBuffer = nullptr;
  };
  class Outlet {
  public:
    Outlet(const char *n) : mName(n) {}
    float *buffer() { return mpBuffer; }
    void setBuffer(float *b) { mpBuffer = b; }
    const char *mName; float *mpBuffer = nullptr;
  };
  class Parameter {
  public:
    Parameter(const char *n, float v = 0.0f) : mName(n), mValue(v) {}
    float value() { return mValue; }
    void hardSet(float v) { mValue = v; }
    const char *mName; float mValue;
  };
  class Option {
  public:
    Option(const char *n, int v) : mName(n), mValue(v) {}
    int value() { return mValue; }
    void set(int v) { mValue = v; }
    void enableSerialization() {}
    const char *mName; int mValue;
  };
  class Object {
  public:
    Object() {} virtual ~Object() {}
    virtual void process() {}
    void addInput(Inlet &) {} void addOutput(Outlet &) {}
    void addParameter(Parameter &) {} void addOption(Option &) {}
  };
}
