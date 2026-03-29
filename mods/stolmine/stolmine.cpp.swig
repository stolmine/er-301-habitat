%module stolmine_libstolmine
%include <od/glue/mod.cpp.swig>

%{
#undef SWIGLUA
#include "NR.h"
#include "NRCircle.h"
#include "Discont.h"
#include "LatchFilter.h"
#include "Canals.h"
#include "TrackerSeq.h"
#include "StepListGraphic.h"
#include "SeqInfoGraphic.h"
#include "GateSeq.h"
#include "ChaselightGraphic.h"
#include "GateSeqInfoGraphic.h"
#include "GestureSeq.h"
#include "GestureHeadDisplay.h"
#include "WriteIndicator.h"
#define SWIGLUA
%}

%include "NR.h"
%include "NRCircle.h"
%include "Discont.h"
%include "LatchFilter.h"
%include "Canals.h"
%include "TrackerSeq.h"
%include "StepListGraphic.h"
%include "SeqInfoGraphic.h"
%include "GateSeq.h"
%include "ChaselightGraphic.h"
%include "GateSeqInfoGraphic.h"
%include "GestureSeq.h"
%include "GestureHeadDisplay.h"
%include "WriteIndicator.h"
