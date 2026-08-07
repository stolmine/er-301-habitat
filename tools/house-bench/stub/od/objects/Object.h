#pragma once
#include <string>
#include <vector>
#include <od/config.h>

// Minimal shims for the ER-301 object framework, sufficient to compile a unit's DSP
// source unmodified and drive it offline. Only the surface the DSP actually touches is
// modelled: buffer(), value(), and the add*() registration calls.
namespace od {

  class Inlet {
  public:
    explicit Inlet(const char *name) : mName(name) { mBuf.assign(kMax, 0.0f); }
    float *buffer() { return mBuf.data(); }
    static const int kMax = 1024;
    std::string mName;
    std::vector<float> mBuf;
  };

  class Outlet {
  public:
    explicit Outlet(const char *name) : mName(name) { mBuf.assign(kMax, 0.0f); }
    float *buffer() { return mBuf.data(); }
    static const int kMax = 1024;
    std::string mName;
    std::vector<float> mBuf;
  };

  class Parameter {
  public:
    Parameter(const char *name, float v = 0.0f) : mName(name), mValue(v) {}
    float value() const { return mValue; }
    float target() const { return mValue; }
    void hardSet(float v) { mValue = v; }
    void set(float v) { mValue = v; }
    std::string mName;
    float mValue;
  };

  class Option;

  class Object {
  public:
    virtual ~Object() {}
    void attach() {}
    void release() {}
    virtual void process() {}
    void addInput(Inlet &) {}
    void addOutput(Outlet &) {}
    void addParameter(Parameter &p) { mParams.push_back(&p); }
    void addOption(Option &) {}
    std::vector<Parameter*> mParams;   // registration order, for the offline harness
  };

}
