%module spreadsheet_libspreadsheet
%include <od/glue/mod.cpp.swig>

%{
#undef SWIGLUA
#include "TrackerSeq.h"
#include "StepListGraphic.h"
#include "SeqInfoGraphic.h"
#include "GateSeq.h"
#include "ChaselightGraphic.h"
#include "GateSeqInfoGraphic.h"
#include "Etcher.h"
#include "SegmentListGraphic.h"
#include "TransferCurveGraphic.h"
#include "Filterbank.h"
#include "BandListGraphic.h"
#include "FilterResponseGraphic.h"
#include "MultitapDelay.h"
#include "TapListGraphic.h"
#include "RaindropGraphic.h"
#define SWIGLUA
%}

%include "TrackerSeq.h"
%include "StepListGraphic.h"
%include "SeqInfoGraphic.h"
%include "GateSeq.h"
%include "ChaselightGraphic.h"
%include "GateSeqInfoGraphic.h"
%include "Etcher.h"
%include "SegmentListGraphic.h"
%include "TransferCurveGraphic.h"
%include "Filterbank.h"
%include "BandListGraphic.h"
%include "FilterResponseGraphic.h"
%include "MultitapDelay.h"
%include "TapListGraphic.h"
%include "RaindropGraphic.h"
