#pragma once

#include <random>
#include <vector>

#include "difftest_gpu/types.hh"
#include "isa/riscv/riscv_meta_list.hh"
#include "types.hh"

namespace fuzzgpu {

/// @brief Samples source and destination registers under mask constraints.
class RegisterSampler {

  ///< PRNG shared with the fuzzer state.
  std::mt19937_64 &rng_;
  ///< Number of architectural registers represented by this sampler.
  unsigned nr_regs_;
  ///< Encoded null/zero register.
  unsigned null_reg_;
  ///< Maximum vector field grouping used by vector registers.
  unsigned max_nfields_;

  ///< Base probability weights for destination register selection.
  std::vector<double> rd_reg_weight_;
  ///< Mutating probability weights for destination register selection.
  std::vector<double> rd_reg_mutate_weight_;

  ///< Per-register masks for threads that cannot read the register.
  std::vector<difftest::ThreadMask> reg_masked_;
  ///< Per-register masks for threads with non-deterministic values.
  std::vector<difftest::ThreadMask> reg_nodet_;

  ///< Precomputed destination distribution without constraints.
  std::discrete_distribution<unsigned> rd_dist_;
  ///< Precomputed mutated destination distribution without constraints.
  std::discrete_distribution<unsigned> rd_mutate_dist_;

  ///< Scratch indices for constrained sampling.
  mutable std::vector<unsigned> valid_indices_;
  ///< Scratch weights for constrained sampling.
  mutable std::vector<double> valid_weights_;

  void rebuild_rd_dist_();

public:
  /// @brief Construct a sampler for one register file.
  RegisterSampler(std::mt19937_64 &rng, unsigned nr_regs, unsigned null_reg,
                  unsigned max_nfields = 0);

  /// @brief Reset register masks and probability state.
  void reset();

  /// @brief Adjust register grouping for an architecture register extension.
  void set_reg_ext(unsigned reg_ext);

  /// @brief Sample an encoded source register.
  GPREncode sample_rs_enc(bool cti, const difftest::ThreadMask &tmask,
                          bool force_mutate);
  /// @brief Sample an encoded destination register.
  GPREncode sample_rd_enc(bool force_mutate);
  /// @brief Return whether @p rs_enc can be read under @p tmask.
  bool is_rs_pickable(GPREncode rs_enc, const difftest::ThreadMask &tmask,
                      bool cti) const;
  /// @brief Mark a destination register as written by active threads.
  void touch_rd_enc(GPREncode rd_enc, const difftest::ThreadMask &tmask);
  /// @brief Mark a register as holding non-deterministic values.
  void mark_reg_nodet(GPREncode rd_enc, const difftest::ThreadMask &tmask);
  /// @brief Mask a register from future reads by active threads.
  void mask_reg(GPREncode rd_enc, const difftest::ThreadMask &tmask);
  /// @brief Return whether the register is non-deterministic under @p tmask.
  bool is_reg_nodet(GPREncode rd_enc, const difftest::ThreadMask &tmask) const;

  /// @brief Return true if constraints leave no source register available.
  bool no_available(const difftest::ThreadMask &tmask, bool cti = true) const;
};

} // namespace fuzzgpu
