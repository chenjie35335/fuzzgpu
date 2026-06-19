#pragma once

#include <deque>
#include <limits>
#include <random>
#include <unordered_map>

#include <difftest_gpu/isa_emulator.hh>

#include "bits_sampler.hh"
#include "config.hh"
#include "instr_pool.hh"
#include "isa/riscv/riscv_meta_list.hh"
#include "ldst_operand_history.hh"
#include "random_mem_allocator.hh"
#include "types.hh"

#include "nlohmann/json.hpp"

namespace fuzzgpu {

class riscv_instr;

/// @brief Fuzzer state shared across the fuzzer components
class FuzzerState {

  ///< Instruction names registered for random selection.
  std::vector<riscv_instr_name_t> registered_instr_;

  ///< Probability weights for registered instructions.
  std::vector<double> instr_prob_dist_;

  ///< Distribution used to sample instruction indices.
  std::discrete_distribution<unsigned> instr_dist_;

  ///< Scratch buffer for randomized thread-order traversal.
  std::vector<difftest::ThreadLID> shuffled_thread_lid_;

  ///< CSR pools partitioned by access permissions.
  std::vector<uint32_t> writable_csrs_, readonly_csrs_, readable_csrs_;

  ///< CSR distributions for random CSR operand selection.
  std::uniform_int_distribution<size_t> writable_csrs_dist_,
      readable_csrs_dist_;

  ///< Per-instruction emission counts.
  std::vector<size_t> instr_freq_;
  ///< Total number of emitted fuzzing instructions.
  size_t num_instrs_{0};

public:
  ///< Seed material persisted into generated metadata.
  std::array<std::uint32_t, 16> seed_data;
  ///< Main fuzzer pseudo-random generator.
  std::mt19937_64 rng;

  ///< Bit-width based sampler used for immediate operands.
  BitsSampler bits_sampler{rng};

  ///< Distribution for AMO acquire/release encoding choices.
  std::uniform_int_distribution<uint32_t> aq_rl_dist{0, 2};

  ///< Distribution for arbitrary 12-bit CSR addresses.
  std::uniform_int_distribution<uint16_t> csr_dist{
      0, std::numeric_limits<uint16_t>::max()};

  ///< Distribution for pseudo immediate generation.
  std::uniform_int_distribution<int64_t> pseduo_imm_dist;

  ///< Signed 12-bit immediate distribution.
  std::uniform_int_distribution<int16_t> imm12_dist{-2048, 2047};

  ///< Full-width unsigned immediate distribution.
  std::uniform_int_distribution<uint32_t> uimm32_dist{
      std::numeric_limits<uint32_t>::min(),
      std::numeric_limits<uint32_t>::max()};

  ///< Uniform probability distribution in [0, 1].
  std::uniform_real_distribution<double> uniform_dist{0, 1};

  ///< Basic-block target instruction-count distribution.
  std::uniform_int_distribution<size_t> nr_tgt_instr_dist{
      Config::instance().min_num_fuzzing_instrs_per_bb,
      Config::instance().max_num_fuzzing_instrs_per_bb};

  ///< Global allocator for generated code and data memory.
  RandomMemoryAllocator mem_alloc{Config::instance().arch.data_vaddr_range,
                                  Config::instance().arch.code_vaddr_range,
                                  rng};

  ///< ISA emulator used to guide generation.
  difftest::ISAEmulator isa_emu;

  ///< Global memory locations observed as stores and loads.
  std::unordered_map<uint64_t, LoadStoreInfo> store_set, load_set;

  ///< Mapping from physical byte value to store identifiers.
  std::unordered_map<uint64_t,
                     std::unordered_map<uint8_t, difftest::LoadStoreId>>
      store_id;
  ///< Store IDs that still need to be resolved by generated code.
  std::unordered_set<uint64_t> pending_store_id;

