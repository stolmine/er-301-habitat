%module house_libhouse
%include <od/glue/mod.cpp.swig>

%{
#undef SWIGLUA
#include "atoms/KWoodRoom.h"
#define SWIGLUA
%}

%include "atoms/KWoodRoom.h"
