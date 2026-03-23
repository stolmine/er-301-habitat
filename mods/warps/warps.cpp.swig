%module warps_libwarps
%include <od/glue/mod.cpp.swig>

%{

#undef SWIGLUA

#include "WarpsModulator.h"

#define SWIGLUA

%}

%include "WarpsModulator.h"
