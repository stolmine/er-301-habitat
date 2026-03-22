%module nr_libnr
%include <od/glue/mod.cpp.swig>

%{
#undef SWIGLUA
#include "NR.h"
#include "NRCircle.h"
#define SWIGLUA
%}

%include "NR.h"
%include "NRCircle.h"
