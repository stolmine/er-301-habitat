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
