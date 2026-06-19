#pragma once

#include <fstream>
#include <memory>
#include <stack>
#include <unordered_map>
#include <vector>

#include <difftest_gpu/types.hh>

#include "igraph_node.hh"
#include "instr_pool.hh"
#include "reg_dep_tracker.hh"
#include "register_sampler.hh"
#include "scratchpad.hh"
#include "types.hh"

namespace fuzzgpu {
class riscv_meta;
class FuzzerState;
class RuntimeWarp;

/// @brief Mutable state and helpers for generating one warp's instruction stream.
class GenWarp {
  ///< Shared fuzzer state.
  FuzzerState &fuzzer_state_;

  ///< Runtime warp ID assigned by the architecture scheduler.
  difftest::WarpGID rt_warp_gid_;
  ///< Number of active threads in this generated warp.
  const unsigned num_active_threads_;
  ///< Register-file sizes visible to the generator.
  const unsigned num_int_regs_, num_fp_regs_, num_vec_regs_;

  ///< Scalar register usage hint for Ventus.
  unsigned sgpr_usage_;
  ///< Register samplers for integer, floating-point, and vector files.
  RegisterSampler int_reg_sampler_;
  RegisterSampler fp_reg_sampler_;
  RegisterSampler vec_reg_sampler_;
  ///< Integer registers reserved from random allocation.
  std::vector<riscv_reg_t> exclude_int_regs_;
  ///< Vector LMUL setting used to map vector register groups.
  unsigned vlmul_;

  ///< Prefix for per-warp trace files.
  std::string trace_file_prefix_;
  ///< Trace stream for this generated warp.
  std::ofstream trace_stream_;

public:
private:
#ifndef DISABLE_CHANGE_DETECTION
  static void on_property_changed_(const difftest::ThreadMask &utmask,
                                   difftest::ThreadMask &old_utmask);
#endif

  template <typename T>
  /// @brief RAII proxy that detects writes to a mutable property.
  class PropertyProxy {
    ///< Referenced property being mutated.
    T &utmask_;
#ifndef DISABLE_CHANGE_DETECTION
    ///< Snapshot used to report property changes at proxy destruction.
    T old_utmask_;
#endif

  public:
    PropertyProxy(T &utmask)
      : utmask_(utmask)
#ifndef DISABLE_CHANGE_DETECTION
      , old_utmask_(utmask_)
#endif
    {
    }
    ~PropertyProxy() {
#ifndef DISABLE_CHANGE_DETECTION
      GenWarp::on_property_changed_(utmask_, old_utmask_);
#endif
    }
    T &operator*() const { return utmask_; }
    T *operator->() const { return &utmask_; }
  };

  ///< First active thread for scalar-style execution.
  unsigned tstart_;
  ///< Current active thread mask.
  difftest::ThreadMask tmask_;

public:
  // std::vector<IGraphNode> igraph_nodes;

  ///< Instruction emission buffer for this warp.
  Scratchpad scratchpad;

  ///< Current emulator-visible architectural state for this warp.
  difftest::Trace emu_state;

public:
  /// @brief Snapshot of basic-block emission progress.
  struct BasicBlockSnapshot {
    ///< Target number of fuzzing instructions in the block.
    size_t num_target_instrs;
    ///< Number of generated instructions already emitted in the block.
    size_t num_gen_bb_instrs;
    ///< Memory block allocated for this basic block.
    MemoryBlock mem_blk;

    BasicBlockSnapshot() {}

    /// @brief Create a snapshot for a new basic block.
    explicit BasicBlockSnapshot(size_t num_target_instrs_,
                                const MemoryBlock &mem_blk_)
      : num_target_instrs(num_target_instrs_)
      , mem_blk(mem_blk_) {
      reset();
    }
    /// @brief Reset generated-instruction progress.
    void reset() { num_gen_bb_instrs = 0; }
  } bb_snapshot;

