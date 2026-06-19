# Copyright 2023 Flavien Solt, ETH Zurich.
# Licensed under the General Public License, Version 3.0, see LICENSE for details.
# SPDX-License-Identifier: GPL-3.0-only

import rv.rvprotoinstrs as rvprotoinstrs

RV64A_OPCODE_AMO = 0b0101111

# All functions return uint32_t
# AMO instructions use R-type format with aq/rl bits in funct7

# Helper to build funct7 with aq/rl bits
def _amo_funct7(funct5: int, aq: bool = False, rl: bool = False) -> int:
    return (funct5 << 2) | (int(aq) << 1) | int(rl)

# lr.d rd, (rs1) - Load-Reserved Doubleword
def rv64a_lr_d(rd: int, rs1: int, aq: bool = False, rl: bool = False):
    return rvprotoinstrs.instruc_rtype(RV64A_OPCODE_AMO, rd, 0b011, rs1, 0b00000, _amo_funct7(0b00010, aq, rl))

# sc.d rd, rs2, (rs1) - Store-Conditional Doubleword
def rv64a_sc_d(rd: int, rs1: int, rs2: int, aq: bool = False, rl: bool = False):
    return rvprotoinstrs.instruc_rtype(RV64A_OPCODE_AMO, rd, 0b011, rs1, rs2, _amo_funct7(0b00011, aq, rl))

# amoswap.d rd, rs2, (rs1)
def rv64a_amoswap_d(rd: int, rs1: int, rs2: int, aq: bool = False, rl: bool = False):
    return rvprotoinstrs.instruc_rtype(RV64A_OPCODE_AMO, rd, 0b011, rs1, rs2, _amo_funct7(0b00001, aq, rl))

# amoadd.d rd, rs2, (rs1)
def rv64a_amoadd_d(rd: int, rs1: int, rs2: int, aq: bool = False, rl: bool = False):
    return rvprotoinstrs.instruc_rtype(RV64A_OPCODE_AMO, rd, 0b011, rs1, rs2, _amo_funct7(0b00000, aq, rl))

# amoxor.d rd, rs2, (rs1)
def rv64a_amoxor_d(rd: int, rs1: int, rs2: int, aq: bool = False, rl: bool = False):
    return rvprotoinstrs.instruc_rtype(RV64A_OPCODE_AMO, rd, 0b011, rs1, rs2, _amo_funct7(0b00100, aq, rl))

# amoand.d rd, rs2, (rs1)
def rv64a_amoand_d(rd: int, rs1: int, rs2: int, aq: bool = False, rl: bool = False):
    return rvprotoinstrs.instruc_rtype(RV64A_OPCODE_AMO, rd, 0b011, rs1, rs2, _amo_funct7(0b01100, aq, rl))

# amoor.d rd, rs2, (rs1)
def rv64a_amoor_d(rd: int, rs1: int, rs2: int, aq: bool = False, rl: bool = False):
    return rvprotoinstrs.instruc_rtype(RV64A_OPCODE_AMO, rd, 0b011, rs1, rs2, _amo_funct7(0b01000, aq, rl))

# amomin.d rd, rs2, (rs1)
def rv64a_amomin_d(rd: int, rs1: int, rs2: int, aq: bool = False, rl: bool = False):
    return rvprotoinstrs.instruc_rtype(RV64A_OPCODE_AMO, rd, 0b011, rs1, rs2, _amo_funct7(0b10000, aq, rl))

# amomax.d rd, rs2, (rs1)
def rv64a_amomax_d(rd: int, rs1: int, rs2: int, aq: bool = False, rl: bool = False):
    return rvprotoinstrs.instruc_rtype(RV64A_OPCODE_AMO, rd, 0b011, rs1, rs2, _amo_funct7(0b10100, aq, rl))

# amominu.d rd, rs2, (rs1)
def rv64a_amominu_d(rd: int, rs1: int, rs2: int, aq: bool = False, rl: bool = False):
    return rvprotoinstrs.instruc_rtype(RV64A_OPCODE_AMO, rd, 0b011, rs1, rs2, _amo_funct7(0b11000, aq, rl))

# amomaxu.d rd, rs2, (rs1)
def rv64a_amomaxu_d(rd: int, rs1: int, rs2: int, aq: bool = False, rl: bool = False):
    return rvprotoinstrs.instruc_rtype(RV64A_OPCODE_AMO, rd, 0b011, rs1, rs2, _amo_funct7(0b11100, aq, rl))
