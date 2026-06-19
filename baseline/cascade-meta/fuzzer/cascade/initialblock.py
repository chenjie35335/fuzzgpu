# Copyright 2023 Flavien Solt, ETH Zurich.
# Licensed under the General Public License, Version 3.0, see LICENSE for details.
# SPDX-License-Identifier: GPL-3.0-only

# This module defines the initial basic block of the program.

from params.runparams import DO_ASSERT
from rv.csrids import CSR_IDS
from cascade.toleratebugs import is_forbid_vexriscv_csrs
from cascade.cfinstructionclasses import ImmRdInstruction, RegImmInstruction, R12DInstruction, IntLoadInstruction, FloatLoadInstruction, CSRRegInstruction, WSpawnInstruction, TMCInstruction, RVVInstruction, RegextInstruction, RVVLoadInstruction
from cascade.randomize.createcfinstr import create_instr
from cascade.randomize.pickisainstrclass import ISAInstrClass
from cascade.util import get_range_bits_per_instrclass, BASIC_BLOCK_MIN_SPACE
from params.fuzzparams import RELOCATOR_REGISTER_ID, RDEP_MASK_REGISTER_ID, FPU_ENDIS_REGISTER_ID, MPP_BOTH_ENDIS_REGISTER_ID, MPP_TOP_ENDIS_REGISTER_ID, SPP_ENDIS_REGISTER_ID
import params.fuzzparams as fuzzparams

from rv.asmutil import li_into_reg

import random


