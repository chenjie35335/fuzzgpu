#include "fuzzer_state.hh"
#include "isa/riscv/riscv_instr.hh"

#include "isa/riscv/riscv_amo_meta.hh"

namespace fuzzgpu {

void riscv_amo_meta::set_rand_mode_() {
  assert(format == riscv_instr_format_t::R_ORDERING_FORMAT);
  assert(group == riscv_instr_group_t::RV32A ||
         group == riscv_instr_group_t::RV64A);
  if (instr_name == riscv_instr_name_t::LR_W ||
      instr_name == riscv_instr_name_t::LR_D) {
    operand_mask = (HAS_RD | HAS_ORDERING | HAS_RS1);
  } else {
    operand_mask = (HAS_RD | HAS_ORDERING | HAS_RS2 | HAS_RS1);
  }
}

bool riscv_amo_meta::randomize(FuzzerState &fuzzer_state, GenWarp &warp,
                               riscv_instr *out,
                               const std::vector<riscv_reg_t> &exclude_regs,
                               const std::vector<riscv_fpr_t> &exclude_fprs,
                               bool force_mutate,
                               uint64_t operand_filter) {
  riscv_meta::check_operand_mask();
  bool ret = riscv_meta::randomize(fuzzer_state, warp, out, exclude_regs,
                                   exclude_fprs, force_mutate, operand_filter);

  // Only randomize ordering if not filtering or if HAS_ORDERING is in filter
  if (!operand_filter || (operand_filter & HAS_ORDERING)) {
    auto s = fuzzer_state.aq_rl_dist(fuzzer_state.rng);
    assert(s <= 2);
    out->set_ordering(s & 1, s >> 1);
  }
  return ret;
}

} // namespace fuzzgpu