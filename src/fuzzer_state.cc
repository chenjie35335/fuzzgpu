#include <limits>

#include "config.hh"
#include "gen_warp.hh"
#include "isa/riscv/riscv_floating_point_meta.hh"
#include "isa/riscv/riscv_instr.hh"
#include "isa/riscv/riscv_meta.hh"
#include "isa/riscv/ventus/csr.hh"
#include "isa/riscv/vortex/csr.hh"
#include "spdlog/spdlog.h"

#include "fuzzer_state.hh"

namespace fuzzgpu {

FuzzerState::FuzzerState(uint64_t seed) {
  // init the random seed
  if (seed == uint64_t(-1)) {
    std::random_device rd;
    std::generate(seed_data.begin(), seed_data.end(), std::ref(rd));
    std::seed_seq seed_s(seed_data.begin(), seed_data.end());
    rng = std::mt19937_64(seed_s);
  } else {
    // fixed seed for reproducible generation
    rng = std::mt19937_64(seed);
  }

  // operand for pseduo instructions
  int64_t min_op = (Config::instance().arch.word_bits == 64)
                       ? std::numeric_limits<int64_t>::min()
                       : std::numeric_limits<int32_t>::min();
  int64_t max_op = (Config::instance().arch.word_bits == 64)
                       ? std::numeric_limits<int64_t>::max()
                       : std::numeric_limits<int32_t>::max();
  pseduo_imm_dist = std::uniform_int_distribution<int64_t>(min_op, max_op);

  const auto num_category = size_t(riscv_instr_category_t::Count);
  for (size_t category = 0; category < num_category; ++category) {

#if defined(ARCH_VORTEX) || defined(ARCH_VENTUS)
    // filter out the SYSTEM
    if (category == size_t(riscv_instr_category_t::SYSTEM)) {
      continue;
    }
#endif

    const auto &instr_per_catrgory =
        riscv_meta_list::instance().instr_per_category(
            riscv_instr_category_t(category));

    for (const auto &meta : instr_per_catrgory) {
#if defined(ARCH_VORTEX) || defined(ARCH_VENTUS)
      if (meta->instr_name == riscv_instr_name_t::SFENCE_VMA) {
        continue;
      }
#endif

      registered_instr_.push_back(meta->instr_name);
    }
  }

  instr_prob_dist_.resize(size_t(riscv_instr_name_t::Count));
  // init the probability distribution
  std::fill(instr_prob_dist_.begin(), instr_prob_dist_.end(), 0.);
  for (const auto instr_name : registered_instr_) {
    instr_prob_dist_[size_t(instr_name)] = 1.0 / registered_instr_.size();
  }

  instr_dist_ = std::discrete_distribution<unsigned>(instr_prob_dist_.begin(),
                                                     instr_prob_dist_.end());

  instr_freq_.resize(size_t(riscv_instr_name_t::Count), 0);

  for (difftest::ThreadLID thread_lid = 0;
       thread_lid < Config::instance().arch.nr_threads_per_warp; ++thread_lid) {
    shuffled_thread_lid_.push_back(thread_lid);
  }

  if (Config::instance().arch.riscv_exts_mask & difftest::Arch::rv32f ||
      Config::instance().arch.riscv_exts_mask & difftest::Arch::rv64f) {
#if defined(ARCH_VORTEX)
    writable_csrs_.insert(writable_csrs_.end(), {
                                                    // write & read
                                                    VX_CSR_FFLAGS,
                                                    VX_CSR_FRM,
                                                    VX_CSR_FCSR,
                                                });
#elif defined(ARCH_VENTUS)
    writable_csrs_.insert(writable_csrs_.end(), {
                                                    // write & read
                                                    CSR_FFLAGS,
                                                    CSR_FRM,
                                                    CSR_FCSR,
                                                });
#endif
  }

#if defined(ARCH_VORTEX)
  readonly_csrs_.insert(readonly_csrs_.end(),
                        {
                            VX_CSR_MVENDORID,
                            VX_CSR_MARCHID,
                            VX_CSR_MIMPID,
                            VX_CSR_MISA,
                            VX_CSR_THREAD_ID,
                            VX_CSR_WARP_ID,
                            VX_CSR_CORE_ID,
                            VX_CSR_ACTIVE_THREADS,
                            VX_CSR_ACTIVE_WARPS,
                            VX_CSR_NUM_THREADS,
                            VX_CSR_NUM_WARPS,
                            VX_CSR_NUM_CORES,
                            VX_CSR_LOCAL_MEM_BASE,
                            // VX_CSR_MCYCLE, // undeterminstic value
                            // VX_CSR_MPM_RESERVED,  // undeterminstic value
                            // VX_CSR_MPM_RESERVED_H,  // undeterminstic value
                            VX_CSR_MINSTRET,
                        });

  writable_csrs_.insert(
      writable_csrs_.end(),
      {
          // write & read
          VX_CSR_SATP,
          VX_CSR_MSTATUS,
          VX_CSR_MNSTATUS,
          VX_CSR_MEDELEG,
          VX_CSR_MIDELEG,
          VX_CSR_MIE,
          VX_CSR_MTVEC,
          VX_CSR_MEPC,
          VX_CSR_PMPCFG0,
          VX_CSR_PMPADDR0,
          // VX_CSR_MSCRATCH, // shared by all the warps, undeterminstic value
          // VX_CSR_MCAUSE, // RTL didn't support yet
      });

#elif defined(ARCH_VENTUS)
  readonly_csrs_.insert(readonly_csrs_.end(), {
                                                  CSR_MVENDORID,
                                                  CSR_MARCHID,
                                                  CSR_MIMPID,
                                                  CSR_MHARTID,
                                                  CSR_MISA,

                                                  CSR_TID,
                                                  CSR_NUMW,
                                                  CSR_NUMT,
                                                  CSR_KNL,
                                                  CSR_WGID,
                                                  CSR_WID,
                                                  CSR_LDS,
                                                  CSR_PDS,
                                                  CSR_GIDX,
                                                  CSR_GIDY,
                                                  CSR_GIDZ,
                                                  CSR_PRINT,
                                                  // CSR_RPC,
                                              });

  writable_csrs_.insert(writable_csrs_.end(), {
                                                  // write & read
                                                  // CSR_PRINT,
                                                  // CSR_MSTATUS,
                                                  CSR_MIP,
                                                  CSR_MIE,
                                                  CSR_MCAUSE,
                                                  CSR_MEPC,
                                                  CSR_MTVAL,
                                              });

  // don't read the writable CSRs for now

#else
#warning TODO: CSR selection for other architectures
#endif

  readable_csrs_.insert(readable_csrs_.end(), readonly_csrs_.begin(),
                        readonly_csrs_.end());

  readable_csrs_.insert(readable_csrs_.begin(), writable_csrs_.begin(),
                        writable_csrs_.end());

  readable_csrs_dist_ =
      std::uniform_int_distribution<size_t>(0, readable_csrs_.size() - 1);
  writable_csrs_dist_ =
      std::uniform_int_distribution<size_t>(0, writable_csrs_.size() - 1);
}

void FuzzerState::set_instr_prob(const std::vector<double> &probs) {
  assert(probs.size() == registered_instr_.size());
  for (size_t i = 0; i < registered_instr_.size(); ++i) {
    const auto instr_name = registered_instr_[i];
    instr_prob_dist_[size_t(instr_name)] = probs[i];
  }

  instr_dist_ = std::discrete_distribution<unsigned>(instr_prob_dist_.begin(),
                                                     instr_prob_dist_.end());
}

const std::vector<difftest::ThreadLID> &
FuzzerState::shuffle_thread_lid(unsigned topk) {
  // top-k Fisher-Yates shuffe
  for (unsigned i = 0; i < topk; ++i) {
    std::uniform_int_distribution<unsigned> dist(
        i, shuffled_thread_lid_.size() - 1);
    unsigned j = dist(rng);
    std::swap(shuffled_thread_lid_[i], shuffled_thread_lid_[j]);
  }
  return shuffled_thread_lid_;
}

std::shared_ptr<riscv_meta> FuzzerState::pick_instr() {
  auto instr_name = instr_dist_(rng);
  return riscv_meta_list::instance().instr(riscv_instr_name_t(instr_name));
}

uint32_t FuzzerState::pick_csr(bool read, bool write) {
  if (write)
    return writable_csrs_[writable_csrs_dist_(rng)];
  else {
    assert(read);
    return readable_csrs_[readable_csrs_dist_(rng)];
  }
}

double FuzzerState::get_instr_prob(riscv_instr_name_t instr) const {
  return instr_prob_dist_[unsigned(instr)];
}

nlohmann::json FuzzerState::get_instr_freq() const {
  auto ret = nlohmann::json::array();
  for (size_t i = 0; i < registered_instr_.size(); ++i) {
    const auto instr_name = registered_instr_[i];
    ret.push_back(instr_freq_[size_t(instr_name)]);
  }
  return ret;
}

void FuzzerState::Stat::dump() {
  spdlog::info("Fuzzer Statistics:");
  spdlog::info("  Load/Store Instr Generation: accepted {}, rejected {}",
               accepted_load_store_generations,
               rejected_load_store_generations);
  spdlog::info(
      "    Rejected by Store ID Conflicts: {} ({:.2f} % of rejections)",
      rejected_store_id_conflicts,
      100.0 * rejected_store_id_conflicts /
          (rejected_load_store_generations + rejected_store_id_conflicts));
  spdlog::info("  Thread Divergence Preference: accepted {}, rejected {}",
               accepted_thread_divergence_preference,
               rejected_thread_divergence_preference);
  spdlog::info("  Branch Instr Generation: successful {}:\n\tthread divergence "
               "{}\n\t memalloc failed {}",
               succ_branch_generations, thread_divergence_branch_generations,
               memalloc_failed_branch_generations);
  spdlog::info("  Branch Target Allocation Failures: {}",
               branch_target_allocation_fail);
  spdlog::info("  Predicate Instr Generation: accepted {}, rejected {}",
               accepted_pred_generations, rejected_pred_generations);
  spdlog::info("  Number of generated barriers: {}", num_generated_barriers);
  spdlog::info("  Number of rejected barrier generations: {}",
               rejected_barrier_generations);
  for (const auto &[scope, bytes] : data_footprint_bytes) {
    spdlog::info("  Data Footprint ({}): {} bytes",
                 magic_enum::enum_name(scope), bytes);
  }
  spdlog::info("  Number of tainted load instructions: {}", tainted_load);
  spdlog::info("  Number of tainted store instructions: {}", tainted_store);
  spdlog::info("  Load/Store Operand Replay: attempts {}, immediate successes "
               "{}, eventual successes {}, hash collisions {} ({:.2f}%)",
               replay_attempts, replay_immediate_successes,
               replay_eventual_successes, replay_hash_collisions,
               replay_attempts > 0
                   ? 100.0 * replay_hash_collisions / replay_attempts
                   : 0.0);
  spdlog::info("  Times of CSR generations: accepted {}, rejected {}",
               accepted_csr_generations, rejected_csr_generations);
  spdlog::info("  Times of integer register refresh: {}", num_refresh_int_reg);
  spdlog::info("  Times of vector register refresh: {}",
               num_refresh_vector_reg);
}

} // namespace fuzzgpu
