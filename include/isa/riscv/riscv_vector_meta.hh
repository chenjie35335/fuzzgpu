#pragma once

#include "isa/riscv/riscv_meta.hh"
#include "isa/riscv/riscv_meta_list.hh"

namespace fuzzgpu {

/// @brief Metadata specialization for RISC-V vector instructions.
class riscv_vector_meta : public riscv_meta {
  void set_rand_mode_();

  void set_imm_len_();

public:
  /// @brief Construct vector metadata and initialize vector-specific fields.
  riscv_vector_meta(riscv_instr_name_t instr_name_,
                    riscv_instr_format_t format_,
                    riscv_instr_category_t category_,
                    riscv_instr_group_t group_, const riscv_gen &gen_)
    : riscv_meta(instr_name_, format_, category_, group_, gen_) {
    set_rand_mode_();
    set_imm_len_();
    set_is_vector();
  }

  /// @brief Randomize vector operands and optional vector memory fields.
  bool randomize(FuzzerState &fuzzer_state, GenWarp &warp, riscv_instr *out,
                 const std::vector<riscv_reg_t> &exclude_regs,
                 const std::vector<riscv_fpr_t> &exclude_fprs,
                 bool force_mutate,
                 uint64_t operand_filter = 0) override;

  /// @brief Visit all vector register field offsets used by @p instr.
  static bool
  foreach_vector_nfield_(const riscv_instr &instr,
                         const std::function<bool(unsigned reg_offset)> &f);

#if defined(ARCH_VORTEX) || defined(ARCH_VENTUS)
  ///< Maximum vector field count supported by current targets.
  static constexpr auto max_nfields = 8; // for ventus, vemul=1
#else
#error
#endif
};

} // namespace fuzzgpu