# The first basic block is responsible for the initial setup
def gen_initial_basic_block(fuzzerstate, offset_addr: int, csr_init_rounding_mode: int = 0):
    if DO_ASSERT:
        assert offset_addr >= 0
        assert offset_addr < 1 << 32
        assert not fuzzerstate.instr_objs_seq
        assert csr_init_rounding_mode >= 0 and csr_init_rounding_mode <= 4

    fuzzerstate.init_new_bb() # Update fuzzer state to support a new basic block

    # Set the relocator register to the correct value

    curr_addr = fuzzerstate.curr_bb_start_addr

    lui_imm, addi_imm = li_into_reg(offset_addr, False)
    fuzzerstate.instr_objs_seq[-1].append(ImmRdInstruction("lui", RELOCATOR_REGISTER_ID, lui_imm, fuzzerstate.is_design_64bit))
    fuzzerstate.instr_objs_seq[-1].append(RegImmInstruction("addi", RELOCATOR_REGISTER_ID, RELOCATOR_REGISTER_ID, addi_imm, fuzzerstate.is_design_64bit))
    if fuzzerstate.is_design_64bit:
        # Clear the top 32 bits
        fuzzerstate.instr_objs_seq[-1].append(RegImmInstruction("slli", RELOCATOR_REGISTER_ID, RELOCATOR_REGISTER_ID, 32, fuzzerstate))
        fuzzerstate.instr_objs_seq[-1].append(RegImmInstruction("srli", RELOCATOR_REGISTER_ID, RELOCATOR_REGISTER_ID, 32, fuzzerstate))
    curr_addr += 8 + 8*int(fuzzerstate.is_design_64bit)
    if DO_ASSERT:
        assert curr_addr == fuzzerstate.curr_bb_start_addr + len(fuzzerstate.instr_objs_seq[-1]) * 4 # NO_COMPRESSED

    if not ("vexriscv" in fuzzerstate.design_name and is_forbid_vexriscv_csrs()):
        # Write 0 to medeleg to uniformize across designs. This must be done in initialblock to facilitate the analysis.
        if fuzzerstate.design_has_supervisor_mode:
            fuzzerstate.instr_objs_seq[-1].append(CSRRegInstruction("csrrw", 0, 0, CSR_IDS.MEDELEG))
            curr_addr += 4

        # Write 0 to mtvec and stvec to uniformize across designs. This must be done in initialblock to facilitate the analysis.
        if fuzzerstate.design_name != 'picorv32' and 'vortex' not in fuzzerstate.design_name and 'ventus' not in fuzzerstate.design_name:
            fuzzerstate.instr_objs_seq[-1].append(CSRRegInstruction("csrrw", 0, 0, CSR_IDS.MTVEC))
            curr_addr += 4
        if fuzzerstate.design_has_supervisor_mode:
            fuzzerstate.instr_objs_seq[-1].append(CSRRegInstruction("csrrw", 0, 0, CSR_IDS.STVEC))
            curr_addr += 4

    # We authorize all accesses through the PMP registers
    if not ("vexriscv" in fuzzerstate.design_name and is_forbid_vexriscv_csrs()):
        if fuzzerstate.design_has_pmp:
            # pmpcfg0
            fuzzerstate.instr_objs_seq[-1].append(RegImmInstruction("addi", 1, 0, 31, fuzzerstate.is_design_64bit))
            fuzzerstate.instr_objs_seq[-1].append(CSRRegInstruction("csrrw", 0, 1, CSR_IDS.PMPCFG0))
            curr_addr += 8
            # pmpaddr0
            if fuzzerstate.is_design_64bit:
                fuzzerstate.instr_objs_seq[-1].append(RegImmInstruction("addi", 1, 0, 1, fuzzerstate.is_design_64bit))
                fuzzerstate.instr_objs_seq[-1].append(RegImmInstruction("slli", 1, 0, 0x36, fuzzerstate.is_design_64bit))
                fuzzerstate.instr_objs_seq[-1].append(RegImmInstruction("addi", 1, 1, -1, fuzzerstate.is_design_64bit))
                fuzzerstate.instr_objs_seq[-1].append(CSRRegInstruction("csrrw", 0, 1, CSR_IDS.PMPADDR0))          
                curr_addr += 16
            else:
                fuzzerstate.instr_objs_seq[-1].append(RegImmInstruction("addi", 1, 0, -1, fuzzerstate.is_design_64bit))
                fuzzerstate.instr_objs_seq[-1].append(CSRRegInstruction("csrrw", 0, 1, CSR_IDS.PMPADDR0))
                curr_addr += 8

    # Write random values into the performance monitor CSRs (zeros for now)
    if not ("vexriscv" in fuzzerstate.design_name and is_forbid_vexriscv_csrs()):
        if fuzzerstate.design_name != 'picorv32' and 'vortex' not in fuzzerstate.design_name and 'ventus' not in fuzzerstate.design_name:
            fuzzerstate.instr_objs_seq[-1].append(CSRRegInstruction("csrrw", 0, 0, CSR_IDS.MCYCLE))
            fuzzerstate.instr_objs_seq[-1].append(CSRRegInstruction("csrrw", 0, 0, CSR_IDS.MCAUSE))
            fuzzerstate.instr_objs_seq[-1].append(CSRRegInstruction("csrrw", 0, 0, CSR_IDS.MTVAL))
            fuzzerstate.instr_objs_seq[-1].append(CSRRegInstruction("csrrw", 0, 0, CSR_IDS.MSCRATCH))
            fuzzerstate.instr_objs_seq[-1].append(CSRRegInstruction("csrrw", 0, 0, CSR_IDS.MINSTRET))
            curr_addr += 20

        if fuzzerstate.design_has_supervisor_mode:
            fuzzerstate.instr_objs_seq[-1].append(CSRRegInstruction("csrrw", 0, 0, CSR_IDS.SCAUSE))
            fuzzerstate.instr_objs_seq[-1].append(CSRRegInstruction("csrrw", 0, 0, CSR_IDS.STVAL))
            fuzzerstate.instr_objs_seq[-1].append(CSRRegInstruction("csrrw", 0, 0, CSR_IDS.SSCRATCH))
            curr_addr += 12

        if not fuzzerstate.is_design_64bit and fuzzerstate.design_name != 'picorv32' and 'vortex' not in fuzzerstate.design_name and 'ventus' not in fuzzerstate.design_name:
            fuzzerstate.instr_objs_seq[-1].append(CSRRegInstruction("csrrw", 0, 0, CSR_IDS.MCYCLEH))
            fuzzerstate.instr_objs_seq[-1].append(CSRRegInstruction("csrrw", 0, 0, CSR_IDS.MINSTRET))
            curr_addr += 8

    # Start with enabled FPU, if the FPU exists.
    if fuzzerstate.design_has_fpu or fuzzerstate.design_has_vector:
        # FUTURE Create dependencies on FPU_ENDIS_REGISTER_ID
        # Prepare FPU_ENDIS_REGISTER_ID, which will be used across the program's execution
        fuzzerstate.instr_objs_seq[-1].append(ImmRdInstruction("lui", FPU_ENDIS_REGISTER_ID, 0b10 if fuzzerstate.design_has_fpu else 0, fuzzerstate.is_design_64bit))
        curr_addr += 4
        if fuzzerstate.design_has_vector:
            fuzzerstate.instr_objs_seq[-1].append(RegImmInstruction("addi", FPU_ENDIS_REGISTER_ID, FPU_ENDIS_REGISTER_ID, 0x200, fuzzerstate.is_design_64bit))
            curr_addr += 4
        # Enable the FPU
        fuzzerstate.instr_objs_seq[-1].append(CSRRegInstruction("csrrw", 0, FPU_ENDIS_REGISTER_ID, CSR_IDS.MSTATUS))
        curr_addr += 4
        # Set the initial rounding mode to zero initially, arbitrarily. We arbitrarily use the register x1 as an intermediate register
        if fuzzerstate.design_has_fpu:
            fuzzerstate.instr_objs_seq[-1].append(RegImmInstruction("addi", 1, 0, 0, fuzzerstate.is_design_64bit))
            fuzzerstate.instr_objs_seq[-1].append(CSRRegInstruction("csrrw", 0, 1, CSR_IDS.FCSR))
            curr_addr += 8 # NO_COMPRESSED

    if fuzzerstate.design_has_supervisor_mode or fuzzerstate.design_has_user_mode:
        fuzzerstate.instr_objs_seq[-1].append(RegImmInstruction("srli", MPP_TOP_ENDIS_REGISTER_ID, FPU_ENDIS_REGISTER_ID, 1, fuzzerstate.is_design_64bit))
        fuzzerstate.instr_objs_seq[-1].append(RegImmInstruction("srli", MPP_BOTH_ENDIS_REGISTER_ID, FPU_ENDIS_REGISTER_ID, 2, fuzzerstate.is_design_64bit))
        fuzzerstate.instr_objs_seq[-1].append(R12DInstruction("or", MPP_BOTH_ENDIS_REGISTER_ID, MPP_BOTH_ENDIS_REGISTER_ID, MPP_TOP_ENDIS_REGISTER_ID))
        # Just for the alignment. Could be removed if we improved the alignment prediction. FUTURE.
        fuzzerstate.instr_objs_seq[-1].append(RegImmInstruction("addi", 0, 0, 0, fuzzerstate.is_design_64bit))
        curr_addr += 16 # NO_COMPRESSED
        # While it is not necesary to set the mpp initially, it is convenient to do so. If we don't, then we should adapt the initial values (typically to None) in privilegestate.py
        fuzzerstate.instr_objs_seq[-1].append(CSRRegInstruction("csrrs", 0, MPP_BOTH_ENDIS_REGISTER_ID, CSR_IDS.MSTATUS))
        fuzzerstate.instr_objs_seq[-1].append(CSRRegInstruction("csrrs", 0, MPP_TOP_ENDIS_REGISTER_ID, CSR_IDS.MSTATUS))
        curr_addr += 8 # NO_COMPRESSED

    if not ("vexriscv" in fuzzerstate.design_name and is_forbid_vexriscv_csrs()) and 'vortex' not in fuzzerstate.design_name and 'ventus' not in fuzzerstate.design_name:
        if fuzzerstate.design_has_user_mode:
            fuzzerstate.instr_objs_seq[-1].append(RegImmInstruction("srli", SPP_ENDIS_REGISTER_ID, FPU_ENDIS_REGISTER_ID, 5, fuzzerstate.is_design_64bit))
            curr_addr += 4 # NO_COMPRESSED
            # While it is not necesary to set the mpp initially, it is convenient to do so. If we don't, then we should adapt the initial values (typically to None) in privilegestate.py
            fuzzerstate.instr_objs_seq[-1].append(CSRRegInstruction("csrrs", 0, SPP_ENDIS_REGISTER_ID, CSR_IDS.MSTATUS))
            curr_addr += 4 # NO_COMPRESSED

    # Set the rdep mask to the correct value

    if fuzzerstate.is_design_64bit:
        fuzzerstate.instr_objs_seq[-1].append(RegImmInstruction("addi", RDEP_MASK_REGISTER_ID, 0, -1, fuzzerstate.is_design_64bit))
        fuzzerstate.instr_objs_seq[-1].append(RegImmInstruction("slli", RDEP_MASK_REGISTER_ID, RDEP_MASK_REGISTER_ID, 32, fuzzerstate.is_design_64bit))
        fuzzerstate.instr_objs_seq[-1].append(RegImmInstruction("xori", RDEP_MASK_REGISTER_ID, RDEP_MASK_REGISTER_ID, -1, fuzzerstate.is_design_64bit))
        curr_addr += 12 # NO_COMPRESSED
        if DO_ASSERT:
            assert curr_addr == fuzzerstate.curr_bb_start_addr + len(fuzzerstate.instr_objs_seq[-1]) * 4 # NO_COMPRESSED

    # Set the pickable registers to random values. We use the last pickable register as an intermediate reg.
    # Relocate for the loads
    fuzzerstate.instr_objs_seq[-1].append(R12DInstruction("add", fuzzerstate.num_pickable_regs-1, 0, RELOCATOR_REGISTER_ID))
    curr_addr += 4

    if fuzzerstate.design_has_fpu or fuzzerstate.design_has_vector:
        extra_bytes = 0
        if 'ventus' in fuzzerstate.design_name:
            extra_bytes += 8 + 4 * fuzzerstate.num_pickable_floating_regs
        if 'vortex' in fuzzerstate.design_name:
            # 24 bytes base + 8 bytes for 64-bit address cleanup (slli + srli)
            extra_bytes += 24 + (8 if fuzzerstate.is_design_64bit else 0)
        
        vec_init_size = 0
        if fuzzerstate.design_has_vector:
            # Calculate potential restore_val to determine instruction count
            num_existing_data = fuzzerstate.num_pickable_regs - 1
            if fuzzerstate.design_has_fpu:
                num_existing_data += fuzzerstate.num_pickable_floating_regs
            offset_to_vec_data = num_existing_data * 8
            stride = 4 * fuzzparams.NUM_THREADS_PER_WARP
            restore_val = -(offset_to_vec_data + 32 * stride)
            
            # Determine restore instruction size
            restore_instr_size = 4 if (restore_val >= -2048 and restore_val <= 2047) else 12
            
            # 32 * (vle32.v + addi) + setup (addi, addi, vsetvli) + restore
            vec_init_size = 32 * 8 + 12 + restore_instr_size
            extra_bytes += vec_init_size

        # Check if padding is needed: curr_addr + addi + reg/fp loads + extra_bytes + JAL
        # 4*(num_regs + num_fp_regs) = addi (1) + reg loads (num_regs - 1) + fp loads (num_fp_regs) = num_regs + num_fp_regs instructions
        # + 4 for JAL instruction
        expect_padding = ((curr_addr + (4*(fuzzerstate.num_pickable_regs+fuzzerstate.num_pickable_floating_regs)) + extra_bytes + 4) & 0x7) == 4
        bytes_until_random_vals = 8 + 4*(fuzzerstate.num_pickable_regs+fuzzerstate.num_pickable_floating_regs-1) + int(expect_padding) * 4 # NO_COMPRESSED
        bytes_until_random_vals += vec_init_size
    else:
        extra_bytes = 0
        if 'ventus' in fuzzerstate.design_name:
            extra_bytes += 8
        if 'vortex' in fuzzerstate.design_name:
            # 24 bytes base + 8 bytes for 64-bit address cleanup (slli + srli)
            extra_bytes += 24 + (8 if fuzzerstate.is_design_64bit else 0)
        # Check if padding is needed: curr_addr + addi + reg loads + extra_bytes + JAL
        # 4*num_pickable_regs = addi (1) + reg loads (num_pickable_regs - 1) = num_pickable_regs instructions
        # + 4 for JAL instruction
        expect_padding = ((curr_addr + (4*fuzzerstate.num_pickable_regs) + extra_bytes + 4) & 0x7) == 4
        bytes_until_random_vals = 8 + 4*(fuzzerstate.num_pickable_regs-1) + int(expect_padding) * 4 # NO_COMPRESSED

    if 'vortex' in fuzzerstate.design_name:
        # Vortex wspawn size already included in extra_bytes above
        bytes_until_random_vals += 24 + (8 if fuzzerstate.is_design_64bit else 0)
    
    if 'ventus' in fuzzerstate.design_name:
        if fuzzerstate.design_has_fpu:
            bytes_until_random_vals += 4 * fuzzerstate.num_pickable_floating_regs

    bytes_until_random_vals_base_for_debug = curr_addr
    fuzzerstate.instr_objs_seq[-1].append(RegImmInstruction("addi", fuzzerstate.num_pickable_regs-1, fuzzerstate.num_pickable_regs-1, bytes_until_random_vals, fuzzerstate.is_design_64bit))
    curr_addr += 4

    if fuzzerstate.design_has_vector:
        # Calculate offset to vector data
        # It is at the end of existing data (int + float)
        # Size of existing data = 8 * (num_pickable_regs-1 + num_fp_regs)
        num_existing_data = fuzzerstate.num_pickable_regs - 1
        if fuzzerstate.design_has_fpu:
            num_existing_data += fuzzerstate.num_pickable_floating_regs
        offset_to_vec_data = num_existing_data * 8
        
        base_reg = fuzzerstate.num_pickable_regs - 1
        
        # 1. Point xBase to vector data
        fuzzerstate.instr_objs_seq[-1].append(RegImmInstruction("addi", base_reg, base_reg, offset_to_vec_data, fuzzerstate.is_design_64bit))
        curr_addr += 4
        
        # 2. Setup vsetvli
        # Use x1 as temp
        rs_tmp = 1
        # Load VL (NUM_THREADS_PER_WARP)
        fuzzerstate.instr_objs_seq[-1].append(RegImmInstruction("addi", rs_tmp, 0, fuzzparams.NUM_THREADS_PER_WARP, fuzzerstate.is_design_64bit))
        curr_addr += 4
        # vsetvli
        # vsew=32bit (2), vlmul=1 (0), ta=1, ma=1
        vtypei = (1<<7) | (1<<6) | (2<<3) | 0
        fuzzerstate.instr_objs_seq[-1].append(RVVInstruction("vsetvli", 0, rs1=rs_tmp, imm=vtypei))
        curr_addr += 4
        
        # 3. Load vectors
        stride = 4 * fuzzparams.NUM_THREADS_PER_WARP
        for i in range(32):
            # vle32.v vd, (rs1)
            fuzzerstate.instr_objs_seq[-1].append(RVVLoadInstruction("vle32.v", i, base_reg, 0, 0, 1, -1))
            curr_addr += 4
            # addi rs1, rs1, stride
            fuzzerstate.instr_objs_seq[-1].append(RegImmInstruction("addi", base_reg, base_reg, stride, fuzzerstate.is_design_64bit))
            curr_addr += 4
            
        # 4. Restore xBase
        restore_val = -(offset_to_vec_data + 32 * stride)
        # Check if restore_val fits in 12-bit signed immediate (-2048 to 2047)
        if restore_val >= -2048 and restore_val <= 2047:
            fuzzerstate.instr_objs_seq[-1].append(RegImmInstruction("addi", base_reg, base_reg, restore_val, fuzzerstate.is_design_64bit))
            curr_addr += 4
        else:
            # Use a temporary register (rs_tmp = 1) to hold the restore value
            lui_imm, addi_imm = li_into_reg(restore_val & 0xFFFFFFFF, False)
            fuzzerstate.instr_objs_seq[-1].append(ImmRdInstruction("lui", rs_tmp, lui_imm, fuzzerstate.is_design_64bit))
            fuzzerstate.instr_objs_seq[-1].append(RegImmInstruction("addi", rs_tmp, rs_tmp, addi_imm, fuzzerstate.is_design_64bit))
            fuzzerstate.instr_objs_seq[-1].append(R12DInstruction("add", base_reg, base_reg, rs_tmp))
            curr_addr += 12

    # Floating loads must be done before int loads, because the last pickable int register will be overwritten.
    if fuzzerstate.design_has_fpu:
        if DO_ASSERT:
            assert fuzzerstate.num_pickable_floating_regs <= fuzzerstate.num_pickable_regs, "For this param choice, we need to adapt slightly the initial block."
        for fp_reg_id in range(fuzzerstate.num_pickable_floating_regs):
            if 'ventus' in fuzzerstate.design_name:
                fuzzerstate.instr_objs_seq[-1].append(RegextInstruction(1)) # frd
                curr_addr += 4
            fuzzerstate.instr_objs_seq[-1].append(FloatLoadInstruction("fld" if fuzzerstate.is_design_64bit else "flw", fp_reg_id, fuzzerstate.num_pickable_regs-1, 8*(fp_reg_id+fuzzerstate.num_pickable_regs-1), -1, fuzzerstate.is_design_64bit, regext=0, design_name=fuzzerstate.design_name))
            curr_addr += 4
    for reg_id in range(1, fuzzerstate.num_pickable_regs):
        fuzzerstate.instr_objs_seq[-1].append(IntLoadInstruction("ld" if fuzzerstate.is_design_64bit else "lw", reg_id, fuzzerstate.num_pickable_regs-1, 8*(reg_id-1), -1, fuzzerstate.is_design_64bit))
        curr_addr += 4

    if DO_ASSERT:
        assert curr_addr == fuzzerstate.curr_bb_start_addr + len(fuzzerstate.instr_objs_seq[-1]) * 4, f"{curr_addr}, {fuzzerstate.curr_bb_start_addr + len(fuzzerstate.instr_objs_seq[-1]) * 4}" # NO_COMPRESSED

    # We pre-allocate the space for the initial block before generating the next bb address.
    num_reginit_vals = fuzzerstate.num_pickable_regs-1
    if fuzzerstate.design_has_fpu:
        num_reginit_vals += fuzzerstate.num_pickable_floating_regs
    for _ in range(num_reginit_vals):
        fuzzerstate.initial_reg_data_content.append(0 if random.random() < fuzzerstate.proba_reg_starts_with_zero else random.randrange(1 << 64))

    if fuzzerstate.design_has_vector:
        num_vec_words = 16 * fuzzparams.NUM_THREADS_PER_WARP
        for _ in range(num_vec_words):
             fuzzerstate.initial_reg_data_content.append(random.randrange(1 << 64))

    # Calculate wspawn size for Vortex (needed for alignment calculation)
    wspawn_size = 0
    if 'vortex' in fuzzerstate.design_name:
        # 1. Load count (addi) -> 4 bytes
        # 2. Load address (lui + addi) -> 8 bytes
        #    For 64-bit: + 8 bytes (slli + srli to clear upper 32 bits)
        # 3. wspawn -> 4 bytes
        # 4. tmc -> 4 bytes + 4 bytes (addi) = 8 bytes
        # Total: 24 bytes (32-bit) or 32 bytes (64-bit)
        wspawn_size = 24 + (8 if fuzzerstate.is_design_64bit else 0)

    # If there will be padding between the instructions and data, to ensure proper alignment of doubleword load and store ops for 64-bit CPUs 
    # curr_addr is currently after register loads. We need to account for:
    # - wspawn instructions (wspawn_size bytes, Vortex only)
    # - JAL instruction (4 bytes)
    # Then check if we need padding to align data to 8 bytes
    # If (addr & 0x7) == 4, we're 4 bytes away from 8-byte alignment, so we need a 4-byte NOP
    has_padding = ((curr_addr + wspawn_size + 4) & 0x7) == 4
    if DO_ASSERT:
        assert expect_padding == has_padding, f"{expect_padding} != {has_padding}"
    # Allocate the initial block before choosing an address for the next bb.
    intended_initial_block_plus_reginit_size = len(fuzzerstate.instr_objs_seq[-1]) * 4 + 4 + len(fuzzerstate.initial_reg_data_content) * 8 + int(has_padding) * 4  # NO_COMPRESSED
    
    # Vortex WSPAWN logic
    if 'vortex' in fuzzerstate.design_name:
        intended_initial_block_plus_reginit_size += wspawn_size

    fuzzerstate.memview.alloc_mem_range(fuzzerstate.curr_bb_start_addr, fuzzerstate.curr_bb_start_addr+intended_initial_block_plus_reginit_size+4) # NO_COMPRESSED

    # Jump to the next basic block, say, with jal for simplicity
    range_bits_each_direction = get_range_bits_per_instrclass(ISAInstrClass.JAL)
    fuzzerstate.next_bb_addr = fuzzerstate.memview.gen_random_free_addr(4, BASIC_BLOCK_MIN_SPACE, curr_addr - (1 << range_bits_each_direction), curr_addr + (1 << range_bits_each_direction))
    if fuzzerstate.next_bb_addr is None:
        return False
    
    if 'vortex' in fuzzerstate.design_name:
        # Generate wspawn sequence
        # Use registers x1 and x2 (RELOCATOR_REGISTER_ID is usually 31, so x1/x2 should be safe if we don't care about preserving them here, 
        # but wait, we just loaded initial values into registers!
        # We should use registers that are NOT holding critical initial values if possible.
        # However, we are about to jump to next_bb_addr.
        # The registers we just loaded (x1..xN) hold random values for fuzzing.
        # If we overwrite them, we change the initial state.
        # But we can just overwrite them and it's fine, as long as we don't overwrite the register used for JAL (if any, JAL uses offset).
        # Or we can use registers that are NOT in the pickable set?
        # MAX_NUM_PICKABLE_REGS is 25. So x26..x31 are available?
        # RELOCATOR is 31. RDEP is 30. FPU is 29. MPP is 28, 27. SPP is 26.
        # So x26..x31 are reserved.
        # What about x0? No.
        # We can use x1 and x2, and just accept that their initial random values are overwritten by wspawn setup.
        # Or we can use the registers we pick for wspawn as the ones we load?
        # No, we want to execute wspawn.
        
        # Let's use x1 and x2.
        rs1 = 1
        rs2 = 2
        
        # 1. Setup count
        fuzzerstate.instr_objs_seq[-1].append(RegImmInstruction("addi", rs1, 0, fuzzparams.NUM_WARPS_PER_CORE, fuzzerstate.is_design_64bit))
        curr_addr += 4
        
        # 2. Setup address
        # We want to jump to fuzzerstate.next_bb_addr
        target_addr = offset_addr + fuzzerstate.next_bb_addr
        lui_imm, addi_imm = li_into_reg(target_addr, False)
        fuzzerstate.instr_objs_seq[-1].append(ImmRdInstruction("lui", rs2, lui_imm, fuzzerstate.is_design_64bit))
        fuzzerstate.instr_objs_seq[-1].append(RegImmInstruction("addi", rs2, rs2, addi_imm, fuzzerstate.is_design_64bit))
        curr_addr += 8
        # For 64-bit designs, lui sign-extends the value. We must zero out upper 32 bits.
        if fuzzerstate.is_design_64bit:
            fuzzerstate.instr_objs_seq[-1].append(RegImmInstruction("slli", rs2, rs2, 32, fuzzerstate))
            fuzzerstate.instr_objs_seq[-1].append(RegImmInstruction("srli", rs2, rs2, 32, fuzzerstate))
            curr_addr += 8

        # 3. WSPAWN
        fuzzerstate.instr_objs_seq[-1].append(WSpawnInstruction(rs1, rs2))
        curr_addr += 4

        # 4. TMC
        # Activate all threads in the warp
        tmask = (1 << fuzzparams.NUM_THREADS_PER_WARP) - 1
        fuzzerstate.instr_objs_seq[-1].append(RegImmInstruction("addi", rs1, 0, tmask, fuzzerstate.is_design_64bit))
        curr_addr += 4
        fuzzerstate.instr_objs_seq[-1].append(TMCInstruction(rs1))
        curr_addr += 4

    fuzzerstate.instr_objs_seq[-1].append(create_instr("jal", fuzzerstate, curr_addr))
    curr_addr += 4 # NO_COMPRESSED

    # Add a potential nop to align the ld that load the random vals into the registers
    fuzzerstate.initial_reg_data_addr = curr_addr
    if has_padding:
        fuzzerstate.initial_reg_data_addr += 4
        curr_addr += 4

    fuzzerstate.initial_block_data_start = curr_addr

    if DO_ASSERT:
        assert curr_addr == bytes_until_random_vals_base_for_debug + bytes_until_random_vals, f"curr_addr {hex(curr_addr)}, right-hand {hex(bytes_until_random_vals_base_for_debug + bytes_until_random_vals)} ({hex(bytes_until_random_vals_base_for_debug)} + {hex(bytes_until_random_vals)})"
        # Space taken by the random initial register values. We let some be zero.
    curr_addr += 8 * num_reginit_vals

    if fuzzerstate.design_has_vector:
        curr_addr += 8 * (16 * fuzzparams.NUM_THREADS_PER_WARP)

    fuzzerstate.initial_block_data_end = curr_addr
    if DO_ASSERT:
        assert curr_addr == fuzzerstate.curr_bb_start_addr + intended_initial_block_plus_reginit_size, f"{curr_addr}, {fuzzerstate.curr_bb_start_addr + len(fuzzerstate.instr_objs_seq[-1]) * 4 + 4 + len(fuzzerstate.initial_reg_data_content) * 8 + int(has_padding) * 4}" # NO_COMPRESSED

    return True
