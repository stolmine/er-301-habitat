%module catchall_libcatchall
%include <od/glue/mod.cpp.swig>

%{
#undef SWIGLUA
#include "Sfera.h"
#include "Lambda.h"
#include "Flakes.h"
#include "Som.h"
#include "SomSphereGraphic.h"
#define SWIGLUA
%}

%include "Sfera.h"
%include "Lambda.h"
%include "Flakes.h"
%include "Som.h"
%include "SomSphereGraphic.h"
