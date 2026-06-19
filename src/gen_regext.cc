#if defined(ARCH_VENTUS)
#include "generator.hh"

namespace fuzzgpu {

bool Generator::need_regext_(const riscv_instr &instr) {
  // prefix instructions for FPR operands
  const bool regext_xd = (instr.meta()->operand_mask & riscv_meta::HAS_FD);
  const bool regext_xs1 = (instr.meta()->operand_mask & riscv_meta::HAS_FS1);
  const bool regext_xs2 = (instr.meta()->operand_mask & riscv_meta::HAS_FS2);
  const bool regext_xs3 = (instr.meta()->operand_mask & riscv_meta::HAS_FS3);
  return (regext_xd || regext_xs1 || regext_xs2 || regext_xs3);
}

std::optional<riscv_instr>
Generator::emit_regext_prefix_(const riscv_instr &suffix) {
  // prefix instructions for FPR operands
  const bool regext_xd = (suffix.meta()->operand_mask & riscv_meta::HAS_FD);
  const bool regext_xs1 = (suffix.meta()->operand_mask & riscv_meta::HAS_FS1);
  const bool regext_xs2 = (suffix.meta()->operand_mask & riscv_meta::HAS_FS2);
  const bool regext_xs3 = (suffix.meta()->operand_mask & riscv_meta::HAS_FS3);
  if (regext_xd || regext_xs1 || regext_xs2 || regext_xs3) {
    // need to emit a REGEXT instruction before the actual instruction
    riscv_instr regext_instr(
        riscv_meta_list::instance().instr(riscv_instr_name_t::REGEXT));
    uint32_t regext_imm = 0;
    if (regext_xd) {
      regext_imm |= (1 << 0);
    }
    if (regext_xs1) {
      regext_imm |= (1 << 3);
    }
    if (regext_xs2) {
      regext_imm |= (1 << 6);
    }
    if (regext_xs3) {
      regext_imm |= (1 << 9);
    }
    regext_instr.set_imm(regext_imm);
    return regext_instr;
  }
  return std::nullopt;
}

void Generator::gen_regext_(const std::shared_ptr<GenWarp> &gen_warp,
                            const riscv_instr &instr) {
  auto prefix = emit_regext_prefix_(instr);
  if (prefix) {
    gen_warp->scratchpad.emit(&*prefix);
  }
}

} // namespace fuzzgpu
#endif