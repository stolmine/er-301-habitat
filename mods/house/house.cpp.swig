%module house_libhouse
%include <od/glue/mod.cpp.swig>

%{
#undef SWIGLUA
#include "atoms/KWoodRoom.h"
#include "atoms/WoodenBox.h"
#include "atoms/CreamCoat.h"
#include "atoms/BrightAmbience3.h"
#include "atoms/Verbity.h"
#include "atoms/Galactic.h"
#include "atoms/RotCoat.h"
// XYZ.h parked 2026-06-05: structurally too sin-heavy for
// Cortex-A8 (100%+ CPU spikes even with poly Spiral). Header,
// Lua wrapper, AllpassMono component, and plan doc all
// preserved on disk for rpi-era revival.
// #include "atoms/XYZ.h"
#define SWIGLUA
%}

%include "atoms/KWoodRoom.h"
%include "atoms/WoodenBox.h"
%include "atoms/CreamCoat.h"
%include "atoms/BrightAmbience3.h"
%include "atoms/Verbity.h"
%include "atoms/Galactic.h"
%include "atoms/RotCoat.h"
// %include "atoms/XYZ.h"   -- parked, see %{} block
