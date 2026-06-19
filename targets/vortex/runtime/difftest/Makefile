include ../common.mk

ifndef DIFFTEST_HOME
$(error "DIFFTEST_HOME is not set. Please set the environment variable \"DIFFTEST_HOME\" to the root directory of the difftest repository.")
endif

DESTDIR ?= $(CURDIR)/..

CONFIG_FILE := $(DESTDIR)/difftest_rt_config.stamp
SRC_DIR := $(VORTEX_HOME)/runtime/difftest

CXXFLAGS += -std=c++17 -Wall -Wextra -pedantic -Wfatal-errors
CXXFLAGS += -I$(INC_DIR) -I$(ROOT_DIR)/hw -I$(SIM_DIR)/difftest -I$(SW_COMMON_DIR) -I$(RT_COMMON_DIR)
CXXFLAGS += -I$(DIFFTEST_HOME)/csrc/include
CXXFLAGS += -DXLEN_$(XLEN) -DDIFFTEST_XLEN_$(XLEN) -DARCH_VORTEX

# Position independent code
CXXFLAGS += -fPIC

# Add external configuration
CXXFLAGS += $(CONFIGS)

LDFLAGS += -shared -pthread
LDFLAGS += -L$(DESTDIR) -ldifftest

SRCS := $(SRC_DIR)/vortex.cpp

# Debugging
ifdef DEBUG
	CXXFLAGS += -g -O0
else
	CXXFLAGS += -O2 -DNDEBUG
endif

PROJECT := libvortex-difftest.so

.PHONY: all force driver clean-driver clean-runtime clean

all: $(DESTDIR)/$(PROJECT)

driver: $(DESTDIR)/libdifftest.so

$(DESTDIR)/libdifftest.so: force
	DESTDIR=$(DESTDIR) $(MAKE) -C $(ROOT_DIR)/sim/difftest $@

$(DESTDIR)/$(PROJECT): $(SRCS) $(DESTDIR)/libdifftest.so $(CONFIG_FILE)
	$(CXX) $(CXXFLAGS) $(SRCS) $(LDFLAGS) -o $@

# updates the timestamp when flags changed.
$(CONFIG_FILE): force
	@mkdir -p $(@D)
	@printf '%s\n' "$(CXXFLAGS)" > $@.tmp
	@if ! cmp -s $@.tmp $@; then \
	  mv $@.tmp $@; \
	else \
	  rm $@.tmp; \
	fi

clean-driver:
	DESTDIR=$(DESTDIR) $(MAKE) -C $(ROOT_DIR)/sim/difftest clean-lib

clean-runtime:
	rm -f $(DESTDIR)/$(PROJECT)

clean: clean-driver clean-runtime