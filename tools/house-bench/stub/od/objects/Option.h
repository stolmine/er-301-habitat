#pragma once
#include <string>

// Minimal shim of od::Option for offline DSP benches (see Object.h stub).
namespace od {

  class Option {
  public:
    explicit Option(const char *name, int v = 1) : mName(name), mValue(v) {}
    int value() const { return mValue; }
    void set(int v) { mValue = v; }
    void enableSerialization() {}
    std::string mName;
    int mValue;
  };

}
