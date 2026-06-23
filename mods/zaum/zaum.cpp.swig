%module zaum_libzaum
%include <od/glue/mod.cpp.swig>

%{
#undef SWIGLUA
// APFTank: Phase 1 north-star primitive. Dattorro/Gardner figure-8
// allpass tank as a self-contained od::Object. Zaum (Phase 5) reuses
// it verbatim. Plan: planning/fabula-design.md.
#include "atoms/APFTank.h"
#define SWIGLUA
%}

%include "atoms/APFTank.h"
