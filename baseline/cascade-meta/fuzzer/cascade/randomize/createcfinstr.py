# Copyright 2023 Flavien Solt, ETH Zurich.
# Licensed under the General Public License, Version 3.0, see LICENSE for details.
# SPDX-License-Identifier: GPL-3.0-only

import random
import numpy as np

from params.runparams import DO_ASSERT

from params.fuzzparams import NUM_MIN_FREE_INTREGS, REG_FSM_WEIGHTS, NONTAKEN_BRANCH_INTO_RANDOM_DATA_PROBA
from cascade.bugfilter import get_bug_filter
from cascade.util import IntRegIndivState
from cascade.cfinstructionclasses import *
from rv.util import PARAM_REGTYPE, PARAM_SIZES_BITS_32, PARAM_SIZES_BITS_64

# This module creates an instruction from its instruction string, and some state which will condition which registers and immediates will be picked, and with which probability.

###
# Utility functions
###

def gen_random_imm(instr_str: str, is_design_64bit: bool):
    if DO_ASSERT:
        assert PARAM_REGTYPE[INSTRUCTION_IDS[instr_str]][-1] == ''
    if is_design_64bit:
        imm_width = PARAM_SIZES_BITS_64[INSTRUCTION_IDS[instr_str]][-1]
    else:
        imm_width = PARAM_SIZES_BITS_32[INSTRUCTION_IDS[instr_str]][-1]
    if PARAM_IS_SIGNED[INSTRUCTION_IDS[instr_str]][-1]:
        left_bound  = -(1<<(imm_width-1))
        right_bound = 1<<(imm_width-1)
    else:
        left_bound  = 0
        right_bound = 1<<imm_width
    return random.randrange(left_bound, right_bound)

# Random rounding modes
def gen_random_rounding_mode():
    return random.sample([0, 1, 2, 3, 4, 7], 1)[0]

###
# Functions for creation by CFInstructionClass
###

# Integer instructions

def _create_R12DInstruction(instr_str: str, fuzzerstate, iscompressed: bool):
    if DO_ASSERT:
        assert instr_str in R12DInstructions
    rs1, rs2 = tuple(fuzzerstate.intregpickstate.pick_int_inputregs(2))
    rd = fuzzerstate.intregpickstate.pick_int_outputreg()
    return R12DInstruction(instr_str, rd, rs1, rs2, iscompressed)

def _create_ImmRdInstruction(instr_str: str, fuzzerstate, iscompressed: bool):
    if DO_ASSERT:
        assert instr_str in ImmRdInstructions
    rd = fuzzerstate.intregpickstate.pick_int_outputreg()
    imm = gen_random_imm(instr_str, fuzzerstate.is_design_64bit)
    if instr_str == "auipc" and rd > 0:
        fuzzerstate.intregpickstate.set_regstate(rd, IntRegIndivState.FREE)
    return ImmRdInstruction(instr_str, rd, imm, fuzzerstate.is_design_64bit, iscompressed)

def _create_RegImmInstruction(instr_str: str, fuzzerstate, iscompressed: bool):
    if DO_ASSERT:
        assert instr_str in RegImmInstructions
    rs1 = fuzzerstate.intregpickstate.pick_int_inputreg()
    rd = fuzzerstate.intregpickstate.pick_int_outputreg()
    imm = gen_random_imm(instr_str, fuzzerstate.is_design_64bit)
    return RegImmInstruction(instr_str, rd, rs1, imm, fuzzerstate.is_design_64bit, iscompressed)

