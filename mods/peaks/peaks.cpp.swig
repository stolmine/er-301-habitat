%module peaks_libpeaks
%include <od/glue/mod.cpp.swig>

%{
#undef SWIGLUA
#include "PeaksUnit.h"
#define SWIGLUA
%}

%include "PeaksUnit.h"
