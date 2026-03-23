%module rings_librings
%include <od/glue/mod.cpp.swig>

%{

#undef SWIGLUA

#include "RingsVoice.h"

#define SWIGLUA

%}

%include "RingsVoice.h"
