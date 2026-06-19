#pragma once

#include "spdlog/fmt/fmt.h"
#include <optional>
#include <unordered_set>
#include <vector>

#include <difftest_gpu/types.hh>

namespace fuzzgpu {

namespace vortex {
using WarpLID = unsigned;
using CoreGID = unsigned;
using ClusterGID = unsigned;
} // namespace vortex

/// @brief Generated warp identifier before mapping to runtime warp IDs.
using GenWarpGID = unsigned;

/// @brief Encoded general-purpose register number.
using GPREncode = unsigned;

/// @brief Scope path prefix used to identify barrier hierarchy levels.
using BarrierScopePrefix = std::vector<unsigned>;

/// @brief Tree of generated warps grouped by barrier scope.
struct ScopedWarpTree {
  ///< Scope represented by this node.
  difftest::BarrierScope scope;
  ///< Child scopes.
  std::vector<std::shared_ptr<ScopedWarpTree>> children;
  ///< Runtime warp ID for leaf nodes.
  std::optional<difftest::WarpGID> warp_gid;

  /// @brief Insert all warp leaves under this node into @p ret.
  void insert_all_warps(std::unordered_set<difftest::WarpGID> &ret) const;
};

#if defined(ARCH_VORTEX)
// since vortex's tmask is not contiguous, we need the whole range
// (0~MAX_NUM_THREADS) to represent the tmask
#define foreach_thread_lid(_genwarp, _var)                                     \
  for (difftest::ThreadLID _var = 0;                                           \
       _var < Config::instance().arch.nr_threads_per_warp; ++_var)
#elif defined(ARCH_VENTUS)
// ventus's tmask is contiguous & left-aligned, so we only need to iterate
// over the active threads
#define foreach_thread_lid(_genwarp, _var)                                     \
  for (difftest::ThreadLID _var = 0; _var < _genwarp->num_active_threads();    \
       ++_var)
#endif

/// @brief Summary of the latest load/store activity for one address.
struct LoadStoreInfo {
  ///< True if more than one thread touched this address.
  bool multiple_threads{false};
  ///< One representative thread that accessed this address.
  difftest::GlobalThreadId
      thread_gid;
  ///< One representative PC that accessed this address.
  uint64_t pc;
  ///< One representative memory scope for this address.
  difftest::ScopeIdentifier scope;
  ///< Whether the latest known value was initialized before fuzzing.
  bool initial_written{false};
};

} // namespace fuzzgpu

namespace fmt {

template <>
struct formatter<std::vector<unsigned>> {
  constexpr auto parse(format_parse_context &ctx) { return ctx.begin(); }
  auto format(const std::vector<unsigned> &src, format_context &ctx) const {
    fmt::format_to(ctx.out(), "[");
    for (size_t i = 0; i < src.size(); ++i) {
      if (i != 0)
        fmt::format_to(ctx.out(), ", ");
      fmt::format_to(ctx.out(), "{}", src[i]);
    }
    fmt::format_to(ctx.out(), "]");
    return ctx.out();
  }
};

template <>
struct formatter<difftest::GlobalThreadId> {
  constexpr auto parse(format_parse_context &ctx) { return ctx.begin(); }
  auto format(const difftest::GlobalThreadId &src, format_context &ctx) const {
    fmt::format_to(ctx.out(), "[Warp GID {}, Thread LID {}]", src.warp_gid,
                   src.thread_lid);
    return ctx.out();
  }
};

} // namespace fmt