  /// @brief Saved SIMT divergence state for one split path.
  class SIMTState {
  public:
    ///< Memory block for the else/non-fallthrough path.
    MemoryBlock else_mem_blk;
    ///< Basic-block snapshot before divergence.
    BasicBlockSnapshot orig_bb_snapshot;
    ///< Thread masks before divergence and for the else path.
    difftest::ThreadMask orig_tmask, else_tmask;
#if defined(ARCH_VENTUS)
    ///< Ventus branch PC used for vector branch handling.
    uint64_t pc_vbranch;
    ///< Register clobbered by vsetrpc setup.
    riscv_reg_t vsetrpc_clobber_reg;
    ///< Join trampoline PC for Ventus reconvergence.
    uint64_t pc_join_trampoline;
    ///< Register clobbered by the join trampoline.
    riscv_reg_t pc_join_trampoline_clobber_reg;
#endif
    ///< True when the active path is the fallthrough path.
    bool fallthrough{false};

    ///< Next-block state before generating divergence.
    MemoryBlock orig_next_bb_mem_blk;
    ///< Next-block snapshot before generating divergence.
    GenWarp::BasicBlockSnapshot orig_next_bb_snapshot;
  };
  /// @brief Stack of pending SIMT reconvergence states.
  class SIMIStack : protected std::stack<SIMTState> {

  public:
    /// @brief Statistics for SIMT stack depth.
    struct Stat {
      ///< Maximum observed stack depth.
      size_t max_depth{0};
      /// @brief Dump SIMT stack statistics.
      void dump(unsigned ident = 0);
    } stat;

    /// @brief Pop the current SIMT state.
    void pop() { std::stack<SIMTState>::pop(); }

    /// @brief Push a SIMT state and update depth statistics.
    void emplace(const SIMTState &&state) {
      std::stack<SIMTState>::emplace(std::move(state));
      stat.max_depth = std::max(std::stack<SIMTState>::size(), stat.max_depth);
    }

    /// @brief Return current stack depth.
    auto size() const { return std::stack<SIMTState>::size(); }
    /// @brief Return true when no reconvergence state is pending.
    bool empty() const { return std::stack<SIMTState>::empty(); }
    /// @brief Return the current reconvergence state.
    SIMTState &top() { return std::stack<SIMTState>::top(); }
    /// @brief Return the current reconvergence state.
    const SIMTState &top() const { return std::stack<SIMTState>::top(); }
  };
  ///< Pending SIMT divergence stack.
  SIMIStack simt_stack;
  ///< Number of generated basic blocks in this warp.
  unsigned nr_gen_bb;
  ///< Number of generated instructions in this warp.
  unsigned nr_gen_instrs;

  ///< Barrier scope path for this warp.
  BarrierScopePrefix barrier_scope_id;
  ///< Number of barriers this warp still needs to reach.
  size_t nr_pending_barriers;
  ///< Barrier instruction index within this warp.
  size_t barrier_instr_idx;

  ///< Basic blocks that are branch targets but not taken in this execution.
  std::vector<std::pair<MemoryBlock, uint64_t>>
      dead_basic_blocks;

  ///< PCs whose emitted instructions were overwritten.
  std::vector<uint64_t> overwritten_pc;

  ///< PCs of loads intentionally racing with stores.
  std::vector<uint64_t> data_race_loads;

  ///< PCs that produce tainted state.
  std::vector<uint64_t> taint_pc;

  ///< Register dependency tracker for emitted instructions.
  RegDepTracker reg_dep_tracker;

  /// @brief Construct a generated warp with register and trace settings.
  GenWarp(FuzzerState &fuzzer_state, const std::string &trace_file_prefix_,
          unsigned num_int_regs, unsigned num_fp_regs, unsigned vgpr_usage,
          unsigned vlmul, unsigned num_active_threads_);

  /// @brief Reset mutable generation state for this warp.
  void reset();

