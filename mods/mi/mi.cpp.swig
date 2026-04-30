%module mi_libmi
%include <od/glue/mod.cpp.swig>

%{
#undef SWIGLUA
#include "PlaitsVoice.h"
#include "Clouds.h"
#include "RingsVoice.h"
#include "Grids.h"
#include "GridsCircle.h"
#include "WarpsModulator.h"
#include "Stratos.h"
#include "Commotio.h"
#include "MarblesT.h"
#include "MarblesX.h"
#define SWIGLUA
%}

%include "PlaitsVoice.h"
%include "Clouds.h"
%include "RingsVoice.h"
%include "Grids.h"
%include "GridsCircle.h"
%include "WarpsModulator.h"
%include "Stratos.h"
%include "Commotio.h"
%include "MarblesT.h"
%include "MarblesX.h"
