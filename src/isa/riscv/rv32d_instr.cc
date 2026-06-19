/**
 * Based on riscv-dv/pygen
 *
Copyright 2020 Google LLC
Copyright 2020 PerfectVIPs Inc.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at
http://www.apache.org/licenses/LICENSE-2.0
Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.

 */

#include "isa/riscv/riscv_encode.hh"
#include "isa/riscv/riscv_meta_list.hh"

namespace fuzzgpu {

static const uint32_t RV32D_OPCODE_FLD = 0b0000111;
static const uint32_t RV32D_OPCODE_FSD = 0b0100111;
static const uint32_t RV32D_OPCODE_FMADDD = 0b1000011;
static const uint32_t RV32D_OPCODE_FMSUBD = 0b1000111;
static const uint32_t RV32D_OPCODE_FNMSUBD = 0b1001011;
static const uint32_t RV32D_OPCODE_FNMADDD = 0b1001111;
static const uint32_t RV32D_OPCODE_FALU = 0b1010011;

void riscv_meta_list::register_rv32d() {
  DEFINE_FP_INSTR(riscv_instr_name_t::FLD, riscv_instr_format_t::I_FORMAT,
                  riscv_instr_category_t::LOAD, riscv_instr_group_t::RV32D,
                  [](riscv_gen_I, uint32_t rd, uint32_t rs1, int64_t imm) {
                    return instruc_itype(RV32D_OPCODE_FLD, rd, 0b011, rs1, imm);
                  });

  DEFINE_FP_INSTR(riscv_instr_name_t::FSD, riscv_instr_format_t::S_FORMAT,
                  riscv_instr_category_t::STORE, riscv_instr_group_t::RV32D,
                  [](riscv_gen_S, uint32_t rs1, uint32_t rs2, int64_t imm) {
                    return instruc_stype(RV32D_OPCODE_FSD, 0b011, rs1, rs2,
                                         imm);
                  });

  DEFINE_FP_INSTR(
      riscv_instr_name_t::FMADD_D, riscv_instr_format_t::R4_RM_FORMAT,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RV32D,
      [](riscv_gen_R4_RM, uint32_t rd, uint32_t rm, uint32_t rs1, uint32_t rs2,
         uint32_t rs3) {
        return instruc_r4type(RV32D_OPCODE_FMADDD, rd, rm, rs1, rs2, rs3, 0b01);
      });

  DEFINE_FP_INSTR(
      riscv_instr_name_t::FMSUB_D, riscv_instr_format_t::R4_RM_FORMAT,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RV32D,
      [](riscv_gen_R4_RM, uint32_t rd, uint32_t rm, uint32_t rs1, uint32_t rs2,
         uint32_t rs3) {
        return instruc_r4type(RV32D_OPCODE_FMSUBD, rd, rm, rs1, rs2, rs3, 0b01);
      });

  DEFINE_FP_INSTR(
      riscv_instr_name_t::FNMSUB_D, riscv_instr_format_t::R4_RM_FORMAT,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RV32D,

      [](riscv_gen_R4_RM, uint32_t rd, uint32_t rm, uint32_t rs1, uint32_t rs2,
         uint32_t rs3) {
        return instruc_r4type(RV32D_OPCODE_FNMSUBD, rd, rm, rs1, rs2, rs3,
                              0b01);
      });

  DEFINE_FP_INSTR(
      riscv_instr_name_t::FNMADD_D, riscv_instr_format_t::R4_RM_FORMAT,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RV32D,
      [](riscv_gen_R4_RM, uint32_t rd, uint32_t rm, uint32_t rs1, uint32_t rs2,
         uint32_t rs3) {
        return instruc_r4type(RV32D_OPCODE_FNMADDD, rd, rm, rs1, rs2, rs3,
                              0b01);
      });

  DEFINE_FP_INSTR(
      riscv_instr_name_t::FADD_D, riscv_instr_format_t::R_RM_FORMAT,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RV32D,
      [](riscv_gen_R_RM, uint32_t rd, uint32_t rm, uint32_t rs1, uint32_t rs2) {
        return instruc_rtype(RV32D_OPCODE_FALU, rd, rm, rs1, rs2, 0b0000001);
      });

  DEFINE_FP_INSTR(
      riscv_instr_name_t::FSUB_D, riscv_instr_format_t::R_RM_FORMAT,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RV32D,

      [](riscv_gen_R_RM, uint32_t rd, uint32_t rm, uint32_t rs1, uint32_t rs2) {
        return instruc_rtype(RV32D_OPCODE_FALU, rd, rm, rs1, rs2, 0b0000101);
      });

  DEFINE_FP_INSTR(
      riscv_instr_name_t::FMUL_D, riscv_instr_format_t::R_RM_FORMAT,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RV32D,
      [](riscv_gen_R_RM, uint32_t rd, uint32_t rm, uint32_t rs1, uint32_t rs2) {
        return instruc_rtype(RV32D_OPCODE_FALU, rd, rm, rs1, rs2, 0b0001001);
      });

  DEFINE_FP_INSTR(
      riscv_instr_name_t::FDIV_D, riscv_instr_format_t::R_RM_FORMAT,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RV32D,

      [](riscv_gen_R_RM, uint32_t rd, uint32_t rm, uint32_t rs1, uint32_t rs2) {
        return instruc_rtype(RV32D_OPCODE_FALU, rd, rm, rs1, rs2, 0b0001101);
      });

  DEFINE_FP_INSTR(
      riscv_instr_name_t::FSQRT_D, riscv_instr_format_t::R_RM_FORMAT,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RV32D,
      [](riscv_gen_R_RM, uint32_t rd, uint32_t rm, uint32_t rs1, uint32_t rs2) {
        assert(rs2 == uint32_t(-1));
        return instruc_rtype(RV32D_OPCODE_FALU, rd, rm, rs1, 0, 0b0101101);
      });

  DEFINE_FP_INSTR(
      riscv_instr_name_t::FSGNJ_D, riscv_instr_format_t::R_FORMAT,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RV32D,
      [](riscv_gen_R, uint32_t rd, uint32_t rs1, uint32_t rs2) {
        return instruc_rtype(RV32D_OPCODE_FALU, rd, 0b000, rs1, rs2, 0b0010001);
      });
  DEFINE_FP_INSTR(
      riscv_instr_name_t::FSGNJN_D, riscv_instr_format_t::R_FORMAT,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RV32D,
      [](riscv_gen_R, uint32_t rd, uint32_t rs1, uint32_t rs2) {
        return instruc_rtype(RV32D_OPCODE_FALU, rd, 0b001, rs1, rs2, 0b0010001);
      }

  );
  DEFINE_FP_INSTR(
      riscv_instr_name_t::FSGNJX_D, riscv_instr_format_t::R_FORMAT,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RV32D,
      [](riscv_gen_R, uint32_t rd, uint32_t rs1, uint32_t rs2) {
        return instruc_rtype(RV32D_OPCODE_FALU, rd, 0b010, rs1, rs2, 0b0010001);
      });
  DEFINE_FP_INSTR(
      riscv_instr_name_t::FMIN_D, riscv_instr_format_t::R_FORMAT,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RV32D,
      [](riscv_gen_R, uint32_t rd, uint32_t rs1, uint32_t rs2) {
        return instruc_rtype(RV32D_OPCODE_FALU, rd, 0b000, rs1, rs2, 0b0010101);
      });
  DEFINE_FP_INSTR(
      riscv_instr_name_t::FMAX_D, riscv_instr_format_t::R_FORMAT,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RV32D,
      [](riscv_gen_R, uint32_t rd, uint32_t rs1, uint32_t rs2) {
        return instruc_rtype(RV32D_OPCODE_FALU, rd, 0b001, rs1, rs2, 0b0010101);
      });
  DEFINE_FP_INSTR(
      riscv_instr_name_t::FCVT_S_D, riscv_instr_format_t::R_RM_FORMAT,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RV32D,
      [](riscv_gen_R_RM, uint32_t rd, uint32_t rm, uint32_t rs1, uint32_t rs2) {
        assert(rs2 == uint32_t(-1));
        return instruc_rtype(RV32D_OPCODE_FALU, rd, rm, rs1, 0b1, 0b0100000);
      });
  DEFINE_FP_INSTR(
      riscv_instr_name_t::FCVT_D_S, riscv_instr_format_t::R_RM_FORMAT,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RV32D,
      [](riscv_gen_R_RM, uint32_t rd, uint32_t rm, uint32_t rs1, uint32_t rs2) {
        assert(rs2 == uint32_t(-1));
        return instruc_rtype(RV32D_OPCODE_FALU, rd, rm, rs1, 0b0, 0b0100001);
      });
  DEFINE_FP_INSTR(riscv_instr_name_t::FEQ_D, riscv_instr_format_t::R_FORMAT,
                  riscv_instr_category_t::COMPARE, riscv_instr_group_t::RV32D,
                  [](riscv_gen_R, uint32_t rd, uint32_t rs1, uint32_t rs2) {
                    return instruc_rtype(RV32D_OPCODE_FALU, rd, 0b010, rs1, rs2,
                                         0b1010001);
                  });
  DEFINE_FP_INSTR(riscv_instr_name_t::FLT_D, riscv_instr_format_t::R_FORMAT,
                  riscv_instr_category_t::COMPARE, riscv_instr_group_t::RV32D,
                  [](riscv_gen_R, uint32_t rd, uint32_t rs1, uint32_t rs2) {
                    return instruc_rtype(RV32D_OPCODE_FALU, rd, 0b001, rs1, rs2,
                                         0b1010001);
                  });
  DEFINE_FP_INSTR(riscv_instr_name_t::FLE_D, riscv_instr_format_t::R_FORMAT,
                  riscv_instr_category_t::COMPARE, riscv_instr_group_t::RV32D,
                  [](riscv_gen_R, uint32_t rd, uint32_t rs1, uint32_t rs2) {
                    return instruc_rtype(RV32D_OPCODE_FALU, rd, 0b000, rs1, rs2,
                                         0b1010001);
                  });
  DEFINE_FP_INSTR(
      riscv_instr_name_t::FCLASS_D, riscv_instr_format_t::R_FORMAT,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RV32D,
      [](riscv_gen_R, uint32_t rd, uint32_t rs1, uint32_t rs2) {
        return instruc_rtype(RV32D_OPCODE_FALU, rd, 0b001, rs1, 0b0, 0b1110001);
      });
  DEFINE_FP_INSTR(
      riscv_instr_name_t::FCVT_W_D, riscv_instr_format_t::R_RM_FORMAT,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RV32D,
      [](riscv_gen_R_RM, uint32_t rd, uint32_t rm, uint32_t rs1, uint32_t rs2) {
        assert(rs2 == uint32_t(-1));
        return instruc_rtype(RV32D_OPCODE_FALU, rd, rm, rs1, 0b0, 0b1100001);
      });
  DEFINE_FP_INSTR(
      riscv_instr_name_t::FCVT_WU_D, riscv_instr_format_t::R_RM_FORMAT,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RV32D,
      [](riscv_gen_R_RM, uint32_t rd, uint32_t rm, uint32_t rs1, uint32_t rs2) {
        assert(rs2 == uint32_t(-1));
        return instruc_rtype(RV32D_OPCODE_FALU, rd, rm, rs1, 0b1, 0b1100001);
      });
  DEFINE_FP_INSTR(
      riscv_instr_name_t::FCVT_D_W, riscv_instr_format_t::R_RM_FORMAT,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RV32D,
      [](riscv_gen_R_RM, uint32_t rd, uint32_t rm, uint32_t rs1, uint32_t rs2) {
        assert(rs2 == uint32_t(-1));
        return instruc_rtype(RV32D_OPCODE_FALU, rd, rm, rs1, 0b0, 0b1101001);
      });
  DEFINE_FP_INSTR(
      riscv_instr_name_t::FCVT_D_WU, riscv_instr_format_t::R_RM_FORMAT,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RV32D,
      [](riscv_gen_R_RM, uint32_t rd, uint32_t rm, uint32_t rs1, uint32_t rs2) {
        assert(rs2 == uint32_t(-1));
        return instruc_rtype(RV32D_OPCODE_FALU, rd, rm, rs1, 0b1, 0b1101001);
      });
}

} // namespace fuzzgpu