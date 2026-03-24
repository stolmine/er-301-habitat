%module stolmine_libstolmine
%include <od/glue/mod.cpp.swig>

%{
#undef SWIGLUA
#include "NR.h"
#include "NRCircle.h"
#include "Discont.h"
#include "LatchFilter.h"
#define SWIGLUA
%}

%include "NR.h"
%include "NRCircle.h"
%include "Discont.h"
%include "LatchFilter.h"