def _create_BranchInstruction(instr_str: str, fuzzerstate, curr_addr: int, iscompressed: bool):
    if DO_ASSERT:
        assert instr_str in BranchInstructions
    rs1 = fuzzerstate.intregpickstate.pick_int_inputreg()
    rs2 = fuzzerstate.intregpickstate.pick_int_inputreg()
    plan_taken = fuzzerstate.curr_branch_taken
    if plan_taken:
        # print('A', flush=True)
        imm = fuzzerstate.next_bb_addr-curr_addr
    else:
        # Select whether to direct toward the random data basic block
        is_random_data_block_in_reach = abs(fuzzerstate.random_data_block_start_addr - curr_addr) < (1<<11) and abs(fuzzerstate.random_data_block_end_addr-4 - curr_addr) < (1<<11)
        if is_random_data_block_in_reach and random.random() < NONTAKEN_BRANCH_INTO_RANDOM_DATA_PROBA:
            lowest_random_data_reachable_addr = max(fuzzerstate.random_data_block_start_addr+4, curr_addr - (1<<11))
            highest_random_data_reachable_addr = min(fuzzerstate.random_data_block_end_addr-4, curr_addr + (1<<11))

            target_addr_in_random_data_block = random.randrange(lowest_random_data_reachable_addr//2, highest_random_data_reachable_addr//2)*2
            imm = target_addr_in_random_data_block-curr_addr
        else:
            imm = gen_random_imm(instr_str, fuzzerstate.is_design_64bit)
    
    # print('New imm', hex(imm), flush=True)
    return BranchInstruction(instr_str, rs1, rs2, imm, plan_taken, fuzzerstate.is_design_64bit, iscompressed)

def _create_JALInstruction(instr_str: str, fuzzerstate, curr_addr: int, iscompressed: bool):
    rd = fuzzerstate.intregpickstate.pick_int_outputreg()
    imm = fuzzerstate.next_bb_addr-curr_addr
    if rd > 0:
        fuzzerstate.intregpickstate.set_regstate(rd, IntRegIndivState.FREE)
    return JALInstruction(instr_str, rd, imm, iscompressed)
def _create_JALRInstruction(instr_str: str, fuzzerstate, iscompressed: bool):
    rs1 = fuzzerstate.intregpickstate.pick_int_reg_in_state(IntRegIndivState.CONSUMED)
    rd = fuzzerstate.intregpickstate.pick_int_outputreg()
    imm = 0
    producer_id = fuzzerstate.intregpickstate.get_producer_id(rs1)
    fuzzerstate.intregpickstate.set_regstate(rs1, IntRegIndivState.FREE)
    if rd > 0:
        fuzzerstate.intregpickstate.set_regstate(rd, IntRegIndivState.FREE)
    if DO_ASSERT:
        assert producer_id > 0
    return JALRInstruction(instr_str, rd, rs1, imm, producer_id, fuzzerstate.is_design_64bit, iscompressed)
def _create_SpecialInstruction(instr_str: str, fuzzerstate, iscompressed: bool):
    rd = fuzzerstate.intregpickstate.pick_int_outputreg()
    rs1 = fuzzerstate.intregpickstate.pick_int_inputreg()
    return SpecialInstruction(instr_str, rd, rs1, iscompressed)

def _create_IntLoadInstruction(instr_str: str, fuzzerstate, iscompressed: bool):
    if DO_ASSERT:
        assert instr_str in IntLoadInstructions
    rs1 = fuzzerstate.intregpickstate.pick_int_reg_in_state(IntRegIndivState.CONSUMED)
    rd = fuzzerstate.intregpickstate.pick_int_outputreg()
    imm = 0
    producer_id = fuzzerstate.intregpickstate.get_producer_id(rs1)
    fuzzerstate.intregpickstate.set_regstate(rs1, IntRegIndivState.FREE)
    if DO_ASSERT:
        assert producer_id > 0
    return IntLoadInstruction(instr_str, rd, rs1, imm, producer_id, fuzzerstate.is_design_64bit, iscompressed)

def _create_IntStoreInstruction(instr_str: str, fuzzerstate, iscompressed: bool):
    if DO_ASSERT:
        assert instr_str in IntStoreInstructions
    rs1 = fuzzerstate.intregpickstate.pick_int_reg_in_state(IntRegIndivState.CONSUMED)
    rs2 = fuzzerstate.intregpickstate.pick_int_inputreg()
    imm = 0
    producer_id = fuzzerstate.intregpickstate.get_producer_id(rs1)
    fuzzerstate.intregpickstate.set_regstate(rs1, IntRegIndivState.FREE)
    if DO_ASSERT:
        assert producer_id > 0
    return IntStoreInstruction(instr_str, rs1, rs2, imm, producer_id, fuzzerstate.is_design_64bit, iscompressed)

# Atomic instructions

def _create_AMOInstruction(instr_str: str, fuzzerstate, iscompressed: bool):
    if DO_ASSERT:
        assert instr_str in AMOInstructions or instr_str in AMO64Instructions
    # rs1 is the address register (must be a consumed register with valid address)
    rs1 = fuzzerstate.intregpickstate.pick_int_reg_in_state(IntRegIndivState.CONSUMED)
    rd = fuzzerstate.intregpickstate.pick_int_outputreg()
    producer_id = fuzzerstate.intregpickstate.get_producer_id(rs1)
    fuzzerstate.intregpickstate.set_regstate(rs1, IntRegIndivState.FREE)
    
    # For lr instructions, rs2 is 0 (not used)
    if instr_str in ("lr.w", "lr.d"):
        rs2 = 0
    else:
        rs2 = fuzzerstate.intregpickstate.pick_int_inputreg()
    
    # Randomly choose aq/rl bits
    aq = random.choice([True, False])
    rl = random.choice([True, False])
    
    if DO_ASSERT:
        assert producer_id > 0
    return AMOInstruction(instr_str, rd, rs1, rs2, producer_id, aq, rl, iscompressed)

# Floating-point instructions

def _create_FloatLoadInstruction  (instr_str: str, fuzzerstate, iscompressed: bool):
    if DO_ASSERT:
        assert instr_str in FloatLoadInstructions
    rs1 = fuzzerstate.intregpickstate.pick_int_reg_in_state(IntRegIndivState.CONSUMED)
    frd = fuzzerstate.floatregpickstate.pick_float_outputreg()
    imm = 0
    producer_id = fuzzerstate.intregpickstate.get_producer_id(rs1)
    fuzzerstate.intregpickstate.set_regstate(rs1, IntRegIndivState.FREE)
    if DO_ASSERT:
        assert producer_id > 0
    return FloatLoadInstruction(instr_str, frd, rs1, imm, producer_id, fuzzerstate.is_design_64bit, iscompressed, design_name=fuzzerstate.design_name)
def _create_FloatStoreInstruction (instr_str: str, fuzzerstate, iscompressed: bool):
    if DO_ASSERT:
        assert instr_str in FloatStoreInstructions
    rs1 = fuzzerstate.intregpickstate.pick_int_reg_in_state(IntRegIndivState.CONSUMED)
    frs2 = fuzzerstate.floatregpickstate.pick_float_inputreg()
    imm = 0
    producer_id = fuzzerstate.intregpickstate.get_producer_id(rs1)
    fuzzerstate.intregpickstate.set_regstate(rs1, IntRegIndivState.FREE)
    if DO_ASSERT:
        assert producer_id > 0
    return FloatStoreInstruction(instr_str, rs1, frs2, imm, producer_id, fuzzerstate.is_design_64bit, iscompressed, design_name=fuzzerstate.design_name)
def _create_FloatToIntInstruction (instr_str: str, fuzzerstate, iscompressed: bool):
    if DO_ASSERT:
        assert instr_str in FloatToIntInstructions
    rm = gen_random_rounding_mode()
    frs1 = fuzzerstate.floatregpickstate.pick_float_inputreg()
    if get_bug_filter().allow_float_int_zero_dest():
        rd = fuzzerstate.intregpickstate.pick_int_outputreg()
    else:
        rd = fuzzerstate.intregpickstate.pick_int_outputreg_nonzero()
    return FloatToIntInstruction(instr_str, rd, frs1, rm, iscompressed)
def _create_IntToFloatInstruction (instr_str: str, fuzzerstate, iscompressed: bool):
    if DO_ASSERT:
        assert instr_str in IntToFloatInstructions
    rm = gen_random_rounding_mode()
    rs1 = fuzzerstate.intregpickstate.pick_int_inputreg()
    frd = fuzzerstate.floatregpickstate.pick_float_outputreg()
    return IntToFloatInstruction(instr_str, frd, rs1, rm, iscompressed)
def _create_Float4Instruction     (instr_str: str, fuzzerstate, iscompressed: bool):
    if DO_ASSERT:
        assert instr_str in Float4Instructions
    rm = gen_random_rounding_mode()
    frs1, frs2, frs3 = tuple(fuzzerstate.floatregpickstate.pick_float_inputregs(3))
    frd = fuzzerstate.floatregpickstate.pick_float_outputreg()
    return Float4Instruction(instr_str, frd, frs1, frs2, frs3, rm, iscompressed)
def _create_Float3Instruction     (instr_str: str, fuzzerstate, iscompressed: bool):
    if DO_ASSERT:
        assert instr_str in Float3Instructions
    rm = gen_random_rounding_mode()
    frs1, frs2 = tuple(fuzzerstate.floatregpickstate.pick_float_inputregs(2))
    frd = fuzzerstate.floatregpickstate.pick_float_outputreg()
    return Float3Instruction(instr_str, frd, frs1, frs2, rm, iscompressed)
def _create_Float3NoRmInstruction (instr_str: str, fuzzerstate, iscompressed: bool):
    if DO_ASSERT:
        assert instr_str in Float3NoRmInstructions
    frs1, frs2 = tuple(fuzzerstate.floatregpickstate.pick_float_inputregs(2))
    frd = fuzzerstate.floatregpickstate.pick_float_outputreg()
    return Float3NoRmInstruction(instr_str, frd, frs1, frs2, iscompressed)
def _create_Float2Instruction     (instr_str: str, fuzzerstate, iscompressed: bool):
    if DO_ASSERT:
        assert instr_str in Float2Instructions
    rm = gen_random_rounding_mode()
    frs1 = fuzzerstate.floatregpickstate.pick_float_inputreg()
    frd = fuzzerstate.floatregpickstate.pick_float_outputreg()
    return Float2Instruction(instr_str, frd, frs1, rm, iscompressed)
def _create_FloatIntRd2Instruction(instr_str: str, fuzzerstate, iscompressed: bool):
    if DO_ASSERT:
        assert instr_str in FloatIntRd2Instructions
    frs1, frs2 = tuple(fuzzerstate.floatregpickstate.pick_float_inputregs(2))
    if get_bug_filter().allow_float_int_zero_dest():
        rd = fuzzerstate.intregpickstate.pick_int_outputreg()
    else:
        rd = fuzzerstate.intregpickstate.pick_int_outputreg_nonzero()
    return FloatIntRd2Instruction(instr_str, rd, frs1, frs2, iscompressed)
def _create_FloatIntRd1Instruction(instr_str: str, fuzzerstate, iscompressed: bool):
    if DO_ASSERT:
        assert instr_str in FloatIntRd1Instructions
    frs1 = fuzzerstate.floatregpickstate.pick_float_inputreg()
    if get_bug_filter().allow_float_int_zero_dest():
        rd = fuzzerstate.intregpickstate.pick_int_outputreg()
    else:
        rd = fuzzerstate.intregpickstate.pick_int_outputreg_nonzero()
    return FloatIntRd1Instruction(instr_str, rd, frs1, fuzzerstate.is_design_64bit, fuzzerstate.design_name, iscompressed)
def _create_FloatIntRs1Instruction(instr_str: str, fuzzerstate, iscompressed: bool):
    if DO_ASSERT:
        assert instr_str in FloatIntRs1Instructions
    frd = fuzzerstate.floatregpickstate.pick_float_outputreg()
    rs1 = fuzzerstate.intregpickstate.pick_int_inputreg()
    return FloatIntRs1Instruction(instr_str, frd, rs1, fuzzerstate.is_design_64bit, fuzzerstate.design_name, iscompressed)

###
# Exposed function
###

def create_regfsm_instrobjs(fuzzerstate):
    # Check which reg fsm operations are doable
    doable_fsm_ops = np.zeros(3, dtype=np.int8)
    doable_fsm_ops[0] = fuzzerstate.intregpickstate.get_num_regs_in_state(IntRegIndivState.FREE) > NUM_MIN_FREE_INTREGS
    doable_fsm_ops[1] = fuzzerstate.intregpickstate.exists_reg_in_state(IntRegIndivState.PRODUCED0)
    doable_fsm_ops[2] = fuzzerstate.intregpickstate.exists_reg_in_state(IntRegIndivState.PRODUCED1)

    effective_weights = doable_fsm_ops * REG_FSM_WEIGHTS

    choice = None
    while choice is None or not doable_fsm_ops[choice]:
        choice = random.choices(range(3), effective_weights, k=1)[0]

    if choice == 0: # FREE -> PRODUCED0
        return create_targeted_producer0_instrobj(fuzzerstate)
    elif choice == 1: # PRODUCED0 -> PRODUCED1
        return create_targeted_producer1_instrobj(fuzzerstate)
    elif choice == 2: # PRODUCED1 -> FREE/CONSUMED
        return create_targeted_consumer_instrobj(fuzzerstate)
    else:
        raise ValueError(f"Unexpected choice: `{choice}`.")

def _create_VXBarInstruction(instr_str: str, fuzzerstate, iscompressed: bool):
    if DO_ASSERT:
        assert instr_str in VXBarInstructions
    rs1 = fuzzerstate.intregpickstate.pick_int_inputreg()
    rs2 = fuzzerstate.intregpickstate.pick_int_inputreg()
    return VXBarInstruction(rs1, rs2)

def _create_RVVLoadInstruction(instr_str: str, fuzzerstate, iscompressed: bool):
    if DO_ASSERT:
        assert instr_str in RVVLoadInstructions
    
    vm = 1 # Masking disabled by default
    vd = 0
    rs1 = 0
    rs2 = 0
    vs1 = 0
    vs2 = 0
    imm = 0
    producer_id = 0

    if instr_str in ("vlb12.v", "vlbu12.v", "vlh12.v", "vlhu12.v", "vlw12.v"):
        # the base address is vector, which is impossible to apply the original reg fsm
        vd = fuzzerstate.vectorpickstate.pick_vector_outputreg()
        vs1 = fuzzerstate.vectorpickstate.pick_vector_inputreg()
        imm = random.randint(-2048, 2047)
    
    elif instr_str in ("vlb.v", "vlbu.v", "vlh.v", "vlhu.v", "vlw.v"):
        vd = fuzzerstate.vectorpickstate.pick_vector_outputreg()
        rs1 = fuzzerstate.intregpickstate.pick_int_reg_in_state(IntRegIndivState.CONSUMED)
        producer_id = fuzzerstate.intregpickstate.get_producer_id(rs1)
        fuzzerstate.intregpickstate.set_regstate(rs1, IntRegIndivState.FREE)
        if DO_ASSERT:
            assert producer_id > 0
        imm = 0

    elif instr_str in ("vle32.v", "vloxei32.v", "vlse32.v"):
        vd = fuzzerstate.vectorpickstate.pick_vector_outputreg()
        rs1 = fuzzerstate.intregpickstate.pick_int_reg_in_state(IntRegIndivState.CONSUMED)
        producer_id = fuzzerstate.intregpickstate.get_producer_id(rs1)
        fuzzerstate.intregpickstate.set_regstate(rs1, IntRegIndivState.FREE)
        if DO_ASSERT:
            assert producer_id > 0
        if instr_str == "vloxei32.v":
            vs2 = fuzzerstate.vectorpickstate.pick_vector_inputreg()
        elif instr_str == "vlse32.v":
            rs2 = fuzzerstate.intregpickstate.pick_int_inputreg()
    
    else:
        raise ValueError(f"Unexpected RVV load instruction string: `{instr_str}`")

    return RVVLoadInstruction(instr_str, vd, rs1, vs1, imm, vm, producer_id, iscompressed, rs2=rs2, vs2=vs2)

def _create_RVVStoreInstruction(instr_str: str, fuzzerstate, iscompressed: bool):
    if DO_ASSERT:
        assert instr_str in RVVStoreInstructions

    vm = 1
    vd = 0
    rs1 = 0
    rs2 = 0
    vs1 = 0
    vs2 = 0
    imm = 0
    producer_id = 0

    if instr_str in ("vsb12.v", "vsh12.v", "vsw12.v"):
        # the base address is vector, which is impossible to apply the original reg fsm
        vs1 = fuzzerstate.vectorpickstate.pick_vector_inputreg()
        vs2 = fuzzerstate.vectorpickstate.pick_vector_inputreg()
        imm = random.randint(-2048, 2047)
    
    elif instr_str in ("vsb.v", "vsh.v", "vsw.v"):
        rs1 = fuzzerstate.intregpickstate.pick_int_reg_in_state(IntRegIndivState.CONSUMED)
        producer_id = fuzzerstate.intregpickstate.get_producer_id(rs1)
        fuzzerstate.intregpickstate.set_regstate(rs1, IntRegIndivState.FREE)
        if DO_ASSERT:
            assert producer_id > 0
        vs2 = fuzzerstate.vectorpickstate.pick_vector_inputreg()
        imm = 0

    elif instr_str in ("vse32.v", "vsoxei32.v", "vsse32.v"):
        vd = fuzzerstate.vectorpickstate.pick_vector_inputreg() # vs3 is input data
        rs1 = fuzzerstate.intregpickstate.pick_int_reg_in_state(IntRegIndivState.CONSUMED)
        producer_id = fuzzerstate.intregpickstate.get_producer_id(rs1)
        fuzzerstate.intregpickstate.set_regstate(rs1, IntRegIndivState.FREE)
        if DO_ASSERT:
            assert producer_id > 0
        if instr_str == "vsoxei32.v":
            vs2 = fuzzerstate.vectorpickstate.pick_vector_inputreg()
        elif instr_str == "vsse32.v":
            rs2 = fuzzerstate.intregpickstate.pick_int_inputreg()
    
    else:
        raise ValueError(f"Unexpected RVV store instruction string: `{instr_str}`")

    return RVVStoreInstruction(instr_str, vd, rs1, vs1, vs2, imm, vm, producer_id, iscompressed, rs2=rs2)

def _create_VentusBarInstruction(instr_str: str, fuzzerstate, iscompressed: bool):
    if DO_ASSERT:
        assert instr_str == "barrier"
    return VentusBarInstruction()

def _create_RVVInstruction(instr_str: str, fuzzerstate, iscompressed: bool, curr_addr: int = 0):
    if DO_ASSERT:
        assert instr_str in RVVInstructions or instr_str == "vsetvli"
    
    vm = 1 # Masking disabled by default

    if instr_str == "vsetvli":
        rd = fuzzerstate.intregpickstate.pick_int_outputreg()
        rs1 = fuzzerstate.intregpickstate.pick_int_inputreg()
        # vtypei: 11 bits.
        sew = random.choice([0, 1, 2, 3])
        lmul = random.choice([0, 1, 2, 3, 5, 6, 7])
        ta = random.randint(0, 1)
        ma = random.randint(0, 1)
        vtypei = (ma << 7) | (ta << 6) | (sew << 3) | lmul
        return RVVInstruction(instr_str, vd=rd, rs1=rs1, imm=vtypei, iscompressed=iscompressed)

    elif instr_str == "vadd12.vi":
        vd = fuzzerstate.vectorpickstate.pick_vector_outputreg()
        vs1 = fuzzerstate.vectorpickstate.pick_vector_inputreg()
        imm = random.randint(-2048, 2047)
        return RVVInstruction(instr_str, vd=vd, vs1=vs1, imm=imm, iscompressed=iscompressed)

    elif instr_str == "vfexp.v":
        vd = fuzzerstate.vectorpickstate.pick_vector_outputreg()
        vs2 = fuzzerstate.vectorpickstate.pick_vector_inputreg()
        return RVVInstruction(instr_str, vd=vd, vs2=vs2, vm=vm, iscompressed=iscompressed)

    elif instr_str == "vftta.vv":
        vd = fuzzerstate.vectorpickstate.pick_vector_outputreg()
        vs1 = fuzzerstate.vectorpickstate.pick_vector_inputreg()
        vs2 = fuzzerstate.vectorpickstate.pick_vector_inputreg()
        return RVVInstruction(instr_str, vd=vd, vs1=vs1, vs2=vs2, vm=vm, iscompressed=iscompressed)

    elif instr_str == "vsub12.vi":
        vd = fuzzerstate.vectorpickstate.pick_vector_outputreg()
        vs2 = fuzzerstate.vectorpickstate.pick_vector_inputreg()
        imm = random.randint(0, 31)
        return RVVInstruction(instr_str, vd=vd, vs2=vs2, imm=imm, vm=vm, iscompressed=iscompressed)

    elif instr_str == "join":
        return RVVInstruction(instr_str, vd=0, iscompressed=iscompressed)

    elif instr_str == "setrpc":
        rs1 = fuzzerstate.intregpickstate.pick_int_inputreg()
        imm = random.randint(-2048, 2047)
        return RVVInstruction(instr_str, vd=0, rs1=rs1, imm=imm, iscompressed=iscompressed)
    
    elif instr_str == "vfmv.f.s":
        #rd = fuzzerstate.floatregpickstate.pick_float_outputreg()
        rd = fuzzerstate.intregpickstate.pick_int_outputreg() # ventus uses shared float/int reg file
        vs2 = fuzzerstate.vectorpickstate.pick_vector_inputreg()
        return RVVInstruction(instr_str, vd=rd, vs2=vs2, vm=vm, iscompressed=iscompressed)

    elif instr_str == "vmv.x.s":
        rd = fuzzerstate.intregpickstate.pick_int_outputreg()
        vs2 = fuzzerstate.vectorpickstate.pick_vector_inputreg()
        return RVVInstruction(instr_str, vd=rd, vs2=vs2, vm=vm, iscompressed=iscompressed)

    elif instr_str == "vmv.s.x":
        vd = fuzzerstate.vectorpickstate.pick_vector_outputreg()
        rs1 = fuzzerstate.intregpickstate.pick_int_inputreg()
        return RVVInstruction(instr_str, vd=vd, rs1=rs1, vm=vm, iscompressed=iscompressed)

    elif instr_str.endswith(".vv") or instr_str.endswith(".vvm") or instr_str.endswith(".mm"):
        vd = fuzzerstate.vectorpickstate.pick_vector_outputreg()
        if not vd and instr_str.endswith(".vvm"):
            vd = 1 # cannot be v0 as specified by RISCV
        vs2 = fuzzerstate.vectorpickstate.pick_vector_inputreg()
        vs1 = fuzzerstate.vectorpickstate.pick_vector_inputreg()
        return RVVInstruction(instr_str, vd=vd, vs2=vs2, vs1=vs1, vm=vm, iscompressed=iscompressed)
    
    elif instr_str.endswith(".vx") or instr_str.endswith(".vxm") or instr_str.endswith(".v.x"):
        vd = fuzzerstate.vectorpickstate.pick_vector_outputreg()
        if not vd and instr_str.endswith(".vxm"):
            vd = 1 # cannot be v0 as specified by RISCV
        vs2 = fuzzerstate.vectorpickstate.pick_vector_inputreg()
        rs1 = fuzzerstate.intregpickstate.pick_int_inputreg()
        return RVVInstruction(instr_str, vd=vd, vs2=vs2, rs1=rs1, vm=vm, iscompressed=iscompressed)
    
    elif instr_str.endswith(".vi") or instr_str.endswith(".vim") or instr_str.endswith(".v.i"):
        vd = fuzzerstate.vectorpickstate.pick_vector_outputreg()
        if not vd and instr_str.endswith(".vim"):
            vd = 1 # cannot be v0 as specified by RISCV
        vs2 = fuzzerstate.vectorpickstate.pick_vector_inputreg()
        imm = random.randint(-16, 15)
        return RVVInstruction(instr_str, vd=vd, vs2=vs2, imm=imm, vm=vm, iscompressed=iscompressed)
    
    elif instr_str.endswith(".vf") or instr_str.endswith(".v.f") or instr_str.endswith(".s.f"):
        vd = fuzzerstate.vectorpickstate.pick_vector_outputreg()
        vs2 = fuzzerstate.vectorpickstate.pick_vector_inputreg()
        frs1 = fuzzerstate.floatregpickstate.pick_float_inputreg()
        return RVVInstruction(instr_str, vd=vd, vs2=vs2, rs1=frs1, vm=vm, iscompressed=iscompressed)
    
    elif instr_str.endswith(".v"):
        vd = fuzzerstate.vectorpickstate.pick_vector_outputreg()
        vs2 = fuzzerstate.vectorpickstate.pick_vector_inputreg()
        return RVVInstruction(instr_str, vd=vd, vs2=vs2, vm=vm, iscompressed=iscompressed)

    else:
        raise ValueError(f"Unexpected RVV instruction string: `{instr_str}`")

def create_targeted_producer0_instrobj(fuzzerstate):
    fuzzerstate.next_producer_id += 1
    rd = fuzzerstate.intregpickstate.pick_int_outputreg_nonzero(False)
    fuzzerstate.intregpickstate.set_producer_id(rd, fuzzerstate.next_producer_id)
    # fuzzerstate.intregpickstate.set_producer1_location(rd, len(fuzzerstate.instr_objs_seq), len(fuzzerstate.instr_objs_seq[0])) # Optimization currently unused
    fuzzerstate.intregpickstate.set_regstate(rd, IntRegIndivState.PRODUCED0)
    return [PlaceholderProducerInstr0(rd, fuzzerstate.next_producer_id, fuzzerstate.is_design_64bit)]
def create_targeted_producer1_instrobj(fuzzerstate):
    rd = fuzzerstate.intregpickstate.pick_int_reg_in_state(IntRegIndivState.PRODUCED0)
    # fuzzerstate.intregpickstate.set_producer1_location(rd, len(fuzzerstate.instr_objs_seq), len(fuzzerstate.instr_objs_seq[0])) # Optimization currently unused
    fuzzerstate.intregpickstate.set_regstate(rd, IntRegIndivState.PRODUCED1)
    return [PlaceholderProducerInstr1(rd, fuzzerstate.intregpickstate.get_producer_id(rd), fuzzerstate.is_design_64bit)]
def create_targeted_consumer_instrobj(fuzzerstate):
    rdep = fuzzerstate.intregpickstate.pick_int_inputreg_nonzero(False) # We want to create dependencies, therefore we choose not to accept x0
    rprod = fuzzerstate.intregpickstate.pick_int_reg_in_state(IntRegIndivState.PRODUCED1)
    # WARNING: We CANNOT throw a PRODUCEDX into the nature because its value will change between spike and RTL.
    rd = rprod
    fuzzerstate.intregpickstate.set_regstate(rprod, IntRegIndivState.CONSUMED)
    if fuzzerstate.is_design_64bit:
        return [PlaceholderPreConsumerInstr(rprod), PlaceholderPreConsumerInstr(rdep), PlaceholderConsumerInstr(rd, rdep, rprod, fuzzerstate.intregpickstate.get_producer_id(rprod))]
    else:
        return [PlaceholderConsumerInstr(rd, rdep, rprod, fuzzerstate.intregpickstate.get_producer_id(rprod))]

def _create_VentusBranchInstruction(instr_str: str, fuzzerstate, curr_addr: int, iscompressed: bool):
    vs1 = random.randint(0, 31) # FIXME!
    vs2 = random.randint(0, 31)
    plan_taken = fuzzerstate.curr_branch_taken
    if plan_taken:
        imm = fuzzerstate.next_bb_addr - curr_addr
    else:
        imm = gen_random_imm(instr_str, fuzzerstate.is_design_64bit)
    return VentusBranchInstruction(instr_str, vs1, vs2, imm, plan_taken, iscompressed)

# The reservation in the MemoryView is already done ahead and should not be reiterated here.
# @param jalr_addr_reg: only meaningful if a jalr is present (in the latter case, it should be the next instruction)
def create_instr(instr_str: str, fuzzerstate, curr_addr: int, iscompressed: bool = False):
    if DO_ASSERT:
        assert not iscompressed

    # Integer instructions
    if instr_str in R12DInstructions:
        return _create_R12DInstruction(instr_str, fuzzerstate, iscompressed)
    elif instr_str in ImmRdInstructions:
        return _create_ImmRdInstruction(instr_str, fuzzerstate, iscompressed)
    elif instr_str in RegImmInstructions:
        return _create_RegImmInstruction(instr_str, fuzzerstate, iscompressed)
    elif instr_str in BranchInstructions:
        return _create_BranchInstruction(instr_str, fuzzerstate, curr_addr, iscompressed)
    elif instr_str in JALInstructions:
        return _create_JALInstruction(instr_str, fuzzerstate, curr_addr, iscompressed)
    elif instr_str in JALRInstructions:
        return _create_JALRInstruction(instr_str, fuzzerstate, iscompressed)
    elif instr_str in SpecialInstructions:
        return _create_SpecialInstruction(instr_str, fuzzerstate, iscompressed)
    elif instr_str in IntLoadInstructions:
        return _create_IntLoadInstruction(instr_str, fuzzerstate, iscompressed)
    elif instr_str in IntStoreInstructions:
        return _create_IntStoreInstruction(instr_str, fuzzerstate, iscompressed)
    # Atomic instructions
    elif instr_str in AMOInstructions or instr_str in AMO64Instructions:
        return _create_AMOInstruction(instr_str, fuzzerstate, iscompressed)
    # Floating point instructions
    elif instr_str in FloatLoadInstructions:
        return _create_FloatLoadInstruction(instr_str, fuzzerstate, iscompressed)
    elif instr_str in FloatStoreInstructions:
        return _create_FloatStoreInstruction(instr_str, fuzzerstate, iscompressed)
    elif instr_str in FloatToIntInstructions:
        return _create_FloatToIntInstruction(instr_str, fuzzerstate, iscompressed)
    elif instr_str in IntToFloatInstructions:
        return _create_IntToFloatInstruction(instr_str, fuzzerstate, iscompressed)
    elif instr_str in Float4Instructions:
        return _create_Float4Instruction(instr_str, fuzzerstate, iscompressed)
    elif instr_str in Float3Instructions:
        return _create_Float3Instruction(instr_str, fuzzerstate, iscompressed)
    elif instr_str in Float3NoRmInstructions:
        return _create_Float3NoRmInstruction(instr_str, fuzzerstate, iscompressed)
    elif instr_str in Float2Instructions:
        return _create_Float2Instruction(instr_str, fuzzerstate, iscompressed)
    elif instr_str in FloatIntRd2Instructions:
        return _create_FloatIntRd2Instruction(instr_str, fuzzerstate, iscompressed)
    elif instr_str in FloatIntRd1Instructions:
        return _create_FloatIntRd1Instruction(instr_str, fuzzerstate, iscompressed)
    elif instr_str in FloatIntRs1Instructions:
        return _create_FloatIntRs1Instruction(instr_str, fuzzerstate, iscompressed)
    elif instr_str in VXBarInstructions:
        return _create_VXBarInstruction(instr_str, fuzzerstate, iscompressed)
    elif instr_str in RVVLoadInstructions:
        return _create_RVVLoadInstruction(instr_str, fuzzerstate, iscompressed)
    elif instr_str in RVVStoreInstructions:
        return _create_RVVStoreInstruction(instr_str, fuzzerstate, iscompressed)
    elif instr_str == "barrier":
        return _create_VentusBarInstruction(instr_str, fuzzerstate, iscompressed)
    elif instr_str in RVVInstructions or instr_str == "vsetvli":
        return _create_RVVInstruction(instr_str, fuzzerstate, iscompressed, curr_addr)
    elif instr_str in VentusBranchInstruction.authorized_instr_strs:
        return _create_VentusBranchInstruction(instr_str, fuzzerstate, curr_addr, iscompressed)

    else:
        raise ValueError(f"Unexpected instruction string: `{instr_str}`")
