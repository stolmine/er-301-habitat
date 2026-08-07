PKGNAME ?= anamnesis
PKGVERSION ?= 0.2.0.86

include scripts/env.mk

LIBNAME = lib$(PKGNAME)
OUT_DIR = $(PROFILE)/$(ARCH)
LIB_FILE = $(OUT_DIR)/$(LIBNAME).so
PACKAGE_FILE = $(OUT_DIR)/$(PKGNAME)-$(PKGVERSION).pkg

MOD_DIR = mods/$(PKGNAME)
ASSET_DIR = $(MOD_DIR)/assets

MOD_CPP = $(wildcard $(MOD_DIR)/*.cpp)
MOD_C = $(wildcard $(MOD_DIR)/*.c)

OBJECTS = $(addprefix $(OUT_DIR)/,$(MOD_CPP:%.cpp=%.o))
OBJECTS += $(addprefix $(OUT_DIR)/,$(MOD_C:%.c=%.o))

SWIG_SOURCE = $(MOD_DIR)/$(PKGNAME).cpp.swig
SWIG_WRAPPER = $(OUT_DIR)/$(MOD_DIR)/$(PKGNAME)_swig.cpp
SWIG_OBJECT = $(SWIG_WRAPPER:%.cpp=%.o)
OBJECTS += $(SWIG_OBJECT)

# Track all package headers as SWIG dependencies (per feedback_swig_header_dep).
# Recursive glob so atoms/*.h is tracked. A header edit that changes a class
# layout / sizeof while a stale wrapper .o survives is the documented insert-
# crash trap, so we go BEYOND mtime deps: SWIG_STAMP force-DELETES the generated
# wrapper + its .o whenever ANY header changes, guaranteeing a from-scratch SWIG
# re-parse (bulletproof vs interrupted builds, clock skew, byte-identical regen).
# NOTE: eurorack/ or shared-header edits still require a `-clean` to rebuild
# source .o files that transitively include them.
SWIG_HEADER_DEPS := $(call rwildcard, $(MOD_DIR), *.h)
SWIG_STAMP := $(OUT_DIR)/$(MOD_DIR)/.swig_headers.stamp

ASSETS := $(call rwildcard, $(ASSET_DIR), *)

# eurorack + eurorack/stmlib on the include path: Anamnesis lifts the
# Clouds looper/granular/WSOLA DSP and stmlib delay/interpolation.
INCLUDES = $(MOD_DIR) mods eurorack eurorack/stmlib $(SDKPATH) $(SDKPATH)/arch/$(ARCH) $(SDKPATH)/emu

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
# -msse4 is x86-only; gate it so the package also builds natively on
# aarch64 hosts (CM4 / uConsole dev rig). NEON/ASIMD is baseline on
# armv8 so no substitute flag is needed there.
LINUX_HOST_ARCH := $(shell uname -m)
ifeq ($(LINUX_HOST_ARCH),x86_64)
  LINUX_SIMD_FLAGS = -msse4
else
  LINUX_SIMD_FLAGS =
endif
CFLAGS.linux = -Wno-deprecated-declarations $(LINUX_SIMD_FLAGS) -fPIC
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
CFLAGS += -Wno-unused-variable -Wno-unused-parameter
# Append -fno-tree-vectorize LAST so it wins against the -ftree-vectorize that
# CFLAGS.speed added earlier. On am335x this is the TOP-PRIORITY NEON-safety
# rule (feedback_disable_tree_vectorize_am335x). On linux it also stops -msse4
# + -ffast-math from auto-vectorizing expf/sinf loops (Anamnesis.cpp field
# splat) into libmvec calls (_ZGVbN4v_*) that are undefined at dlopen in the
# emu - same fix as spreadsheet/mod.mk.
ifneq ($(filter $(ARCH),am335x linux),)
CFLAGS += -fno-tree-vectorize
endif

SWIGFLAGS = -lua -no-old-metatable-bindings -nomoduleglobal -small -fvirtual
SWIGFLAGS += $(addprefix -I,$(INCLUDES))

CFLAGS.swig = $(CFLAGS.common) $(CFLAGS.$(ARCH)) $(CFLAGS.size)
CFLAGS.swig += $(addprefix -I,$(INCLUDES)) -I$(SDKPATH)/libs/lua54
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

$(OUT_DIR)/%.o: %.c
	@echo [CC $<]
	@mkdir -p $(@D)
	@$(CC) $(CFLAGS) -std=gnu11 -c $< -o $@

# Force-clean: any header change wipes the generated wrapper + its .o BEFORE the
# regen below runs, so a stale sizeof/layout can never survive an incremental build.
$(SWIG_STAMP): $(SWIG_HEADER_DEPS)
	@mkdir -p $(@D)
	@rm -f $(SWIG_WRAPPER) $(SWIG_OBJECT)
	@touch $@

$(SWIG_WRAPPER): $(SWIG_SOURCE) $(SWIG_HEADER_DEPS) $(SWIG_STAMP)
	@echo [SWIG $<]
	@mkdir -p $(@D)
	@$(SWIG) -c++ $(SWIGFLAGS) -o $@ $<

$(SWIG_OBJECT): $(SWIG_WRAPPER) $(SWIG_HEADER_DEPS)
	@echo [C++ SWIG $<]
	@mkdir -p $(@D)
	@$(CPP) $(CFLAGS.swig) -std=gnu++11 -I$(MOD_DIR) -c $< -o $@

clean:
	rm -rf $(OUT_DIR)

install: $(PACKAGE_FILE)
	cp $(PACKAGE_FILE) $(HOME)/.od/rear/

.PHONY: all clean install
