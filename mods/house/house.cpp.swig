%module house_libhouse
%include <od/glue/mod.cpp.swig>

%{
#undef SWIGLUA
#include "atoms/KWoodRoom.h"
#include "atoms/WoodenBox.h"
#include "atoms/CreamCoat.h"
#include "atoms/BrightAmbience3.h"
#define SWIGLUA
%}

%include "atoms/KWoodRoom.h"
%include "atoms/WoodenBox.h"
%include "atoms/CreamCoat.h"
%include "atoms/BrightAmbience3.h"
