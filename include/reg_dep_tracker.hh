#pragma once

#include "types.hh"
#include <vector>

#include <unordered_map>

#include "isa/riscv/riscv_meta_list.hh"
namespace fuzzgpu {

class riscv_instr;

/// @brief Tracks register data flow to derive load/store dependencies.
class RegDepTracker {
  /// @brief Normalized instruction class for dependency analysis.
  enum class Opcode { Load, Store, Atomic, Others };
  /// @brief Node in the per-warp data-flow graph.
  struct DFGNode {
    ///< Program counter of the producer/consumer instruction.
    uint64_t pc;
    ///< Normalized opcode class.
    Opcode opcode;
    ///< True when vs2 is the store-data operand rather than address input.
    bool using_vs2_as_store_data;
  };
  /// @brief Operand slot represented by a dependency edge.
  enum class Operand { RS1, RS2, RD, FS1, FS2, FS3, FD, VS1, VS2, VS3, VD };
  /// @brief Edge from a writer to a consumer operand.
  struct DFGEdge {
    ///< Consumer node.
    DFGNode to_node;
    ///< Consumer operand slot.
    Operand to_operand;
  };
  ///< Producer PC to outgoing dependency edges.
  std::unordered_map<uint64_t, std::vector<DFGEdge>> dfg_map_;

  ///< Latest writer for each integer, floating-point, and vector register.
  std::unordered_map<riscv_reg_t, DFGNode> reg_writer;
  std::unordered_map<riscv_fpr_t, DFGNode> fpr_writer;
  std::unordered_map<riscv_vreg_t, DFGNode> vreg_writer;

  ///< Normalized opcode class per program counter.
  std::unordered_map<uint64_t, Opcode> pc_opcode_map_;

  bool is_to_store_data_(const DFGEdge &e);
  bool is_to_ldst_addr_(const DFGEdge &e);

  ///< Cached load/store data-dependency graph.
  std::unordered_map<uint64_t, std::vector<DFGEdge>> data_dep_cache_,
      ///< Cached load/store address-dependency graph.
      addr_dep_cache_;

  const std::unordered_map<uint64_t, std::vector<DFGEdge>> &
  build_data_dep_cache_();
  const std::unordered_map<uint64_t, std::vector<DFGEdge>> &
  build_addr_dep_cache_();

  std::unordered_map<uint64_t, std::vector<uint64_t>>
  condense_ldst(const std::unordered_map<uint64_t, std::vector<DFGEdge>> &G);

public:
  /// @brief Update dependency state after emitting @p instr at @p pc.
  void propagate_dep(const riscv_instr &instr, uint64_t pc);

  /// @brief Drop cached condensed dependency graphs.
  void clear_dep_caches();
  /// @brief Build load/store data dependencies keyed by producer PC.
  std::unordered_map<uint64_t, std::vector<uint64_t>> build_ldst_data_dep();
  /// @brief Build load/store address dependencies keyed by producer PC.
  std::unordered_map<uint64_t, std::vector<uint64_t>> build_ldst_addr_dep();

  /// @brief Return histogram data for dependency chain lengths.
  std::vector<size_t> count_dep_chain_lengths();
};

} // namespace fuzzgpu
