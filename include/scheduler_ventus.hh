#include <vector>

#include <difftest_gpu/types.hh>

#include "gen_warp.hh"
#include "random_mem_allocator.hh"

#include "scheduler.hh"

namespace fuzzgpu {

/// @brief Scheduler that maps generated warps onto Ventus workgroups.
class VentusScheduler : public Scheduler {
  ///< Shared fuzzer state.
  FuzzerState &state_;
  ///< Leader/basic-block memory used for scheduler code.
  MemoryBlock lb_mem_blk_;
  ///< Leader warp that emits shared scheduler setup.
  std::shared_ptr<GenWarp> leader_gen_warp_;
  ///< Maximum generated workgroup and wavefront entry sizes.
  size_t max_wg_entry_sz_, max_wf_entry_sz_;
  ///< Kernel dimensions exposed to the Ventus runtime.
  uint32_t kernel_size_[3];
  /// @brief Per-workgroup scheduler record.
  struct Workgroup {
    ///< First wavefront entry address for this workgroup.
    uint64_t first_wf_entry_addr;
  };
  ///< Generated workgroup records.
  std::vector<Workgroup> wg_;

public:
  /// @brief Construct the Ventus scheduler and its leader warp.
  VentusScheduler(FuzzerState &state, const std::string &trace_file_prefix_,
                  unsigned sgpr_usage_, unsigned num_int_regs, unsigned num_fp_regs, unsigned vgpr_usage_, unsigned vlmul_,
                  unsigned num_activate_threads);

  void add_warp(size_t genwarp_idx, uint64_t entry_addr,
                const std::shared_ptr<GenWarp> &gen_warp) override;

  void emit_end_of_warp(size_t genwarp_idx,
                        const std::shared_ptr<GenWarp> &gen_warp) override;

  bool grid_level_begins(size_t genwarp_idx) override;

  std::shared_ptr<ScopedWarpTree> get_scoped_warp_tree_per_grid_level(
      unsigned num_generated_warps) const override;

  /// @brief Return the maximum runtime warp ID that Ventus may schedule.
  static difftest::WarpGID max_warp_gid();

  const std::vector<std::shared_ptr<GenWarp>> gen_warps() const override;

public:
  /// @brief Return generated kernel dimensions.
  const uint32_t* ventus_get_kernel_size() { return kernel_size_; }
};

} // namespace fuzzgpu
