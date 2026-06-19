# Copyright 2023 Flavien Solt, ETH Zurich.
# Licensed under the General Public License, Version 3.0, see LICENSE for details.
# SPDX-License-Identifier: GPL-3.0-only

from cascade.cfinstructionclasses import WSpawnInstruction, ImmRdInstruction, RegImmInstruction
from rv.asmutil import li_into_reg
import random

def gen_wspawn_instrs(fuzzerstate):
    instrs = []
    
    # 1. Setup count (rs1)
    # Pick a random count, say 2 to 4 warps
    count = random.randint(2, 4)
    rs1 = fuzzerstate.intregpickstate.pick_int_outputreg()
    # Load count into rs1
    # We can use addi x, x0, imm if imm is small
    instrs.append(RegImmInstruction("addi", rs1, 0, count, fuzzerstate.is_design_64bit))
    
    # 2. Setup address (rs2)
    # We want to jump to next_bb_addr
    target_addr = fuzzerstate.next_bb_addr
    rs2 = fuzzerstate.intregpickstate.pick_int_outputreg()
    # Load target_addr into rs2
    # Use li_into_reg logic
    lui_imm, addi_imm = li_into_reg(target_addr, False)
    instrs.append(ImmRdInstruction("lui", rs2, lui_imm, fuzzerstate.is_design_64bit))
    instrs.append(RegImmInstruction("addi", rs2, rs2, addi_imm, fuzzerstate.is_design_64bit))
    if fuzzerstate.is_design_64bit:
        # Clear the top 32 bits
        # Note: RegImmInstruction expects fuzzerstate.is_design_64bit as bool, but initialblock.py passed fuzzerstate?
        # Let's check initialblock.py again.
        # fuzzerstate.instr_objs_seq[-1].append(RegImmInstruction("slli", RELOCATOR_REGISTER_ID, RELOCATOR_REGISTER_ID, 32, fuzzerstate))
        # Wait, RegImmInstruction constructor:
        # def __init__(self, instr_str: str, rd: int, rs1: int, imm: int, is_design_64bit: bool, iscompressed: bool = False):
        # So passing fuzzerstate as is_design_64bit is wrong if fuzzerstate is an object.
        # But maybe fuzzerstate behaves like bool? No.
        # Let's check initialblock.py again.
        pass

    if fuzzerstate.is_design_64bit:
         instrs.append(RegImmInstruction("slli", rs2, rs2, 32, fuzzerstate.is_design_64bit))
         instrs.append(RegImmInstruction("srli", rs2, rs2, 32, fuzzerstate.is_design_64bit))
    
    # 3. WSPAWN
    instrs.append(WSpawnInstruction(rs1, rs2))
    
    return instrs
