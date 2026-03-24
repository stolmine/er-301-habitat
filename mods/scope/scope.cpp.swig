%module scope_libscope
%include <od/glue/mod.cpp.swig>

%{
#undef SWIGLUA
#include "Scope.h"
#define SWIGLUA
%}

%include "Scope.h"
