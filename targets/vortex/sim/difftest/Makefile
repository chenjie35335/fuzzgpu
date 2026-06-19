include ../common.mk

ifndef DIFFTEST_HOME
$(error "DIFFTEST_HOME is not set. Please set the environment variable \"DIFFTEST_HOME\" to the root directory of the difftest repository.")
endif

DESTDIR ?= $(CURDIR)

CONFIG_FILE = $(DESTDIR)/difftest_config.stamp
SRC_DIR = $(VORTEX_HOME)/sim/difftest

# Test the RTL design instead of DPI FPU by default
ifeq (,$(findstring -DDPI_DPI, $(CONFIGS)))
ifeq (,$(findstring -DDPI_DSP, $(CONFIGS)))
CONFIGS += -DFPU_FPNEW
endif
endif

CXXFLAGS += -std=c++17 -Wall -Wextra -Wfatal-errors -Wno-array-bounds
CXXFLAGS += -fPIC -Wno-maybe-uninitialized
CXXFLAGS += -I$(ROOT_DIR)/hw -I$(SW_COMMON_DIR)
CXXFLAGS += -I$(THIRD_PARTY_DIR)/softfloat/source/include
CXXFLAGS += -I$(THIRD_PARTY_DIR)/ramulator/ext/spdlog/include
CXXFLAGS += -I$(THIRD_PARTY_DIR)/ramulator/ext/yaml-cpp/include
CXXFLAGS += -I$(THIRD_PARTY_DIR)/ramulator/src
CXXFLAGS += -I${DIFFTEST_HOME}/csrc/include
CXXFLAGS += -DXLEN_$(XLEN) -DARCH_VORTEX
CXXFLAGS += $(CONFIGS)

LDFLAGS += $(THIRD_PARTY_DIR)/softfloat/build/Linux-x86_64-GCC/softfloat.a
LDFLAGS += -Wl,-rpath,$(THIRD_PARTY_DIR)/ramulator  -L$(THIRD_PARTY_DIR)/ramulator -lramulator

# control RTL debug tracing states
DBG_TRACE_FLAGS += -DDBG_TRACE_PIPELINE
DBG_TRACE_FLAGS += -DDBG_TRACE_MEM
DBG_TRACE_FLAGS += -DDBG_TRACE_CACHE
DBG_TRACE_FLAGS += -DDBG_TRACE_AFU
DBG_TRACE_FLAGS += -DDBG_TRACE_SCOPE
DBG_TRACE_FLAGS += -DDBG_TRACE_GBAR
DBG_TRACE_FLAGS += -DDBG_TRACE_TCU

DBG_FLAGS += -DDEBUG_LEVEL=$(DEBUG) -DVCD_OUTPUT $(DBG_TRACE_FLAGS)

RTL_PKGS = $(RTL_DIR)/VX_gpu_pkg.sv $(RTL_DIR)/fpu/VX_fpu_pkg.sv

FPU_INCLUDE = -I$(RTL_DIR)/fpu
ifneq (,$(findstring -DFPU_FPNEW, $(CONFIGS)))
	RTL_PKGS += $(THIRD_PARTY_DIR)/cvfpu/src/fpnew_pkg.sv $(THIRD_PARTY_DIR)/cvfpu/src/common_cells/src/cf_math_pkg $(THIRD_PARTY_DIR)/cvfpu/src/fpu_div_sqrt_mvp/hdl/defs_div_sqrt_mvp.sv
	FPU_INCLUDE += -I$(THIRD_PARTY_DIR)/cvfpu/src/common_cells/include -I$(THIRD_PARTY_DIR)/cvfpu/src/common_cells/src -I$(THIRD_PARTY_DIR)/cvfpu/src/fpu_div_sqrt_mvp/hdl -I$(THIRD_PARTY_DIR)/cvfpu/src
endif

# Add difftest includes
DIFFTEST_INCLUDE = -I${DIFFTEST_HOME}/vsrc

RTL_INCLUDE = -I$(SRC_DIR) -I$(RTL_DIR) -I$(DPI_DIR) -I$(RTL_DIR)/libs -I$(RTL_DIR)/interfaces -I$(RTL_DIR)/core -I$(RTL_DIR)/mem -I$(RTL_DIR)/cache $(FPU_INCLUDE) $(DIFFTEST_INCLUDE)

