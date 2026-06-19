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

static const uint32_t RV32M_OPCODE_MUL = 0b0110011;

void riscv_meta_list::register_rv32m() {
  // Multiplication and Division Instructions
  DEFINE_INSTR(riscv_instr_name_t::MUL, riscv_instr_format_t::R_FORMAT,
               riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RV32M,
               [](riscv_gen_R, uint32_t rd, uint32_t rs1, uint32_t rs2) {
                 return instruc_rtype(RV32M_OPCODE_MUL, rd, 0b000, rs1, rs2,
                                      0b1);
               });
  DEFINE_INSTR(riscv_instr_name_t::MULH, riscv_instr_format_t::R_FORMAT,
               riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RV32M,
               [](riscv_gen_R, uint32_t rd, uint32_t rs1, uint32_t rs2) {
                 return instruc_rtype(RV32M_OPCODE_MUL, rd, 0b001, rs1, rs2,
                                      0b1);
               });
  DEFINE_INSTR(riscv_instr_name_t::MULHSU, riscv_instr_format_t::R_FORMAT,
               riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RV32M,
               [](riscv_gen_R, uint32_t rd, uint32_t rs1, uint32_t rs2) {
                 return instruc_rtype(RV32M_OPCODE_MUL, rd, 0b010, rs1, rs2,
                                      0b1);
               });
  DEFINE_INSTR(riscv_instr_name_t::MULHU, riscv_instr_format_t::R_FORMAT,
               riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RV32M,
               [](riscv_gen_R, uint32_t rd, uint32_t rs1, uint32_t rs2) {
                 return instruc_rtype(RV32M_OPCODE_MUL, rd, 0b011, rs1, rs2,
                                      0b1);
               });
  DEFINE_INSTR(riscv_instr_name_t::DIV, riscv_instr_format_t::R_FORMAT,
               riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RV32M,
               [](riscv_gen_R, uint32_t rd, uint32_t rs1, uint32_t rs2) {
                 return instruc_rtype(RV32M_OPCODE_MUL, rd, 0b100, rs1, rs2,
                                      0b1);
               });
  DEFINE_INSTR(riscv_instr_name_t::DIVU, riscv_instr_format_t::R_FORMAT,
               riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RV32M,
               [](riscv_gen_R, uint32_t rd, uint32_t rs1, uint32_t rs2) {
                 return instruc_rtype(RV32M_OPCODE_MUL, rd, 0b101, rs1, rs2,
                                      0b1);
               });
  DEFINE_INSTR(riscv_instr_name_t::REM, riscv_instr_format_t::R_FORMAT,
               riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RV32M,
               [](riscv_gen_R, uint32_t rd, uint32_t rs1, uint32_t rs2) {
                 return instruc_rtype(RV32M_OPCODE_MUL, rd, 0b110, rs1, rs2,
                                      0b1);
               });
  DEFINE_INSTR(riscv_instr_name_t::REMU, riscv_instr_format_t::R_FORMAT,
               riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RV32M,
               [](riscv_gen_R, uint32_t rd, uint32_t rs1, uint32_t rs2) {
                 return instruc_rtype(RV32M_OPCODE_MUL, rd, 0b111, rs1, rs2,
                                      0b1);
               });
}

} // namespace fuzzgpu