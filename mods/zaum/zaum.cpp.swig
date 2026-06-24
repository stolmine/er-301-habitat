%module zaum_libzaum
%include <od/glue/mod.cpp.swig>

%{
#undef SWIGLUA
// APFTank: Phase 1 north-star primitive. Dattorro/Gardner figure-8
// allpass tank as a self-contained od::Object. Zaum (Phase 5) reuses
// it verbatim. Plan: planning/fabula-design.md.
#include "atoms/APFTank.h"
// STFTSpectral: Phase 2 north-star primitive. STFT spectral reverb (SMD)
// using ShyFFT, N=1024, hop=256, sine window. Sub-phase 0.2.0.1: identity
// passthrough. Plan: planning/sujet-design.md.
#include "atoms/STFTSpectral.h"
#define SWIGLUA
%}

%include "atoms/APFTank.h"
%include "atoms/STFTSpectral.h"
