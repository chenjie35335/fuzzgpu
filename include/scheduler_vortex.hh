#pragma once

#ifdef ARCH_VORTEX
#include <vector>

#include <difftest_gpu/types.hh>

#include "gen_warp.hh"
#include "random_mem_allocator.hh"

#include "scheduler.hh"

namespace fuzzgpu {

/// @brief Scheduler that maps generated warps onto the Vortex hierarchy.
class VortexScheduler : public Scheduler {
  ///< Shared fuzzer state.
  FuzzerState &state_;
  ///< Leader/basic-block memory used for scheduler code.
  MemoryBlock lb_mem_blk_;
  ///< Leader warp that emits shared scheduler setup.
  std::shared_ptr<GenWarp> leader_gen_warp_;
  ///< Maximum generated entry sizes for wspawn lists.
  size_t max_wspawn_list_core_entry_sz_, max_wspawn_list_grid_entry_sz_;
  ///< Number of generated grids and grid capacity per core.
  unsigned nr_grids_, max_nr_grids_per_core_;
  ///< Maximum generated warp entry size.
  size_t max_warp_entry_sz_;
  ///< First entry PC for core-level scheduler code.
  uint64_t first_core_entry_pc_;
  /// @brief Per-grid scheduling record.
  struct Grid {
    ///< Generated warp assigned to this grid.
    std::shared_ptr<GenWarp> gen_warp;
    ///< Memory block containing grid entry code.
    MemoryBlock mem_blk;
    ///< PC of the first warp entry in this grid.
    uint64_t first_warp_entry_pc;
    ///< Registers reserved by scheduler glue.
    std::vector<riscv_reg_t> exclude_regs;
    ///< Return-address register used by scheduler glue.
    riscv_reg_t ret_addr_reg;
    ///< Whether warp 0 has been emulated for this grid.
    bool emulated_warp0{false};
  };
  ///< Generated grid records.
  std::vector<Grid> grid_gen_warps_;
  ///< Generated warp IDs grouped per Vortex core.
  std::vector<std::vector<unsigned>> genwarp_ids_per_core_;
  ///< Generated core IDs grouped per Vortex cluster.
  std::vector<std::set<vortex::CoreGID>> gen_cores_per_cluster_;
  ///< Number of parallel warps supported per Vortex core.
  unsigned vortex_nr_parallel_warps_per_core;

public:
  /// @brief Construct the Vortex scheduler and its leader warp.
  VortexScheduler(FuzzerState &state, const std::string &trace_file_prefix_,
                  unsigned vlmul, unsigned num_active_threads);

  void add_warp(size_t genwarp_idx, uint64_t entry_addr,
                const std::shared_ptr<GenWarp> &gen_warp) override;

  void emit_end_of_warp(size_t genwarp_idx,
                        const std::shared_ptr<GenWarp> &gen_warp) override;

  bool grid_level_begins(size_t genwarp_idx) override;

  std::shared_ptr<ScopedWarpTree> get_scoped_warp_tree_per_grid_level(
      unsigned num_generated_warps) const override;

  /// @brief Return the maximum runtime warp ID that Vortex may schedule.
  static difftest::WarpGID max_warp_gid();

  const std::vector<std::shared_ptr<GenWarp>> gen_warps() const override;

public:
  /// @brief Return the warp's index inside its generated grid.
  unsigned vortex_get_warp_id_per_grid(difftest::WarpGID rt_warp_gid) const;
  /// @brief Return the runtime core for a runtime warp.
  vortex::CoreGID vortex_get_core_gid(difftest::WarpGID rt_warp_gid) const;
  /// @brief Return the runtime cluster for a runtime warp.
  vortex::ClusterGID
  vortex_get_cluster_gid(difftest::WarpGID rt_warp_gid) const;
  /// @brief Return the number of generated grids.
  unsigned vortex_nr_grids() const { return nr_grids_; }

  /// @brief Return remaining capacity for @p core_gid.
  unsigned vortex_get_remaining_warps_per_core(vortex::CoreGID core_gid);
  /// @brief Return remaining capacity for @p cluster_gid.
  unsigned
  vortex_get_remaining_cores_per_cluster(vortex::ClusterGID cluster_gid);
};

} // namespace fuzzgpu

#endif // ARCH_VORTEX
