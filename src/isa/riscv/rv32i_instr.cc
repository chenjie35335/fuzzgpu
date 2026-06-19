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

#include "config.hh"
#include "isa/riscv/riscv_encode.hh"
#include "isa/riscv/riscv_meta_list.hh"

namespace fuzzgpu {

static const uint32_t RV32I_OPCODE_LUI = 0b0110111;
static const uint32_t RV32I_OPCODE_AUIPC = 0b0010111;
static const uint32_t RV32I_OPCODE_JAL = 0b1101111;
static const uint32_t RV32I_OPCODE_JALR = 0b1100111;
static const uint32_t RV32I_OPCODE_B = 0b1100011; // For all rv32i branches
static const uint32_t RV32I_OPCODE_L = 0b0000011; // For all rv32i loads
static const uint32_t RV32I_OPCODE_S = 0b0100011; // For all rv32i stores
static const uint32_t RV32I_OPCODE_ALU_IMM =
    0b0010011; // For all rv32i arithmetics with immediate
static const uint32_t RV32I_OPCODE_ALU_REG =
    0b0110011; // For all rv32i arithmetics without immediate
static const uint32_t RV32I_OPCODE_FEN = 0b0001111; // For rv32i fences
static const uint32_t RV32I_OPCODE_E = 0b1110011; // For rv32i ECALL and EBREAK

static const uint32_t ZIFENCEI_OPCODE_FENCEI = 0b0001111;

static const uint32_t RV32I_OPCODE_PRIVILEGED = 0b1110011;

static const uint32_t ZICSR_OPCODE_CSR = 0b1110011;

void riscv_meta_list::register_rv32i() {

  // LOAD instructions
  DEFINE_INSTR(riscv_instr_name_t::LB, riscv_instr_format_t::I_FORMAT,
               riscv_instr_category_t::LOAD, riscv_instr_group_t::RV32I,
               [](riscv_gen_I, uint32_t rd, uint32_t rs1, int64_t imm) {
                 return instruc_itype(RV32I_OPCODE_L, rd, 0b000, rs1, imm);
               });

  DEFINE_INSTR(riscv_instr_name_t::LH, riscv_instr_format_t::I_FORMAT,
               riscv_instr_category_t::LOAD, riscv_instr_group_t::RV32I,
               [](riscv_gen_I, uint32_t rd, uint32_t rs1, int64_t imm) {
                 return instruc_itype(RV32I_OPCODE_L, rd, 0b001, rs1, imm);
               });

  DEFINE_INSTR(riscv_instr_name_t::LW, riscv_instr_format_t::I_FORMAT,
               riscv_instr_category_t::LOAD, riscv_instr_group_t::RV32I,
               [](riscv_gen_I, uint32_t rd, uint32_t rs1, int64_t imm) {
                 return instruc_itype(RV32I_OPCODE_L, rd, 0b010, rs1, imm);
               });

  DEFINE_INSTR(riscv_instr_name_t::LBU, riscv_instr_format_t::I_FORMAT,
               riscv_instr_category_t::LOAD, riscv_instr_group_t::RV32I,
               [](riscv_gen_I, uint32_t rd, uint32_t rs1, int64_t imm) {
                 return instruc_itype(RV32I_OPCODE_L, rd, 0b100, rs1, imm);
               });

  DEFINE_INSTR(riscv_instr_name_t::LHU, riscv_instr_format_t::I_FORMAT,
               riscv_instr_category_t::LOAD, riscv_instr_group_t::RV32I,
               [](riscv_gen_I, uint32_t rd, uint32_t rs1, int64_t imm) {
                 return instruc_itype(RV32I_OPCODE_L, rd, 0b101, rs1, imm);
               });

  // STORE instructions
  DEFINE_INSTR(riscv_instr_name_t::SB, riscv_instr_format_t::S_FORMAT,
               riscv_instr_category_t::STORE, riscv_instr_group_t::RV32I,
               [](riscv_gen_S, uint32_t rs1, uint32_t rs2, int64_t imm) {
                 return instruc_stype(RV32I_OPCODE_S, 0b000, rs1, rs2, imm);
               });

  DEFINE_INSTR(riscv_instr_name_t::SH, riscv_instr_format_t::S_FORMAT,
               riscv_instr_category_t::STORE, riscv_instr_group_t::RV32I,
               [](riscv_gen_S, uint32_t rs1, uint32_t rs2, int64_t imm) {
                 return instruc_stype(RV32I_OPCODE_S, 0b001, rs1, rs2, imm);
               });

  DEFINE_INSTR(riscv_instr_name_t::SW, riscv_instr_format_t::S_FORMAT,
               riscv_instr_category_t::STORE, riscv_instr_group_t::RV32I,
               [](riscv_gen_S, uint32_t rs1, uint32_t rs2, int64_t imm) {
                 return instruc_stype(RV32I_OPCODE_S, 0b010, rs1, rs2, imm);
               });

  // SHIFT intructions
  DEFINE_INSTR(riscv_instr_name_t::SLL, riscv_instr_format_t::R_FORMAT,
               riscv_instr_category_t::SHIFT, riscv_instr_group_t::RV32I,
               [](riscv_gen_R, uint32_t rd, uint32_t rs1, uint32_t rs2) {
                 return instruc_rtype(RV32I_OPCODE_ALU_REG, rd, 0b001, rs1, rs2,
                                      0b0000000);
               });

  DEFINE_INSTR(riscv_instr_name_t::SLLI, riscv_instr_format_t::I_FORMAT,
               riscv_instr_category_t::SHIFT, riscv_instr_group_t::RV32I,
               [](riscv_gen_I, uint32_t rd, uint32_t rs1, int64_t imm) {
                 // imm = shamt;
                 assert(imm < 64); // max shamt (RV64I)
                 return instruc_itype(RV32I_OPCODE_ALU_IMM, rd, 0b001, rs1,
                                      imm);
               });

  DEFINE_INSTR(riscv_instr_name_t::SRL, riscv_instr_format_t::R_FORMAT,
               riscv_instr_category_t::SHIFT, riscv_instr_group_t::RV32I,
               [](riscv_gen_R, uint32_t rd, uint32_t rs1, uint32_t rs2) {
                 return instruc_rtype(RV32I_OPCODE_ALU_REG, rd, 0b101, rs1, rs2,
                                      0b0000000);
               });

  DEFINE_INSTR(riscv_instr_name_t::SRLI, riscv_instr_format_t::I_FORMAT,
               riscv_instr_category_t::SHIFT, riscv_instr_group_t::RV32I,
               [](riscv_gen_I, uint32_t rd, uint32_t rs1, int64_t imm) {
                 // imm = shamt
                 assert(imm < 64); // max shamt (RV64I)
                 return instruc_itype(RV32I_OPCODE_ALU_IMM, rd, 0b101, rs1,
                                      imm);
               });

  DEFINE_INSTR(riscv_instr_name_t::SRA, riscv_instr_format_t::R_FORMAT,
               riscv_instr_category_t::SHIFT, riscv_instr_group_t::RV32I,
               [](riscv_gen_R, uint32_t rd, uint32_t rs1, uint32_t rs2) {
                 return instruc_rtype(RV32I_OPCODE_ALU_REG, rd, 0b101, rs1, rs2,
                                      0b0100000);
               });

  DEFINE_INSTR(
      riscv_instr_name_t::SRAI, riscv_instr_format_t::I_FORMAT,
      riscv_instr_category_t::SHIFT, riscv_instr_group_t::RV32I,
      [](riscv_gen_I, uint32_t rd, uint32_t rs1, int64_t imm) {
        assert(imm < Config::instance().arch.word_bits); // different in RV64I
        imm |= 0b010000000000;
        return instruc_itype(RV32I_OPCODE_ALU_IMM, rd, 0b101, rs1, imm);
      });

  // ARITHMETIC intructions
  DEFINE_INSTR(riscv_instr_name_t::ADD, riscv_instr_format_t::R_FORMAT,
               riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RV32I,
               [](riscv_gen_R, uint32_t rd, uint32_t rs1, uint32_t rs2) {
                 return instruc_rtype(RV32I_OPCODE_ALU_REG, rd, 0b000, rs1, rs2,
                                      0b0000000);
               });

  DEFINE_INSTR(riscv_instr_name_t::ADDI, riscv_instr_format_t::I_FORMAT,
               riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RV32I,
               [](riscv_gen_I, uint32_t rd, uint32_t rs1, int64_t imm) {
                 return instruc_itype(RV32I_OPCODE_ALU_IMM, rd, 0b000, rs1,
                                      imm);
               });

  DEFINE_INSTR(riscv_instr_name_t::SUB, riscv_instr_format_t::R_FORMAT,
               riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RV32I,
               [](riscv_gen_R, uint32_t rd, uint32_t rs1, uint32_t rs2) {
                 return instruc_rtype(RV32I_OPCODE_ALU_REG, rd, 0b000, rs1, rs2,
                                      0b0100000);
               });

  DEFINE_INSTR(riscv_instr_name_t::LUI, riscv_instr_format_t::U_FORMAT,
               riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RV32I,
               [](riscv_gen_U, uint32_t rd, uint32_t uimm) {
                 return instruc_utype(RV32I_OPCODE_LUI, rd, uimm);
               });

  DEFINE_INSTR(riscv_instr_name_t::AUIPC, riscv_instr_format_t::U_FORMAT,
               riscv_instr_category_t::ARITHMETIC, riscv_instr_group_t::RV32I,
               [](riscv_gen_U, uint32_t rd, uint32_t uimm) {
                 return instruc_utype(RV32I_OPCODE_AUIPC, rd, uimm);
               });

  // LOGICAL instructions
  DEFINE_INSTR(riscv_instr_name_t::XOR, riscv_instr_format_t::R_FORMAT,
               riscv_instr_category_t::LOGICAL, riscv_instr_group_t::RV32I,
               [](riscv_gen_R, uint32_t rd, uint32_t rs1, uint32_t rs2) {
                 return instruc_rtype(RV32I_OPCODE_ALU_REG, rd, 0b100, rs1, rs2,
                                      0b0000000);
               });

  DEFINE_INSTR(riscv_instr_name_t::XORI, riscv_instr_format_t::I_FORMAT,
               riscv_instr_category_t::LOGICAL, riscv_instr_group_t::RV32I,
               [](riscv_gen_I, uint32_t rd, uint32_t rs1, int64_t imm) {
                 return instruc_itype(RV32I_OPCODE_ALU_IMM, rd, 0b100, rs1,
                                      imm);
               });

  DEFINE_INSTR(riscv_instr_name_t::OR, riscv_instr_format_t::R_FORMAT,
               riscv_instr_category_t::LOGICAL, riscv_instr_group_t::RV32I,
               [](riscv_gen_R, uint32_t rd, uint32_t rs1, uint32_t rs2) {
                 return instruc_rtype(RV32I_OPCODE_ALU_REG, rd, 0b110, rs1, rs2,
                                      0b0000000);
               });

  DEFINE_INSTR(riscv_instr_name_t::ORI, riscv_instr_format_t::I_FORMAT,
               riscv_instr_category_t::LOGICAL, riscv_instr_group_t::RV32I,
               [](riscv_gen_I, uint32_t rd, uint32_t rs1, int64_t imm) {
                 return instruc_itype(RV32I_OPCODE_ALU_IMM, rd, 0b110, rs1,
                                      imm);
               });

  DEFINE_INSTR(riscv_instr_name_t::AND, riscv_instr_format_t::R_FORMAT,
               riscv_instr_category_t::LOGICAL, riscv_instr_group_t::RV32I,
               [](riscv_gen_R, uint32_t rd, uint32_t rs1, uint32_t rs2) {
                 return instruc_rtype(RV32I_OPCODE_ALU_REG, rd, 0b111, rs1, rs2,
                                      0b0000000);
               });

  DEFINE_INSTR(riscv_instr_name_t::ANDI, riscv_instr_format_t::I_FORMAT,
               riscv_instr_category_t::LOGICAL, riscv_instr_group_t::RV32I,
               [](riscv_gen_I, uint32_t rd, uint32_t rs1, int64_t imm) {
                 return instruc_itype(RV32I_OPCODE_ALU_IMM, rd, 0b111, rs1,
                                      imm);
               });

  // COMPARE instructions
  DEFINE_INSTR(riscv_instr_name_t::SLT, riscv_instr_format_t::R_FORMAT,
               riscv_instr_category_t::COMPARE, riscv_instr_group_t::RV32I,
               [](riscv_gen_R, uint32_t rd, uint32_t rs1, uint32_t rs2) {
                 return instruc_rtype(RV32I_OPCODE_ALU_REG, rd, 0b010, rs1, rs2,
                                      0b0000000);
               });

  DEFINE_INSTR(riscv_instr_name_t::SLTI, riscv_instr_format_t::I_FORMAT,
               riscv_instr_category_t::COMPARE, riscv_instr_group_t::RV32I,
               [](riscv_gen_I, uint32_t rd, uint32_t rs1, int64_t imm) {
                 return instruc_itype(RV32I_OPCODE_ALU_IMM, rd, 0b010, rs1,
                                      imm);
               });

  DEFINE_INSTR(riscv_instr_name_t::SLTU, riscv_instr_format_t::R_FORMAT,
               riscv_instr_category_t::COMPARE, riscv_instr_group_t::RV32I,
               [](riscv_gen_R, uint32_t rd, uint32_t rs1, uint32_t rs2) {
                 return instruc_rtype(RV32I_OPCODE_ALU_REG, rd, 0b011, rs1, rs2,
                                      0b0000000);
               });

  DEFINE_INSTR(riscv_instr_name_t::SLTIU, riscv_instr_format_t::I_FORMAT,
               riscv_instr_category_t::COMPARE, riscv_instr_group_t::RV32I,
               [](riscv_gen_I, uint32_t rd, uint32_t rs1, int64_t imm) {
                 return instruc_itype(RV32I_OPCODE_ALU_IMM, rd, 0b011, rs1,
                                      imm);
               });

  // BRANCH instructions
  DEFINE_INSTR(riscv_instr_name_t::BEQ, riscv_instr_format_t::B_FORMAT,
               riscv_instr_category_t::BRANCH, riscv_instr_group_t::RV32I,
               [](riscv_gen_B, uint32_t rs1, uint32_t rs2, int64_t imm) {
                 return instruc_btype(RV32I_OPCODE_B, 0b000, rs1, rs2, imm);
               });

  DEFINE_INSTR(riscv_instr_name_t::BNE, riscv_instr_format_t::B_FORMAT,
               riscv_instr_category_t::BRANCH, riscv_instr_group_t::RV32I,
               [](riscv_gen_B, uint32_t rs1, uint32_t rs2, int64_t imm) {
                 return instruc_btype(RV32I_OPCODE_B, 0b001, rs1, rs2, imm);
               });

  DEFINE_INSTR(riscv_instr_name_t::BLT, riscv_instr_format_t::B_FORMAT,
               riscv_instr_category_t::BRANCH, riscv_instr_group_t::RV32I,
               [](riscv_gen_B, uint32_t rs1, uint32_t rs2, int64_t imm) {
                 return instruc_btype(RV32I_OPCODE_B, 0b100, rs1, rs2, imm);
               });

  DEFINE_INSTR(riscv_instr_name_t::BGE, riscv_instr_format_t::B_FORMAT,
               riscv_instr_category_t::BRANCH, riscv_instr_group_t::RV32I,
               [](riscv_gen_B, uint32_t rs1, uint32_t rs2, int64_t imm) {
                 return instruc_btype(RV32I_OPCODE_B, 0b101, rs1, rs2, imm);
               });

  DEFINE_INSTR(riscv_instr_name_t::BLTU, riscv_instr_format_t::B_FORMAT,
               riscv_instr_category_t::BRANCH, riscv_instr_group_t::RV32I,
               [](riscv_gen_B, uint32_t rs1, uint32_t rs2, int64_t imm) {
                 return instruc_btype(RV32I_OPCODE_B, 0b110, rs1, rs2, imm);
               });

  DEFINE_INSTR(riscv_instr_name_t::BGEU, riscv_instr_format_t::B_FORMAT,
               riscv_instr_category_t::BRANCH, riscv_instr_group_t::RV32I,
               [](riscv_gen_B, uint32_t rs1, uint32_t rs2, int64_t imm) {
                 return instruc_btype(RV32I_OPCODE_B, 0b111, rs1, rs2, imm);
               });

  // JUMP instructions
  DEFINE_INSTR(riscv_instr_name_t::JAL, riscv_instr_format_t::J_FORMAT,
               riscv_instr_category_t::JUMP, riscv_instr_group_t::RV32I,
               [](riscv_gen_J, uint32_t rd, int64_t imm) {
                 return instruc_jtype(RV32I_OPCODE_JAL, rd, imm);
               });

  DEFINE_INSTR(riscv_instr_name_t::JALR, riscv_instr_format_t::I_FORMAT,
               riscv_instr_category_t::JUMP, riscv_instr_group_t::RV32I,
               [](riscv_gen_I, uint32_t rd, uint32_t rs1, int64_t imm) {
                 return instruc_itype(RV32I_OPCODE_JALR, rd, 0, rs1, imm);
               });

  // SYNCH instructions
  DEFINE_INSTR(riscv_instr_name_t::FENCE, riscv_instr_format_t::I_FORMAT,
               riscv_instr_category_t::SYNCH, riscv_instr_group_t::RV32I,
               [](riscv_gen_I, uint32_t rd, uint32_t rs1, int64_t) {
                 assert(rd == uint32_t(-1));
                 assert(rs1 == uint32_t(-1));
                 // imm is dropped
                 return instruc_itype(RV32I_OPCODE_FEN, 0, 0b000, 0,
                                      0b000011111111);
               });

#if !defined(ARCH_VENTUS) // it currently does not support
  DEFINE_INSTR(riscv_instr_name_t::FENCE_I, riscv_instr_format_t::I_FORMAT,
               riscv_instr_category_t::SYNCH, riscv_instr_group_t::RV32I,
               [](riscv_gen_I, uint32_t rd, uint32_t rs1, int64_t) {
                 assert(rd == uint32_t(-1));
                 assert(rs1 == uint32_t(-1));
                 // imm is dropped
                 return instruc_itype(ZIFENCEI_OPCODE_FENCEI, 0, 0b001, 0, 0);
               });
#endif

  DEFINE_INSTR(riscv_instr_name_t::SFENCE_VMA, riscv_instr_format_t::R_FORMAT,
               riscv_instr_category_t::SYNCH, riscv_instr_group_t::RV32I,
               [](riscv_gen_R, uint32_t rd, uint32_t rs1, uint32_t rs2) {
                 assert(rd == uint32_t(-1));
                 return instruc_rtype(RV32I_OPCODE_PRIVILEGED, 0, 0, rs1, rs2,
                                      0b0001001);
               });
  // SYSTEM instructions
  // Not presented in GPGPU
  DEFINE_INSTR(riscv_instr_name_t::EBREAK, riscv_instr_format_t::I_FORMAT,
               riscv_instr_category_t::SYSTEM, riscv_instr_group_t::RV32I,
               [](riscv_gen_I, uint32_t rd, uint32_t rs1, int64_t imm) {
                 assert(rd == uint32_t(-1));
                 assert(rs1 == uint32_t(-1));
                 assert(imm == -1);
                 return instruc_itype(RV32I_OPCODE_E, 0, 0, 0, 1);
               });
#if 0 // the hardware not yet supported these instructions
  DEFINE_INSTR(riscv_instr_name_t::ECALL, riscv_instr_format_t::I_FORMAT,
               riscv_instr_category_t::SYSTEM, riscv_instr_group_t::RV32I);
  DEFINE_INSTR(riscv_instr_name_t::URET, riscv_instr_format_t::I_FORMAT,
               riscv_instr_category_t::SYSTEM, riscv_instr_group_t::RV32I);
  DEFINE_INSTR(riscv_instr_name_t::SRET, riscv_instr_format_t::I_FORMAT,
               riscv_instr_category_t::SYSTEM, riscv_instr_group_t::RV32I);
  DEFINE_INSTR(riscv_instr_name_t::MRET, riscv_instr_format_t::I_FORMAT,
               riscv_instr_category_t::SYSTEM, riscv_instr_group_t::RV32I);
  DEFINE_INSTR(riscv_instr_name_t::DRET, riscv_instr_format_t::I_FORMAT,
               riscv_instr_category_t::SYSTEM, riscv_instr_group_t::RV32I);
  DEFINE_INSTR(riscv_instr_name_t::WFI, riscv_instr_format_t::I_FORMAT,
               riscv_instr_category_t::INTERRUPT, riscv_instr_group_t::RV32I);
#endif
  // CSR instructions
  DEFINE_INSTR(riscv_instr_name_t::CSRRW, riscv_instr_format_t::I_CSR_FORMAT,
               riscv_instr_category_t::CSR, riscv_instr_group_t::RV32I,
               [](riscv_gen_I_CSR, uint32_t rd, uint32_t rs1, uint32_t csr) {
                 return instruc_itype_csr(ZICSR_OPCODE_CSR, rd, 0b001, rs1,
                                          csr);
               });

  DEFINE_INSTR(riscv_instr_name_t::CSRRS, riscv_instr_format_t::I_CSR_FORMAT,
               riscv_instr_category_t::CSR, riscv_instr_group_t::RV32I,
               [](riscv_gen_I_CSR, uint32_t rd, uint32_t rs1, uint32_t csr) {
                 return instruc_itype_csr(ZICSR_OPCODE_CSR, rd, 0b010, rs1,
                                          csr);
               });

  DEFINE_INSTR(riscv_instr_name_t::CSRRC, riscv_instr_format_t::I_CSR_FORMAT,
               riscv_instr_category_t::CSR, riscv_instr_group_t::RV32I,
               [](riscv_gen_I_CSR, uint32_t rd, uint32_t rs1, uint32_t csr) {
                 return instruc_itype_csr(ZICSR_OPCODE_CSR, rd, 0b011, rs1,
                                          csr);
               });

  DEFINE_INSTR(
      riscv_instr_name_t::CSRRWI, riscv_instr_format_t::I_UIMM_CSR_FORMAT,
      riscv_instr_category_t::CSR, riscv_instr_group_t::RV32I,
      [](riscv_gen_I_UIMM_CSR, uint32_t rd, uint32_t uimm, uint32_t csr) {
        return instruc_itype_csr(ZICSR_OPCODE_CSR, rd, 0b101, uimm, csr);
      });

  DEFINE_INSTR(
      riscv_instr_name_t::CSRRSI, riscv_instr_format_t::I_UIMM_CSR_FORMAT,
      riscv_instr_category_t::CSR, riscv_instr_group_t::RV32I,
      [](riscv_gen_I_UIMM_CSR, uint32_t rd, uint32_t uimm, uint32_t csr) {
        return instruc_itype_csr(ZICSR_OPCODE_CSR, rd, 0b110, uimm, csr);
      });

  DEFINE_INSTR(
      riscv_instr_name_t::CSRRCI, riscv_instr_format_t::I_UIMM_CSR_FORMAT,
      riscv_instr_category_t::CSR, riscv_instr_group_t::RV32I,
      [](riscv_gen_I_UIMM_CSR, uint32_t rd, uint32_t uimm, uint32_t csr) {
        return instruc_itype_csr(ZICSR_OPCODE_CSR, rd, 0b111, uimm, csr);
      });

  // Pseudo instructions
  DEFINE_INSTR(riscv_instr_name_t::NOP, riscv_instr_format_t::I_FORMAT,
               riscv_instr_category_t::PSEUDO, riscv_instr_group_t::RV32I,
               [](riscv_gen_I, uint32_t rd, uint32_t rs1, int64_t imm) {
                 assert(rd == uint32_t(-1));
                 assert(rs1 == uint32_t(-1));
                 assert(imm == -1);
                 return instruc_itype(RV32I_OPCODE_ALU_IMM, 0, 0b000, 0, 0);
               });
}

} // namespace fuzzgpu