#pragma once

#include "fuzzer_state.hh"

#include "gen_warp.hh"

namespace fuzzgpu {

/// @brief Architecture-specific scheduler interface for generated warps.
class Scheduler {

public:
  virtual ~Scheduler() {}

  /// @brief Register a generated warp with its entry address.
  virtual void add_warp(size_t genwarp_idx, uint64_t entry_addr,
                        const std::shared_ptr<GenWarp> &gen_warp) = 0;

  /// @brief Emit architecture-specific end-of-warp code.
  virtual void emit_end_of_warp(size_t genwarp_idx,
                                const std::shared_ptr<GenWarp> &gen_warp) = 0;

  /// @brief Return whether @p genwarp_idx starts a new grid-level group.
  virtual bool grid_level_begins(size_t genwarp_idx) = 0;

  /// @brief Build the scope tree for the current grid-level group.
  virtual std::shared_ptr<ScopedWarpTree>
  get_scoped_warp_tree_per_grid_level(unsigned num_generated_warps) const = 0;

  /// @brief Return generated warps in scheduler order.
  virtual const std::vector<std::shared_ptr<GenWarp>> gen_warps() const = 0;
};

} // namespace fuzzgpu
