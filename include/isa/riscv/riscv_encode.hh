#pragma once

#include <cstdint>

namespace fuzzgpu {

uint32_t instruc_rtype(uint32_t opcode, uint32_t rd, uint32_t funct3,
                       uint32_t rs1, uint32_t rs2, uint32_t funct7);

uint64_t instruc_itype(uint64_t opcode, uint64_t rd, uint64_t funct3,
                       uint64_t rs1, int64_t imm);

uint64_t instruc_itype_csr(uint64_t opcode, uint64_t rd, uint64_t funct3,
                           uint64_t rs1, uint16_t csr);

uint64_t instruc_itype_funct1(uint64_t opcode, uint64_t rd, uint64_t funct3,
                              uint64_t rs1, int64_t imm, bool funct1);

uint32_t instruc_stype(uint32_t opcode, uint32_t funct3, uint32_t rs1,
                       uint32_t rs2, int64_t imm);

uint32_t instruc_stype_funct1(uint32_t opcode, uint32_t funct3, uint32_t rs1,
                              uint32_t rs2, int64_t imm, bool funct1);

uint32_t instruc_btype(uint32_t opcode, uint32_t funct3, uint32_t rs1,
                       uint32_t rs2, int64_t imm);

uint32_t instruc_utype(uint32_t opcode, uint32_t rd, uint32_t uimm);

uint32_t instruc_jtype(uint32_t opcode, uint32_t rd, int64_t imm);

uint32_t instruc_r4type(uint32_t opcode, uint32_t rd, uint32_t funct3,
                        uint32_t rs1, uint32_t rs2, uint32_t rs3,
                        uint32_t funct2);
} // namespace fuzzgpu
