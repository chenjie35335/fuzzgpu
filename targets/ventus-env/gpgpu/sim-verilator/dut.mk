# based on gvm.mk

XLEN = 32

# Check for sanity to avoid later confusion
ifneq ($(words $(CURDIR)),1)
 $(error Unsupported: GNU Make cannot build in directories containing spaces, build elsewhere: '$(CURDIR)')
endif
export MAKEFLAGS += +r

RELEASE ?= 0
PREFIX ?= $(CURDIR)/install
DUMP_TRACE ?= 1

# read by chisel
export RTL_DIFFTEST_ENABLED ?= true

#=====================================================================
# Helpers
#=====================================================================

# Choose the smaller one between two numbers
define MIN_FUNC
$(strip $(shell if [ $(1) -lt $(2) ]; then echo $(1); else echo $(2); fi))
endef

#=====================================================================
# Toolchain check
#=====================================================================

# If $VERILATOR_ROOT isn't in the environment, we assume it is part of a
# package install, and verilator is in your path. Otherwise find the
# binary relative to $VERILATOR_ROOT (such as when inside the git sources).
ifeq ($(VERILATOR_ROOT),)
VLIB_VERILATOR = verilator
VLIB_VERILATOR_COVERAGE = verilator_coverage
else
export VERILATOR_ROOT
VLIB_VERILATOR = $(VERILATOR_ROOT)/bin/verilator
VLIB_VERILATOR_COVERAGE = $(VERILATOR_ROOT)/bin/verilator_coverage
endif

VLIB_TOP_MODULE ?= GPGPU_SimTop

CCACHE = $(shell which ccache)
ifeq ($(CCACHE),)
CC  = gcc
CXX = g++
else
CC  = ccache gcc
CXX = ccache g++
endif
MOLD = $(shell which mold)

#=====================================================================
# Difftest framework check
#=====================================================================

ifndef DIFFTEST_HOME
$(error "DIFFTEST_HOME is not set. Please set the environment variable \"DIFFTEST_HOME\" to the root directory of the difftest repository.")
endif

ifeq ($(RELEASE),1)
  BUILD_TYPE = Release
else
  BUILD_TYPE = Debug
endif

ifeq ($(RTL_DIFFTEST_ENABLED),true)
  ENABLE_DPI = ON
else
  ENABLE_DPI = OFF
endif

DIFFTEST_BUILD_DIR := ${DIFFTEST_HOME}/build${XLEN}-ventus-dpi-$(ENABLE_DPI)
DIFFTEST_LIB := $(DIFFTEST_BUILD_DIR)/libdifftest_gpu${XLEN}.a

$(DIFFTEST_LIB): $(DIFFTEST_BUILD_DIR) force
	cd $(DIFFTEST_BUILD_DIR) && cmake .. -DDIFFTEST_XLEN=$(XLEN) -DARCH=ventus -DCMAKE_BUILD_TYPE=${BUILD_TYPE} -DENABLE_DPI=$(ENABLE_DPI) $(CMAKE_OPTS) && make -j$(THREADS)

$(DIFFTEST_BUILD_DIR):
	-mkdir $@

#=====================================================================
# Source file list and build directories
#=====================================================================

# Determine coverage type: none, line, toggle, or expr
ifeq ($(COVERAGE_TOGGLE),1)
  COV_TYPE = toggle
else ifeq ($(COVERAGE_LINE),1)
  COV_TYPE = line
else ifeq ($(COVERAGE_EXPR),1)
  COV_TYPE = expr
else
  COV_TYPE = none
endif

# Check if any coverage is enabled (for coverage_off analysis)
COVERAGE_ENABLED = 0
ifeq ($(COV_TYPE),toggle)
  COVERAGE_ENABLED = 1
else ifeq ($(COV_TYPE),line)
  COVERAGE_ENABLED = 1
else ifeq ($(COV_TYPE),expr)
  COVERAGE_ENABLED = 1
endif

# Determine difftest enabled: 0 or 1
ifeq ($(RTL_DIFFTEST_ENABLED),1)
  DFT_ENABLED = 1
else ifeq ($(RTL_DIFFTEST_ENABLED),true)
  DFT_ENABLED = 1
else
  DFT_ENABLED = 0
endif

