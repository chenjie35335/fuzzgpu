#include "config.hh"
#include "isa/riscv/riscv_meta.hh"
#include "isa/riscv/riscv_meta_list.hh"
#include <numeric>
#include <random>

#include "register_sampler.hh"

namespace fuzzgpu {

RegisterSampler::RegisterSampler(std::mt19937_64 &rng, unsigned nr_regs,
                                 unsigned null_reg, unsigned max_nfields)
  : rng_(rng)
  , nr_regs_(nr_regs)
  , null_reg_(null_reg)
  , max_nfields_(max_nfields)
  , rd_reg_weight_(nr_regs)
  , rd_reg_mutate_weight_(nr_regs)
  , reg_masked_(nr_regs,
                difftest::ThreadMask(
                    Config::instance().arch.nr_threads_per_warp, false))
  , reg_nodet_(nr_regs + max_nfields,
               difftest::ThreadMask(Config::instance().arch.nr_threads_per_warp,
                                    false)) {
  valid_indices_.reserve(nr_regs);
  valid_weights_.reserve(nr_regs);
  reset();
}

void RegisterSampler::reset() {
  const double uniform_weight = 1.0 / nr_regs_;
  std::fill(rd_reg_weight_.begin(), rd_reg_weight_.end(), uniform_weight);
  std::fill(rd_reg_mutate_weight_.begin(), rd_reg_mutate_weight_.end(),
            uniform_weight);

  const difftest::ThreadMask empty_mask(
      Config::instance().arch.nr_threads_per_warp, false);
  std::fill(reg_masked_.begin(), reg_masked_.end(), empty_mask);
  std::fill(reg_nodet_.begin(), reg_nodet_.end(), empty_mask);

  rebuild_rd_dist_();
}

void RegisterSampler::set_reg_ext(unsigned /*reg_ext*/) {
  // reg_ext is no longer used in the simplified design
  // Kept for API compatibility
}

void RegisterSampler::rebuild_rd_dist_() {
  rd_dist_ = std::discrete_distribution<unsigned>(rd_reg_weight_.begin(),
                                                  rd_reg_weight_.end());
  rd_mutate_dist_ = std::discrete_distribution<unsigned>(
      rd_reg_mutate_weight_.begin(), rd_reg_mutate_weight_.end());
}

GPREncode RegisterSampler::sample_rs_enc(bool cti,
                                         const difftest::ThreadMask &tmask,
                                         bool force_mutate) {
  assert(nr_regs_ > 0 && "no registers");

  // Select weight source based on force_mutate
  const bool use_mutate =
      force_mutate || std::bernoulli_distribution(
                          Config::instance().reg_pick_mutate_prob)(rng_);
  const auto &weights = use_mutate ? rd_reg_mutate_weight_ : rd_reg_weight_;

  // Build list of valid registers and their weights
  valid_indices_.clear();
  valid_weights_.clear();

  for (unsigned r = 0; r < nr_regs_; ++r) {
    if (!is_rs_pickable(GPREncode(r), tmask, cti)) {
      continue;
    }

    // Register is valid, add to candidates
    if (weights[r] > 0.0) {
      valid_indices_.push_back(r);
      valid_weights_.push_back(weights[r]);
      if (force_mutate)
      spdlog::info("rs candidate reg {} weight {}", r, weights[r]);
    }
  }

  // No valid registers available
  if (valid_indices_.empty()) {
    return null_reg_;
  }

  // Sample from valid registers using their weights
  std::discrete_distribution<size_t> dist(valid_weights_.begin(),
                                          valid_weights_.end());
  const size_t idx = dist(rng_);
  const GPREncode result = valid_indices_[idx];
  return result;
}

GPREncode RegisterSampler::sample_rd_enc(bool force_mutate) {
  assert(nr_regs_ > 0 && "no registers");

  // rd sampling has no constraints, use pre-computed distribution
  const bool use_mutate =
      force_mutate || std::bernoulli_distribution(
                          Config::instance().reg_pick_mutate_prob)(rng_);

  unsigned rd = use_mutate ? rd_mutate_dist_(rng_) : rd_dist_(rng_);
  return static_cast<GPREncode>(rd);
}

bool RegisterSampler::is_rs_pickable(GPREncode rs_enc,
                                     const difftest::ThreadMask &tmask,
                                     bool cti) const {
  if (rs_enc == null_reg_) {
    return true;
  }
  if (rs_enc >= nr_regs_) {
    return false;
  }

  // Hard constraint 1: register must not be masked for any thread in tmask
  if ((reg_masked_[rs_enc] & tmask).any())
    return false;

  // Hard constraint 2 (cti only): reg_nodet_ & tmask must be zero
  if (cti && (reg_nodet_[rs_enc] & tmask).any())
    return false;

  return true;
}

void RegisterSampler::touch_rd_enc(GPREncode rd_enc,
                                   const difftest::ThreadMask &tmask) {
  assert(nr_regs_ > 0 && "no registers");
  if (rd_enc >= nr_regs_)
    return;

  // Update reg weights using cascade heuristic
  // This creates more RAW and WAW dependencies
  const double protuberance = Config::instance().REGPICK_PROTUBERANCE_RATIO;
  const double sum_of_others =
      std::accumulate(rd_reg_weight_.begin(), rd_reg_weight_.end(), 0.0) -
      rd_reg_weight_[rd_enc];

  if (sum_of_others > 0.0) {
    const double scale_factor = (1.0 - protuberance) / sum_of_others;
    for (unsigned r = 0; r < nr_regs_; ++r) {
      rd_reg_weight_[r] *= scale_factor;
    }
  }
  rd_reg_weight_[rd_enc] = protuberance;

  // Clear nodet and masked flags for the specified threads
  reg_nodet_[rd_enc] &= ~tmask;
  reg_masked_[rd_enc] &= ~tmask;
  spdlog::debug("untaint {:#x} rd {} cleared tmask {} -> {}", uintptr_t(this), rd_enc,
                tmask, reg_nodet_[rd_enc]);

  rebuild_rd_dist_();
}

void RegisterSampler::mark_reg_nodet(GPREncode rd_enc,
                                     const difftest::ThreadMask &tmask) {
  if (rd_enc == null_reg_ || rd_enc >= reg_nodet_.size())
    return;

  reg_nodet_[rd_enc] |= tmask;
  spdlog::debug("taint {:#x} rd {} {} -> nodet={}", uintptr_t(this), rd_enc, tmask, reg_nodet_[rd_enc]);
}

void RegisterSampler::mask_reg(GPREncode rd_enc,
                               const difftest::ThreadMask &tmask) {
  if (rd_enc == null_reg_ || rd_enc >= nr_regs_)
    return;

  reg_masked_[rd_enc] |= tmask;
}

bool RegisterSampler::is_reg_nodet(GPREncode rd_enc,
                                   const difftest::ThreadMask &tmask) const {
  if (rd_enc >= reg_nodet_.size())
    return false;

  return (reg_nodet_[rd_enc] & tmask).any();
}

bool RegisterSampler::no_available(const difftest::ThreadMask &tmask,
                                   bool cti) const {
  for (unsigned r = 0; r < nr_regs_; ++r) {
    if (is_rs_pickable(GPREncode(r), tmask, cti))
      return false;
  }
  return true;
}

} // namespace fuzzgpu