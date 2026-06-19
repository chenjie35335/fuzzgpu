// ==========================================
// Auto-generated RISC-V Vector Instructions
// Changelog:
//  2026/1/4 Manutally edited to add bug filters, merge it after regenerating!!!
// ==========================================
#include "bug_filter.hh"
#include "isa/riscv/riscv_encode.hh"
#include "isa/riscv/riscv_meta_list.hh"

namespace fuzzgpu {

void riscv_meta_list::register_rvv() {

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VADD_VI, riscv_instr_format_t::R_VIV,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VIV, uint32_t vd, int64_t imm, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b011, imm & 0b11111, vs2,
                             0b0000000 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VADD_VV, riscv_instr_format_t::R_VVV,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VVV, uint32_t vd, uint32_t vs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b000, vs1, vs2, 0b0000000 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VADD_VX, riscv_instr_format_t::R_VXV,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VXV, uint32_t vd, uint32_t rs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b100, rs1, vs2, 0b0000000 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VAND_VI, riscv_instr_format_t::R_VIV,
      riscv_instr_category_t::LOGICAL, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VIV, uint32_t vd, int64_t imm, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b011, imm & 0b11111, vs2,
                             0b0010010 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VAND_VV, riscv_instr_format_t::R_VVV,
      riscv_instr_category_t::LOGICAL, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VVV, uint32_t vd, uint32_t vs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b000, vs1, vs2, 0b0010010 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VAND_VX, riscv_instr_format_t::R_VXV,
      riscv_instr_category_t::LOGICAL, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VXV, uint32_t vd, uint32_t rs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b100, rs1, vs2, 0b0010010 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VDIV_VV, riscv_instr_format_t::R_VVV,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VVV, uint32_t vd, uint32_t vs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b010, vs1, vs2, 0b1000010 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VDIV_VX, riscv_instr_format_t::R_VXV,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VXV, uint32_t vd, uint32_t rs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b110, rs1, vs2, 0b1000010 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VDIVU_VV, riscv_instr_format_t::R_VVV,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VVV, uint32_t vd, uint32_t vs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b010, vs1, vs2, 0b1000000 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VDIVU_VX, riscv_instr_format_t::R_VXV,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VXV, uint32_t vd, uint32_t rs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b110, rs1, vs2, 0b1000000 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VFADD_VF, riscv_instr_format_t::R_VXV,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VXV, uint32_t vd, uint32_t rs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b101, rs1, vs2, 0b0000000 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VFADD_VV, riscv_instr_format_t::R_VVV,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VVV, uint32_t vd, uint32_t vs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b001, vs1, vs2, 0b0000000 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VFCLASS_V, riscv_instr_format_t::R_VVV,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VVV, uint32_t vd, uint32_t vs1, uint32_t vs2, bool vm) {
        assert(vs1 == uint32_t(-1));
        return instruc_rtype(0b1010111, vd, 0b001, 0b10000, vs2,
                             0b0100110 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VFCVT_F_X_V, riscv_instr_format_t::R_VVV,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VVV, uint32_t vd, uint32_t vs1, uint32_t vs2, bool vm) {
        assert(vs1 == uint32_t(-1));
        return instruc_rtype(0b1010111, vd, 0b001, 0b00011, vs2,
                             0b0100100 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VFCVT_F_XU_V, riscv_instr_format_t::R_VVV,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VVV, uint32_t vd, uint32_t vs1, uint32_t vs2, bool vm) {
        assert(vs1 == uint32_t(-1));
        return instruc_rtype(0b1010111, vd, 0b001, 0b00010, vs2,
                             0b0100100 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VFCVT_RTZ_X_F_V, riscv_instr_format_t::R_VVV,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VVV, uint32_t vd, uint32_t vs1, uint32_t vs2, bool vm) {
        assert(vs1 == uint32_t(-1));
        return instruc_rtype(0b1010111, vd, 0b001, 0b00111, vs2,
                             0b0100100 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VFCVT_RTZ_XU_F_V, riscv_instr_format_t::R_VVV,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VVV, uint32_t vd, uint32_t vs1, uint32_t vs2, bool vm) {
        assert(vs1 == uint32_t(-1));
        return instruc_rtype(0b1010111, vd, 0b001, 0b00110, vs2,
                             0b0100100 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VFCVT_X_F_V, riscv_instr_format_t::R_VVV,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VVV, uint32_t vd, uint32_t vs1, uint32_t vs2, bool vm) {
        assert(vs1 == uint32_t(-1));
        return instruc_rtype(0b1010111, vd, 0b001, 0b00001, vs2,
                             0b0100100 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VFCVT_XU_F_V, riscv_instr_format_t::R_VVV,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VVV, uint32_t vd, uint32_t vs1, uint32_t vs2, bool vm) {
        assert(vs1 == uint32_t(-1));
        return instruc_rtype(0b1010111, vd, 0b001, 0b00000, vs2,
                             0b0100100 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VFDIV_VF, riscv_instr_format_t::R_VXV,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VXV, uint32_t vd, uint32_t rs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b101, rs1, vs2, 0b1000000 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VFDIV_VV, riscv_instr_format_t::R_VVV,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VVV, uint32_t vd, uint32_t vs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b001, vs1, vs2, 0b1000000 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VFMACC_VF, riscv_instr_format_t::R_VXV,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VXV, uint32_t vd, uint32_t rs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b101, rs1, vs2, 0b1011000 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VFMACC_VV, riscv_instr_format_t::R_VVV,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VVV, uint32_t vd, uint32_t vs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b001, vs1, vs2, 0b1011000 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VFMADD_VF, riscv_instr_format_t::R_VXV,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VXV, uint32_t vd, uint32_t rs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b101, rs1, vs2, 0b1010000 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VFMADD_VV, riscv_instr_format_t::R_VVV,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VVV, uint32_t vd, uint32_t vs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b001, vs1, vs2, 0b1010000 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VFMAX_VF, riscv_instr_format_t::R_VXV,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VXV, uint32_t vd, uint32_t rs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b101, rs1, vs2, 0b0001100 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VFMAX_VV, riscv_instr_format_t::R_VVV,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VVV, uint32_t vd, uint32_t vs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b001, vs1, vs2, 0b0001100 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VFMIN_VF, riscv_instr_format_t::R_VXV,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VXV, uint32_t vd, uint32_t rs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b101, rs1, vs2, 0b0001000 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VFMIN_VV, riscv_instr_format_t::R_VVV,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VVV, uint32_t vd, uint32_t vs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b001, vs1, vs2, 0b0001000 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VFMSAC_VF, riscv_instr_format_t::R_VXV,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VXV, uint32_t vd, uint32_t rs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b101, rs1, vs2, 0b1011100 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VFMSAC_VV, riscv_instr_format_t::R_VVV,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VVV, uint32_t vd, uint32_t vs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b001, vs1, vs2, 0b1011100 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VFMSUB_VF, riscv_instr_format_t::R_VXV,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VXV, uint32_t vd, uint32_t rs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b101, rs1, vs2, 0b1010100 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VFMSUB_VV, riscv_instr_format_t::R_VVV,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VVV, uint32_t vd, uint32_t vs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b001, vs1, vs2, 0b1010100 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VFMUL_VF, riscv_instr_format_t::R_VXV,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VXV, uint32_t vd, uint32_t rs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b101, rs1, vs2, 0b1001000 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VFMUL_VV, riscv_instr_format_t::R_VVV,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VVV, uint32_t vd, uint32_t vs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b001, vs1, vs2, 0b1001000 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VFMV_F_S, riscv_instr_format_t::R_XXV,
      riscv_instr_category_t::THREAD_MERGE, riscv_instr_group_t::RVV,
      [](riscv_gen_R_XXV, uint32_t rd, uint32_t rs1, uint32_t vs2, bool vm) {
        assert(rs1 == uint32_t(-1));
        return instruc_rtype(0b1010111, rd, 0b001, 0b00000, vs2, 0b0100001);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VFMV_S_F, riscv_instr_format_t::R_VXV,
      riscv_instr_category_t::THREAD_MERGE, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VXV, uint32_t vd, uint32_t rs1, uint32_t vs2, bool vm) {
        assert(vs2 == uint32_t(-1));
        return instruc_rtype(0b1010111, vd, 0b101, rs1, 0b00000, 0b0100001);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VFMV_V_F, riscv_instr_format_t::R_VXV,
      riscv_instr_category_t::THREAD_MERGE, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VXV, uint32_t vd, uint32_t rs1, uint32_t vs2, bool vm) {
        assert(vs2 == uint32_t(-1));
        return instruc_rtype(0b1010111, vd, 0b101, rs1, 0b00000, 0b0101111);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VFNMACC_VF, riscv_instr_format_t::R_VXV,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VXV, uint32_t vd, uint32_t rs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b101, rs1, vs2, 0b1011010 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VFNMACC_VV, riscv_instr_format_t::R_VVV,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VVV, uint32_t vd, uint32_t vs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b001, vs1, vs2, 0b1011010 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VFNMADD_VF, riscv_instr_format_t::R_VXV,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VXV, uint32_t vd, uint32_t rs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b101, rs1, vs2, 0b1010010 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VFNMADD_VV, riscv_instr_format_t::R_VVV,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VVV, uint32_t vd, uint32_t vs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b001, vs1, vs2, 0b1010010 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VFNMSAC_VF, riscv_instr_format_t::R_VXV,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VXV, uint32_t vd, uint32_t rs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b101, rs1, vs2, 0b1011110 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VFNMSAC_VV, riscv_instr_format_t::R_VVV,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VVV, uint32_t vd, uint32_t vs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b001, vs1, vs2, 0b1011110 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VFNMSUB_VF, riscv_instr_format_t::R_VXV,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VXV, uint32_t vd, uint32_t rs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b101, rs1, vs2, 0b1010110 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VFNMSUB_VV, riscv_instr_format_t::R_VVV,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VVV, uint32_t vd, uint32_t vs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b001, vs1, vs2, 0b1010110 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VFRDIV_VF, riscv_instr_format_t::R_VXV,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VXV, uint32_t vd, uint32_t rs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b101, rs1, vs2, 0b1000010 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VFRSUB_VF, riscv_instr_format_t::R_VXV,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VXV, uint32_t vd, uint32_t rs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b101, rs1, vs2, 0b1001110 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VFSGNJ_VF, riscv_instr_format_t::R_VXV,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VXV, uint32_t vd, uint32_t rs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b101, rs1, vs2, 0b0010000 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VFSGNJ_VV, riscv_instr_format_t::R_VVV,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VVV, uint32_t vd, uint32_t vs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b001, vs1, vs2, 0b0010000 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VFSGNJN_VF, riscv_instr_format_t::R_VXV,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VXV, uint32_t vd, uint32_t rs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b101, rs1, vs2, 0b0010010 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VFSGNJN_VV, riscv_instr_format_t::R_VVV,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VVV, uint32_t vd, uint32_t vs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b001, vs1, vs2, 0b0010010 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VFSGNJX_VF, riscv_instr_format_t::R_VXV,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VXV, uint32_t vd, uint32_t rs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b101, rs1, vs2, 0b0010100 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VFSGNJX_VV, riscv_instr_format_t::R_VVV,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VVV, uint32_t vd, uint32_t vs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b001, vs1, vs2, 0b0010100 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VFSQRT_V, riscv_instr_format_t::R_VVV,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VVV, uint32_t vd, uint32_t vs1, uint32_t vs2, bool vm) {
        assert(vs1 == uint32_t(-1));
        return instruc_rtype(0b1010111, vd, 0b001, 0b00000, vs2,
                             0b0100110 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VFSUB_VF, riscv_instr_format_t::R_VXV,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VXV, uint32_t vd, uint32_t rs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b101, rs1, vs2, 0b0000100 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VFSUB_VV, riscv_instr_format_t::R_VVV,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VVV, uint32_t vd, uint32_t vs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b001, vs1, vs2, 0b0000100 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VID_V, riscv_instr_format_t::R_VVV,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VVV, uint32_t vd, uint32_t vs1, uint32_t vs2, bool vm) {
        assert(vs1 == uint32_t(-1));
        assert(vs2 == uint32_t(-1));
        return instruc_rtype(0b1010111, vd, 0b010, 0b10001, 0b00000,
                             0b0101000 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VLE32_V, riscv_instr_format_t::R_VXV_MOP_MEW_NF,
      riscv_instr_category_t::LOAD, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VXV_MOP_MEW_NF, uint32_t vd, uint32_t rs1, uint32_t vs2,
         bool vm, uint32_t mop, uint32_t mew, uint32_t nf) {
        assert(vs2 == uint32_t(-1));
        return instruc_rtype(0b0000111, vd, 0b110, rs1, 0b00000,
                             0b0000000 | vm | (nf << 4));
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VLOXEI32_V, riscv_instr_format_t::R_VXV_MOP_MEW_NF,
      riscv_instr_category_t::LOAD, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VXV_MOP_MEW_NF, uint32_t vd, uint32_t rs1, uint32_t vs2,
         bool vm, uint32_t mop, uint32_t mew, uint32_t nf) {
        return instruc_rtype(0b0000111, vd, 0b110, rs1, vs2,
                             0b0000110 | vm | (nf << 4));
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VLSE32_V, riscv_instr_format_t::R_VXX_MOP_MEW_NF,
      riscv_instr_category_t::LOAD, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VXX_MOP_MEW_NF, uint32_t vd, uint32_t rs1, uint32_t rs2,
         bool vm, uint32_t mop, uint32_t mew, uint32_t nf) {
        return instruc_rtype(0b0000111, vd, 0b110, rs1, rs2,
                             0b0000100 | vm | (nf << 4));
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VMACC_VV, riscv_instr_format_t::R_VVV,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VVV, uint32_t vd, uint32_t vs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b010, vs1, vs2, 0b1011010 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VMACC_VX, riscv_instr_format_t::R_VXV,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VXV, uint32_t vd, uint32_t rs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b110, rs1, vs2, 0b1011010 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VMADD_VV, riscv_instr_format_t::R_VVV,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VVV, uint32_t vd, uint32_t vs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b010, vs1, vs2, 0b1010010 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VMADD_VX, riscv_instr_format_t::R_VXV,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VXV, uint32_t vd, uint32_t rs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b110, rs1, vs2, 0b1010010 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VMAND_MM, riscv_instr_format_t::R_VVV,
      riscv_instr_category_t::THREAD_CTRL, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VVV, uint32_t vd, uint32_t vs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b010, vs1, vs2, 0b0110010 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VMANDN_MM, riscv_instr_format_t::R_VVV,
      riscv_instr_category_t::THREAD_CTRL, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VVV, uint32_t vd, uint32_t vs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b010, vs1, vs2, 0b0110000 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VMAX_VV, riscv_instr_format_t::R_VVV,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VVV, uint32_t vd, uint32_t vs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b000, vs1, vs2, 0b0001110 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VMAX_VX, riscv_instr_format_t::R_VXV,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VXV, uint32_t vd, uint32_t rs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b100, rs1, vs2, 0b0001110 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VMAXU_VV, riscv_instr_format_t::R_VVV,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VVV, uint32_t vd, uint32_t vs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b000, vs1, vs2, 0b0001100 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VMAXU_VX, riscv_instr_format_t::R_VXV,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VXV, uint32_t vd, uint32_t rs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b100, rs1, vs2, 0b0001100 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VMERGE_VIM, riscv_instr_format_t::R_VIV,
      riscv_instr_category_t::THREAD_MERGE, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VIV, uint32_t vd, int64_t imm, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b011, imm & 0b11111, vs2,
                             0b0101110);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VMERGE_VVM, riscv_instr_format_t::R_VVV,
      riscv_instr_category_t::THREAD_MERGE, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VVV, uint32_t vd, uint32_t vs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b000, vs1, vs2, 0b0101110);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VMERGE_VXM, riscv_instr_format_t::R_VXV,
      riscv_instr_category_t::THREAD_MERGE, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VXV, uint32_t vd, uint32_t rs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b100, rs1, vs2, 0b0101110);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VMFEQ_VF, riscv_instr_format_t::R_VXV,
      riscv_instr_category_t::COMPARE, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VXV, uint32_t vd, uint32_t rs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b101, rs1, vs2, 0b0110000 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VMFEQ_VV, riscv_instr_format_t::R_VVV,
      riscv_instr_category_t::COMPARE, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VVV, uint32_t vd, uint32_t vs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b001, vs1, vs2, 0b0110000 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VMFGE_VF, riscv_instr_format_t::R_VXV,
      riscv_instr_category_t::COMPARE, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VXV, uint32_t vd, uint32_t rs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b101, rs1, vs2, 0b0111110 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VMFGT_VF, riscv_instr_format_t::R_VXV,
      riscv_instr_category_t::COMPARE, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VXV, uint32_t vd, uint32_t rs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b101, rs1, vs2, 0b0111010 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VMFLE_VF, riscv_instr_format_t::R_VXV,
      riscv_instr_category_t::COMPARE, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VXV, uint32_t vd, uint32_t rs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b101, rs1, vs2, 0b0110010 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VMFLE_VV, riscv_instr_format_t::R_VVV,
      riscv_instr_category_t::COMPARE, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VVV, uint32_t vd, uint32_t vs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b001, vs1, vs2, 0b0110010 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VMFLT_VF, riscv_instr_format_t::R_VXV,
      riscv_instr_category_t::COMPARE, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VXV, uint32_t vd, uint32_t rs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b101, rs1, vs2, 0b0110110 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VMFLT_VV, riscv_instr_format_t::R_VVV,
      riscv_instr_category_t::COMPARE, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VVV, uint32_t vd, uint32_t vs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b001, vs1, vs2, 0b0110110 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VMFNE_VF, riscv_instr_format_t::R_VXV,
      riscv_instr_category_t::COMPARE, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VXV, uint32_t vd, uint32_t rs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b101, rs1, vs2, 0b0111000 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VMFNE_VV, riscv_instr_format_t::R_VVV,
      riscv_instr_category_t::COMPARE, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VVV, uint32_t vd, uint32_t vs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b001, vs1, vs2, 0b0111000 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VMIN_VV, riscv_instr_format_t::R_VVV,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VVV, uint32_t vd, uint32_t vs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b000, vs1, vs2, 0b0001010 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VMIN_VX, riscv_instr_format_t::R_VXV,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VXV, uint32_t vd, uint32_t rs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b100, rs1, vs2, 0b0001010 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VMINU_VV, riscv_instr_format_t::R_VVV,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VVV, uint32_t vd, uint32_t vs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b000, vs1, vs2, 0b0001000 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VMINU_VX, riscv_instr_format_t::R_VXV,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VXV, uint32_t vd, uint32_t rs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b100, rs1, vs2, 0b0001000 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VMNAND_MM, riscv_instr_format_t::R_VVV,
      riscv_instr_category_t::THREAD_CTRL, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VVV, uint32_t vd, uint32_t vs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b010, vs1, vs2, 0b0111010 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VMNOR_MM, riscv_instr_format_t::R_VVV,
      riscv_instr_category_t::THREAD_CTRL, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VVV, uint32_t vd, uint32_t vs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b010, vs1, vs2, 0b0111100 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VMOR_MM, riscv_instr_format_t::R_VVV,
      riscv_instr_category_t::THREAD_CTRL, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VVV, uint32_t vd, uint32_t vs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b010, vs1, vs2, 0b0110100 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VMORN_MM, riscv_instr_format_t::R_VVV,
      riscv_instr_category_t::THREAD_CTRL, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VVV, uint32_t vd, uint32_t vs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b010, vs1, vs2, 0b0111000 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VMSEQ_VI, riscv_instr_format_t::R_VIV,
      riscv_instr_category_t::COMPARE, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VIV, uint32_t vd, int64_t imm, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b011, imm & 0b11111, vs2,
                             0b0110000 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VMSEQ_VV, riscv_instr_format_t::R_VVV,
      riscv_instr_category_t::COMPARE, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VVV, uint32_t vd, uint32_t vs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b000, vs1, vs2, 0b0110000 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VMSEQ_VX, riscv_instr_format_t::R_VXV,
      riscv_instr_category_t::COMPARE, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VXV, uint32_t vd, uint32_t rs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b100, rs1, vs2, 0b0110000 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VMSGT_VI, riscv_instr_format_t::R_VIV,
      riscv_instr_category_t::COMPARE, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VIV, uint32_t vd, int64_t imm, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b011, imm & 0b11111, vs2,
                             0b0111110 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VMSGT_VX, riscv_instr_format_t::R_VXV,
      riscv_instr_category_t::COMPARE, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VXV, uint32_t vd, uint32_t rs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b100, rs1, vs2, 0b0111110 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VMSGTU_VI, riscv_instr_format_t::R_VIV,
      riscv_instr_category_t::COMPARE, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VIV, uint32_t vd, int64_t imm, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b011, imm & 0b11111, vs2,
                             0b0111100 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VMSGTU_VX, riscv_instr_format_t::R_VXV,
      riscv_instr_category_t::COMPARE, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VXV, uint32_t vd, uint32_t rs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b100, rs1, vs2, 0b0111100 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VMSLE_VI, riscv_instr_format_t::R_VIV,
      riscv_instr_category_t::COMPARE, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VIV, uint32_t vd, int64_t imm, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b011, imm & 0b11111, vs2,
                             0b0111010 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VMSLE_VV, riscv_instr_format_t::R_VVV,
      riscv_instr_category_t::COMPARE, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VVV, uint32_t vd, uint32_t vs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b000, vs1, vs2, 0b0111010 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VMSLE_VX, riscv_instr_format_t::R_VXV,
      riscv_instr_category_t::COMPARE, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VXV, uint32_t vd, uint32_t rs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b100, rs1, vs2, 0b0111010 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VMSLEU_VI, riscv_instr_format_t::R_VIV,
      riscv_instr_category_t::COMPARE, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VIV, uint32_t vd, int64_t imm, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b011, imm & 0b11111, vs2,
                             0b0111000 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VMSLEU_VV, riscv_instr_format_t::R_VVV,
      riscv_instr_category_t::COMPARE, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VVV, uint32_t vd, uint32_t vs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b000, vs1, vs2, 0b0111000 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VMSLEU_VX, riscv_instr_format_t::R_VXV,
      riscv_instr_category_t::COMPARE, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VXV, uint32_t vd, uint32_t rs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b100, rs1, vs2, 0b0111000 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VMSLT_VV, riscv_instr_format_t::R_VVV,
      riscv_instr_category_t::COMPARE, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VVV, uint32_t vd, uint32_t vs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b000, vs1, vs2, 0b0110110 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VMSLT_VX, riscv_instr_format_t::R_VXV,
      riscv_instr_category_t::COMPARE, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VXV, uint32_t vd, uint32_t rs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b100, rs1, vs2, 0b0110110 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VMSLTU_VV, riscv_instr_format_t::R_VVV,
      riscv_instr_category_t::COMPARE, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VVV, uint32_t vd, uint32_t vs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b000, vs1, vs2, 0b0110100 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VMSLTU_VX, riscv_instr_format_t::R_VXV,
      riscv_instr_category_t::COMPARE, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VXV, uint32_t vd, uint32_t rs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b100, rs1, vs2, 0b0110100 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VMSNE_VI, riscv_instr_format_t::R_VIV,
      riscv_instr_category_t::COMPARE, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VIV, uint32_t vd, int64_t imm, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b011, imm & 0b11111, vs2,
                             0b0110010 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VMSNE_VV, riscv_instr_format_t::R_VVV,
      riscv_instr_category_t::COMPARE, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VVV, uint32_t vd, uint32_t vs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b000, vs1, vs2, 0b0110010 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VMSNE_VX, riscv_instr_format_t::R_VXV,
      riscv_instr_category_t::COMPARE, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VXV, uint32_t vd, uint32_t rs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b100, rs1, vs2, 0b0110010 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VMUL_VV, riscv_instr_format_t::R_VVV,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VVV, uint32_t vd, uint32_t vs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b010, vs1, vs2, 0b1001010 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VMUL_VX, riscv_instr_format_t::R_VXV,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VXV, uint32_t vd, uint32_t rs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b110, rs1, vs2, 0b1001010 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VMULH_VV, riscv_instr_format_t::R_VVV,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VVV, uint32_t vd, uint32_t vs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b010, vs1, vs2, 0b1001110 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VMULH_VX, riscv_instr_format_t::R_VXV,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VXV, uint32_t vd, uint32_t rs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b110, rs1, vs2, 0b1001110 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VMULHSU_VV, riscv_instr_format_t::R_VVV,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VVV, uint32_t vd, uint32_t vs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b010, vs1, vs2, 0b1001100 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VMULHSU_VX, riscv_instr_format_t::R_VXV,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VXV, uint32_t vd, uint32_t rs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b110, rs1, vs2, 0b1001100 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VMULHU_VV, riscv_instr_format_t::R_VVV,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VVV, uint32_t vd, uint32_t vs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b010, vs1, vs2, 0b1001000 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VMULHU_VX, riscv_instr_format_t::R_VXV,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VXV, uint32_t vd, uint32_t rs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b110, rs1, vs2, 0b1001000 | vm);
      });

  //
  // NOTE: ventus has different semantics for vmv instructions
  // not THREAD_MERGE category
  //
  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VMV_S_X, riscv_instr_format_t::R_VXV,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VXV, uint32_t vd, uint32_t rs1, uint32_t vs2, bool vm) {
        assert(vs2 == uint32_t(-1));
        return instruc_rtype(0b1010111, vd, 0b110, rs1, 0b00000, 0b0100001);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VMV_V_I, riscv_instr_format_t::R_VIV,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VIV, uint32_t vd, int64_t imm, uint32_t vs2, bool vm) {
        assert(vs2 == uint32_t(-1));
        return instruc_rtype(0b1010111, vd, 0b011, imm & 0b11111, 0b00000,
                             0b0101111);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VMV_V_V, riscv_instr_format_t::R_VVV,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VVV, uint32_t vd, uint32_t vs1, uint32_t vs2, bool vm) {
        assert(vs2 == uint32_t(-1));
        return instruc_rtype(0b1010111, vd, 0b000, vs1, 0b00000, 0b0101111);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VMV_V_X, riscv_instr_format_t::R_VXV,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VXV, uint32_t vd, uint32_t rs1, uint32_t vs2, bool vm) {
        assert(vs2 == uint32_t(-1));
        return instruc_rtype(0b1010111, vd, 0b100, rs1, 0b00000, 0b0101111);
      });

  if (!BugFilter::instance().filter("vmv_x_s")) {
    DEFINE_VEC_INSTR(
        riscv_instr_name_t::VMV_X_S, riscv_instr_format_t::R_XXV,
        riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RVV,
        [](riscv_gen_R_XXV, uint32_t rd, uint32_t rs1, uint32_t vs2, bool vm) {
          assert(rs1 == uint32_t(-1));
          return instruc_rtype(0b1010111, rd, 0b010, 0b00000, vs2, 0b0100001);
        });
  }

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VMXNOR_MM, riscv_instr_format_t::R_VVV,
      riscv_instr_category_t::THREAD_CTRL, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VVV, uint32_t vd, uint32_t vs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b010, vs1, vs2, 0b0111110 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VMXOR_MM, riscv_instr_format_t::R_VVV,
      riscv_instr_category_t::THREAD_CTRL, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VVV, uint32_t vd, uint32_t vs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b010, vs1, vs2, 0b0110110 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VNMSAC_VV, riscv_instr_format_t::R_VVV,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VVV, uint32_t vd, uint32_t vs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b010, vs1, vs2, 0b1011110 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VNMSAC_VX, riscv_instr_format_t::R_VXV,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VXV, uint32_t vd, uint32_t rs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b110, rs1, vs2, 0b1011110 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VNMSUB_VV, riscv_instr_format_t::R_VVV,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VVV, uint32_t vd, uint32_t vs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b010, vs1, vs2, 0b1010110 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VNMSUB_VX, riscv_instr_format_t::R_VXV,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VXV, uint32_t vd, uint32_t rs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b110, rs1, vs2, 0b1010110 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VOR_VI, riscv_instr_format_t::R_VIV,
      riscv_instr_category_t::LOGICAL, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VIV, uint32_t vd, int64_t imm, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b011, imm & 0b11111, vs2,
                             0b0010100 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VOR_VV, riscv_instr_format_t::R_VVV,
      riscv_instr_category_t::LOGICAL, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VVV, uint32_t vd, uint32_t vs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b000, vs1, vs2, 0b0010100 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VOR_VX, riscv_instr_format_t::R_VXV,
      riscv_instr_category_t::LOGICAL, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VXV, uint32_t vd, uint32_t rs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b100, rs1, vs2, 0b0010100 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VREM_VV, riscv_instr_format_t::R_VVV,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VVV, uint32_t vd, uint32_t vs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b010, vs1, vs2, 0b1000110 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VREM_VX, riscv_instr_format_t::R_VXV,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VXV, uint32_t vd, uint32_t rs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b110, rs1, vs2, 0b1000110 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VREMU_VV, riscv_instr_format_t::R_VVV,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VVV, uint32_t vd, uint32_t vs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b010, vs1, vs2, 0b1000100 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VREMU_VX, riscv_instr_format_t::R_VXV,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VXV, uint32_t vd, uint32_t rs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b110, rs1, vs2, 0b1000100 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VRSUB_VI, riscv_instr_format_t::R_VIV,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VIV, uint32_t vd, int64_t imm, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b011, imm & 0b11111, vs2,
                             0b0000110 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VRSUB_VX, riscv_instr_format_t::R_VXV,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VXV, uint32_t vd, uint32_t rs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b100, rs1, vs2, 0b0000110 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VSE32_V, riscv_instr_format_t::R_V3XV_MOP_MEW_NF,
      riscv_instr_category_t::STORE, riscv_instr_group_t::RVV,
      [](riscv_gen_R_V3XV_MOP_MEW_NF, uint32_t vs3, uint32_t rs1, uint32_t vs2,
         bool vm, uint32_t mop, uint32_t mew, uint32_t nf) {
        assert(vs2 == uint32_t(-1));
        return instruc_rtype(0b0100111, vs3, 0b110, rs1, 0b00000,
                             0b0000000 | vm | (nf << 4));
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VSETIVLI, riscv_instr_format_t::I_UIMM_CSR_FORMAT,
      riscv_instr_category_t::SYSTEM, riscv_instr_group_t::RVV,
      [](riscv_gen_I_UIMM_CSR, uint32_t rd, uint32_t uimm, uint32_t csr) {
        csr |= 0b110000000000;
        return instruc_itype(0b1010111, rd, 0b111, uimm, csr);
      });

  DEFINE_VEC_INSTR(riscv_instr_name_t::VSETVL, riscv_instr_format_t::R_FORMAT,
                   riscv_instr_category_t::SYSTEM, riscv_instr_group_t::RVV,
                   [](riscv_gen_R, uint32_t rd, uint32_t rs1, uint32_t rs2) {
                     return instruc_rtype(0b1010111, rd, 0b111, rs1, rs2,
                                          0b1000000);
                   });

  DEFINE_VEC_INSTR(riscv_instr_name_t::VSETVLI, riscv_instr_format_t::I_FORMAT,
                   riscv_instr_category_t::SYSTEM, riscv_instr_group_t::RVV,
                   [](riscv_gen_I, uint32_t rd, uint32_t rs1, int64_t imm) {
                     return instruc_itype(0b1010111, rd, 0b111, rs1, imm);
                   });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VSLL_VI, riscv_instr_format_t::R_VIV,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VIV, uint32_t vd, int64_t imm, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b011, imm & 0b11111, vs2,
                             0b1001010 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VSLL_VV, riscv_instr_format_t::R_VVV,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VVV, uint32_t vd, uint32_t vs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b000, vs1, vs2, 0b1001010 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VSLL_VX, riscv_instr_format_t::R_VXV,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VXV, uint32_t vd, uint32_t rs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b100, rs1, vs2, 0b1001010 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VSOXEI32_V, riscv_instr_format_t::R_V3XV_MOP_MEW_NF,
      riscv_instr_category_t::STORE, riscv_instr_group_t::RVV,
      [](riscv_gen_R_V3XV_MOP_MEW_NF, uint32_t vs3, uint32_t rs1, uint32_t vs2,
         bool vm, uint32_t mop, uint32_t mew, uint32_t nf) {
        return instruc_rtype(0b0100111, vs3, 0b110, rs1, vs2,
                             0b0000110 | vm | (nf << 4));
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VSRA_VI, riscv_instr_format_t::R_VIV,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VIV, uint32_t vd, int64_t imm, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b011, imm & 0b11111, vs2,
                             0b1010010 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VSRA_VV, riscv_instr_format_t::R_VVV,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VVV, uint32_t vd, uint32_t vs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b000, vs1, vs2, 0b1010010 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VSRA_VX, riscv_instr_format_t::R_VXV,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VXV, uint32_t vd, uint32_t rs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b100, rs1, vs2, 0b1010010 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VSRL_VI, riscv_instr_format_t::R_VIV,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VIV, uint32_t vd, int64_t imm, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b011, imm & 0b11111, vs2,
                             0b1010000 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VSRL_VV, riscv_instr_format_t::R_VVV,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VVV, uint32_t vd, uint32_t vs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b000, vs1, vs2, 0b1010000 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VSRL_VX, riscv_instr_format_t::R_VXV,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VXV, uint32_t vd, uint32_t rs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b100, rs1, vs2, 0b1010000 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VSSE32_V, riscv_instr_format_t::R_V3XX_MOP_MEW_NF,
      riscv_instr_category_t::STORE, riscv_instr_group_t::RVV,
      [](riscv_gen_R_V3XX_MOP_MEW_NF, uint32_t vs3, uint32_t rs1, uint32_t rs2,
         bool vm, uint32_t mop, uint32_t mew, uint32_t nf) {
        return instruc_rtype(0b0100111, vs3, 0b110, rs1, rs2,
                             0b0000100 | vm | (nf << 4));
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VSUB_VV, riscv_instr_format_t::R_VVV,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VVV, uint32_t vd, uint32_t vs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b000, vs1, vs2, 0b0000100 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VSUB_VX, riscv_instr_format_t::R_VXV,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VXV, uint32_t vd, uint32_t rs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b100, rs1, vs2, 0b0000100 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VXOR_VI, riscv_instr_format_t::R_VIV,
      riscv_instr_category_t::LOGICAL, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VIV, uint32_t vd, int64_t imm, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b011, imm & 0b11111, vs2,
                             0b0010110 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VXOR_VV, riscv_instr_format_t::R_VVV,
      riscv_instr_category_t::LOGICAL, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VVV, uint32_t vd, uint32_t vs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b000, vs1, vs2, 0b0010110 | vm);
      });

  DEFINE_VEC_INSTR(
      riscv_instr_name_t::VXOR_VX, riscv_instr_format_t::R_VXV,
      riscv_instr_category_t::LOGICAL, riscv_instr_group_t::RVV,
      [](riscv_gen_R_VXV, uint32_t vd, uint32_t rs1, uint32_t vs2, bool vm) {
        return instruc_rtype(0b1010111, vd, 0b100, rs1, vs2, 0b0010110 | vm);
      });
}
} // namespace fuzzgpu
