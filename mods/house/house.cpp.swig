%module house_libhouse
%include <od/glue/mod.cpp.swig>

%{
#undef SWIGLUA
#include "Smoketest.h"
#define SWIGLUA
%}

%include "Smoketest.h"
