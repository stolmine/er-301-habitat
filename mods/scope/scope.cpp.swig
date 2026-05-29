%module scope_libscope
%include <od/glue/mod.cpp.swig>

%{
#undef SWIGLUA
#include "Scope.h"
#include "ScopeControlBox.h"
#include "ScopeGraphic.h"
#include "ScopeVoltsReadout.h"
#include "Spectrogram.h"
#define SWIGLUA
%}

%include "Scope.h"
%include "ScopeControlBox.h"
%include "ScopeGraphic.h"
%include "ScopeVoltsReadout.h"
%include "Spectrogram.h"
