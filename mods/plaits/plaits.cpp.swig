%module plaits_libplaits
%include <od/glue/mod.cpp.swig>

%{

#undef SWIGLUA

#include "PlaitsVoice.h"

#define SWIGLUA

%}

%include "PlaitsVoice.h"