  /// @brief Flush the per-warp trace stream.
  void flush_trace();

  /// @brief Assign the scheduler-provided runtime warp ID.
  void set_warp_gid(difftest::WarpGID rt_warp_gid);

  ///< Enable verbose register dumps while emulating this warp.
  bool dump_regs{false};

  /// @brief Return the assigned runtime warp ID.
  difftest::WarpGID warp_gid() const { return rt_warp_gid_; }

  /// @brief Return the number of active lanes in this warp.
  unsigned num_active_threads() const { return num_active_threads_; }

  /// @brief Return integer register count.
  auto num_int_regs() const { return num_int_regs_; }
  /// @brief Return floating-point register count.
  auto num_fp_regs() const { return num_fp_regs_; }
  /// @brief Return vector register count.
  auto num_vec_regs() const { return num_vec_regs_; }

  /// @brief Return shared fuzzer state.
  FuzzerState &fuzzer_state() { return fuzzer_state_; }

  /// @brief Reserve an integer register from random use.
  void exclude_int_regs(riscv_reg_t reg);

  /// @brief Return whether an integer register is reserved.
  bool is_int_reg_excluded(riscv_reg_t reg) const;

  /// @brief Mask an integer register from future source selection.
  void mask_int_reg(riscv_reg_t reg);

  /// @brief Return the current thread mask.
  const difftest::ThreadMask &tmask() const { return tmask_; }
  /// @brief Return a write proxy for the current thread mask.
  PropertyProxy<difftest::ThreadMask> tmask_proxy();
  /// @brief Return the first active scalar thread.
  unsigned tstart() const { return tstart_; }

  /// @brief Function type for emitting instructions.
  /// @details Implementations must propagate tags for emitted instructions.
  using fnEmitInstr = std::function<void(riscv_instr *instr)>;

  /// @brief Policy used when clearing non-deterministic register state.
  enum class ClearNodetMode {
    ForcedFirstThread,
    ForcedAllThread,
    FollowTmask,
  };
  /// @brief Pick an integer source register.
  riscv_reg_t pick_rs(bool cti, bool force_mutate = false);
  /// @brief Pick an integer destination register.
  riscv_reg_t pick_rd(const std::vector<riscv_reg_t> &exclude,
                      bool force_mutate = false);
  /// @brief Mark an integer destination as written.
  void touch_rd(riscv_reg_t reg, ClearNodetMode clear_nodet);

  /// @brief Pick a floating-point source register.
  riscv_fpr_t pick_fs(bool force_mutate = false);
  /// @brief Pick a floating-point destination register.
  riscv_fpr_t pick_fd(const std::vector<riscv_fpr_t> &exclude,
                      bool force_mutate = false);
  /// @brief Mark a floating-point destination as written.
  void touch_fd(riscv_fpr_t fpr, ClearNodetMode clear_nodet);

  /// @brief Pick a vector source register.
  riscv_vreg_t pick_vs(bool cti, bool force_mutate = false,
                       bool ret_null = false);
  /// @brief Pick a vector destination register.
  riscv_vreg_t pick_vd(const std::vector<riscv_vreg_t> &exclude,
                       bool force_mutate = false);
  /// @brief Mark a vector destination as written.
  void touch_vd(riscv_vreg_t vreg, ClearNodetMode clear_nodet);

  /// @brief Return whether an integer register is non-deterministic.
  bool is_nodet_int_reg(riscv_reg_t reg) const {
    return int_reg_sampler_.is_reg_nodet(GPREncode(reg),
                                         get_tmask_for_scala_reg_());
  }
  /// @brief Return whether a vector register is non-deterministic.
  bool is_nodet_vec_reg(riscv_vreg_t reg) const {
    return vec_reg_sampler_.is_reg_nodet(GPREncode(reg) / vlmul_, tmask_);
  }

