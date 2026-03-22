%module clouds_libclouds
%include <od/glue/mod.cpp.swig>

%{
#undef SWIGLUA
#include "Clouds.h"
#define SWIGLUA
%}

%include "Clouds.h"
