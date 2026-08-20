#!/usr/bin/env bash
set -e
cd "$(dirname "$0")"
rm -rf stub; mkdir -p stub/od/objects stub/hal
cat > stub/od/config.h <<'H'
#pragma once
struct _GlobalConfig { int frameLength = 128; float sampleRate = 48000.0f; };
extern _GlobalConfig globalConfig;
#define FRAMELENGTH ((int)(4 * (globalConfig.frameLength / 4)))
H
cp ../parametric-band-test/stub/od/config.h stub/od/config.h 2>/dev/null || true
cat > stub/hal/ops.h <<'H'
#pragma once
#ifndef CLAMP
#define CLAMP(lo,hi,x) ((x)<(lo)?(lo):((x)>(hi)?(hi):(x)))
#endif
H
cat > stub/od/objects/Object.h <<'H'
#pragma once
#include <od/config.h>
namespace od {
 class Inlet { public: Inlet(const char*n):mName(n){} float*buffer(){return b;} void setBuffer(float*p){b=p;} const char*mName; float*b=nullptr; };
 class Outlet { public: Outlet(const char*n):mName(n){} float*buffer(){return b;} void setBuffer(float*p){b=p;} const char*mName; float*b=nullptr; };
 class Parameter { public: Parameter(const char*n,float v=0.f):mName(n),mV(v){} float value(){return mV;} void hardSet(float v){mV=v;} const char*mName; float mV; };
 class Option { public: Option(const char*n,int v):mName(n),mV(v){} int value(){return mV;} void set(int v){mV=v;} void enableSerialization(){} const char*mName; int mV; };
 class Object { public: Object(){} virtual ~Object(){} virtual void process(){}
  void addInput(Inlet&){} void addOutput(Outlet&){} void addParameter(Parameter&){} void addOption(Option&){} };
}
H
g++ -O3 -ffast-math -fno-tree-vectorize -msse4 -std=gnu++11 -w \
    -Istub -I../../mods/house/atoms main.cpp -o /tmp/parametric-eq-test -lm
exec /tmp/parametric-eq-test