  ///< Global program order and per-location program-order relations.
  difftest::ProgramOrder po;
  std::unordered_map<uint64_t, difftest::ProgramOrder> po_loc;
  ///< Opcode metadata for load/store IDs.
  std::unordered_map<difftest::LoadStoreId, difftest::LoadStoreOpcode,
                     difftest::LoadStoreIdHash>
      ldst_opcodes;

  ///< Store trace indexed by program counter.
  difftest::StoreTrace store_trace_;

  ///< Physical addresses currently considered tainted.
  std::unordered_set<uint64_t> taint_paddr;

  /// @brief Shared indexed load/store operand history for replay.
  LoadStoreOperandHistoryIndex ldst_operand_history;

  ///< Instruction arena used to allocate generated instructions.
  InstrPool instr_pool;

  ///< Counter used to generate unique initial_write_id values.
  uint64_t next_initial_write_id{0};

  /// @brief Return the total number of emitted fuzzing instructions.
  auto get_num_instrs() const { return num_instrs_; }

  ///< Whether any barrier instruction was emitted.
  bool emitted_barrier{false};

  /// @brief Aggregated generation statistics.
  struct Stat {
    size_t rejected_load_store_generations{0}, rejected_store_id_conflicts{0},
        accepted_load_store_generations{0};

    size_t rejected_thread_divergence_preference{0},
        accepted_thread_divergence_preference{0};

    size_t succ_branch_generations{0}, thread_divergence_branch_generations{0};

    size_t memalloc_failed_branch_generations{0};

    size_t branch_target_allocation_fail{0};

    size_t rejected_pred_generations{0}, accepted_pred_generations{0};

    size_t num_generated_warps{0};

    size_t num_generated_barriers{0}, rejected_barrier_generations{0};

    size_t num_refresh_int_reg{0}, num_refresh_vector_reg{0};

    size_t rejected_csr_generations{0}, accepted_csr_generations{0};

    size_t tainted_load{0}, tainted_store{0};
    ///< Load/store replay statistics.
    size_t replay_attempts{0};
    size_t replay_successes{0};
    size_t replay_immediate_successes{0};
    size_t replay_eventual_successes{0};
    ///< Hash matches whose full operand values differed.
    size_t replay_hash_collisions{0};

    ///< Data footprint grouped by memory scope.
    std::unordered_map<difftest::MemScope, size_t> data_footprint_bytes;

    ///< Total file-writing time excluding dependency analysis.
    size_t file_write_time_us{0};

    /// @brief Dump the collected counters to the configured logger.
    void dump();
  } stat;

public:
  /// @brief Initialize state from a deterministic seed.
  FuzzerState(uint64_t seed);

  /// @brief Sample instruction metadata according to current probabilities.
  std::shared_ptr<riscv_meta> pick_instr();

  /// @brief Shuffle and return up to @p topk thread IDs.
  const std::vector<difftest::ThreadLID> &shuffle_thread_lid(unsigned topk);

  /// @brief Pick a CSR satisfying read/write requirements.
  uint32_t pick_csr(bool read, bool write);

  /// @brief Return the configured generation probability for @p instr.
  double get_instr_prob(riscv_instr_name_t instr) const;

  /// @brief Dump aggregate fuzzer statistics.
  void dump_stat();

  /// @brief Return the number of registered instruction kinds.
  size_t get_registered_instr_count() const { return registered_instr_.size(); }

  /// @brief Return registered instruction names in sampling order.
  const std::vector<riscv_instr_name_t> &get_registered_instr_names() const {
    return registered_instr_;
  }

  /// @brief Replace instruction sampling probabilities.
  void set_instr_prob(const std::vector<double> &probs);

  /// @brief Account for one emitted instruction of kind @p instr.
  void add_instr_freq(riscv_instr_name_t instr) {
    instr_freq_[size_t(instr)]++;
    num_instrs_++;
  }

  /// @brief Serialize instruction frequencies as JSON.
  nlohmann::json get_instr_freq() const;
};

} // namespace fuzzgpu
