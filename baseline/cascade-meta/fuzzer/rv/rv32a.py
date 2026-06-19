# Copyright 2023 Flavien Solt, ETH Zurich.
# Licensed under the General Public License, Version 3.0, see LICENSE for details.
# SPDX-License-Identifier: GPL-3.0-only

import rv.rvprotoinstrs as rvprotoinstrs

RV32A_OPCODE_AMO = 0b0101111

# All functions return uint32_t
# AMO instructions use R-type format with aq/rl bits in funct7

# Helper to build funct7 with aq/rl bits
def _amo_funct7(funct5: int, aq: bool = False, rl: bool = False) -> int:
    return (funct5 << 2) | (int(aq) << 1) | int(rl)

# lr.w rd, (rs1) - Load-Reserved Word
def rv32a_lr_w(rd: int, rs1: int, aq: bool = False, rl: bool = False):
    return rvprotoinstrs.instruc_rtype(RV32A_OPCODE_AMO, rd, 0b010, rs1, 0b00000, _amo_funct7(0b00010, aq, rl))

# sc.w rd, rs2, (rs1) - Store-Conditional Word
def rv32a_sc_w(rd: int, rs1: int, rs2: int, aq: bool = False, rl: bool = False):
    return rvprotoinstrs.instruc_rtype(RV32A_OPCODE_AMO, rd, 0b010, rs1, rs2, _amo_funct7(0b00011, aq, rl))

# amoswap.w rd, rs2, (rs1)
def rv32a_amoswap_w(rd: int, rs1: int, rs2: int, aq: bool = False, rl: bool = False):
    return rvprotoinstrs.instruc_rtype(RV32A_OPCODE_AMO, rd, 0b010, rs1, rs2, _amo_funct7(0b00001, aq, rl))

# amoadd.w rd, rs2, (rs1)
def rv32a_amoadd_w(rd: int, rs1: int, rs2: int, aq: bool = False, rl: bool = False):
    return rvprotoinstrs.instruc_rtype(RV32A_OPCODE_AMO, rd, 0b010, rs1, rs2, _amo_funct7(0b00000, aq, rl))

# amoxor.w rd, rs2, (rs1)
def rv32a_amoxor_w(rd: int, rs1: int, rs2: int, aq: bool = False, rl: bool = False):
    return rvprotoinstrs.instruc_rtype(RV32A_OPCODE_AMO, rd, 0b010, rs1, rs2, _amo_funct7(0b00100, aq, rl))

# amoand.w rd, rs2, (rs1)
def rv32a_amoand_w(rd: int, rs1: int, rs2: int, aq: bool = False, rl: bool = False):
    return rvprotoinstrs.instruc_rtype(RV32A_OPCODE_AMO, rd, 0b010, rs1, rs2, _amo_funct7(0b01100, aq, rl))

# amoor.w rd, rs2, (rs1)
def rv32a_amoor_w(rd: int, rs1: int, rs2: int, aq: bool = False, rl: bool = False):
    return rvprotoinstrs.instruc_rtype(RV32A_OPCODE_AMO, rd, 0b010, rs1, rs2, _amo_funct7(0b01000, aq, rl))

# amomin.w rd, rs2, (rs1)
def rv32a_amomin_w(rd: int, rs1: int, rs2: int, aq: bool = False, rl: bool = False):
    return rvprotoinstrs.instruc_rtype(RV32A_OPCODE_AMO, rd, 0b010, rs1, rs2, _amo_funct7(0b10000, aq, rl))

# amomax.w rd, rs2, (rs1)
def rv32a_amomax_w(rd: int, rs1: int, rs2: int, aq: bool = False, rl: bool = False):
    return rvprotoinstrs.instruc_rtype(RV32A_OPCODE_AMO, rd, 0b010, rs1, rs2, _amo_funct7(0b10100, aq, rl))

# amominu.w rd, rs2, (rs1)
def rv32a_amominu_w(rd: int, rs1: int, rs2: int, aq: bool = False, rl: bool = False):
    return rvprotoinstrs.instruc_rtype(RV32A_OPCODE_AMO, rd, 0b010, rs1, rs2, _amo_funct7(0b11000, aq, rl))

# amomaxu.w rd, rs2, (rs1)
def rv32a_amomaxu_w(rd: int, rs1: int, rs2: int, aq: bool = False, rl: bool = False):
    return rvprotoinstrs.instruc_rtype(RV32A_OPCODE_AMO, rd, 0b010, rs1, rs2, _amo_funct7(0b11100, aq, rl))
