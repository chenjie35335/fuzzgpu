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

static const uint32_t RV64D_OPCODE_FCVT = 0b1010011;

void riscv_meta_list::register_rv64d() {
  DEFINE_FP_INSTR(riscv_instr_name_t::FMV_X_D, riscv_instr_format_t::R_FORMAT,
                  riscv_instr_category_t::ARITHMETIC,
                  riscv_instr_group_t::RV64D,
                  [](riscv_gen_R, uint32_t rd, uint32_t rs1, uint32_t rs2) {
                    assert(rs2 == uint32_t(-1));
                    return instruc_rtype(RV64D_OPCODE_FCVT, rd, 0b000, rs1,
                                         0b00000, 0b1110001);
                  });

  DEFINE_FP_INSTR(riscv_instr_name_t::FMV_D_X, riscv_instr_format_t::R_FORMAT,
                  riscv_instr_category_t::ARITHMETIC,
                  riscv_instr_group_t::RV64D,
                  [](riscv_gen_R, uint32_t rd, uint32_t rs1, uint32_t rs2) {
                    assert(rs2 == uint32_t(-1));
                    return instruc_rtype(RV64D_OPCODE_FCVT, rd, 0b000, rs1,
                                         0b00000, 0b1111001);
                  });
  DEFINE_FP_INSTR(
      riscv_instr_name_t::FCVT_L_D, riscv_instr_format_t::R_RM_FORMAT,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RV64D,
      [](riscv_gen_R_RM, uint32_t rd, uint32_t rm, uint32_t rs1, uint32_t rs2) {
        assert(rs2 == uint32_t(-1));
        return instruc_rtype(RV64D_OPCODE_FCVT, rd, rm, rs1, 0b00010,
                             0b1100001);
      });
  DEFINE_FP_INSTR(
      riscv_instr_name_t::FCVT_LU_D, riscv_instr_format_t::R_RM_FORMAT,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RV64D,

      [](riscv_gen_R_RM, uint32_t rd, uint32_t rm, uint32_t rs1, uint32_t rs2) {
        assert(rs2 == uint32_t(-1));
        return instruc_rtype(RV64D_OPCODE_FCVT, rd, rm, rs1, 0b00011,
                             0b1100001);
      });

  DEFINE_FP_INSTR(
      riscv_instr_name_t::FCVT_D_L, riscv_instr_format_t::R_RM_FORMAT,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RV64D,
      [](riscv_gen_R_RM, uint32_t rd, uint32_t rm, uint32_t rs1, uint32_t rs2) {
        assert(rs2 == uint32_t(-1));
        return instruc_rtype(RV64D_OPCODE_FCVT, rd, rm, rs1, 0b00010,
                             0b1101001);
      });
  DEFINE_FP_INSTR(
      riscv_instr_name_t::FCVT_D_LU, riscv_instr_format_t::R_RM_FORMAT,
      riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RV64D,
      [](riscv_gen_R_RM, uint32_t rd, uint32_t rm, uint32_t rs1, uint32_t rs2) {
        assert(rs2 == uint32_t(-1));
        return instruc_rtype(RV64D_OPCODE_FCVT, rd, rm, rs1, 0b00011,
                             0b1101001);
      });
}

} // namespace fuzzgpu