# Add V extension sources
ifneq (,$(findstring -DEXT_V_ENABLE, $(CONFIGS)))
	RTL_PKGS += $(RTL_DIR)/vpu/VX_vpu_pkg.sv
	RTL_INCLUDE += -I$(RTL_DIR)/vpu
endif

# Add TCU extension sources
ifneq (,$(findstring -DEXT_TCU_ENABLE, $(CONFIGS)))
	RTL_PKGS += $(RTL_DIR)/tcu/VX_tcu_pkg.sv
	RTL_INCLUDE += -I$(RTL_DIR)/tcu
	RTL_INCLUDE += -I$(RTL_DIR)/tcu/bhf
	RTL_INCLUDE += -I$(THIRD_PARTY_DIR)/hardfloat/source/RISCV
	RTL_PKGS += $(THIRD_PARTY_DIR)/hardfloat/source/fNToRecFN.v
	RTL_PKGS += $(THIRD_PARTY_DIR)/hardfloat/source/addRecFN.v
	RTL_PKGS += $(THIRD_PARTY_DIR)/hardfloat/source/bsg_hardfloat_pkg.sv
	RTL_PKGS += $(THIRD_PARTY_DIR)/hardfloat/source/HardFloat_primitives.v
	RTL_PKGS += $(THIRD_PARTY_DIR)/hardfloat/source/HardFloat_rawFN.v
	RTL_PKGS += $(THIRD_PARTY_DIR)/hardfloat/source/isSigNaNRecFN.v
	RTL_PKGS += $(THIRD_PARTY_DIR)/hardfloat/source/mulRecFN.v
	RTL_PKGS += $(THIRD_PARTY_DIR)/hardfloat/source/recFNToFN.v
	RTL_PKGS += $(THIRD_PARTY_DIR)/hardfloat/source/recFNToRecFN.v
endif

RTL_PKGS += $(RTL_DIR)/VX_trace_pkg.sv

# Add difftest package
RTL_PKGS += ${DIFFTEST_HOME}/vsrc/Difftest_pkg.sv VX_difftest_pkg.sv

# Add difftest configs
CONFIGS += -DDIFFTEST_XLEN_$(XLEN)

# Coverage analysis
ifneq ($(findstring -DCOVERAGE_LINE, $(CONFIGS)),)
	VL_FLAGS += --coverage-line -Wno-SIDEEFFECT
	CXXFLAGS += -DENABLE_COVERAGE
endif
ifneq ($(findstring -DCOVERAGE_TOGGLE, $(CONFIGS)),)
	VL_FLAGS += --coverage-toggle
	CXXFLAGS += -DENABLE_COVERAGE
endif
ifneq ($(findstring -DCOVERAGE_EXPR, $(CONFIGS)),)
	VL_FLAGS += --coverage-expr
	CXXFLAGS += -DENABLE_COVERAGE
endif

# Disable coverage by default
ifeq ($(filter -DCOVERAGE_%, $(CONFIGS)),)
	CXXFLAGS += -DDISABLE_COVERAGE
endif

ifeq ($(findstring -DDISABLE_DIFFTEST, $(CONFIGS)),)
	VL_FLAGS += -DDIFFTEST
	CXXFLAGS += -DDIFFTEST
	LDFLAGS += -lreadline
endif

SRCS = $(SW_COMMON_DIR)/util.cpp $(SW_COMMON_DIR)/mem.cpp $(SW_COMMON_DIR)/softfloat_ext.cpp $(SW_COMMON_DIR)/rvfloats.cpp #$(SW_COMMON_DIR)/dram_sim.cpp
SRCS += $(DPI_DIR)/util_dpi.cpp $(DPI_DIR)/float_dpi.cpp
SRCS += $(SRC_DIR)/processor.cpp
SRCS += $(SRC_DIR)/dut.cpp

TOP = difftest_shim

VL_FLAGS += --exe
VL_FLAGS += --language 1800-2009 --assert -Wall -Wpedantic
VL_FLAGS += -Wno-DECLFILENAME -Wno-REDEFMACRO
VL_FLAGS += --x-initial unique --x-assign unique
VL_FLAGS += verilator.vlt
VL_FLAGS += -DSIMULATION -DSV_DPI
VL_FLAGS += -DXLEN_$(XLEN)
VL_FLAGS += $(CONFIGS)
VL_FLAGS += $(RTL_INCLUDE)
VL_FLAGS += $(RTL_PKGS)
VL_FLAGS += --cc $(TOP) --top-module $(TOP)

