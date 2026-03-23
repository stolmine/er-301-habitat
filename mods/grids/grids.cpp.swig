%module grids_libgrids
%include <od/glue/mod.cpp.swig>

%{
#undef SWIGLUA
#include "Grids.h"
#include "GridsCircle.h"
#define SWIGLUA
%}

%include "Grids.h"
%include "GridsCircle.h"
