#pragma once

#include <fstream>
#include <functional>
#include <string>
#include <vector>

#include <difftest_gpu/isa_emulator.hh>

namespace fuzzgpu {

int run_debug_commands(const std::string &cmd_file,
                        difftest::ISAEmulator &isa_emu);

} // namespace fuzzgpu