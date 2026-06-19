// Copyright © 2019-2023
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "processor.h"
#include <VX_config.h>
#include <VX_types.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mem.h>
#include <unistd.h>
#include <util.h>

#include <difftest_gpu/api.hh>

#define RAM_PAGE_SIZE 4096

using namespace vortex;

static void show_usage() {
  std::cout << "Usage: [-h: help] <program>" << std::endl;
}

const char *program = nullptr;

static void parse_args(int argc, char **argv) {
  int c;
  while ((c = getopt(argc, argv, "rh")) != -1) {
    switch (c) {
    case 'h':
      show_usage();
      exit(0);
    default:
      show_usage();
      exit(-1);
    }
  }

  if (optind < argc) {
    program = argv[optind];
    std::cout << "Running " << program << "..." << std::endl;
  } else {
    show_usage();
    exit(-1);
  }
}

int main(int argc, char **argv) {
  int exitcode = 0;

  std::cout << "DEBUG_LEVEL = " << DEBUG_LEVEL << std::endl;
#if DEBUG_LEVEL == 0
  difftest::set_loglevel(2); // >= info
#else                        // trace
  difftest::set_loglevel(0); // >= trace
#endif

  parse_args(argc, argv);

  // create memory module
  vortex::RAM ram(0, RAM_PAGE_SIZE);

  // create processor
  vortex::Processor processor;

  // attach memory module
  processor.attach_ram(&ram);

  auto &isa_emu = difftest::get_isa_emulator();

  // setup base DCRs
  const uint64_t startup_addr(STARTUP_ADDR);
  processor.dcr_write(VX_DCR_BASE_STARTUP_ADDR0, startup_addr & 0xffffffff);
  isa_emu.send_command(VX_DCR_BASE_STARTUP_ADDR0, startup_addr & 0xffffffff);
#if (XLEN == 64)
  processor.dcr_write(VX_DCR_BASE_STARTUP_ADDR1, startup_addr >> 32);
  difftest_send_cmd(VX_DCR_BASE_STARTUP_ADDR1, startup_addr >> 32);
#endif
  processor.dcr_write(VX_DCR_BASE_MPM_CLASS, 0);
  isa_emu.send_command(VX_DCR_BASE_MPM_CLASS, 0);

  // load program
  size_t image_len;
  {
    std::string program_ext(fileExtension(program));
    if (program_ext == "bin") {
      image_len = ram.loadBinImage(program, startup_addr);
    } else if (program_ext == "hex") {
      image_len = ram.loadHexImage(program);
    } else {
      std::cerr << "Error: only *.bin or *.hex images supported." << std::endl;
      return -1;
    }
  }

  // sync to the emulator
  for (size_t b = startup_addr; b < startup_addr + image_len; ++b) {
    uint8_t buf = ram[b];
    isa_emu.pmem_write(difftest::ScopeIdentifier(difftest::ScopeIdentifier::global()),
                       b,
                       &buf,
                       sizeof(buf));
  }
  std::cout << "Memory image loaded " << image_len << " bytes." << std::endl;

#ifndef NDEBUG
  std::cout << "[VXDRV] START: program=" << program << std::endl;
#endif
  // run simulation
  processor.run();

  // read exitcode from @MPM.1
  ram.read(&exitcode, (IO_MPM_ADDR + 8), 4);

  return exitcode;
}
