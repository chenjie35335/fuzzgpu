# Copyright 2023 Flavien Solt, ETH Zurich.
# Licensed under the General Public License, Version 3.0, see LICENSE for details.
# SPDX-License-Identifier: GPL-3.0-only

# This module picks some random valid CSR operation.

from params.runparams import DO_ASSERT
from rv.csrids import CSR_IDS
from cascade.toleratebugs import is_no_interaction_minstret, is_tolerate_kronos_minstret, is_tolerate_vexriscv_minstret, is_tolerate_picorv32_missingmandatorycsrs, is_tolerate_picorv32_readnonimplcsr, is_tolerate_picorv32_writehpm, is_tolerate_cva6_mhpmcounter, is_tolerate_boom_minstret, is_tolerate_picorv32_readhpm_nocsrrs, is_tolerate_vexriscv_mhpmcountern, is_tolerate_cva6_mhpmevent31
from cascade.privilegestate import PrivilegeStateEnum
from cascade.cfinstructionclasses import CSRRegInstruction, CSRImmInstruction, RegImmInstruction
import random

from enum import Enum, auto

class MachineCSROpCandidates64(Enum):
    SCAUSE = auto()
    MCAUSE = auto()
    SSCRATCH = auto()
    MSCRATCH = auto()
    MINSTRET = auto()
    MHPMCOUNTER3 = auto()
    MHPMEVENT31 = auto()
    VX_CSR_THREAD_ID = auto()
    VX_CSR_WARP_ID = auto()
    VX_CSR_CORE_ID = auto()
    VX_CSR_ACTIVE_THREADS = auto()
    VX_CSR_ACTIVE_WARPS = auto()

class MachineCSROpCandidates32(Enum):
    SCAUSE = auto()
    MCAUSE = auto()
    SSCRATCH = auto()
    MSCRATCH = auto()
    MINSTRET = auto()
    VX_CSR_THREAD_ID = auto()
    VX_CSR_WARP_ID = auto()
    VX_CSR_CORE_ID = auto()
    VX_CSR_ACTIVE_THREADS = auto()
    VX_CSR_ACTIVE_WARPS = auto()
    CSR_TID = auto()
    CSR_NUMW = auto()
    CSR_NUMT = auto()
    CSR_KNL = auto()
    CSR_WGID = auto()
    CSR_WID = auto()
    CSR_LDS = auto()
    CSR_PDS = auto()
    CSR_GIDX = auto()
    CSR_GIDY = auto()
    CSR_GIDZ = auto()

class SupervisorCSROpCandidates(Enum):
    SCAUSE = auto()
    SSCRATCH = auto()

