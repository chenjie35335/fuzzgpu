#pragma once

#include "gen_warp.hh"
#include "isa/riscv/riscv_instr.hh"
#include <difftest_gpu/types.hh>

namespace fuzzgpu {

/// @brief Propagates taint state across generated instruction operands.
class Taint {
public:
  /// @brief Return whether executing @p instr makes the outgoing value tainted.
  static bool propagate(const GenWarp &gen_warp_, const riscv_instr &instr,
                        bool taint);
};

} // namespace fuzzgpu
