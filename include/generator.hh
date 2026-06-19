#pragma once

#include <stack>
#include <unordered_set>

#include "exchange_file.hh"
#include "fuzzer_state.hh"
#include "gen_warp.hh"
#include "instr_pool.hh"
#include "interleaved_state.hh"
#include "isa/riscv/riscv_meta_list.hh"
#include "random_mem_allocator.hh"
#include "scheduler_ventus.hh"
#include "scheduler_vortex.hh"
#include "scratchpad.hh"

#include "nlohmann/json.hpp"

namespace fuzzgpu {

/// @brief Top-level program generator that emits code, data, aux, and metadata.
class Generator {

  ///< Shared mutable fuzzer state.
  FuzzerState &state_;
#if defined(ARCH_VENTUS)
  ///< Architecture scheduler for Ventus workgroups.
  std::shared_ptr<VentusScheduler> scheduler_;
#elif defined(ARCH_VORTEX)
  ///< Architecture scheduler for Vortex grids.
  std::shared_ptr<VortexScheduler> scheduler_;
#else
#error
#endif
  ///< Maximum trampoline size in bytes for branch/jump generation.
  size_t max_trampoline_sz_;

  ///< Physical MMIO addresses that generated memory ops must avoid.
  std::unordered_set<uint64_t> mmio_addrs_;
  void init_mmio_addrs_();

  ///< Data fragments emitted into the linear memory data stream.
  std::vector<LinearMemoryDataFragmentHeader> data_mem_regions_;

  //
  // gen_bcc.cc
  //
  /// @brief Candidate trampoline implementation for jump-like control flow.
  struct TrampolineCandidate {
    ///< Legal PC-relative jump offset range.
    std::pair<uint64_t, uint64_t> jmp_range;
    ///< Offset from the jump PC to the encoded jump instruction.
    uint64_t pc_jmp_offset;
    ///< Instruction used by this trampoline.
    riscv_instr_name_t jmp_instr;
    ///< Emitter for the trampoline instruction sequence.
    std::function<void(const std::shared_ptr<GenWarp> &, uint64_t)> emit;
  };

  /// @brief Candidate branch expansion with architecture-specific prologue.
  struct BranchCandidate {
    ///< Legal branch offset range.
    std::pair<uint64_t, uint64_t> jmp_range;
    ///< Emit setup code before the final branch target is known.
    std::function<std::tuple<uint64_t, uint64_t, riscv_instr *>(
        const std::shared_ptr<GenWarp> &gen_warp, riscv_instr *instr,
        bool thread_diverged,
        const std::function<uint64_t(const riscv_instr *)> &emit,
        std::optional<riscv_reg_t> &reg_to_mask,
        const std::function<void()> &branch_cond_flipped)>
        emit_prologue;
    ///< Patch or finish the candidate after target allocation.
    std::function<void(
        std::tuple<uint64_t, uint64_t, riscv_instr *> prologue_ret,
        const std::shared_ptr<GenWarp> &gen_warp,
        const std::function<uint64_t(const riscv_instr *)> &emit,
        uint64_t tgt_addr)>
        emit_epilogue;
  };

  std::vector<TrampolineCandidate> trampoline_candidates_;
  ///< Mapping from instruction name to trampoline candidate index.
  std::unordered_map<riscv_instr_name_t, size_t> trampoline_type_;
  std::vector<BranchCandidate> branch_candidates_;
  void init_trampoline_candidates_();
  bool gen_bcc_(const std::shared_ptr<GenWarp> &gen_warp, riscv_instr &instr);

  //
  // gen_initial_block.cc
  //
  GenExitReason gen_initial_block_(std::shared_ptr<GenWarp> &gen_warp,
                                   InterleavedState &istate);

  //
  // gen_barrier.cc
  //
  /// @brief Barrier participation plan for one generated program.
  struct BarrierInfo {
    ///< Barrier scope.
    difftest::BarrierScope scope;
    ///< Architecture-visible barrier identifier.
    unsigned barrier_id;
    ///< Required participant count.
    size_t count;
    ///< Warps selected to participate in this barrier.
    std::unordered_set<difftest::WarpGID> participants;

    BarrierInfo(difftest::BarrierScope scope_, unsigned barrier_id_,
                size_t count_)
      : scope(scope_)
      , barrier_id(barrier_id_)
      , count(count_) {}
  };
  std::vector<BarrierInfo> barrier_plan_;
  ///< Number of barriers allocated per scope.
  std::vector<size_t> barrier_num_;
  ///< Next barrier identifier to allocate.
  unsigned alloc_barrier_id_{0};

  void gen_barrier_plan_(const std::shared_ptr<ScopedWarpTree> &warps);
  void gen_barrier_init_warp_(const std::shared_ptr<GenWarp> &gen_warp,
                              unsigned genwarp_idx);

  BarrierScopePrefix get_barrier_scope_id_(difftest::WarpGID rt_warp_gid);

  difftest::BarrierScope max_support_barrier_scope_() const;
  difftest::BarrierScope min_support_barrier_scope_() const;

  void dump_barrier_scope_id_(const BarrierScopePrefix &scope_id,
                              fmt::memory_buffer &buf) const;

