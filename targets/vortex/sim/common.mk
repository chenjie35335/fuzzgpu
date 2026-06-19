ROOT_DIR := $(realpath ../..)
include $(ROOT_DIR)/config.mk

HW_DIR := $(VORTEX_HOME)/hw
RTL_DIR := $(HW_DIR)/rtl
DPI_DIR := $(HW_DIR)/dpi
SCRIPT_DIR := $(HW_DIR)/scripts

# Debugging
ifdef DEBUG
	CMAKE_OPTS += -DCMAKE_BUILD_TYPE=Debug
else
	CMAKE_OPTS += -DCMAKE_BUILD_TYPE=Release
endif

#
# Difftest GPU library
#
.PHONY: force

ifeq ($(findstring -DDISABLE_DIFFTEST, $(CONFIGS)),)
DIFFTEST_ON:=ON
else
DIFFTEST_ON:=OFF
endif

DIFFTEST_BUILD_HOME := $(DIFFTEST_HOME)/build${XLEN}-vortex-dpi-$(DIFFTEST_ON)
DIFFTEST_LIB := $(DIFFTEST_BUILD_HOME)/libdifftest_gpu${XLEN}.a

$(DIFFTEST_LIB): ${DIFFTEST_BUILD_HOME} force
	cd ${DIFFTEST_BUILD_HOME} && cmake .. -DDIFFTEST_XLEN=$(XLEN) -DARCH=vortex $(CMAKE_OPTS) && make -j$(THREADS)

${DIFFTEST_BUILD_HOME}:
	mkdir $@
