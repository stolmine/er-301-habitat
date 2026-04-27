PKGNAME ?= spreadsheet
PKGVERSION ?= 2.5.5.150

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

ASSETS := $(call rwildcard, $(ASSET_DIR), *)

INCLUDES = $(MOD_DIR) mods $(SDKPATH) $(SDKPATH)/arch/$(ARCH) $(SDKPATH)/emu $(EURORACK)

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