  bool gen_barrier_(const std::shared_ptr<GenWarp> &gen_warp,
                    riscv_instr &instr);

  ///< Serialized barrier metadata written to aux output.
  difftest::BarrierMeta barrier_meta_;

  //
  // gen_load_store.cc
  //
  std::vector<difftest::MemTrace>
  emulate_load_store_(const std::shared_ptr<GenWarp> &gen_warp,
                      riscv_instr &instr, bool &trap_taken);
  bool gen_load_store_addr_(const std::shared_ptr<GenWarp> &gen_warp,
                            riscv_instr &instr, InterleavedState &istate,
                            bool can_resample);
  /// @brief Result of trying to replay historical load/store operands.
  enum class ReplayAttemptResult {
    NotAttempted = 0,
    AttemptedNoMatch,
    AttemptedSuccess
  };
  ReplayAttemptResult
  replay_load_store_operands_(const std::shared_ptr<GenWarp> &gen_warp,
                              riscv_instr &instr);
  void fix_misaligned_imm_(const std::shared_ptr<GenWarp> &gen_warp,
                           riscv_instr &instr);
  /// @brief Encapsulated history update for load/store operand replay.
  void update_ldst_operand_history_(const std::shared_ptr<GenWarp> &gen_warp,
                                    const riscv_instr &instr);
  void add_initial_store_id_(difftest::GlobalThreadId thread_gid, uint64_t pa,
                             uint8_t rand_byte);
  void commit_load_store_set_(const std::shared_ptr<GenWarp> &gen_warp,
                              InterleavedState &ist);
  void gen_load_store_(const std::shared_ptr<GenWarp> &gen_warp,
                       riscv_instr &instr, InterleavedState &istate);
  bool check_store_id_conflict_(
      const std::unordered_map<
          uint64_t, std::unordered_map<uint8_t, difftest::LoadStoreId>>
          &store_id_map,
      uint64_t pa, uint8_t byte_val) const;
  void gen_fence_(const std::shared_ptr<GenWarp> &gen_warp, riscv_instr &instr);

  //
  // gen_thread_ctrl.cc
  //
  /// @brief Result of attempting thread-control generation for one step.
  enum class ThreadCtrlGenStatus { OK = 0, Skip, SwitchBasicBlock };
  ThreadCtrlGenStatus
  gen_thread_ctrl_(const std::shared_ptr<GenWarp> &gen_warp, riscv_instr &instr,
                   MemoryBlock &next_bb_mem_blk,
                   GenWarp::BasicBlockSnapshot &next_bb_snapshot,
                   difftest::ThreadMask &next_bb_tmask);

  //
  // gen_thread_merge.cc
  //
  bool gen_thread_merge_(const std::shared_ptr<GenWarp> &gen_warp,
                         riscv_instr &instr);

  //
  // gen_tensor_core.cc
  //
  bool gen_tensor_core_(const std::shared_ptr<GenWarp> &gen_warp,
                        riscv_instr &instr);

  //
  // gen_csr.cc
  //
  bool gen_csr_(const std::shared_ptr<GenWarp> &gen_warp, riscv_instr &instr);

  //
  // gen_refresh.cc
  //
  void gen_refresh_(const std::shared_ptr<GenWarp> &gen_warp,
                    const std::shared_ptr<riscv_meta> &meta);

  //
  // gen_regext.cc
  //
#if defined(ARCH_VENTUS)
  bool need_regext_(const riscv_instr &instr);
  std::optional<riscv_instr> emit_regext_prefix_(const riscv_instr &suffix);

  void gen_regext_(const std::shared_ptr<GenWarp> &gen_warp,
                   const riscv_instr &instr);
#endif

  //
  // generator.cc
  //

  size_t get_trampoline_type_(const std::shared_ptr<riscv_meta> &meta) const;
  bool check_trampoline_range_(uint64_t pc_jmp,
                               const std::pair<uint64_t, uint64_t> &jmp_range,
                               uint64_t pc_tgt);
  size_t get_max_branch_trampoline_num_instr_() const;
  size_t get_max_jump_trampoline_num_instr_() const;

  bool illegal_vaddr_(uint64_t va, unsigned access_size) const;
  bool illegal_paddr_(uint64_t pa, unsigned access_size, bool is_store) const;

  size_t get_basic_block_sz_(size_t num_target_instrs) const;

  bool eof_basic_block_(const std::shared_ptr<GenWarp> &gen_warp,
                        const std::shared_ptr<riscv_meta> &meta) const;

  GenExitReason gen_interleaved_step_(size_t igw,
                                      std::shared_ptr<GenWarp> &gen_warp,
                                      InterleavedState &istate);

  void end_of_basic_block_(const std::shared_ptr<GenWarp> &gen_warp,
                           InterleavedState &istate);

  void end_of_warp_(const std::shared_ptr<GenWarp> &gen_warp,
                    InterleavedState &istate);

public:
  /// @brief Create a generator bound to shared fuzzer state.
  Generator(FuzzerState &state);

  /// @brief Generate a complete fuzzing program and associated metadata.
  void generate_backbone(std::ostream &instr_stream, std::ostream &data_stream,
                         std::ostream &aux_stream, nlohmann::json &meta_json,
                         const std::string &trace_file_prefix);
};

} // namespace fuzzgpu
