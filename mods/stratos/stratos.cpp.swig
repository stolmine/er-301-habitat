%module stratos_libstratos
%include <od/glue/mod.cpp.swig>

%{
#undef SWIGLUA
#include "Stratos.h"
#define SWIGLUA
%}

%include "Stratos.h"
