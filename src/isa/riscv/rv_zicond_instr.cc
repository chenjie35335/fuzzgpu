
#include "isa/riscv/riscv_encode.hh"
#include "isa/riscv/riscv_meta_list.hh"

namespace fuzzgpu {

static const uint32_t ZICOND_OPCODE_COND = 0b0110011;

void riscv_meta_list::register_zicond() {
  DEFINE_INSTR(riscv_instr_name_t::CZERO_EQZ, riscv_instr_format_t::R_FORMAT,
               riscv_instr_category_t::COMPARE, riscv_instr_group_t::ZICOND,
               [](riscv_gen_R, uint32_t rd, uint32_t rs1, uint32_t rs2) {
                 return instruc_rtype(ZICOND_OPCODE_COND, rd, 0b101, rs1, rs2,
                                      0b1111);
               });

  DEFINE_INSTR(riscv_instr_name_t::CZERO_NEZ, riscv_instr_format_t::R_FORMAT,
               riscv_instr_category_t::COMPARE, riscv_instr_group_t::ZICOND,
               [](riscv_gen_R, uint32_t rd, uint32_t rs1, uint32_t rs2) {
                 return instruc_rtype(ZICOND_OPCODE_COND, rd, 0b111, rs1, rs2,
                                      0b1111);
               });
}
} // namespace fuzzgpu