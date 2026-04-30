PKGNAME ?= mi
PKGVERSION ?= 1.0.0

include scripts/env.mk

EURORACK = eurorack

LIBNAME = lib$(PKGNAME)
OUT_DIR = $(PROFILE)/$(ARCH)
LIB_FILE = $(OUT_DIR)/$(LIBNAME).so
PACKAGE_FILE = $(OUT_DIR)/$(PKGNAME)-$(PKGVERSION).pkg

MOD_DIR = mods/$(PKGNAME)
ASSET_DIR = $(MOD_DIR)/assets

# Top-level unit wrappers (Clouds.cpp, Commotio.cpp, Grids.cpp, MarblesT.cpp,
# MarblesX.cpp, PlaitsVoice.cpp, RingsVoice.cpp, Stratos.cpp,
# WarpsModulator.cpp) plus pffft.c + pffft_stubs.c (Clouds dependency).
MOD_CPP = $(wildcard $(MOD_DIR)/*.cpp)
MOD_C = $(wildcard $(MOD_DIR)/*.c)

# Per-unit vendor source from eurorack/<vendor>/. Stolmine override files
# (mods/mi/elements/dsp/exciter.cc, mods/mi/rings/dsp/{part,resonator}.cc)
# replace specific upstream files; the find/exclude pattern picks up the
# canonical eurorack source for everything else.

# Clouds: granular processor + pvoc spectral chain + clouds-specific resources
CLOUDS_CC = $(EURORACK)/clouds/dsp/granular_processor.cc \
            $(EURORACK)/clouds/dsp/correlator.cc \
            $(EURORACK)/clouds/dsp/mu_law.cc \
            $(EURORACK)/clouds/dsp/pvoc/phase_vocoder.cc \
            $(EURORACK)/clouds/dsp/pvoc/stft.cc \
            $(EURORACK)/clouds/dsp/pvoc/frame_transformation.cc \
            $(EURORACK)/clouds/resources.cc

# Elements (Commotio): exciter.cc is the stolmine 48 kHz override; tube.cc +
# multistage_envelope.cc are upstream canonical (their .h overrides live in
# mods/mi/elements/dsp/).
ELEMENTS_CC = $(MOD_DIR)/elements/dsp/exciter.cc \
              $(EURORACK)/elements/dsp/tube.cc \
              $(EURORACK)/elements/dsp/multistage_envelope.cc \
              $(EURORACK)/elements/resources.cc

# Plaits: full dsp tree from eurorack canonical
PLAITS_CC = $(shell find -L $(EURORACK)/plaits/dsp -name '*.cc') \
            $(EURORACK)/plaits/resources.cc

# Rings: stolmine NEON-fork part.cc + resonator.cc replace upstream; rest of
# dsp tree is canonical
RINGS_CC = $(shell find -L $(EURORACK)/rings/dsp -name '*.cc' \
                   ! -name 'part.cc' ! -name 'resonator.cc') \
           $(MOD_DIR)/rings/dsp/part.cc \
           $(MOD_DIR)/rings/dsp/resonator.cc \
           $(EURORACK)/rings/resources.cc

# Warps: full dsp tree from eurorack canonical
WARPS_CC = $(shell find -L $(EURORACK)/warps/dsp -name '*.cc') \
           $(EURORACK)/warps/resources.cc

# Marbles: random + ramp generator trees + resources
MARBLES_CC = $(shell find -L $(EURORACK)/marbles/random -name '*.cc') \
             $(shell find -L $(EURORACK)/marbles/ramp -name '*.cc') \
             $(EURORACK)/marbles/resources.cc

# Single shared stmlib build (de-duplicated; previously each pkg had its own
# symlink to eurorack/stmlib so all 8 packages compiled units.cc / atan.cc /
# random.cc separately -- in the consolidated build that would be duplicate
# symbols at link time).
STMLIB_CC = $(EURORACK)/stmlib/dsp/units.cc \
            $(EURORACK)/stmlib/dsp/atan.cc \
            $(EURORACK)/stmlib/utils/random.cc

OBJECTS = $(addprefix $(OUT_DIR)/,$(MOD_CPP:%.cpp=%.o))
OBJECTS += $(addprefix $(OUT_DIR)/,$(MOD_C:%.c=%.o))
OBJECTS += $(addprefix $(OUT_DIR)/,$(CLOUDS_CC:%.cc=%.o))
OBJECTS += $(addprefix $(OUT_DIR)/,$(ELEMENTS_CC:%.cc=%.o))
OBJECTS += $(addprefix $(OUT_DIR)/,$(PLAITS_CC:%.cc=%.o))
OBJECTS += $(addprefix $(OUT_DIR)/,$(RINGS_CC:%.cc=%.o))
OBJECTS += $(addprefix $(OUT_DIR)/,$(WARPS_CC:%.cc=%.o))
OBJECTS += $(addprefix $(OUT_DIR)/,$(MARBLES_CC:%.cc=%.o))
OBJECTS += $(addprefix $(OUT_DIR)/,$(STMLIB_CC:%.cc=%.o))

SWIG_SOURCE = $(MOD_DIR)/$(PKGNAME).cpp.swig
SWIG_WRAPPER = $(OUT_DIR)/$(MOD_DIR)/$(PKGNAME)_swig.cpp
SWIG_OBJECT = $(SWIG_WRAPPER:%.cpp=%.o)
OBJECTS += $(SWIG_OBJECT)

ASSETS := $(call rwildcard, $(ASSET_DIR), *)

# INCLUDES order: $(MOD_DIR) FIRST so override headers (e.g. mods/mi/elements/
# dsp/dsp.h, mods/mi/rings/dsp/part.h) win over the eurorack canonical when
# unit code does `#include "elements/dsp/dsp.h"` etc. Then $(MOD_DIR)/elements/
# dsp for Commotio's bare `#include "exciter.h"` style. Then mods + SDK +
# eurorack canonical fallback.
INCLUDES = $(MOD_DIR) $(MOD_DIR)/elements/dsp mods $(SDKPATH) \
           $(SDKPATH)/arch/$(ARCH) $(SDKPATH)/emu $(EURORACK)

SYMBOLS = TEST

CFLAGS.common = -Wall -ffunction-sections -fdata-sections
CFLAGS.speed = -O3 -ftree-vectorize -ffast-math
CFLAGS.size = -Os

CFLAGS.release = $(CFLAGS.speed) -Wno-unused
CFLAGS.testing = $(CFLAGS.speed) -DBUILDOPT_TESTING
CFLAGS.debug = -g -DBUILDOPT_TESTING

ifeq ($(ARCH),am335x)
CFLAGS.am335x = -mcpu=cortex-a8 -mfpu=neon -mfloat-abi=hard -mabi=aapcs -Dfar= -D__DYNAMIC_REENT__
LFLAGS = -nostdlib -nodefaultlibs -r
endif

ifeq ($(ARCH),linux)
HOST_ARCH := $(shell uname -m)
ifeq ($(HOST_ARCH),aarch64)
CFLAGS.linux = -Wno-deprecated-declarations -fPIC -fno-tree-loop-vectorize
else
CFLAGS.linux = -Wno-deprecated-declarations -msse4 -fPIC -fno-tree-loop-vectorize
endif
LFLAGS = -shared
endif

ifeq ($(ARCH),darwin)
ifeq ($(shell uname -m),arm64)
CFLAGS.darwin = -Wno-deprecated-declarations -march=armv8.2-a -fPIC
else
CFLAGS.darwin = -Wno-deprecated-declarations -march=native -fPIC
endif
LFLAGS = -shared -Wl,-undefined,dynamic_lookup
endif

CFLAGS += $(CFLAGS.common) $(CFLAGS.$(ARCH)) $(CFLAGS.$(PROFILE))
CFLAGS += $(addprefix -I,$(INCLUDES))
CFLAGS += $(addprefix -D,$(SYMBOLS))
CFLAGS += -Wno-unused-variable -Wno-unused-parameter -Wno-sign-compare

SWIGFLAGS = -lua -no-old-metatable-bindings -nomoduleglobal -small -fvirtual
SWIGFLAGS += $(addprefix -I,$(INCLUDES))

CFLAGS.swig = $(CFLAGS.common) $(CFLAGS.$(ARCH)) $(CFLAGS.size)
CFLAGS.swig += $(addprefix -I,$(INCLUDES)) -I$(SDKPATH)/libs/lua54
CFLAGS.swig += $(addprefix -D,$(SYMBOLS))
CFLAGS.swig += -include cstdint

all: $(PACKAGE_FILE)

$(LIB_FILE): $(OBJECTS)
	@echo [LINK $@]
	@mkdir -p $(@D)
	@$(CC) $(CFLAGS) -o $@ $(OBJECTS) $(LFLAGS)

$(PACKAGE_FILE): $(LIB_FILE) $(ASSETS)
	@echo [ZIP $@]
	@rm -f $@
	@cd $(ASSET_DIR) && zip -rq $(abspath $@) *
	@zip -jq $@ $(LIB_FILE)

$(OUT_DIR)/%.o: %.cpp
	@echo [C++ $<]
	@mkdir -p $(@D)
	@$(CPP) $(CFLAGS) -std=gnu++11 -c $< -o $@

$(OUT_DIR)/%.o: %.cc
	@echo [C++ $<]
	@mkdir -p $(@D)
	@$(CPP) $(CFLAGS) -std=gnu++11 -c $< -o $@

$(OUT_DIR)/%.o: %.c
	@echo [CC $<]
	@mkdir -p $(@D)
	@$(CC) $(CFLAGS) -std=gnu11 -c $< -o $@

$(SWIG_WRAPPER): $(SWIG_SOURCE)
	@echo [SWIG $<]
	@mkdir -p $(@D)
	@$(SWIG) -c++ $(SWIGFLAGS) -o $@ $<

$(SWIG_OBJECT): $(SWIG_WRAPPER)
	@echo [C++ SWIG $<]
	@mkdir -p $(@D)
	@$(CPP) $(CFLAGS.swig) -std=gnu++11 -I$(MOD_DIR) -c $< -o $@

clean:
	rm -rf $(OUT_DIR)

install: $(PACKAGE_FILE)
	cp $(PACKAGE_FILE) $(HOME)/.od/rear/

.PHONY: all clean install
