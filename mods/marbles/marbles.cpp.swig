%module marbles_libmarbles
%include <od/glue/mod.cpp.swig>

%{
#undef SWIGLUA
#include "MarblesT.h"
#include "MarblesX.h"
#define SWIGLUA
%}

%include "MarblesT.h"
%include "MarblesX.h"
