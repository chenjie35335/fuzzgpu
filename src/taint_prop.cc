#include "gen_warp.hh"
#include "isa/riscv/riscv_instr.hh"
#include "isa/riscv/riscv_meta.hh"
#include "isa/riscv/riscv_vector_meta.hh"

#include "taint_prop.hh"

namespace fuzzgpu {

bool Taint::propagate(const GenWarp &gen_warp_, const riscv_instr &instr,
                      bool taint) {
  // taint propagation from source operands
  if (instr.meta()->operand_mask & riscv_meta::HAS_RS1)
    taint |= gen_warp_.is_nodet_int_reg(instr.rs1());
  if (instr.meta()->operand_mask & riscv_meta::HAS_RS2)
    taint |= gen_warp_.is_nodet_int_reg(instr.rs2());

  // these integer instructions, such as vmv.x.s, may move from FPR
  taint |= (instr.meta()->operand_mask &
            (riscv_meta::HAS_FS1 | riscv_meta::HAS_FS2));

  if (instr.meta()->operand_mask & riscv_meta::HAS_VS1)
    taint |= gen_warp_.is_nodet_vec_reg(instr.vs1());
  if (instr.meta()->operand_mask & riscv_meta::HAS_VS2)
    taint |= gen_warp_.is_nodet_vec_reg(instr.vs2());
  if (instr.meta()->operand_mask & riscv_meta::HAS_VS3) {
    riscv_vector_meta::foreach_vector_nfield_(instr, [&](unsigned reg_offset) {
      taint |= gen_warp_.is_nodet_vec_reg(static_cast<riscv_vreg_t>(
          static_cast<unsigned>(instr.vs3()) + reg_offset));
      return true;
    });
  }
  if ((instr.meta()->operand_mask & riscv_meta::HAS_VD) &&
      instr.meta()->read_vd()) {
    riscv_vector_meta::foreach_vector_nfield_(instr, [&](unsigned reg_offset) {
      taint |= gen_warp_.is_nodet_vec_reg(static_cast<riscv_vreg_t>(
          static_cast<unsigned>(instr.vd()) + reg_offset));
      return true;
    });
  }
  if (instr.meta()->operand_mask & riscv_meta::HAS_VM &&
      (!instr.vm() || instr.meta()->vm_hardcoded_0())) {
    taint |= gen_warp_.is_nodet_vec_reg(riscv_vreg_t::V0); // vm mask reg
  }
  if (instr.meta()->category == riscv_instr_category_t::CSR) {
    taint |= true; // TODO: more fine-grained tracking for CSRs
  }
  return taint;
}

/**
 * See also:
 *    scratchpad.cc
 *    gen_load_store.cc
 */

} // namespace fuzzgpu