# Copyright 2023 Flavien Solt, ETH Zurich.
# Licensed under the General Public License, Version 3.0, see LICENSE for details.
# SPDX-License-Identifier: GPL-3.0-only

# Zicond extension: conditional zero instructions

import rv.rvprotoinstrs as rvprotoinstrs

ZICOND_OPCODE = 0b0110011  # Same as RV32I ALU_REG

# czero.eqz rd, rs1, rs2
# If rs2 is equal to zero, output zero (rd = 0), else output rs1 (rd = rs1)
def zicond_czero_eqz(rd: int, rs1: int, rs2: int):
    return rvprotoinstrs.instruc_rtype(ZICOND_OPCODE, rd, 0b101, rs1, rs2, 0b0000111)

# czero.nez rd, rs1, rs2
# If rs2 is not equal to zero, output zero (rd = 0), else output rs1 (rd = rs1)
def zicond_czero_nez(rd: int, rs1: int, rs2: int):
    return rvprotoinstrs.instruc_rtype(ZICOND_OPCODE, rd, 0b111, rs1, rs2, 0b0000111)