# Extract RTL directories from include directories
RTL_DIRS := $(patsubst -I%,%,$(filter -I%,$(RTL_INCLUDE)))

# Discover RTL source files from source directories
RTL_SRCS := $(shell find $(RTL_DIRS) -type f \( -name '*.v' -o -name '*.vh' -o -name '*.sv' -o -name '*.vi' \))

# Enable Verilator multithreaded simulation
THREADS ?= $(shell python3 -c 'import multiprocessing as mp; print(mp.cpu_count())')
VL_FLAGS += -j $(THREADS)

# enable multithreading sim only when no RTL DPI boxes are used
ifeq (,$(findstring -DDPI_DPI, $(CONFIGS)))
ifeq (,$(findstring -DDPI_DSP, $(CONFIGS)))
# Depends on RTL circuit size, just try and find a verilator-allowed largest number
VL_FLAGS += --threads 8 #$(THREADS)
VL_FLAGS += --threads-dpi none -Wno-UNOPTTHREADS
endif
endif


# Debugging
ifdef DEBUG
	VL_FLAGS += --trace --trace-structs $(DBG_FLAGS)
	CXXFLAGS += -g -O0 $(DBG_FLAGS)
else
	VL_FLAGS += -DNDEBUG
	CXXFLAGS += -O2 -DNDEBUG -DDEBUG_LEVEL=0
endif

# Enable perf counters
ifdef PERF
	VL_FLAGS += -DPERF_ENABLE
	CXXFLAGS += -DPERF_ENABLE
endif

# Enable Address Sanitizer
ifdef ASAN
	CXXFLAGS += -fsanitize=address -fno-omit-frame-pointer
	LDFLAGS += -fsanitize=address
endif

PROJECT := difftest

.PHONY: all force clean clean-lib clean-exe lib

all: $(DESTDIR)/$(PROJECT)

$(DESTDIR)/$(PROJECT): $(SRCS) $(SRC_DIR)/main.cpp $(CONFIG_FILE) $(RTL_SRCS) $(DESTDIR)/libemulator_vortex.a $(DIFFTEST_LIB)
	verilator --build $(VL_FLAGS) $(SRCS) $(SRC_DIR)/main.cpp \
				$(DIFFTEST_LIB) $(DESTDIR)/libemulator_vortex.a \
				-CFLAGS '$(CXXFLAGS) -DSTARTUP_ADDR=0x80000000' -LDFLAGS '$(LDFLAGS)' --MMD --Mdir $@.obj_dir -o $@

lib: $(DESTDIR)/lib$(PROJECT).so

$(DESTDIR)/lib$(PROJECT).so: $(SRCS) $(CONFIG_FILE) $(RTL_SRCS) $(DESTDIR)/libemulator_vortex.a $(DIFFTEST_LIB)
	verilator --build $(VL_FLAGS) $(SRCS) \
				$(DIFFTEST_LIB) $(DESTDIR)/libemulator_vortex.a \
				-CFLAGS '$(CXXFLAGS)' -LDFLAGS '-shared $(LDFLAGS)' --MMD --Mdir $@.obj_dir -o $@


$(DESTDIR)/libemulator_vortex.a: force
	SIMIX_DIFFTEST_ISA_EMULATOR=1 CONFIGS="$(CONFIGS)" DESTDIR="$(DESTDIR)" $(MAKE) -C $(CURDIR)/../simx $(DESTDIR)/libemulator_vortex.a

# updates the timestamp when flags changed.
$(CONFIG_FILE): force
	@mkdir -p $(@D)
	@printf '%s\n' "$(VL_FLAGS)" > $@.tmp
	@if ! cmp -s $@.tmp $@; then \
	  mv $@.tmp $@; \
	else \
	  rm $@.tmp; \
	fi

clean-lib:
	rm -rf $(DESTDIR)/lib$(PROJECT).so.obj_dir
	rm -f $(DESTDIR)/lib$(PROJECT).so

clean-exe:
	rm -rf $(DESTDIR)/$(PROJECT).obj_dir
	rm -f $(DESTDIR)/$(PROJECT)

clean: clean-lib clean-exe
