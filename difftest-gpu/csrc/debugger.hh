#pragma once
#include "per_warp.hh"

namespace difftest {

class Debugger {
    const std::vector<PerWarp> & dut_per_warp_;
public:
  bool repl();

  Debugger(const std::vector<PerWarp> & dut_per_warp) : dut_per_warp_(dut_per_warp) {}
};

} // namespace difftest