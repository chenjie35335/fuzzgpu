#pragma once

#include "isa/riscv/riscv_meta.hh"
#include "isa/riscv/riscv_meta_list.hh"

namespace fuzzgpu {

/// @brief Metadata specialization for RISC-V atomic memory operations.
class riscv_amo_meta : public riscv_meta {
  void set_rand_mode_();

public:
  /// @brief Construct AMO metadata and initialize AMO-specific randomization.
  riscv_amo_meta(riscv_instr_name_t instr_name_, riscv_instr_format_t format_,
                 riscv_instr_category_t category_, riscv_instr_group_t group_,
                 const riscv_gen &gen_)
    : riscv_meta(instr_name_, format_, category_, group_, gen_) {
    set_rand_mode_();
  }

  /// @brief Randomize AMO operands, including ordering bits.
  bool randomize(FuzzerState &fuzzer_state, GenWarp &warp, riscv_instr *out,
                 const std::vector<riscv_reg_t> &exclude_regs,
                 const std::vector<riscv_fpr_t> &exclude_fprs,
                 bool force_mutate,
                 uint64_t operand_filter = 0) override;
};

} // namespace fuzzgpu
