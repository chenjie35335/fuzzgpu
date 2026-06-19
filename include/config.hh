#pragma once

#include <cstdint>
#include <string>

#include "spdlog/spdlog.h"
#include <difftest_gpu/types.hh>

namespace fuzzgpu {

/// @brief Architecture description extended with fuzzgpu memory size.
class ArchConfig : public difftest::Arch {
public:
  /// @brief Copy base architecture fields and attach global memory size.
  ArchConfig(const difftest::Arch &arch_, size_t gmem_size_)
    : difftest::Arch(arch_)
    , gmem_size(gmem_size_) {}

  ///< Global memory size used by the generated program.
  size_t gmem_size;
};

/// @brief Global generation configuration singleton.
class Config {
  ///< Singleton instance installed by create().
  static Config *g_instance_;

public:
  ///< Target architecture and memory configuration.
  ArchConfig arch;

  ///< Probability perturbation ratio for register-picking mutation.
  const double REGPICK_PROTUBERANCE_RATIO{0.2};

  ///< Toolchain name used by legacy flows.
  const std::string compiler_name{"vortex"};
  ///< Toolchain path used by legacy flows.
  const std::string compilers_path{"../../../tools"};

  ///< Number of generated workgroups/blocks.
  unsigned num_genblocks;
  ///< Number of generated warps per workgroup/block.
  unsigned num_genwarps_per_block;
  ///< Total number of generated warps.
  const unsigned num_genwarps;
  ///< Maximum fuzzing instructions in each generated basic block.
  size_t max_num_fuzzing_instrs_per_bb{100};
  ///< Minimum fuzzing instructions in each generated basic block.
  size_t min_num_fuzzing_instrs_per_bb{1};

  ///< Global cap on generated fuzzing instructions.
  size_t max_num_instrs{size_t(-1)};

  ///< Minimum number of thread-mask flips for divergent control flow.
  unsigned min_tmask_flips{1};

  ///< Register mutation probability.
  double reg_pick_mutate_prob{0.5};
  ///< Probability that a generated branch is taken.
  double branch_taken_prob{0.5};
  ///< Probability that the major path is taken in a branch.
  double branch_major_taken_prob{0.5};
  ///< Probability of generating divergent branches.
  double branch_divergent_prob{0.5};
  ///< Probability of forcing load/store generation.
  double force_lds_prob{0.2};

  ///< Number of instructions per arena chunk.
  size_t instr_pool_node_size{10000};

  ///< Maximum attempts for memory allocation.
  size_t max_alloc_tries{1000};

  ///< Maximum operand resampling attempts.
  unsigned max_resample_times{10};

  ///< Probability of trying load/store operand replay.
  double replay_load_store_operands_prob{0.8};
  ///< Sliding window size for load/store operand history.
  size_t ldst_operand_history_window_size{size_t(-1)};
  ///< Maximum registers sampled while attempting replay.
  unsigned k_max_replay_sample_regs{4};
  ///< Maximum history entries sampled per replay attempt.
  unsigned k_max_replay_sample_history{1024};

  ///< Initial RAM byte value, matching the emulator.
  uint8_t ram_initial_byte_val{0};

  ///< Enable emulator-guided generation.
  bool emu_guide{true};
  ///< Enable preplanned barrier generation.
  bool barrier_plan{true};
  ///< Enable taint-guided filtering and metadata.
  bool taint{true};

public:
  /// @brief Construct the global configuration.
  template <typename... Args>
  Config(const difftest::Arch &arch_, size_t gmem_size, unsigned num_genblocks_,
         unsigned num_genwarps_per_block_)
    : arch(arch_, gmem_size)
    , num_genblocks(num_genblocks_)
    , num_genwarps_per_block(num_genwarps_per_block_)
    , num_genwarps(num_genblocks * num_genwarps_per_block_) {
#ifdef ARCH_VENTUS
    assert(num_genwarps_per_block <= arch.ventus_nr_warp_per_block &&
           "too many workgroups for ventus");
#endif
  }

  /// @brief Create and install the global configuration singleton.
  template <typename... Args>
  static void create(Args &&...args) {
    g_instance_ = new Config(std::forward<Args>(args)...);
  }

  /// @brief Return the global configuration singleton.
  static Config &instance() { return *g_instance_; }

  /// @brief Dump configuration values to the logger.
  void dump() const;
};

} // namespace fuzzgpu
