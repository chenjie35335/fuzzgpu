#pragma once

#include "config.hh"
#include "isa/riscv/riscv_instr.hh"
#include "random_mem_allocator.hh"
#include "types.hh"

namespace fuzzgpu {

/// @brief Reason why one interleaved generation step stopped.
enum class GenExitReason {
  Step = 0,
  MemAddrGen,
  Retry,
  EndOfBasicBlock,
  EndOfInit,
  WarpTerminated,
};

/// @brief Per-warp state kept while generator interleaves multiple warps.
class InterleavedState {

public:
  ///< Whether scalar/vector base-address registers have been initialized.
  bool inited_dat_blk_baseaddr_regs{false};
  ///< Scalar base-address register used by generated load/store setup code.
  riscv_reg_t sgpr_baseaddr_reg;
  ///< Vector base-address register used by generated load/store setup code.
  riscv_vreg_t vgpr_baseaddr_reg;
  ///< Number of initialized integer, floating-point, and vector registers.
  unsigned inited_int_regs{0}, inited_fp_regs{0}, inited_vec_regs{0};
  ///< Warp termination state for the current generation step.
  bool warp_terminated{false}, terminating_warp{false};
  ///< Allocated memory block for the next basic block.
  MemoryBlock next_bb_mem_blk;
  ///< Snapshot restored when switching to the next basic block.
  GenWarp::BasicBlockSnapshot next_bb_snapshot;
  ///< Thread mask for the next basic block.
  difftest::ThreadMask next_bb_tmask;

  ///< Metadata for the instruction selected in the current step.
  std::shared_ptr<riscv_meta> meta;
  ///< Instruction instance being generated in the current step.
  riscv_instr instr{nullptr};

  //
  // gen_load_store.cc
  //
  ///< Emulator memory traces observed while generating a load/store.
  std::vector<difftest::MemTrace> mem_trace;
  ///< Local load/store sets staged before commit to global state.
  std::unordered_map<uint64_t, LoadStoreInfo> store_set, load_set;
  ///< Local store-id map staged before commit to global state.
  std::unordered_map<uint64_t,
                     std::unordered_map<uint8_t, difftest::LoadStoreId>>
      store_id;
  ///< Program counter of the load/store currently being generated.
  uint64_t pc_load_store;
  ///< True when load/store operands were replayed from history.
  bool replayed_from_history{false};

  ///< Last stop reason returned by the interleaved step.
  GenExitReason last_exit_reason{GenExitReason::Retry};
};

} // namespace fuzzgpu
