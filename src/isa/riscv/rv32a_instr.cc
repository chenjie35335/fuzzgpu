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

#include "bug_filter.hh"
#include "isa/riscv/riscv_encode.hh"
#include "isa/riscv/riscv_meta_list.hh"

namespace fuzzgpu {

static const uint32_t RV32A_OPCODE_AMO = 0b0101111;

void riscv_meta_list::register_rv32a() {
#if defined(ARCH_VENTUS)
  if (!BugFilter::instance().filter("amo_dec")) {
    DEFINE_AMO_INSTR(
        riscv_instr_name_t::LR_W, riscv_instr_format_t::R_ORDERING_FORMAT,
        riscv_instr_category_t::LOAD, riscv_instr_group_t::RV32A,
        [](riscv_gen_R_ORDERING, bool aq, bool rl, uint32_t rd, uint32_t rs1,
           uint32_t rs2) {
          assert(rs2 == uint32_t(-1));
          return instruc_rtype(RV32A_OPCODE_AMO, rd, 0b010, rs1, 0b00000,
                               (rl) | (aq << 1) | 0b0001000);
        });
    DEFINE_AMO_INSTR(riscv_instr_name_t::SC_W,
                     riscv_instr_format_t::R_ORDERING_FORMAT,
                     riscv_instr_category_t::STORE, riscv_instr_group_t::RV32A,
                     [](riscv_gen_R_ORDERING, bool aq, bool rl, uint32_t rd,
                        uint32_t rs1, uint32_t rs2) {
                       return instruc_rtype(RV32A_OPCODE_AMO, rd, 0b010, rs1,
                                            rs2, (rl) | (aq << 1) | 0b0001100);
                     });

    DEFINE_AMO_INSTR(riscv_instr_name_t::AMOSWAP_W,
                     riscv_instr_format_t::R_ORDERING_FORMAT,
                     riscv_instr_category_t::AMO, riscv_instr_group_t::RV32A,
                     [](riscv_gen_R_ORDERING, bool aq, bool rl, uint32_t rd,
                        uint32_t rs1, uint32_t rs2) {
                       return instruc_rtype(RV32A_OPCODE_AMO, rd, 0b010, rs1,
                                            rs2, (rl) | (aq << 1) | 0b0000100);
                     });
    DEFINE_AMO_INSTR(riscv_instr_name_t::AMOADD_W,
                     riscv_instr_format_t::R_ORDERING_FORMAT,
                     riscv_instr_category_t::AMO, riscv_instr_group_t::RV32A,
                     [](riscv_gen_R_ORDERING, bool aq, bool rl, uint32_t rd,
                        uint32_t rs1, uint32_t rs2) {
                       return instruc_rtype(RV32A_OPCODE_AMO, rd, 0b010, rs1,
                                            rs2, (rl) | (aq << 1) | 0b0000000);
                     });
    DEFINE_AMO_INSTR(riscv_instr_name_t::AMOXOR_W,
                     riscv_instr_format_t::R_ORDERING_FORMAT,
                     riscv_instr_category_t::AMO, riscv_instr_group_t::RV32A,
                     [](riscv_gen_R_ORDERING, bool aq, bool rl, uint32_t rd,
                        uint32_t rs1, uint32_t rs2) {
                       return instruc_rtype(RV32A_OPCODE_AMO, rd, 0b010, rs1,
                                            rs2, (rl) | (aq << 1) | 0b0010000);
                     });
    DEFINE_AMO_INSTR(riscv_instr_name_t::AMOAND_W,
                     riscv_instr_format_t::R_ORDERING_FORMAT,
                     riscv_instr_category_t::AMO, riscv_instr_group_t::RV32A,
                     [](riscv_gen_R_ORDERING, bool aq, bool rl, uint32_t rd,
                        uint32_t rs1, uint32_t rs2) {
                       return instruc_rtype(RV32A_OPCODE_AMO, rd, 0b010, rs1,
                                            rs2, (rl) | (aq << 1) | 0b0110000);
                     });
    DEFINE_AMO_INSTR(riscv_instr_name_t::AMOOR_W,
                     riscv_instr_format_t::R_ORDERING_FORMAT,
                     riscv_instr_category_t::AMO, riscv_instr_group_t::RV32A,
                     [](riscv_gen_R_ORDERING, bool aq, bool rl, uint32_t rd,
                        uint32_t rs1, uint32_t rs2) {
                       return instruc_rtype(RV32A_OPCODE_AMO, rd, 0b010, rs1,
                                            rs2, (rl) | (aq << 1) | 0b0100000);
                     });
    DEFINE_AMO_INSTR(riscv_instr_name_t::AMOMIN_W,
                     riscv_instr_format_t::R_ORDERING_FORMAT,
                     riscv_instr_category_t::AMO, riscv_instr_group_t::RV32A,
                     [](riscv_gen_R_ORDERING, bool aq, bool rl, uint32_t rd,
                        uint32_t rs1, uint32_t rs2) {
                       return instruc_rtype(RV32A_OPCODE_AMO, rd, 0b010, rs1,
                                            rs2, (rl) | (aq << 1) | 0b1000000);
                     });
    DEFINE_AMO_INSTR(riscv_instr_name_t::AMOMAX_W,
                     riscv_instr_format_t::R_ORDERING_FORMAT,
                     riscv_instr_category_t::AMO, riscv_instr_group_t::RV32A,
                     [](riscv_gen_R_ORDERING, bool aq, bool rl, uint32_t rd,
                        uint32_t rs1, uint32_t rs2) {
                       return instruc_rtype(RV32A_OPCODE_AMO, rd, 0b010, rs1,
                                            rs2, (rl) | (aq << 1) | 0b1010000);
                     });
    DEFINE_AMO_INSTR(riscv_instr_name_t::AMOMINU_W,
                     riscv_instr_format_t::R_ORDERING_FORMAT,
                     riscv_instr_category_t::AMO, riscv_instr_group_t::RV32A,
                     [](riscv_gen_R_ORDERING, bool aq, bool rl, uint32_t rd,
                        uint32_t rs1, uint32_t rs2) {
                       return instruc_rtype(RV32A_OPCODE_AMO, rd, 0b010, rs1,
                                            rs2, (rl) | (aq << 1) | 0b1100000);
                     });
    DEFINE_AMO_INSTR(riscv_instr_name_t::AMOMAXU_W,
                     riscv_instr_format_t::R_ORDERING_FORMAT,
                     riscv_instr_category_t::AMO, riscv_instr_group_t::RV32A,
                     [](riscv_gen_R_ORDERING, bool aq, bool rl, uint32_t rd,
                        uint32_t rs1, uint32_t rs2) {
                       return instruc_rtype(RV32A_OPCODE_AMO, rd, 0b010, rs1,
                                            rs2, (rl) | (aq << 1) | 0b1110000);
                     });
  }
#endif
}

} // namespace fuzzgpu