#include "isa/riscv/riscv_floating_point_meta.hh"
#include "isa/riscv/ventus/csr.hh"
#include "isa/riscv/vortex/csr.hh"

#include "generator.hh"

namespace fuzzgpu {

bool Generator::gen_csr_(const std::shared_ptr<GenWarp> &gen_warp,
                         riscv_instr &instr) {

#if defined(ARCH_VENTUS)
  static const auto csr_frm = CSR_FRM;
  static const auto csr_fcsr = CSR_FCSR;
#elif defined(ARCH_VORTEX)
  static const auto csr_frm = VX_CSR_FRM;
  static const auto csr_fcsr = VX_CSR_FCSR;
#endif

  const uint16_t csr_addr = instr.csr();
  if (csr_addr == csr_frm || csr_addr == csr_fcsr) {
    // override the RM field with a random valid value
    const auto rand_rm = std::uniform_int_distribution<unsigned>(
        riscv_floating_point_meta::min_supported_instr_rm,
        riscv_floating_point_meta::max_supported_instr_rm)(state_.rng);

    const auto new_meta =
        riscv_meta_list::instance().instr(riscv_instr_name_t::CSRRWI);
    instr = riscv_instr(new_meta);
    instr.set_csr(csr_frm);
    instr.set_rd(riscv_reg_t::ZERO);
    instr.set_imm(rand_rm);

    spdlog::info("updated RM = {:#x}", rand_rm);
  }

  gen_warp->scratchpad.emit(&instr);
  state_.stat.accepted_csr_generations++;
  return true;
}

} // namespace fuzzgpu