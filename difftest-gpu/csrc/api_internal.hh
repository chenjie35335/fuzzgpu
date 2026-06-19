#pragma once

#include "types_internal.hh"

namespace difftest {

extern uint64_t rtl_clk_cycles;

bool read_dut_pmem(uint64_t paddr, uint8_t* out, size_t len);

void pause_emulator_thread();
void resume_emulator_thread();
void stop_emulator_thread();

}
