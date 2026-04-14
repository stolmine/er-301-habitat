%module porcelain_libporcelain
%include <od/glue/mod.cpp.swig>

%{
#undef SWIGLUA
#include "Chime.h"
#include "ChimeEnergyGraphic.h"
#define SWIGLUA
%}

%include "Chime.h"
%include "ChimeEnergyGraphic.h"
