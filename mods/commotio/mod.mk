PKGNAME ?= commotio
PKGVERSION ?= 0.1.0

include scripts/env.mk

EURORACK = eurorack

LIBNAME = lib$(PKGNAME)
OUT_DIR = $(PROFILE)/$(ARCH)
LIB_FILE = $(OUT_DIR)/$(LIBNAME).so
PACKAGE_FILE = $(OUT_DIR)/$(PKGNAME)-$(PKGVERSION).pkg

MOD_DIR = mods/$(PKGNAME)
ASSET_DIR = $(MOD_DIR)/assets

# Mod wrapper sources
MOD_CPP = $(wildcard $(MOD_DIR)/*.cpp)

# Elements DSP sources (local 48kHz copies)
ELEMENTS_CC = $(MOD_DIR)/elements/dsp/exciter.cc \
              $(MOD_DIR)/elements/dsp/tube.cc \
              $(MOD_DIR)/elements/dsp/multistage_envelope.cc

# Elements resources (from eurorack submodule — sample data + lookup tables)
RESOURCES_CC = $(EURORACK)/elements/resources.cc

# stmlib sources
STMLIB_CC = $(EURORACK)/stmlib/dsp/units.cc \
            $(EURORACK)/stmlib/dsp/atan.cc \
            $(EURORACK)/stmlib/utils/random.cc

# Objects
OBJECTS = $(addprefix $(OUT_DIR)/,$(MOD_CPP:%.cpp=%.o))
OBJECTS += $(addprefix $(OUT_DIR)/,$(ELEMENTS_CC:%.cc=%.o))
OBJECTS += $(addprefix $(OUT_DIR)/,$(RESOURCES_CC:%.cc=%.o))
OBJECTS += $(addprefix $(OUT_DIR)/,$(STMLIB_CC:%.cc=%.o))

# SWIG
SWIG_SOURCE = $(MOD_DIR)/$(PKGNAME).cpp.swig
SWIG_WRAPPER = $(OUT_DIR)/$(MOD_DIR)/$(PKGNAME)_swig.cpp
SWIG_OBJECT = $(SWIG_WRAPPER:%.cpp=%.o)
OBJECTS += $(SWIG_OBJECT)

# Assets
ASSETS := $(call rwildcard, $(ASSET_DIR), *)

# Includes — local elements/dsp FIRST to shadow eurorack's dsp.h
INCLUDES = $(MOD_DIR)/elements/dsp $(MOD_DIR) mods $(SDKPATH) $(SDKPATH)/arch/$(ARCH) $(SDKPATH)/emu $(EURORACK)

# TEST required for stmlib on am335x
SYMBOLS = TEST

# Compiler flags
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
CFLAGS.linux = -Wno-deprecated-declarations -msse4 -fPIC
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

# .cpp (mod wrapper)
$(OUT_DIR)/%.o: %.cpp
	@echo [C++ $<]
	@mkdir -p $(@D)
	@$(CPP) $(CFLAGS) -std=gnu++11 -c $< -o $@

# .cc (Elements/stmlib sources)
$(OUT_DIR)/%.o: %.cc
	@echo [C++ $<]
	@mkdir -p $(@D)
	@$(CPP) $(CFLAGS) -std=gnu++11 -c $< -o $@

# SWIG
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
