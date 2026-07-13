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
// FabricGraphic: Fabula's 2.5D "fabric" overview viz (od::Graphic subclass,
// header-only, all-inline). Reads APFTank's inline viz getters.
#include "FabricGraphic.h"
#define SWIGLUA
%}

%include "atoms/APFTank.h"
%include "atoms/STFTSpectral.h"
%include "FabricGraphic.h"
