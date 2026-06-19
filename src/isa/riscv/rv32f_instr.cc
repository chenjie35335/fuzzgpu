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

static const uint32_t RV32F_OPCODE_FLW = 0b0000111;
static const uint32_t RV32F_OPCODE_FSW = 0b0100111;
static const uint32_t RV32F_OPCODE_FMADDS = 0b1000011;
static const uint32_t RV32F_OPCODE_FMSUBS = 0b1000111;
static const uint32_t RV32F_OPCODE_FNMSUBS = 0b1001011;
static const uint32_t RV32F_OPCODE_FNMADDS = 0b1001111;
static const uint32_t RV32F_OPCODE_FALU = 0b1010011;

#if defined(ARCH_VENTUS)
static const uint32_t RV32I_OPCODE_ALU_IMM =
    0b0010011; // For all rv32i arithmetics with immediate
static const uint32_t RV32I_OPCODE_L = 0b0000011; // For all rv32i loads
static const uint32_t RV32I_OPCODE_S = 0b0100011; // For all rv32i stores
#endif

void riscv_meta_list::register_rv32f() {
  DEFINE_FP_INSTR(riscv_instr_name_t::FLW, riscv_instr_format_t::I_FORMAT,
                  riscv_instr_category_t::LOAD, riscv_instr_group_t::RV32F,
                  [](riscv_gen_I, uint32_t rd, uint32_t rs1, int64_t imm) {
#if defined(ARCH_VENTUS) // ventus using unified int & fp regs
                    // lw rd, imm(rs1)
                    return instruc_itype(RV32I_OPCODE_L, rd, 0b010, rs1, imm);
#else
                    return instruc_itype(RV32F_OPCODE_FLW, rd, 0b010, rs1, imm);
#endif
                  });
  DEFINE_FP_INSTR(riscv_instr_name_t::FSW, riscv_instr_format_t::S_FORMAT,
                  riscv_instr_category_t::STORE, riscv_instr_group_t::RV32F,
                  [](riscv_gen_S, uint32_t rs1, uint32_t rs2, int64_t imm) {
#if defined(ARCH_VENTUS) // ventus using unified int & fp regs
                    // sw rs2, imm(rs1)
                    return instruc_stype(RV32I_OPCODE_S, 0b010, rs1, rs2, imm);
#else
                    return instruc_stype(RV32F_OPCODE_FSW, 0b010, rs1, rs2,
                                         imm);
#endif
                  });

  DEFINE_FP_INSTR(
      riscv_instr_name_t::FMADD_S, riscv_instr_format_t::R4_RM_FORMAT,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RV32F,
      [](riscv_gen_R4_RM, uint32_t rd, uint32_t rm, uint32_t rs1, uint32_t rs2,
         uint32_t rs3) {
        return instruc_r4type(RV32F_OPCODE_FMADDS, rd, rm, rs1, rs2, rs3, 0);
      });
  DEFINE_FP_INSTR(
      riscv_instr_name_t::FMSUB_S, riscv_instr_format_t::R4_RM_FORMAT,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RV32F,
      [](riscv_gen_R4_RM, uint32_t rd, uint32_t rm, uint32_t rs1, uint32_t rs2,
         uint32_t rs3) {
        return instruc_r4type(RV32F_OPCODE_FMSUBS, rd, rm, rs1, rs2, rs3, 0);
      });
  DEFINE_FP_INSTR(
      riscv_instr_name_t::FNMSUB_S, riscv_instr_format_t::R4_RM_FORMAT,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RV32F,
      [](riscv_gen_R4_RM, uint32_t rd, uint32_t rm, uint32_t rs1, uint32_t rs2,
         uint32_t rs3) {
        return instruc_r4type(RV32F_OPCODE_FNMSUBS, rd, rm, rs1, rs2, rs3, 0);
      });
  DEFINE_FP_INSTR(
      riscv_instr_name_t::FNMADD_S, riscv_instr_format_t::R4_RM_FORMAT,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RV32F,
      [](riscv_gen_R4_RM, uint32_t rd, uint32_t rm, uint32_t rs1, uint32_t rs2,
         uint32_t rs3) {
        return instruc_r4type(RV32F_OPCODE_FNMADDS, rd, rm, rs1, rs2, rs3, 0);
      });
  DEFINE_FP_INSTR(
      riscv_instr_name_t::FADD_S, riscv_instr_format_t::R_RM_FORMAT,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RV32F,
      [](riscv_gen_R_RM, uint32_t rd, uint32_t rm, uint32_t rs1, uint32_t rs2) {
        return instruc_rtype(RV32F_OPCODE_FALU, rd, rm, rs1, rs2, 0b0000000);
      }

  );
  DEFINE_FP_INSTR(
      riscv_instr_name_t::FSUB_S, riscv_instr_format_t::R_RM_FORMAT,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RV32F,
      [](riscv_gen_R_RM, uint32_t rd, uint32_t rm, uint32_t rs1, uint32_t rs2) {
        return instruc_rtype(RV32F_OPCODE_FALU, rd, rm, rs1, rs2, 0b0000100);
      });
  DEFINE_FP_INSTR(
      riscv_instr_name_t::FMUL_S, riscv_instr_format_t::R_RM_FORMAT,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RV32F,
      [](riscv_gen_R_RM, uint32_t rd, uint32_t rm, uint32_t rs1, uint32_t rs2) {
        return instruc_rtype(RV32F_OPCODE_FALU, rd, rm, rs1, rs2, 0b0001000);
      });
  DEFINE_FP_INSTR(
      riscv_instr_name_t::FDIV_S, riscv_instr_format_t::R_RM_FORMAT,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RV32F,
      [](riscv_gen_R_RM, uint32_t rd, uint32_t rm, uint32_t rs1, uint32_t rs2) {
        return instruc_rtype(RV32F_OPCODE_FALU, rd, rm, rs1, rs2, 0b0001100);
      });
  DEFINE_FP_INSTR(
      riscv_instr_name_t::FSQRT_S, riscv_instr_format_t::R_RM_FORMAT,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RV32F,
      [](riscv_gen_R_RM, uint32_t rd, uint32_t rm, uint32_t rs1, uint32_t rs2) {
        assert(rs2 == uint32_t(-1));
        return instruc_rtype(RV32F_OPCODE_FALU, rd, rm, rs1, 0, 0b0101100);
      });
  DEFINE_FP_INSTR(
      riscv_instr_name_t::FSGNJ_S, riscv_instr_format_t::R_FORMAT,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RV32F,
      [](riscv_gen_R, uint32_t rd, uint32_t rs1, uint32_t rs2) {
        return instruc_rtype(RV32F_OPCODE_FALU, rd, 0b000, rs1, rs2, 0b0010000);
      });
  DEFINE_FP_INSTR(
      riscv_instr_name_t::FSGNJN_S, riscv_instr_format_t::R_FORMAT,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RV32F,
      [](riscv_gen_R, uint32_t rd, uint32_t rs1, uint32_t rs2) {
        return instruc_rtype(RV32F_OPCODE_FALU, rd, 0b001, rs1, rs2, 0b0010000);
      });
  DEFINE_FP_INSTR(
      riscv_instr_name_t::FSGNJX_S, riscv_instr_format_t::R_FORMAT,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RV32F,

      [](riscv_gen_R, uint32_t rd, uint32_t rs1, uint32_t rs2) {
        return instruc_rtype(RV32F_OPCODE_FALU, rd, 0b010, rs1, rs2, 0b0010000);
      });
  DEFINE_FP_INSTR(
      riscv_instr_name_t::FMIN_S, riscv_instr_format_t::R_FORMAT,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RV32F,
      [](riscv_gen_R, uint32_t rd, uint32_t rs1, uint32_t rs2) {
        return instruc_rtype(RV32F_OPCODE_FALU, rd, 0b000, rs1, rs2, 0b0010100);
      });
  DEFINE_FP_INSTR(
      riscv_instr_name_t::FMAX_S, riscv_instr_format_t::R_FORMAT,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RV32F,
      [](riscv_gen_R, uint32_t rd, uint32_t rs1, uint32_t rs2) {
        return instruc_rtype(RV32F_OPCODE_FALU, rd, 0b001, rs1, rs2, 0b0010100);
      });
  DEFINE_FP_INSTR(
      riscv_instr_name_t::FCVT_W_S, riscv_instr_format_t::R_RM_FORMAT,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RV32F,
      [](riscv_gen_R_RM, uint32_t rd, uint32_t rm, uint32_t rs1, uint32_t rs2) {
        assert(rs2 == uint32_t(-1));
        return instruc_rtype(RV32F_OPCODE_FALU, rd, rm, rs1, 0b0, 0b1100000);
      });
  DEFINE_FP_INSTR(
      riscv_instr_name_t::FCVT_WU_S, riscv_instr_format_t::R_RM_FORMAT,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RV32F,
      [](riscv_gen_R_RM, uint32_t rd, uint32_t rm, uint32_t rs1, uint32_t rs2) {
        assert(rs2 == uint32_t(-1));
        return instruc_rtype(RV32F_OPCODE_FALU, rd, rm, rs1, 0b1, 0b1100000);
      });
  DEFINE_FP_INSTR(
      riscv_instr_name_t::FMV_X_W, riscv_instr_format_t::R_FORMAT,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RV32F,
      [](riscv_gen_R, uint32_t rd, uint32_t rs1, uint32_t rs2) {
        assert(rs2 == uint32_t(-1));
#if defined(ARCH_VENTUS) // ventus using unified int & fp regs
        // addi rd, rs1, 0
        return instruc_itype(RV32I_OPCODE_ALU_IMM, rd, 0b000, rs1, 0);
#else
        return instruc_rtype(RV32F_OPCODE_FALU, rd, 0b000, rs1, 0b0, 0b1110000);
#endif
      });
  DEFINE_FP_INSTR(riscv_instr_name_t::FEQ_S, riscv_instr_format_t::R_FORMAT,
                  riscv_instr_category_t::COMPARE, riscv_instr_group_t::RV32F,

                  [](riscv_gen_R, uint32_t rd, uint32_t rs1, uint32_t rs2) {
                    return instruc_rtype(RV32F_OPCODE_FALU, rd, 0b010, rs1, rs2,
                                         0b1010000);
                  });
  DEFINE_FP_INSTR(riscv_instr_name_t::FLT_S, riscv_instr_format_t::R_FORMAT,
                  riscv_instr_category_t::COMPARE, riscv_instr_group_t::RV32F,

                  [](riscv_gen_R, uint32_t rd, uint32_t rs1, uint32_t rs2) {
                    return instruc_rtype(RV32F_OPCODE_FALU, rd, 0b001, rs1, rs2,
                                         0b1010000);
                  });
  DEFINE_FP_INSTR(riscv_instr_name_t::FLE_S, riscv_instr_format_t::R_FORMAT,
                  riscv_instr_category_t::COMPARE, riscv_instr_group_t::RV32F,
                  [](riscv_gen_R, uint32_t rd, uint32_t rs1, uint32_t rs2) {
                    return instruc_rtype(RV32F_OPCODE_FALU, rd, 0b000, rs1, rs2,
                                         0b1010000);
                  });
  DEFINE_FP_INSTR(
      riscv_instr_name_t::FCLASS_S, riscv_instr_format_t::R_FORMAT,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RV32F,
      [](riscv_gen_R, uint32_t rd, uint32_t rs1, uint32_t rs2) {
        assert(rs2 == uint32_t(-1));
        return instruc_rtype(RV32F_OPCODE_FALU, rd, 0b001, rs1, 0b0, 0b1110000);
      });
  DEFINE_FP_INSTR(
      riscv_instr_name_t::FCVT_S_W, riscv_instr_format_t::R_RM_FORMAT,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RV32F,
      [](riscv_gen_R_RM, uint32_t rd, uint32_t rm, uint32_t rs1, uint32_t rs2) {
        // rs2 is dropped
        return instruc_rtype(RV32F_OPCODE_FALU, rd, rm, rs1, 0b0, 0b1101000);
      });
  DEFINE_FP_INSTR(
      riscv_instr_name_t::FCVT_S_WU, riscv_instr_format_t::R_RM_FORMAT,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RV32F,
      [](riscv_gen_R_RM, uint32_t rd, uint32_t rm, uint32_t rs1, uint32_t rs2) {
        // rs2 is dropped
        return instruc_rtype(RV32F_OPCODE_FALU, rd, rm, rs1, 0b1, 0b1101000);
      });
  DEFINE_FP_INSTR(
      riscv_instr_name_t::FMV_W_X, riscv_instr_format_t::R_FORMAT,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RV32F,
      [](riscv_gen_R, uint32_t rd, uint32_t rs1, uint32_t rs2) {
        assert(rs2 == uint32_t(-1));
#if defined(ARCH_VENTUS) // ventus using unified int & fp regs
        // addi rd, rs1, 0
        return instruc_itype(RV32I_OPCODE_ALU_IMM, rd, 0b000, rs1, 0);
#else
        return instruc_rtype(RV32F_OPCODE_FALU, rd, 0b000, rs1, 0b0, 0b1111000);
#endif
      });
}

} // namespace fuzzgpu