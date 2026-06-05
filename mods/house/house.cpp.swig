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
// Chain-as-unit atoms (composed by TickerTape and future chains;
// component-only -- no Lua units per feedback_atoms_as_components).
#include "atoms/Console0Channel.h"
#include "atoms/Console0Buss.h"
#include "atoms/ChromeOxide.h"
#include "atoms/ChainMix.h"
// Lacquer: monolithic chain-as-unit (mixed-rate brackets, can't
// graph-compose at host rate). Inlines Console0 sat curves +
// downsample shell + Cojones + 2x upsample bracket + TapeFat +
// ChainMix into a single Object.
#include "atoms/Lacquer.h"
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
%include "atoms/Console0Channel.h"
%include "atoms/Console0Buss.h"
%include "atoms/ChromeOxide.h"
%include "atoms/ChainMix.h"
%include "atoms/Lacquer.h"
