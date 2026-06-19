/** Copyright 2023 Flavien Solt, ETH Zurich.
 * Licensed under the General Public License, Version 3.0, see LICENSE for
 * details. SPDX-License-Identifier: GPL-3.0-only
 */
#include <cassert>

#include "isa/riscv/riscv_encode.hh"

namespace fuzzgpu {

// @return uint32_t
// @param opcode: uint8_t
// @param rd: uint8_t
// @param funct3: uint8_t
// @param rs1: uint8_t
// @param rs2: uint8_t
// @param funct7: uint8_t
uint32_t instruc_rtype(uint32_t opcode, uint32_t rd, uint32_t funct3,
                       uint32_t rs1, uint32_t rs2, uint32_t funct7) {
  assert(opcode >= 0);
  assert(rd >= 0);
  assert(funct3 >= 0);
  assert(rs1 >= 0);
  assert(rs2 >= 0);
  assert(funct7 >= 0);

  assert(opcode < (1 << 7));
  assert(rd < 32);
  assert(funct3 < 8);
  assert(rs1 < 32);
  assert(rs2 < 32);
  assert(funct7 < (1 << 7));

  uint32_t rd_offset = 7;
  uint32_t funct3_offset = rd_offset + 5;
  uint32_t rs1_offset = funct3_offset + 3;
  uint32_t rs2_offset = rs1_offset + 5;
  uint32_t funct7_offset = rs2_offset + 5;

  return opcode | (rd << rd_offset) | (funct3 << funct3_offset) |
         (rs1 << rs1_offset) | (rs2 << rs2_offset) | (funct7 << funct7_offset);
}

// @return uint32_t
// @param opcode: uint8_t
// @param rd: uint8_t
// @param funct3: uint8_t
// @param rs1: uint8_t
uint64_t instruc_itype(uint64_t opcode, uint64_t rd, uint64_t funct3,
                       uint64_t rs1, int64_t imm) {
  assert(opcode < (1 << 7));
  assert(rd < 32);
  assert(funct3 < 8);
  assert(rs1 < 32);
  // assert(imm < (1 << 12))

  assert(opcode >= 0);
  assert(rd >= 0);
  assert(funct3 >= 0);
  assert(rs1 >= 0);
  // assert(imm >= 0)

  // imm = imm & 0b111111111111;
  assert(imm < (1 << 11) && imm >= -(1 << 11));

  uint32_t rd_offset = 7;
  uint32_t funct3_offset = rd_offset + 5;
  uint32_t rs1_offset = funct3_offset + 3;
  uint32_t imm_offset = rs1_offset + 5;

  return opcode | (rd << rd_offset) | (funct3 << funct3_offset) |
         (rs1 << rs1_offset) | (imm << imm_offset);
}

// @return uint32_t
// @param opcode: uint8_t
// @param rd: uint8_t
// @param funct3: uint8_t
// @param rs1: uint8_t
uint64_t instruc_itype_csr(uint64_t opcode, uint64_t rd, uint64_t funct3,
                           uint64_t rs1, uint16_t csr) {
  assert(opcode < (1 << 7));
  assert(rd < 32);
  assert(funct3 < 8);
  assert(rs1 < 32);
  // assert(imm < (1 << 12))

  assert(opcode >= 0);
  assert(rd >= 0);
  assert(funct3 >= 0);
  assert(rs1 >= 0);
  // assert(imm >= 0)

  // imm = imm & 0b111111111111;
  assert(csr < (1u << 12));

  uint32_t rd_offset = 7;
  uint32_t funct3_offset = rd_offset + 5;
  uint32_t rs1_offset = funct3_offset + 3;
  uint32_t imm_offset = rs1_offset + 5;

  return opcode | (rd << rd_offset) | (funct3 << funct3_offset) |
         (rs1 << rs1_offset) | (csr << imm_offset);
}

uint64_t instruc_itype_funct1(uint64_t opcode, uint64_t rd, uint64_t funct3,
                              uint64_t rs1, int64_t imm, bool funct1) {
  assert(opcode < (1 << 7));
  assert(rd < 32);
  assert(funct3 < 8);
  assert(rs1 < 32);
  // assert(imm < (1 << 12))

  assert(opcode >= 0);
  assert(rd >= 0);
  assert(funct3 >= 0);
  assert(rs1 >= 0);
  // assert(imm >= 0)

  // imm = imm & 0b111111111111;
  assert(imm < (1 << 10) && imm >= -(1 << 10));
  imm &= 0b11111111111; // 11bits

  uint32_t rd_offset = 7;
  uint32_t funct3_offset = rd_offset + 5;
  uint32_t rs1_offset = funct3_offset + 3;
  uint32_t imm_offset = rs1_offset + 5;

  return opcode | (rd << rd_offset) | (funct3 << funct3_offset) |
         (rs1 << rs1_offset) | (imm << imm_offset) | (uint32_t(funct1) << 31);
}

// @return uint32_t
// @param opcode: uint8_t
// @param funct3: uint8_t
// @param rs1: uint8_t
// @param rs2: uint8_t
uint32_t instruc_stype(uint32_t opcode, uint32_t funct3, uint32_t rs1,
                       uint32_t rs2, int64_t imm) {
  assert(opcode < (1 << 7));
  assert(funct3 < 8);
  assert(rs1 < 32);
  assert(rs2 < 32);
  // assert(imm < (1 << 12))

  assert(opcode >= 0);
  assert(funct3 >= 0);
  assert(rs1 >= 0);
  assert(rs2 >= 0);
  // assert(imm >= 0)

  // imm = imm & 0b111111111111;
  assert(imm < (1 << 11) && imm >= -(1 << 11));

  uint32_t imm4_0_offset = 7;
  uint32_t funct3_offset = imm4_0_offset + 5;
  uint32_t rs1_offset = funct3_offset + 3;
  uint32_t rs2_offset = rs1_offset + 5;
  uint32_t imm11_5_offset = rs2_offset + 5;

  uint32_t imm4_0 = imm & 0b11111;
  uint32_t imm11_5 = (imm >> 5) & 0b1111111;

  return opcode | (imm4_0 << imm4_0_offset) | (funct3 << funct3_offset) |
         (rs1 << rs1_offset) | (rs2 << rs2_offset) |
         (imm11_5 << imm11_5_offset);
}

uint32_t instruc_stype_funct1(uint32_t opcode, uint32_t funct3, uint32_t rs1,
                              uint32_t rs2, int64_t imm, bool funct1) {
  assert(opcode < (1 << 7));
  assert(funct3 < 8);
  assert(rs1 < 32);
  assert(rs2 < 32);
  // assert(imm < (1 << 12))

  assert(opcode >= 0);
  assert(funct3 >= 0);
  assert(rs1 >= 0);
  assert(rs2 >= 0);
  // assert(imm >= 0)

  // imm = imm & 0b111111111111;
  assert(imm < (1 << 10) && imm >= -(1 << 10));

  uint32_t imm4_0_offset = 7;
  uint32_t funct3_offset = imm4_0_offset + 5;
  uint32_t rs1_offset = funct3_offset + 3;
  uint32_t rs2_offset = rs1_offset + 5;
  uint32_t imm10_5_offset = rs2_offset + 5;

  uint32_t imm4_0 = imm & 0b11111;
  uint32_t imm10_5 = (imm >> 5) & 0b111111;

  return opcode | (imm4_0 << imm4_0_offset) | (funct3 << funct3_offset) |
         (rs1 << rs1_offset) | (rs2 << rs2_offset) |
         (imm10_5 << imm10_5_offset) | (uint32_t(funct1) << 31);
}

// @return uint32_t
// @param opcode: uint8_t
// @param funct3: uint8_t
// @param rs1: uint8_t
// @para rs2: uint8_tm
// @param imm the lsb should be zero (will be ignored).
uint32_t instruc_btype(uint32_t opcode, uint32_t funct3, uint32_t rs1,
                       uint32_t rs2, int64_t imm) {
  assert(opcode < (1 << 7));
  assert(funct3 < 8);
  assert(rs1 < 32);
  assert(rs2 < 32);
  // assert(imm < (1 << 13))
  // assert(not (imm & 0b1))

  assert(opcode >= 0);
  assert(funct3 >= 0);
  assert(rs1 >= 0);
  assert(rs2 >= 0);
  // assert(imm >= 0)

  // imm = imm & 0b1111111111111;
  assert(imm < (1 << 12) && imm >= -(1 << 12));
  assert(!(imm & 1));

  uint32_t imm4_1_11_offset = 7;
  uint32_t funct3_offset = imm4_1_11_offset + 5;
  uint32_t rs1_offset = funct3_offset + 3;
  uint32_t rs2_offset = rs1_offset + 5;
  uint32_t imm12_10_5_offset = rs2_offset + 5;

  uint32_t imm4_1_11 = ((imm >> 11) & 1) | (((imm >> 1) & 0b1111) << 1);
  uint32_t imm12_10_5 = ((imm >> 5) & 0b111111) | (((imm >> 12) & 0b1) << 6);

  return opcode | (imm4_1_11 << imm4_1_11_offset) | (funct3 << funct3_offset) |
         (rs1 << rs1_offset) | (rs2 << rs2_offset) |
         (imm12_10_5 << imm12_10_5_offset);
}

// @return uint32_t
// @param opcode: uint8_t
// @param rd: uint8_t
uint32_t instruc_utype(uint32_t opcode, uint32_t rd, uint32_t uimm) {
  assert(opcode < (1 << 7));
  assert(rd < 32);
  // assert(imm < (1 << 32))

  assert(opcode >= 0);
  assert(rd >= 0);
  // assert(imm >= 0)

  uint32_t rd_offset = 7;
  uint32_t imm31_12_offset = rd_offset + 5;

  assert(uimm <= 0xFFFFF000u && (uimm & 0xFFF) == 0);
  uint32_t imm31_12 = uimm >> 12; //& 0b11111111111111111111;

  return opcode | rd << rd_offset | imm31_12 << imm31_12_offset;
}

// @return uint32_t
// @param opcode: uint8_t
// @param rd: uint8_t
// @param imm the lsb should be zero (will be ignored).
uint32_t instruc_jtype(uint32_t opcode, uint32_t rd, int64_t imm) {
  assert(opcode < (1 << 7));
  assert(rd < 32);
  // assert(imm < (1 << 20)) // totally wrong!!!
  assert(imm < (1 << 20) || imm >= -(1 << 20));
  assert(!(imm & 1));

  assert(opcode >= 0);
  assert(rd >= 0);

  uint32_t rd_offset = 7;
  uint32_t immparts_offset = rd_offset + 5;

  uint32_t immparts = ((imm >> 12) & 0b11111111) | (((imm >> 11) & 0b1) << 8) |
                      (((imm >> 1) & 0b1111111111) << 9) |
                      (((imm >> 20) & 0b1) << 19);

  return opcode | rd << rd_offset | immparts << immparts_offset;
}

// @return uint32_t
// @param opcode: uint8_t
// @param rd: uint8_t
// @param funct3: uint8_t
// @param rs1: uint8_t
// @param rs2: uint8_t
// @param rs3: uint8_t
// @param funct2: uint8_t
uint32_t instruc_r4type(uint32_t opcode, uint32_t rd, uint32_t funct3,
                        uint32_t rs1, uint32_t rs2, uint32_t rs3,
                        uint32_t funct2) {
  assert(opcode < (1 << 7));
  assert(rd < 32);
  assert(funct3 < 8);
  assert(rs1 < 32);
  assert(rs2 < 32);
  assert(rs3 < 32);
  assert(funct2 < (1 << 2));

  assert(opcode >= 0);
  assert(rd >= 0);
  assert(funct3 >= 0);
  assert(rs1 >= 0);
  assert(rs2 >= 0);
  assert(rs3 >= 0);
  assert(funct2 >= 0);

  uint32_t rd_offset = 7;
  uint32_t funct3_offset = rd_offset + 5;
  uint32_t rs1_offset = funct3_offset + 3;
  uint32_t rs2_offset = rs1_offset + 5;
  uint32_t funct2_offset = rs2_offset + 5;
  uint32_t rs3_offset = funct2_offset + 2;

  return opcode | (rd << rd_offset) | (funct3 << funct3_offset) |
         (rs1 << rs1_offset) | (rs2 << rs2_offset) | (funct2 << funct2_offset) |
         (rs3 << rs3_offset);
}

} // namespace fuzzgpu