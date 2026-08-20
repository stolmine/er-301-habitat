PKGNAME ?= biome
PKGVERSION ?= 2.2.5.1

include scripts/env.mk

# Header dependency tracking: without it, editing a .h does not rebuild the .o
# that includes it, and a stale object with a mismatched sizeof corrupts the
# heap (feedback_swig_header_dep).
DEPFLAGS = -MMD -MP

LIBNAME = lib$(PKGNAME)
OUT_DIR = $(PROFILE)/$(ARCH)
LIB_FILE = $(OUT_DIR)/$(LIBNAME).so
PACKAGE_FILE = $(OUT_DIR)/$(PKGNAME)-$(PKGVERSION).pkg

MOD_DIR = mods/$(PKGNAME)
ASSET_DIR = $(MOD_DIR)/assets

MOD_CPP = $(wildcard $(MOD_DIR)/*.cpp)

# pffft, for the Spectral Freeze STFT.
MOD_C = $(wildcard $(MOD_DIR)/*.c)

# Vendored Open303 engine (Bassline). Kept in its own subdirectory for namespace
# isolation, so the flat glob above does not reach it. These MUST be listed as
# real .cpp objects rather than pulled in through a header: anything that only
# lives in headers gets compiled inside the SWIG wrapper TU at CFLAGS.swig =
# CFLAGS.size = -Os with no -ffast-math, which is how anamnesis silently
# shipped its whole DSP at -Os. See planning/open303-port.md.
MOD_CPP += $(wildcard $(MOD_DIR)/open303/*.cpp)

# stmlib for Svf filter (used by Canals)
STMLIB_CC = $(EURORACK)/stmlib/dsp/units.cc

OBJECTS = $(addprefix $(OUT_DIR)/,$(MOD_CPP:%.cpp=%.o))
OBJECTS += $(addprefix $(OUT_DIR)/,$(MOD_C:%.c=%.o))
OBJECTS += $(addprefix $(OUT_DIR)/,$(STMLIB_CC:%.cc=%.o))

SWIG_SOURCE = $(MOD_DIR)/$(PKGNAME).cpp.swig
SWIG_WRAPPER = $(OUT_DIR)/$(MOD_DIR)/$(PKGNAME)_swig.cpp
SWIG_OBJECT = $(SWIG_WRAPPER:%.cpp=%.o)
OBJECTS += $(SWIG_OBJECT)

# Track all package headers as SWIG dependencies. Without this,
# editing a %include'd header doesn't retrigger SWIG; stale wrapper's
# sizeof corrupts the heap, crashing later on delete/quicksave. Per
# feedback_swig_header_dep. Recursive glob so subdirectory headers are
# tracked too. NOTE: only catches changes in mods/<pkg>/*.h — eurorack/
# or shared-header edits still require `make <pkg>-clean` to force a
# rebuild of source .o files that transitively include them.
SWIG_HEADER_DEPS := $(call rwildcard, $(MOD_DIR), *.h)

ASSETS := $(call rwildcard, $(ASSET_DIR), *)

EURORACK = eurorack

INCLUDES = $(MOD_DIR) mods $(SDKPATH) $(SDKPATH)/arch/$(ARCH) $(SDKPATH)/emu $(EURORACK)

SYMBOLS = TEST
# (Cloudling/CloudSeed wind-down 2026-05-18: BUFFER_SIZE=128 was added
# during Phase B to size the cloudseed DSP stack-arrays for FRAMELENGTH.
# Phase B was shelved at biome 2.2.0.17 -- ReverbController::Process
# hangs Cortex-A8 on first call regardless of compiler-opt level or
# input data. See planning/cloudseed-port-plan.md "2026-05-17/18 wind
# down" for the full bisect record. CloudSeed standalone .cpp files
# don't reference BUFFER_SIZE so leaving it undefined here is safe;
# future Phase B revisit will need to re-add it.)

CFLAGS.common = -Wall -ffunction-sections -fdata-sections
CFLAGS.speed = -O3 -ftree-vectorize -ffast-math
CFLAGS.size = -Os

CFLAGS.release = $(CFLAGS.speed) -Wno-unused
CFLAGS.testing = $(CFLAGS.speed) -DBUILDOPT_TESTING
CFLAGS.debug = -g -DBUILDOPT_TESTING

ifeq ($(ARCH),am335x)
CFLAGS.am335x = -mcpu=cortex-a8 -mfpu=neon -mfloat-abi=hard -mabi=aapcs -Dfar= -D__DYNAMIC_REENT__
# Bassline / Open303 oversampling tier: 2x on hardware, 4x elsewhere. Set here
# EXPLICITLY rather than sniffed in o3_config.h - there is no -Dam335x in this
# build, so a header-side `defined(am335x)` test silently never fires and the
# hardware build quietly takes the heavier 4x path.
CFLAGS.am335x += -DO3_OVERSAMPLING=2
LFLAGS = -nostdlib -nodefaultlibs -r
endif

ifeq ($(ARCH),linux)
CFLAGS.linux = -Wno-deprecated-declarations -msse4 -fPIC -fno-tree-loop-vectorize
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
# NEON-safety rule (feedback_disable_tree_vectorize_am335x).
#
# LINUX NEEDS IT TOO: GCC vectorizes a loop calling double sin() into libmvec
# (_ZGVbN2v_sin), which a package .so does not link against, so the module
# fails to load with "undefined symbol" the moment a unit is inserted. Hit
# exactly that building the Spectral Freeze sine LUT, 2026-08-13. It also keeps
# emu codegen in parity with hardware. Matches spreadsheet.
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
	@$(CPP) $(CFLAGS) $(DEPFLAGS) -std=gnu++11 -c $< -o $@

$(OUT_DIR)/%.o: %.c
	@echo [CC $<]
	@mkdir -p $(@D)
	@$(CC) $(CFLAGS) $(DEPFLAGS) -std=gnu11 -c $< -o $@

$(OUT_DIR)/%.o: %.cc
	@echo [C++ $<]
	@mkdir -p $(@D)
	@$(CPP) $(CFLAGS) -std=gnu++11 -c $< -o $@

$(SWIG_WRAPPER): $(SWIG_SOURCE) $(SWIG_HEADER_DEPS)
	@echo [SWIG $<]
	@mkdir -p $(@D)
	@$(SWIG) -c++ $(SWIGFLAGS) -o $@ $<

$(SWIG_OBJECT): $(SWIG_WRAPPER) $(SWIG_HEADER_DEPS)
	@echo [C++ SWIG $<]
	@mkdir -p $(@D)
	@$(CPP) $(CFLAGS.swig) -std=gnu++11 -I$(MOD_DIR) -c $< -o $@

-include $(OBJECTS:.o=.d)

clean:
	rm -rf $(OUT_DIR)

install: $(PACKAGE_FILE)
	cp $(PACKAGE_FILE) $(HOME)/.od/rear/

.PHONY: all clean install