# Build directory name based on configuration
VLIB_DIR_BUILD = build-cov$(COV_TYPE)-dft$(DFT_ENABLED)/libDUT
VLIB_DIR_SCALA = ../ventus/src
VLIB_DIR_BUILDOBJ_DEBUG = $(VLIB_DIR_BUILD)/debug
VLIB_DIR_BUILDOBJ_RELEASE = $(VLIB_DIR_BUILD)/release
ifeq ($(RELEASE),1)
VLIB_DIR_BUILDOBJ = $(VLIB_DIR_BUILDOBJ_RELEASE)
else
VLIB_DIR_BUILDOBJ = $(VLIB_DIR_BUILDOBJ_DEBUG)
endif

VLIB_SRC_SCALA = $(shell find $(VLIB_DIR_SCALA) -name "*.scala")
VLIB_SRC_V_DIR = verilog-out-dft$(DFT_ENABLED)
VLIB_SRC_V = $(VLIB_SRC_V_DIR)/dut.sv
VLIB_SRC_CXX_EXPORT = dut.cpp# API in these files will be exported to shared library
VLIB_SRC_CXX = ventus_rtlsim.cpp kernel.cpp physical_mem.cpp cta_sche_wrapper.cpp ventus_rtlsim_impl.cpp rtl_parameters.cpp $(VLIB_SRC_CXX_EXPORT)
VLIB_SRC_CXX_ABSPATH = $(abspath $(VLIB_SRC_CXX))
VLIB_VERILATOR_INPUT = $(wildcard $(VLIB_SRC_V_DIR)/*.sv) $(VLIB_SRC_CXX_ABSPATH)
VLIB_VERILATOR_OUTPUT = $(VLIB_DIR_BUILDOBJ)/libVdut.a
#VLIB_VERILATOR_OUTPUT = $(VLIB_DIR_BUILDOBJ)/libVdut.a $(VLIB_DIR_BUILDOBJ)/libverilated.a

# Coverage_off configuration for excluding SYNTHESIS blocks (via Verilator Control File)
ifeq ($(COVERAGE_ENABLED),1)
	VLIB_COVERAGE_OFF_VLT = $(VLIB_SRC_V_DIR)/coverage_off.vlt
	VLIB_COVERAGE_OFF_SCRIPT = extract_coverage_off.py
else
	VLIB_COVERAGE_OFF_VLT =
endif

VLIB_TARGET_NAME = dut
VLIB_TARGET_PATH = $(VLIB_DIR_BUILDOBJ)
VLIB_TARGET = $(VLIB_TARGET_PATH)/lib$(VLIB_TARGET_NAME).so

# API in these objects will be exported to shared library
VLIB_OBJ_EXPORT = $(VLIB_SRC_CXX_EXPORT:%.cpp=$(VLIB_DIR_BUILDOBJ)/%.o)

#=====================================================================
# Verilator and toolchain flags
#=====================================================================

# Verilated model parallelism config
VLIB_NPROC_CPU = $(shell nproc)
VLIB_NPROC_DUT = 8 # Depends on RTL circuit size, just try and find a verilator-allowed largest number
VLIB_NPROC_SIM = $(call MIN_FUNC, $(VLIB_NPROC_CPU), $(VLIB_NPROC_DUT))
VLIB_NPROC_TRACE_FST = $(call MIN_FUNC, $(VLIB_NPROC_SIM), 2)

# Generate C++ in executable form
VLIB_VERILATOR_FLAGS += -cc --build
VLIB_VERILATOR_FLAGS += -MMD
VLIB_VERILATOR_FLAGS += --error-limit 100
# How to deal with verilog value 'x' and 'z'
ifeq ($(RELEASE),1)
VLIB_VERILATOR_FLAGS += --x-assign fast --x-initial unique
else
VLIB_VERILATOR_FLAGS += --x-assign unique --x-initial unique
endif
# Warn about lint issues; may not want this on less solid designs
#VLIB_VERILATOR_FLAGS += -Wall
VLIB_VERILATOR_FLAGS += -Wno-WIDTHEXPAND
VLIB_VERILATOR_FLAGS += -Wno-WIDTHTRUNC
# Define macros for Verilog
# random init
VLIB_VERILATOR_FLAGS += -DPRINTF_COND=1
# VLIB_VERILATOR_FLAGS += -DRANDOMIZE
# VLIB_VERILATOR_FLAGS += -DRANDOMIZE_MEM_INIT
# VLIB_VERILATOR_FLAGS += -DRANDOMIZE_REG_INIT
# Make waveforms
ifneq ($(filter 1 yes true on,$(DUMP_TRACE)),)
	VLIB_VERILATOR_FLAGS += --trace-fst
	VLIB_VERILATOR_FLAGS += --trace-threads $(VLIB_NPROC_TRACE_FST)
endif
# Check SystemVerilog assertions
VLIB_VERILATOR_FLAGS += --assert
# Generate coverage analysis
ifeq ($(COVERAGE_LINE),1)
VLIB_VERILATOR_FLAGS += --coverage-line
VLIB_CFLAGS += -DENABLE_COVERAGE
endif
ifeq ($(COVERAGE_TOGGLE),1)
VLIB_VERILATOR_FLAGS += --coverage-toggle
VLIB_CFLAGS += -DENABLE_COVERAGE
endif
ifeq ($(COVERAGE_EXPR),1)
VLIB_VERILATOR_FLAGS += --coverage-expr
VLIB_CFLAGS += -DENABLE_COVERAGE
endif
# Run Verilator in debug mode
#VLIB_VERILATOR_FLAGS += --debug
# Add this trace to get a backtrace in gdb
#VLIB_VERILATOR_FLAGS += --gdbbt
# Disable DPI threads to avoid issues with some toolchains

VLIB_VERILATOR_FLAGS += --top-module $(VLIB_TOP_MODULE)

ifeq ($(RELEASE),1)
VLIB_CFLAGS += -O2 -fvisibility=hidden
else
VLIB_CFLAGS += -g -O0
endif
VLIB_CFLAGS += -fPIC -mcmodel=medium -gsplit-dwarf -gdwarf-5
VLIB_CXXFLAGS += $(VLIB_CFLAGS)
VLIB_CXXFLAGS += -std=c++20
VLIB_CXXFLAGS += -DSPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE
VLIB_CXXFLAGS += -I${DIFFTEST_HOME}/csrc/include -DDIFFTEST -DDIFFTEST_XLEN_${XLEN} -DXLEN_${XLEN} -DARCH_VENTUS
#VLIB_CXXFLAGS += -fsanitize=address,undefined
#VLIB_LDFLAGS += -fsanitize=address,undefined
VLIB_LDFLAGS += -lc
ifeq ($(MOLD),1)
VLIB_LDFLAGS += -fuse-ld=mold
endif

VLIB_VERILATOR_FLAGS += --threads $(VLIB_NPROC_SIM)
VLIB_VERILATOR_FLAGS += --threads-dpi none 
VLIB_VERILATOR_FLAGS += --trace-threads $(VLIB_NPROC_TRACE_FST)
VLIB_VERILATOR_FLAGS += -j $(VLIB_NPROC_CPU)
VLIB_VERILATOR_FLAGS += -CFLAGS "$(VLIB_CXXFLAGS)"
VLIB_VERILATOR_FLAGS += -LDFLAGS "$(VLIB_LDFLAGS)"
VLIB_VERILATOR_FLAGS += --prefix Vdut -Mdir $(VLIB_DIR_BUILDOBJ)

# Add coverage_off directives if coverage is enabled
ifeq ($(COVERAGE_ENABLED),1)
	VLIB_VERILATOR_INPUT += $(VLIB_COVERAGE_OFF_VLT)
endif

#=====================================================================
# Build rules and targets
#=====================================================================

# Generate coverage_off directives by analyzing SYNTHESIS blocks
# Depend on generated dut.sv to guarantee the directory exists before globbing
$(VLIB_COVERAGE_OFF_VLT): $(VLIB_SRC_V) $(wildcard $(VLIB_SRC_V_DIR)/*.sv) $(wildcard $(VLIB_SRC_V_DIR)/*.v)
	@mkdir -p $(VLIB_DIR_BUILD)
	@if [ -d "$(VLIB_SRC_V_DIR)" ]; then \
		python3 $(VLIB_COVERAGE_OFF_SCRIPT) $(VLIB_SRC_V_DIR) $@; \
	else \
		touch $@; \
	fi

default: lib

$(VLIB_SRC_V) parameters.json &: $(VLIB_SRC_SCALA)
	mkdir -p $(VLIB_SRC_V_DIR)
	cd .. && ./mill ventus[6.4.0].runMain top.paramToJson
	if command -v firtool >/dev/null 2>&1; then \
		(cd .. && ./mill ventus[6.4.0].runMain circt.stage.ChiselMain --module top.GPGPU_SimTop --target chirrtl --target-dir sim-verilator/$(VLIB_SRC_V_DIR)/); \
		(cd $(VLIB_SRC_V_DIR) && firtool --split-verilog GPGPU_SimTop.fir -o .); \
		mv $(VLIB_SRC_V_DIR)/GPGPU_SimTop.sv $(VLIB_SRC_V); \
	else \
		(cd .. && ./mill ventus[6.4.0].runMain top.emitVerilog); \
		mv ../GPGPU_SimTop.v $(VLIB_SRC_V); \
	fi
	find $(VLIB_SRC_V_DIR) -name "*.sv" -type f -exec sed -i '1i\`define PRINTF_COND 1' {} \;

rtl_parameters.cpp: parameters.json json2cpp.py
	python3 json2cpp.py

verilog: $(VLIB_SRC_V)

# Prepare coverage analysis if enabled
ifeq ($(COVERAGE_ENABLED),1)
prepare-coverage: $(VLIB_COVERAGE_OFF_VLT)
else
prepare-coverage:
endif

verilate: $(VLIB_SRC_V) $(VLIB_SRC_CXX) prepare-coverage
	@mkdir -p $(VLIB_DIR_BUILDOBJ)
	+$(VLIB_VERILATOR) $(VLIB_VERILATOR_FLAGS) $(VLIB_VERILATOR_INPUT)

$(VLIB_VERILATOR_OUTPUT): $(VLIB_SRC_V) $(VLIB_SRC_CXX) prepare-coverage
	@mkdir -p $(VLIB_DIR_BUILDOBJ)
	+$(VLIB_VERILATOR) $(VLIB_VERILATOR_FLAGS) $(VLIB_VERILATOR_INPUT)

$(VLIB_TARGET): $(VLIB_VERILATOR_OUTPUT) $(DIFFTEST_LIB)
	$(CXX) $(VLIB_CXXFLAGS) $(VLIB_LDFLAGS) -shared -o $@ \
	  -Wl,--whole-archive $(DIFFTEST_LIB) $(VLIB_OBJ_EXPORT) -Wl,--no-whole-archive \
	  $(VLIB_DIR_BUILDOBJ)/libVdut.a \
	  $(VLIB_DIR_BUILDOBJ)/libverilated.a \
	  -lspdlog -lfmt -pthread -lpthread -lz -latomic \
	  -Wl,--enable-new-dtags -Wl,-rpath,'$$ORIGIN'
	ln -sf $(abspath $(VLIB_TARGET)) $(VLIB_DIR_BUILD)/lib$(VLIB_TARGET_NAME).so

lib: $(VLIB_TARGET)

.PHONY: force verilog verilate lib prepare-coverage

#=====================================================================
# Other targets
#=====================================================================

info-verilator:
	$(VLIB_VERILATOR) -V

install: $(VLIB_TARGET)
	install -d $(PREFIX)/lib
	install -m 644 $(VLIB_TARGET) $(PREFIX)/lib/
	install -d $(PREFIX)/include
	install -m 644 ventus_rtlsim.h $(PREFIX)/include/

clean-lib:
	-rm -f $(VLIB_DIR_BUILDOBJ_DEBUG)/*.a $(VLIB_DIR_BUILDOBJ_DEBUG)/*.o $(VLIB_DIR_BUILDOBJ_DEBUG)/*.so
	-rm -f $(VLIB_DIR_BUILDOBJ_RELEASE)/*.a $(VLIB_DIR_BUILDOBJ_RELEASE)/*.o $(VLIB_DIR_BUILDOBJ_RELEASE)/*.so
	-rm -f $(VLIB_DIR_BUILD)/*.so

clean-lib-dep: clean-lib
	-rm -f $(VLIB_DIR_BUILDOBJ_DEBUG)/*.d
	-rm -f $(VLIB_DIR_BUILDOBJ_RELEASE)/*.d

clean-verilated: 
	-rm -rf $(VLIB_DIR_BUILD)
	-rm -f $(VLIB_COVERAGE_OFF_VLT)

clean-verilog: clean-verilated
	-rm -r $(VLIB_SRC_V_DIR)

clean: clean-verilog clean-verilated

.PHONY: clean-lib clean-lib-dep clean-verilated clean-verilog info-verilator install prepare-coverage
