PKGNAME ?= spreadsheet
PKGVERSION ?= 2.8.3.36

include scripts/env.mk

EURORACK = eurorack

LIBNAME = lib$(PKGNAME)
OUT_DIR = $(PROFILE)/$(ARCH)
LIB_FILE = $(OUT_DIR)/$(LIBNAME).so
PACKAGE_FILE = $(OUT_DIR)/$(PKGNAME)-$(PKGVERSION).pkg

MOD_DIR = mods/$(PKGNAME)
ASSET_DIR = $(MOD_DIR)/assets

MOD_CPP = $(wildcard $(MOD_DIR)/*.cpp)
MOD_C = $(wildcard $(MOD_DIR)/*.c)

# stmlib for Svf filter (used by Petrichor/MultitapDelay)
STMLIB_CC = $(EURORACK)/stmlib/dsp/units.cc

OBJECTS = $(addprefix $(OUT_DIR)/,$(MOD_CPP:%.cpp=%.o))
OBJECTS += $(addprefix $(OUT_DIR)/,$(MOD_C:%.c=%.o))
OBJECTS += $(addprefix $(OUT_DIR)/,$(STMLIB_CC:%.cc=%.o))

SWIG_SOURCE = $(MOD_DIR)/$(PKGNAME).cpp.swig
SWIG_WRAPPER = $(OUT_DIR)/$(MOD_DIR)/$(PKGNAME)_swig.cpp
SWIG_OBJECT = $(SWIG_WRAPPER:%.cpp=%.o)
OBJECTS += $(SWIG_OBJECT)

# All headers (including subdirectory headers like visadhara/*.h,
# jf/*.h) that the SWIG wrapper TU may pull in via #include. The
# wrapper's inline-function bodies live in the wrapper .o, so any
# header change must trigger wrapper .o recompilation — otherwise
# the wrapper carries stale function bodies / class layouts and we
# get the "small change crashed hardware" trap from
# feedback_swig_header_dep. Recursive glob so subdirectory headers
# (visadhara/voice.h, etc.) are tracked too.
SWIG_HEADER_DEPS := $(call rwildcard, $(MOD_DIR), *.h)

ASSETS := $(call rwildcard, $(ASSET_DIR), *)

# mods/house/atoms: header-only Airwindows-derived atoms (AllpassMono.h, Spiral.h)
# used by APFTank.h (Fabula). No link deps; they compile into the swig .o.
INCLUDES = $(MOD_DIR) mods mods/house/atoms $(SDKPATH) $(SDKPATH)/arch/$(ARCH) $(SDKPATH)/emu $(EURORACK)

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
# RPi 4 dev rig: NEON is in the base ISA so no special flag; intrinsics
# from arm_neon.h work directly. -fno-tree-loop-vectorize matches x86
# default to keep behavior comparable across host arches.
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
# Append -fno-tree-vectorize LAST so it wins against the -ftree-vectorize that
# CFLAGS.speed added earlier in the line. On am335x this is the TOP-PRIORITY
# NEON-safety rule (feedback_disable_tree_vectorize_am335x). On linux it also
# stops -msse4 + -ffast-math from auto-vectorizing expf/sinf/cosf/powf loops
# (e.g. DrumVoice) into libmvec calls (_ZGVbN4v_*) that are undefined at load
# and make the whole .so fail to dlopen in the emu - and it keeps emu codegen
# in parity with hardware. Units use explicit intrinsics, so no perf is lost.
ifneq ($(filter $(ARCH),am335x linux),)
CFLAGS += -fno-tree-vectorize
endif

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
	$(call neon_hint_check,$@)

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

# SWIG re-runs when the .swig file OR any %include'd header changes.
# Without the header deps, editing e.g. VisadharaCoronaGraphic.h
# (header-only graphic, inline draw()) wouldn't regen the wrapper —
# stale wrapper.o ships with old function bodies, hard-crashes on
# hardware. The header dep list catches this automatically.
$(SWIG_WRAPPER): $(SWIG_SOURCE) $(SWIG_HEADER_DEPS)
	@echo [SWIG $<]
	@mkdir -p $(@D)
	@$(SWIG) -c++ $(SWIGFLAGS) -o $@ $<

# Wrapper .o also depends on headers — its inline-function bodies
# are pulled in via #include and need a recompile if any change.
$(SWIG_OBJECT): $(SWIG_WRAPPER) $(SWIG_HEADER_DEPS)
	@echo [C++ SWIG $<]
	@mkdir -p $(@D)
	@$(CPP) $(CFLAGS.swig) -std=gnu++11 -I$(MOD_DIR) -c $< -o $@

clean:
	rm -rf $(OUT_DIR)

install: $(PACKAGE_FILE)
	cp $(PACKAGE_FILE) $(HOME)/.od/rear/

.PHONY: all clean install
