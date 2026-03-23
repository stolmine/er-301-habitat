%module commotio_libcommotio
%include <od/glue/mod.cpp.swig>

%{
#undef SWIGLUA
#include "Commotio.h"
#define SWIGLUA
%}

%include "Commotio.h"
