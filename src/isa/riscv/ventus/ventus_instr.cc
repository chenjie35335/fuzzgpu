// ==========================================
// Auto-generated RISC-V Vector Instructions
// ==========================================
#include "isa/riscv/riscv_encode.hh"
#include "isa/riscv/riscv_meta_list.hh"

namespace fuzzgpu {

void riscv_meta_list::register_ventus() {

  DEFINE_VEC_INSTR(riscv_instr_name_t::VADD12_VI,
                   riscv_instr_format_t::I_VV_FORMAT,
                   riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RVV,
                   [](riscv_gen_I_VV, uint32_t vd, uint32_t vs1, int64_t imm) {
                     return instruc_itype(0b0001011, vd, 0b000, vs1, imm);
                   });

  DEFINE_VEC_INSTR(riscv_instr_name_t::VBEQ, riscv_instr_format_t::B_VV,
                   riscv_instr_category_t::BRANCH, riscv_instr_group_t::RVV,
                   [](riscv_gen_B_VV, uint32_t vs1, uint32_t vs2, int64_t imm) {
                     return instruc_btype(0b1011011, 0b000, vs1, vs2, imm);
                   });

  DEFINE_VEC_INSTR(riscv_instr_name_t::VBGE, riscv_instr_format_t::B_VV,
                   riscv_instr_category_t::BRANCH, riscv_instr_group_t::RVV,
                   [](riscv_gen_B_VV, uint32_t vs1, uint32_t vs2, int64_t imm) {
                     return instruc_btype(0b1011011, 0b101, vs1, vs2, imm);
                   });

  DEFINE_VEC_INSTR(riscv_instr_name_t::VBGEU, riscv_instr_format_t::B_VV,
                   riscv_instr_category_t::BRANCH, riscv_instr_group_t::RVV,
                   [](riscv_gen_B_VV, uint32_t vs1, uint32_t vs2, int64_t imm) {
                     return instruc_btype(0b1011011, 0b111, vs1, vs2, imm);
                   });

  DEFINE_VEC_INSTR(riscv_instr_name_t::VBLT, riscv_instr_format_t::B_VV,
                   riscv_instr_category_t::BRANCH, riscv_instr_group_t::RVV,
                   [](riscv_gen_B_VV, uint32_t vs1, uint32_t vs2, int64_t imm) {
                     return instruc_btype(0b1011011, 0b100, vs1, vs2, imm);
                   });

  DEFINE_VEC_INSTR(riscv_instr_name_t::VBLTU, riscv_instr_format_t::B_VV,
                   riscv_instr_category_t::BRANCH, riscv_instr_group_t::RVV,
                   [](riscv_gen_B_VV, uint32_t vs1, uint32_t vs2, int64_t imm) {
                     return instruc_btype(0b1011011, 0b110, vs1, vs2, imm);
                   });

  DEFINE_VEC_INSTR(riscv_instr_name_t::VBNE, riscv_instr_format_t::B_VV,
                   riscv_instr_category_t::BRANCH, riscv_instr_group_t::RVV,
                   [](riscv_gen_B_VV, uint32_t vs1, uint32_t vs2, int64_t imm) {
                     return instruc_btype(0b1011011, 0b001, vs1, vs2, imm);
                   });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VFEXP_V, riscv_instr_format_t::R_VVV,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VVV, uint32_t vd, uint32_t vs1, uint32_t vs2, bool vm) {
        assert(vs1 == uint32_t(-1));
        return instruc_rtype(0b0001011, vd, 0b110, 0b00000, vs2,
                             0b0000100 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VFTTA_VV, riscv_instr_format_t::R_VVV,
      riscv_instr_category_t::TENSOR, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VVV, uint32_t vd, uint32_t vs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b0001011, vd, 0b100, vs1, vs2, 0b0000110 | vm);
      });

  DEFINE_VEC_INSTR(riscv_instr_name_t::VLB12_V,
                   riscv_instr_format_t::I_VV_FORMAT,
                   riscv_instr_category_t::LOAD, riscv_instr_group_t::RVV,
                   [](riscv_gen_I_VV, uint32_t vd, uint32_t vs1, int64_t imm) {
                     return instruc_itype(0b1111011, vd, 0b000, vs1, imm);
                   });

  DEFINE_VEC_INSTR(riscv_instr_name_t::VLB_V, riscv_instr_format_t::I_VX_FORMAT,
                   riscv_instr_category_t::LOAD, riscv_instr_group_t::RVV,
                   [](riscv_gen_I_VX, uint32_t vd, uint32_t rs1, int64_t imm) {
                     return instruc_itype_funct1(0b0101011, vd, 0b000, rs1, imm,
                                                 0);
                   });

  DEFINE_VEC_INSTR(riscv_instr_name_t::VLBU12_V,
                   riscv_instr_format_t::I_VV_FORMAT,
                   riscv_instr_category_t::LOAD, riscv_instr_group_t::RVV,
                   [](riscv_gen_I_VV, uint32_t vd, uint32_t vs1, int64_t imm) {
                     return instruc_itype(0b1111011, vd, 0b100, vs1, imm);
                   });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VLBU_V, riscv_instr_format_t::I_VX_FORMAT,
      riscv_instr_category_t::LOAD, riscv_instr_group_t::RVV,
      [](riscv_gen_I_VX, uint32_t vd, uint32_t rs1, int64_t imm) {
        return instruc_itype_funct1(0b0101011, vd, 0b100, rs1, imm, 0);
      });

  DEFINE_VEC_INSTR(riscv_instr_name_t::VLH12_V,
                   riscv_instr_format_t::I_VV_FORMAT,
                   riscv_instr_category_t::LOAD, riscv_instr_group_t::RVV,
                   [](riscv_gen_I_VV, uint32_t vd, uint32_t vs1, int64_t imm) {
                     return instruc_itype(0b1111011, vd, 0b001, vs1, imm);
                   });

  DEFINE_VEC_INSTR(riscv_instr_name_t::VLH_V, riscv_instr_format_t::I_VX_FORMAT,
                   riscv_instr_category_t::LOAD, riscv_instr_group_t::RVV,
                   [](riscv_gen_I_VX, uint32_t vd, uint32_t rs1, int64_t imm) {
                     return instruc_itype_funct1(0b0101011, vd, 0b001, rs1, imm,
                                                 0);
                   });

  DEFINE_VEC_INSTR(riscv_instr_name_t::VLHU12_V,
                   riscv_instr_format_t::I_VV_FORMAT,
                   riscv_instr_category_t::LOAD, riscv_instr_group_t::RVV,
                   [](riscv_gen_I_VV, uint32_t vd, uint32_t vs1, int64_t imm) {
                     return instruc_itype(0b1111011, vd, 0b101, vs1, imm);
                   });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VLHU_V, riscv_instr_format_t::I_VX_FORMAT,
      riscv_instr_category_t::LOAD, riscv_instr_group_t::RVV,
      [](riscv_gen_I_VX, uint32_t vd, uint32_t rs1, int64_t imm) {
        return instruc_itype_funct1(0b0101011, vd, 0b101, rs1, imm, 0);
      });

  DEFINE_VEC_INSTR(riscv_instr_name_t::VLW12_V,
                   riscv_instr_format_t::I_VV_FORMAT,
                   riscv_instr_category_t::LOAD, riscv_instr_group_t::RVV,
                   [](riscv_gen_I_VV, uint32_t vd, uint32_t vs1, int64_t imm) {
                     return instruc_itype(0b1111011, vd, 0b010, vs1, imm);
                   });

  DEFINE_VEC_INSTR(riscv_instr_name_t::VLW_V, riscv_instr_format_t::I_VX_FORMAT,
                   riscv_instr_category_t::LOAD, riscv_instr_group_t::RVV,
                   [](riscv_gen_I_VX, uint32_t vd, uint32_t rs1, int64_t imm) {
                     return instruc_itype_funct1(0b0101011, vd, 0b010, rs1, imm,
                                                 0);
                   });

  DEFINE_VEC_INSTR(riscv_instr_name_t::VSB12_V, riscv_instr_format_t::S_VV,
                   riscv_instr_category_t::STORE, riscv_instr_group_t::RVV,
                   [](riscv_gen_S_VV, uint32_t vs1, uint32_t vs2, int64_t imm) {
                     return instruc_stype(0b1111011, 0b111, vs1, vs2, imm);
                   });

  DEFINE_VEC_INSTR(riscv_instr_name_t::VSB_V, riscv_instr_format_t::S_XV,
                   riscv_instr_category_t::STORE, riscv_instr_group_t::RVV,
                   [](riscv_gen_S_XV, uint32_t rs1, uint32_t rs2, int64_t imm) {
                     return instruc_stype_funct1(0b0101011, 0b000, rs1, rs2,
                                                 imm, 1);
                   });

  DEFINE_VEC_INSTR(riscv_instr_name_t::VSH12_V, riscv_instr_format_t::S_VV,
                   riscv_instr_category_t::STORE, riscv_instr_group_t::RVV,
                   [](riscv_gen_S_VV, uint32_t vs1, uint32_t vs2, int64_t imm) {
                     return instruc_stype(0b1111011, 0b011, vs1, vs2, imm);
                   });

  DEFINE_VEC_INSTR(riscv_instr_name_t::VSH_V, riscv_instr_format_t::S_XV,
                   riscv_instr_category_t::STORE, riscv_instr_group_t::RVV,
                   [](riscv_gen_S_XV, uint32_t rs1, uint32_t rs2, int64_t imm) {
                     return instruc_stype_funct1(0b0101011, 0b001, rs1, rs2,
                                                 imm, 1);
                   });

  DEFINE_VEC_INSTR(riscv_instr_name_t::VSUB12_VI,
                   riscv_instr_format_t::I_VV_FORMAT,
                   riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RVV,
                   [](riscv_gen_I_VV, uint32_t vd, uint32_t vs1, int64_t imm) {
                     return instruc_itype(0b0001011, vd, 0b001, vs1, imm);
                   });

  DEFINE_VEC_INSTR(riscv_instr_name_t::VSW12_V, riscv_instr_format_t::S_VV,
                   riscv_instr_category_t::STORE, riscv_instr_group_t::RVV,
                   [](riscv_gen_S_VV, uint32_t vs1, uint32_t vs2, int64_t imm) {
                     return instruc_stype(0b1111011, 0b110, vs1, vs2, imm);
                   });

  DEFINE_VEC_INSTR(riscv_instr_name_t::VSW_V, riscv_instr_format_t::S_XV,
                   riscv_instr_category_t::STORE, riscv_instr_group_t::RVV,
                   [](riscv_gen_S_XV, uint32_t rs1, uint32_t rs2, int64_t imm) {
                     return instruc_stype_funct1(0b0101011, 0b010, rs1, rs2,
                                                 imm, 1);
                   });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::JOIN, riscv_instr_format_t::B_VV,
      riscv_instr_category_t::THREAD_CTRL, riscv_instr_group_t::RVV,
      [](riscv_gen_B_VV, uint32_t vs1, uint32_t vs2, int64_t imm) {
        assert(vs1 == uint32_t(-1));
        assert(vs2 == uint32_t(-1));
        assert(imm == int64_t(-1));
        imm = 0;
        return instruc_btype(0b1011011, 0b010, 0b00000, 0b00000, imm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::BARRIER, riscv_instr_format_t::I_UIMM_CSR_FORMAT,
      riscv_instr_category_t::THREAD_CTRL, riscv_instr_group_t::RVV,
      [](riscv_gen_I_UIMM_CSR, uint32_t rd, uint32_t uimm, uint32_t csr) {
        assert(rd == uint32_t(-1));
        assert(uimm == uint32_t(-1));
        assert(csr == uint32_t(-1));
        csr = 0;
        csr |= 0b000001000000;
        return instruc_itype(0b0001011, 0b00000, 0b100, 0b00000, csr);
      });

  DEFINE_VEC_INSTR(riscv_instr_name_t::ENDPRG, riscv_instr_format_t::R_FORMAT,
                   riscv_instr_category_t::THREAD_CTRL,
                   riscv_instr_group_t::RVV,
                   [](riscv_gen_R, uint32_t rd, uint32_t rs1, uint32_t rs2) {
                     assert(rd == uint32_t(-1));
                     assert(rs1 == uint32_t(-1));
                     assert(rs2 == uint32_t(-1));
                     return instruc_rtype(0b0001011, 0b00000, 0b100, 0b00000,
                                          0b00000, 0b0000000);
                   });

  DEFINE_VEC_INSTR(riscv_instr_name_t::REGEXT, riscv_instr_format_t::I_FORMAT,
                   riscv_instr_category_t::REG_CTRL, riscv_instr_group_t::RVV,
                   [](riscv_gen_I, uint32_t rd, uint32_t rs1, int64_t imm) {
                     assert(rd == uint32_t(-1));
                     assert(rs1 == uint32_t(-1));
                     return instruc_itype(0b0001011, 0b00000, 0b010, 0b00000,
                                          imm);
                   });

  DEFINE_VEC_INSTR(riscv_instr_name_t::SETRPC, riscv_instr_format_t::I_FORMAT,
                   riscv_instr_category_t::THREAD_CTRL,
                   riscv_instr_group_t::RVV,
                   [](riscv_gen_I, uint32_t rd, uint32_t rs1, int64_t imm) {
                     assert(rd == uint32_t(-1));
                     return instruc_itype(0b1011011, 0b00000, 0b011, rs1, imm);
                   });
}
} // namespace fuzzgpu
