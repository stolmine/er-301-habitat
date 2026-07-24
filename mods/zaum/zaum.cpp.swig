%module zaum_libzaum
%include <od/glue/mod.cpp.swig>

%{
#undef SWIGLUA
// STFTSpectral: STFT spectral reverb (SMD) using ShyFFT, N=1024, hop=256,
// sine window. Plan: planning/sujet-design.md.
// (Fabula/APFTank + FabricGraphic were promoted to the spreadsheet package.)
#include "atoms/STFTSpectral.h"
#define SWIGLUA
%}

%include "atoms/STFTSpectral.h"