  /// @brief Mark an integer register as non-deterministic.
  void mark_int_reg_nodet(riscv_reg_t reg) {
    int_reg_sampler_.mark_reg_nodet(GPREncode(reg), get_tmask_for_scala_reg_());
  }
  /// @brief Mark a vector register as non-deterministic.
  void mark_vec_reg_nodet(riscv_vreg_t reg, ClearNodetMode nodet_mode) {
    vec_reg_sampler_.mark_reg_nodet(GPREncode(reg) / vlmul_,
                                    get_tmask_for_clearing_nodet_(nodet_mode));
  }

  /// @brief Return whether an integer register can be sampled as source.
  bool is_pickable_int_reg(riscv_reg_t reg, bool cti) const {
    return int_reg_sampler_.is_rs_pickable(GPREncode(reg),
                                           get_tmask_for_scala_reg_(), cti);
  }
  /// @brief Return whether a vector register can be sampled as source.
  bool is_pickable_vec_reg(riscv_vreg_t reg, bool cti) const {
    return vec_reg_sampler_.is_rs_pickable(GPREncode(reg) / vlmul_, tmask_,
                                           cti);
  }

  /// @brief Return whether no integer source register is currently available.
  bool no_available_int_reg() const {
#if defined(ARCH_VORTEX)
    for (unsigned reg = 1; reg < num_int_regs_; ++reg) {
      if (int_reg_sampler_.is_rs_pickable(reg, get_tmask_for_scala_reg_(),
                                          false)) {
        return false;
      }
    }
    return true;
#else
    return int_reg_sampler_.no_available(get_tmask_for_scala_reg_());
#endif
  }
  /// @brief Return whether no vector source register is currently available.
  bool no_available_vec_reg() const {
    return vec_reg_sampler_.no_available(tmask_);
  }

  /// @brief Configure architecture register-extension state.
  void set_reg_ext(unsigned rd_ext, unsigned rs1_ext, unsigned rs2_ext,
                   unsigned rs3_ext);

  /// @brief Switch emission to another basic block and thread mask.
  void switch_basic_block(const MemoryBlock &pc_mem_blk,
                          const GenWarp::BasicBlockSnapshot &bb_snapshot,
                          const difftest::ThreadMask &tmask);

  /// @brief Emulate this warp until @p pc is reached.
  void emulate_until(uint64_t pc, bool is_next_pc);

  /// @brief Emit and emulate thread-initialization code.
  void gen_init_threads();

  /// @brief Generate a join/reconvergence sequence.
  bool gen_join(riscv_instr *join_instr, MemoryBlock &next_bb_mem_blk_,
                GenWarp::BasicBlockSnapshot &next_bb_snapshot_,
                difftest::ThreadMask &next_bb_tmask_);
  /// @brief Return maximum instruction count used by a join reconvergence point.
  static unsigned join_reconv_point_instrs();

  /// @brief Return whether this warp still has unresolved barriers.
  bool has_unsolved_barrier() const;

private:
  ///< Cached masks for one thread and all threads.
  difftest::ThreadMask tmask_one_, tmask_all_;

  const difftest::ThreadMask &
  get_tmask_for_clearing_nodet_(ClearNodetMode mode) const;
  const difftest::ThreadMask &get_tmask_for_scala_reg_() const;
};

} // namespace fuzzgpu

namespace fmt {

template <>
struct formatter<fuzzgpu::GenWarp::BasicBlockSnapshot> {
  constexpr auto parse(format_parse_context &ctx) { return ctx.begin(); }
  auto format(const fuzzgpu::GenWarp::BasicBlockSnapshot &src,
              format_context &ctx) const {
    fmt::format_to(ctx.out(),
                   "{{mem blk [{:#x},{:#x}], instrs {}, gen_instrs {}}}",
                   src.mem_blk.begin, src.mem_blk.end, src.num_target_instrs,
                   src.num_gen_bb_instrs);
    return ctx.out();
  }
};

} // namespace fmt
