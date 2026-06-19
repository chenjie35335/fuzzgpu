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

static const uint32_t RV64I_OPCODE_LWU = 0b0000011;
static const uint32_t RV64I_OPCODE_LD = 0b0000011;
static const uint32_t RV64I_OPCODE_SD = 0b0100011;
static const uint32_t RV64I_OPCODE_ALU_IMM =
    0b0011011; // For all rv64i arithmetics with immediate
static const uint32_t RV64I_OPCODE_ALU_REG =
    0b0111011; // For all rv64i arithmetics without immediate

void riscv_meta_list::register_rv64i() {
  DEFINE_INSTR(riscv_instr_name_t::LWU, riscv_instr_format_t::I_FORMAT,
               riscv_instr_category_t::LOAD, riscv_instr_group_t::RV64I,
               [](riscv_gen_I, uint32_t rd, uint32_t rs1, int64_t imm) {
                 return instruc_itype(RV64I_OPCODE_LWU, rd, 0b110, rs1, imm);
               });

  DEFINE_INSTR(riscv_instr_name_t::LD, riscv_instr_format_t::I_FORMAT,
               riscv_instr_category_t::LOAD, riscv_instr_group_t::RV64I,
               [](riscv_gen_I, uint32_t rd, uint32_t rs1, int64_t imm) {
                 return instruc_itype(RV64I_OPCODE_LD, rd, 0b011, rs1, imm);
               });

  DEFINE_INSTR(riscv_instr_name_t::SD, riscv_instr_format_t::S_FORMAT,
               riscv_instr_category_t::STORE, riscv_instr_group_t::RV64I,
               [](riscv_gen_S, uint32_t rs1, uint32_t rs2, int64_t imm) {
                 return instruc_stype(RV64I_OPCODE_SD, 0b011, rs1, rs2, imm);
               });

  // SHIFT intructions
  DEFINE_INSTR(riscv_instr_name_t::SLLW, riscv_instr_format_t::R_FORMAT,
               riscv_instr_category_t::SHIFT, riscv_instr_group_t::RV64I,
               [](riscv_gen_R, uint32_t rd, uint32_t rs1, uint32_t rs2) {
                 return instruc_rtype(RV64I_OPCODE_ALU_REG, rd, 0b001, rs1, rs2,
                                      0);
               });

  DEFINE_INSTR(riscv_instr_name_t::SLLIW, riscv_instr_format_t::I_FORMAT,
               riscv_instr_category_t::SHIFT, riscv_instr_group_t::RV64I,
               [](riscv_gen_I, uint32_t rd, uint32_t rs1, int64_t imm) {
                 // imm = shamt
                 assert(imm < 32);
                 return instruc_itype(RV64I_OPCODE_ALU_IMM, rd, 0b001, rs1,
                                      imm);
               });

  DEFINE_INSTR(riscv_instr_name_t::SRLW, riscv_instr_format_t::R_FORMAT,
               riscv_instr_category_t::SHIFT, riscv_instr_group_t::RV64I,
               [](riscv_gen_R, uint32_t rd, uint32_t rs1, uint32_t rs2) {
                 return instruc_rtype(RV64I_OPCODE_ALU_REG, rd, 0b101, rs1, rs2,
                                      0);
               });

  DEFINE_INSTR(riscv_instr_name_t::SRLIW, riscv_instr_format_t::I_FORMAT,
               riscv_instr_category_t::SHIFT, riscv_instr_group_t::RV64I,
               [](riscv_gen_I, uint32_t rd, uint32_t rs1, int64_t imm) {
                 // imm = shamt
                 assert(imm < 32);
                 return instruc_itype(RV64I_OPCODE_ALU_IMM, rd, 0b101, rs1,
                                      imm);
               });

  DEFINE_INSTR(riscv_instr_name_t::SRAW, riscv_instr_format_t::R_FORMAT,
               riscv_instr_category_t::SHIFT, riscv_instr_group_t::RV64I,
               [](riscv_gen_R, uint32_t rd, uint32_t rs1, uint32_t rs2) {
                 return instruc_rtype(RV64I_OPCODE_ALU_REG, rd, 0b101, rs1, rs2,
                                      0b0100000);
               });

  DEFINE_INSTR(riscv_instr_name_t::SRAIW, riscv_instr_format_t::I_FORMAT,
               riscv_instr_category_t::SHIFT, riscv_instr_group_t::RV64I,
               [](riscv_gen_I, uint32_t rd, uint32_t rs1, int64_t imm) {
                 assert(imm < 32);
                 imm |= 0b010000000000;
                 return instruc_itype(RV64I_OPCODE_ALU_IMM, rd, 0b101, rs1,
                                      imm);
               });

  // ARITHMETIC intructions
  DEFINE_INSTR(riscv_instr_name_t::ADDW, riscv_instr_format_t::R_FORMAT,
               riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RV64I,
               [](riscv_gen_R, uint32_t rd, uint32_t rs1, uint32_t rs2) {
                 return instruc_rtype(RV64I_OPCODE_ALU_REG, rd, 0b000, rs1, rs2,
                                      0);
               });

  DEFINE_INSTR(riscv_instr_name_t::ADDIW, riscv_instr_format_t::I_FORMAT,
               riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RV64I,
               [](riscv_gen_I, uint32_t rd, uint32_t rs1, int64_t imm) {
                 return instruc_itype(RV64I_OPCODE_ALU_IMM, rd, 0b000, rs1,
                                      imm);
               });

  DEFINE_INSTR(riscv_instr_name_t::SUBW, riscv_instr_format_t::R_FORMAT,
               riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RV64I,
               [](riscv_gen_R, uint32_t rd, uint32_t rs1, uint32_t rs2) {
                 return instruc_rtype(RV64I_OPCODE_ALU_REG, rd, 0b000, rs1, rs2,
                                      0b0100000);
               });
}

} // namespace fuzzgpu