# @brief Generate a privileged descent instruction or an mpp/spp write instruction.
# @return a list of instructions
def gen_random_csr_op(fuzzerstate):
    if DO_ASSERT:
        assert fuzzerstate.privilegestate.privstate in (PrivilegeStateEnum.MACHINE, PrivilegeStateEnum.SUPERVISOR)

    if fuzzerstate.privilegestate.privstate == PrivilegeStateEnum.MACHINE:
        if fuzzerstate.is_design_64bit:
            target_csr = None
            while target_csr is None or (target_csr == MachineCSROpCandidates64.MINSTRET and is_no_interaction_minstret(fuzzerstate.design_name)):
                target_csr = random.choice(list(MachineCSROpCandidates64))
            if not fuzzerstate.design_has_supervisor_mode:
                while target_csr in (MachineCSROpCandidates64.SCAUSE, MachineCSROpCandidates64.SSCRATCH):
                    target_csr = random.choice(list(MachineCSROpCandidates64))
            while 'cva6' in fuzzerstate.design_name and not is_tolerate_cva6_mhpmcounter() and not is_tolerate_cva6_mhpmevent31() and target_csr in (MachineCSROpCandidates64.MHPMCOUNTER3, MachineCSROpCandidates64.MHPMEVENT31):
                target_csr = random.choice(list(MachineCSROpCandidates64))
            while "vortex" in fuzzerstate.design_name and target_csr not in (MachineCSROpCandidates64.VX_CSR_THREAD_ID, MachineCSROpCandidates64.VX_CSR_WARP_ID, MachineCSROpCandidates64.VX_CSR_CORE_ID, MachineCSROpCandidates64.VX_CSR_ACTIVE_THREADS, MachineCSROpCandidates64.VX_CSR_ACTIVE_WARPS):
                target_csr = random.choice(list(MachineCSROpCandidates64))
            while "vortex" not in fuzzerstate.design_name and target_csr in (MachineCSROpCandidates64.VX_CSR_THREAD_ID, MachineCSROpCandidates64.VX_CSR_WARP_ID, MachineCSROpCandidates64.VX_CSR_CORE_ID, MachineCSROpCandidates64.VX_CSR_ACTIVE_THREADS, MachineCSROpCandidates64.VX_CSR_ACTIVE_WARPS):
                target_csr = random.choice(list(MachineCSROpCandidates64))

            if target_csr == MachineCSROpCandidates64.SCAUSE:
                # According to the spec, the SCAUSE CSR must be able to hold bits 0 to 4. mret is not required to.
                ret = CSRImmInstruction("csrrwi", fuzzerstate.intregpickstate.pick_int_outputreg(), random.randrange(32), CSR_IDS.SCAUSE)
            elif target_csr == MachineCSROpCandidates64.MCAUSE:
                ret = CSRImmInstruction("csrrwi", fuzzerstate.intregpickstate.pick_int_outputreg(), random.randrange(16), CSR_IDS.MCAUSE)
            elif target_csr == MachineCSROpCandidates64.SSCRATCH:
                    ret = CSRRegInstruction("csrrw", fuzzerstate.intregpickstate.pick_int_outputreg(), fuzzerstate.intregpickstate.pick_int_inputreg(), CSR_IDS.SSCRATCH)
            elif target_csr == MachineCSROpCandidates64.MSCRATCH:
                    ret = CSRRegInstruction("csrrw", fuzzerstate.intregpickstate.pick_int_outputreg(), fuzzerstate.intregpickstate.pick_int_inputreg(), CSR_IDS.MSCRATCH)
            elif target_csr == MachineCSROpCandidates64.MINSTRET:
                if fuzzerstate.is_minstret_inaccurate_because_ecall_ebreak or ("boom" in fuzzerstate.design_name and not is_tolerate_boom_minstret()):
                    ret = CSRImmInstruction("csrrwi", 0, random.randrange(16), CSR_IDS.MINSTRET)
                else:
                    
                    ret = CSRRegInstruction("csrrw", fuzzerstate.intregpickstate.pick_int_outputreg(), fuzzerstate.intregpickstate.pick_int_inputreg(), CSR_IDS.MINSTRET)
                fuzzerstate.is_minstret_inaccurate_because_ecall_ebreak = False
            elif target_csr == MachineCSROpCandidates64.MHPMCOUNTER3:
                ret = CSRImmInstruction("csrrwi", 0, random.randrange(16), CSR_IDS.MHPMCOUNTER3)
            elif target_csr == MachineCSROpCandidates64.MHPMEVENT31:
                ret = CSRImmInstruction("csrrwi", 0, random.randrange(16), CSR_IDS.MHPMEVENT31)
            elif target_csr == MachineCSROpCandidates64.VX_CSR_THREAD_ID:
                ret = CSRRegInstruction("csrrs", fuzzerstate.intregpickstate.pick_int_outputreg(), 0, CSR_IDS.VX_CSR_THREAD_ID)
            elif target_csr == MachineCSROpCandidates64.VX_CSR_WARP_ID:
                ret = CSRRegInstruction("csrrs", fuzzerstate.intregpickstate.pick_int_outputreg(), 0, CSR_IDS.VX_CSR_WARP_ID)
            elif target_csr == MachineCSROpCandidates64.VX_CSR_CORE_ID:
                ret = CSRRegInstruction("csrrs", fuzzerstate.intregpickstate.pick_int_outputreg(), 0, CSR_IDS.VX_CSR_CORE_ID)
            elif target_csr == MachineCSROpCandidates64.VX_CSR_ACTIVE_THREADS:
                ret = CSRRegInstruction("csrrs", fuzzerstate.intregpickstate.pick_int_outputreg(), 0, CSR_IDS.VX_CSR_ACTIVE_THREADS)
            elif target_csr == MachineCSROpCandidates64.VX_CSR_ACTIVE_WARPS:
                ret = CSRRegInstruction("csrrs", fuzzerstate.intregpickstate.pick_int_outputreg(), 0, CSR_IDS.VX_CSR_ACTIVE_WARPS)
            else:
                raise Exception("Unexpected target_csr: {}".format(target_csr))
        else:
            if "vexriscv" in fuzzerstate.design_name and is_tolerate_vexriscv_mhpmcountern():
                return CSRImmInstruction("csrrwi", fuzzerstate.intregpickstate.pick_int_outputreg(), random.randrange(16), CSR_IDS.MHPMCOUNTER3)
            elif "picorv32" in fuzzerstate.design_name and not is_tolerate_picorv32_missingmandatorycsrs() and not is_tolerate_picorv32_readnonimplcsr():
                assert not is_no_interaction_minstret(fuzzerstate.design_name), "picorv32 only has minstret in this config."
                target_csr = MachineCSROpCandidates32.MINSTRET
            else:
                target_csr = None
                while target_csr is None or (target_csr == MachineCSROpCandidates32.MINSTRET and is_no_interaction_minstret(fuzzerstate.design_name)) \
                    or (not fuzzerstate.design_has_supervisor_mode and (target_csr in (MachineCSROpCandidates32.SCAUSE, MachineCSROpCandidates32.SSCRATCH))) \
                    or ("vortex" not in fuzzerstate.design_name and target_csr in (MachineCSROpCandidates32.VX_CSR_THREAD_ID, MachineCSROpCandidates32.VX_CSR_WARP_ID, MachineCSROpCandidates32.VX_CSR_CORE_ID, MachineCSROpCandidates32.VX_CSR_ACTIVE_THREADS, MachineCSROpCandidates32.VX_CSR_ACTIVE_WARPS)) \
                    or ("ventus" not in fuzzerstate.design_name and target_csr in (MachineCSROpCandidates32.CSR_TID, MachineCSROpCandidates32.CSR_NUMW, MachineCSROpCandidates32.CSR_NUMT, MachineCSROpCandidates32.CSR_KNL, MachineCSROpCandidates32.CSR_WGID, MachineCSROpCandidates32.CSR_WID, MachineCSROpCandidates32.CSR_LDS, MachineCSROpCandidates32.CSR_PDS, MachineCSROpCandidates32.CSR_GIDX, MachineCSROpCandidates32.CSR_GIDY, MachineCSROpCandidates32.CSR_GIDZ)):
                    target_csr = random.choice(list(MachineCSROpCandidates32))
            if target_csr == MachineCSROpCandidates32.SCAUSE:
                # According to the spec, the SCAUSE CSR must be able to hold bits 0 to 4. mret is not required to.
                if "vexriscv" in fuzzerstate.design_name: # vexriscv complies with the privileged spec v1.10, which does not require scause to hold the 5th bit. Similarly, kronos implements privileged spec v1.11
                    randval = random.randrange(16)
                    ret = CSRImmInstruction("csrrwi", fuzzerstate.intregpickstate.pick_int_outputreg(), randval, CSR_IDS.SCAUSE)
                else:
                    ret = CSRImmInstruction("csrrwi", fuzzerstate.intregpickstate.pick_int_outputreg(), random.randrange(32), CSR_IDS.SCAUSE)
            elif target_csr == MachineCSROpCandidates32.MCAUSE:
                ret = CSRImmInstruction("csrrwi", fuzzerstate.intregpickstate.pick_int_outputreg(), random.randrange(16), CSR_IDS.MCAUSE)
            elif target_csr == MachineCSROpCandidates32.SSCRATCH:
                ret = CSRRegInstruction("csrrw", fuzzerstate.intregpickstate.pick_int_outputreg(), fuzzerstate.intregpickstate.pick_int_inputreg(), CSR_IDS.SSCRATCH)
            elif target_csr == MachineCSROpCandidates32.MSCRATCH:
                ret = CSRRegInstruction("csrrw", fuzzerstate.intregpickstate.pick_int_outputreg(), fuzzerstate.intregpickstate.pick_int_inputreg(), CSR_IDS.MSCRATCH)
            elif target_csr == MachineCSROpCandidates32.MSCRATCH:
                ret = CSRRegInstruction("csrrw", fuzzerstate.intregpickstate.pick_int_outputreg(), fuzzerstate.intregpickstate.pick_int_inputreg(), CSR_IDS.MSCRATCH)
            elif target_csr == MachineCSROpCandidates32.MINSTRET:
                if "kronos" in fuzzerstate.design_name and not is_tolerate_kronos_minstret() or "vexriscv" in fuzzerstate.design_name and not is_tolerate_vexriscv_minstret():
                    ret = CSRRegInstruction("csrrw", 0, fuzzerstate.intregpickstate.pick_int_inputreg(), CSR_IDS.MINSTRET)
                elif "picorv32" in fuzzerstate.design_name:
                    if is_tolerate_picorv32_readhpm_nocsrrs():
                        opcode_str = random.choice(("csrrw", "csrrs"))
                    else:
                        opcode_str = "csrrs"
                    if is_tolerate_picorv32_writehpm():
                        rs = fuzzerstate.intregpickstate.pick_int_inputreg()
                    else:
                        rs = 0
                    ret = CSRRegInstruction(opcode_str, fuzzerstate.intregpickstate.pick_int_outputreg(), rs, CSR_IDS.MINSTRET)
                else:
                    ret = CSRRegInstruction("csrrw", fuzzerstate.intregpickstate.pick_int_outputreg(), fuzzerstate.intregpickstate.pick_int_inputreg(), CSR_IDS.MINSTRET)
            elif target_csr == MachineCSROpCandidates32.VX_CSR_THREAD_ID:
                ret = CSRRegInstruction("csrrs", fuzzerstate.intregpickstate.pick_int_outputreg(), 0, CSR_IDS.VX_CSR_THREAD_ID)
            elif target_csr == MachineCSROpCandidates32.VX_CSR_WARP_ID:
                ret = CSRRegInstruction("csrrs", fuzzerstate.intregpickstate.pick_int_outputreg(), 0, CSR_IDS.VX_CSR_WARP_ID)
            elif target_csr == MachineCSROpCandidates32.VX_CSR_CORE_ID:
                ret = CSRRegInstruction("csrrs", fuzzerstate.intregpickstate.pick_int_outputreg(), 0, CSR_IDS.VX_CSR_CORE_ID)
            elif target_csr == MachineCSROpCandidates32.VX_CSR_ACTIVE_THREADS:
                ret = CSRRegInstruction("csrrs", fuzzerstate.intregpickstate.pick_int_outputreg(), 0, CSR_IDS.VX_CSR_ACTIVE_THREADS)
            elif target_csr == MachineCSROpCandidates32.VX_CSR_ACTIVE_WARPS:
                ret = CSRRegInstruction("csrrs", fuzzerstate.intregpickstate.pick_int_outputreg(), 0, CSR_IDS.VX_CSR_ACTIVE_WARPS)
            elif target_csr == MachineCSROpCandidates32.CSR_TID:
                ret = CSRRegInstruction("csrrs", fuzzerstate.intregpickstate.pick_int_outputreg(), 0, CSR_IDS.CSR_TID)
            elif target_csr == MachineCSROpCandidates32.CSR_NUMW:
                ret = CSRRegInstruction("csrrs", fuzzerstate.intregpickstate.pick_int_outputreg(), 0, CSR_IDS.CSR_NUMW)
            elif target_csr == MachineCSROpCandidates32.CSR_NUMT:
                ret = CSRRegInstruction("csrrs", fuzzerstate.intregpickstate.pick_int_outputreg(), 0, CSR_IDS.CSR_NUMT)
            elif target_csr == MachineCSROpCandidates32.CSR_KNL:
                ret = CSRRegInstruction("csrrs", fuzzerstate.intregpickstate.pick_int_outputreg(), 0, CSR_IDS.CSR_KNL)
            elif target_csr == MachineCSROpCandidates32.CSR_WGID:
                ret = CSRRegInstruction("csrrs", fuzzerstate.intregpickstate.pick_int_outputreg(), 0, CSR_IDS.CSR_WGID)
            elif target_csr == MachineCSROpCandidates32.CSR_WID:
                ret = CSRRegInstruction("csrrs", fuzzerstate.intregpickstate.pick_int_outputreg(), 0, CSR_IDS.CSR_WID)
            elif target_csr == MachineCSROpCandidates32.CSR_LDS:
                ret = CSRRegInstruction("csrrs", fuzzerstate.intregpickstate.pick_int_outputreg(), 0, CSR_IDS.CSR_LDS)
            elif target_csr == MachineCSROpCandidates32.CSR_PDS:
                ret = CSRRegInstruction("csrrs", fuzzerstate.intregpickstate.pick_int_outputreg(), 0, CSR_IDS.CSR_PDS)
            elif target_csr == MachineCSROpCandidates32.CSR_GIDX:
                ret = CSRRegInstruction("csrrs", fuzzerstate.intregpickstate.pick_int_outputreg(), 0, CSR_IDS.CSR_GIDX)
            elif target_csr == MachineCSROpCandidates32.CSR_GIDY:
                ret = CSRRegInstruction("csrrs", fuzzerstate.intregpickstate.pick_int_outputreg(), 0, CSR_IDS.CSR_GIDY)
            elif target_csr == MachineCSROpCandidates32.CSR_GIDZ:
                ret = CSRRegInstruction("csrrs", fuzzerstate.intregpickstate.pick_int_outputreg(), 0, CSR_IDS.CSR_GIDZ)
            else:
                raise Exception("Unexpected target_csr: {}".format(target_csr))
    else:
        target_csr = random.choice(list(SupervisorCSROpCandidates))
        if target_csr == SupervisorCSROpCandidates.SCAUSE:
            if "vexriscv" in fuzzerstate.design_name: # vexriscv complies with the privileged spec v1.10, which does not require scause to hold the 5th bit. Similarly, kronos implements privileged spec v1.11
                ret = CSRImmInstruction("csrrwi", fuzzerstate.intregpickstate.pick_int_outputreg(), random.randrange(16), CSR_IDS.SCAUSE)
            else:
                ret = CSRImmInstruction("csrrwi", fuzzerstate.intregpickstate.pick_int_outputreg(), random.randrange(32), CSR_IDS.SCAUSE)
        elif target_csr == SupervisorCSROpCandidates.SSCRATCH:
            ret = CSRRegInstruction("csrrw", fuzzerstate.intregpickstate.pick_int_outputreg(), fuzzerstate.intregpickstate.pick_int_inputreg(), CSR_IDS.SSCRATCH)
        else:
            raise Exception("Unexpected target_csr: {}".format(target_csr))
    return ret
