%module kryos_libkryos
%include <od/glue/mod.cpp.swig>

%{
#undef SWIGLUA
#include <cstdint>
#include "Kryos.h"
#define SWIGLUA
%}

%include "Kryos.h"
