%module anamnesis_libanamnesis
%include <od/glue/mod.cpp.swig>

%{
#undef SWIGLUA
#include "atoms/Anamnesis.h"
#define SWIGLUA
%}

%include "atoms/Anamnesis.h"
