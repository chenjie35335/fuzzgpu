#pragma once

namespace fuzzgpu {

class riscv_instr;

/// @brief Node in a generated instruction control-flow graph.
class IGraphNode {
public:
  ///< Instruction represented by this graph node.
  riscv_instr *instr;
  ///< Successor reached by the fall-through/true path and alternate path.
  riscv_instr *next_if, *next_else{nullptr};
};

} // namespace fuzzgpu
