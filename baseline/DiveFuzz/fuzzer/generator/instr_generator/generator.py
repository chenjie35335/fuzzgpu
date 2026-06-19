# Copyright (c) 2024-2025 Institute of Information Engineering, Chinese Academy of Sciences
# 
# DiveFuzz is licensed under Mulan PSL v2.
# You can use this software according to the terms and conditions of the Mulan PSL v2.
# You may obtain a copy of Mulan PSL v2 at:
#          http://license.coscl.org.cn/MulanPSL2
# 
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
# EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
# MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
# 
# See the Mulan PSL v2 for more details.

import re
import random
from .config import special_instr
from .formats import INSTRUCTION_FORMATS, expand_dot_insn
from .variables import variable_range
from .memory_manager import MemoryAccessManager
from ..bug_filter import bug_filter

# Vortex Tensor Core Format Definitions (based on gen_tensor_core.cc)
# Format IDs for different data types
TENSOR_FORMATS = {
    'fp32': 0,
    'fp16': 1,
    'bf16': 2,
    'int32': 8,
    'int8': 9,
    'uint8': 10,
    'int4': 11,
    'uint4': 12
}

# Valid input formats (FMT2) for each output format (FMT1)
# Based on vortex_input_formats_ in gen_tensor_core.cc
VORTEX_TENSOR_INPUT_FORMATS = {
    0: [1, 2],        # fp32 output: supports fp16, bf16 inputs
    1: [1],           # fp16 output: supports fp16 input
    2: [2],           # bf16 output: supports bf16 input
    8: [9, 10, 11, 12] # int32 output: supports int8, uint8, int4, uint4 inputs
}

def gen_imm(imm_type, length, arch_bits=64):
    """
    Generates a hexadecimal immediate value based on the specified type and bit length.
    Supports formats like 'IMM_x_y', where x is the bit length and y is a multiple requirement.
    Additionally, there is a 10% chance to generate extreme or special values.

    :param imm_type: The type of immediate value ('IMM_x', 'UNIMM_x', 'IMM_x_y' etc.).
    :param length: Bit length of the immediate value. Ignored if 'IMM_x' or 'IMM_x_y' format is used.
    :param arch_bits: Architecture bit width (32 for RV32, 64 for RV64). Constrains maximum value.
    :return: Generated hexadecimal immediate value.
    """
    # Extract bit length and type from imm_type if it's in the IMM_x or UNIMM_x format
    multiple = 0
    imm = -1
    match = re.match(r'(IMM|NZIMM|NZUIMM|UIMM|ZIMM)(?:_(\d+)(?:_(\d+))?)?', imm_type)
    if match:
        imm_type = match.group(1)
        length = int(match.group(2)) if match.group(2) else 12 # Default 12-bit immediate value
        # Used for aes64ks1i instructions
        # if int(match.group(3)) == 104:
        #     return hex(random.randint(0x0, 0xA))
        # else:
        #multiple = int(match.group(3)) if match.group(3) else 0 # How many bytes must be aligned
        if match.group(3):
            if int(match.group(3)) == 104:
                return hex(random.randint(0x0, 0xA))
            else:
                multiple = int(match.group(3))
        else:
            multiple = 0

    if length is None:
        raise ValueError("Bit length must be specified")
    
    # Calculate value ranges based on immediate type
    if imm_type in ['IMM', 'NZIMM']:
        min_value = -2 ** (length - 1)
        max_value = 2 ** (length - 1) - 1
    elif imm_type in ['UIMM', 'NZUIMM', 'ZIMM']:
        min_value = 1 if imm_type == 'NZUIMM' else 0
        max_value = 2 ** length - 1
    else:
        print(imm_type)
        raise ValueError("Unsupported immediate type")
    
    # Apply architecture-specific constraints for li pseudo-instruction compatibility
    # RV32 li can only handle 32-bit signed immediates: [-2^31, 2^31-1]
    # RV64 li can handle larger values but still constrained by encoding
    if arch_bits == 32:
        # Constrain to 32-bit signed range for RV32
        arch_min = -2**31
        arch_max = 2**31 - 1
        min_value = max(min_value, arch_min)
        max_value = min(max_value, arch_max)
    special_values = [min_value, max_value, 1]
    if imm_type in ['IMM']:
        special_values.append(0)
        special_values.append(-1)
    elif imm_type in ['NZIMM']:
        special_values.append(-1)
    elif imm_type in ['UIMM', 'ZIMM']:
        special_values.append(0)

    # 10% chance to generate extreme or special values
    if random.random() < 0.5 and not multiple:
        imm = random.choice(special_values)
    else:
        if multiple:
            # Adjust range for the multiple requirement
            min_value = (min_value + multiple - 1) // multiple * multiple
            max_value = max_value // multiple * multiple
            if imm_type in ['IMM', 'NZIMM', 'UIMM', 'NZUIMM', 'ZIMM']:
                imm = random.randint(min_value // multiple, max_value // multiple) * multiple
                if imm_type in ['NZIMM', 'NZUIMM', 'ZIMM'] and imm == 0:
                    imm = multiple
        else:
            if imm_type in ['IMM', 'NZIMM', 'UIMM', 'NZUIMM', 'ZIMM']:
                imm = random.randint(min_value, max_value)
                if imm_type in ['NZIMM', 'NZUIMM', 'ZIMM'] and imm == 0:
                    while imm == 0:
                        imm = random.randint(min_value, max_value)
            else:
                raise ValueError("Unsupported immediate type")
    
    # Final validation: ensure generated value respects architecture constraints
    # This is a safety check to prevent invalid values from being generated
    if arch_bits == 32:
        if imm < -2**31 or imm > 2**31 - 1:
            # Clamp to 32-bit range if somehow exceeded
            imm = max(-2**31, min(2**31 - 1, imm))
    
    # Convert to hexadecimal format, but there are problems with expressing negative numbers, so ignore it for now
    hex_format = '0x{0:0{1}X}'.format(imm, length // 4 if length % 4 == 0 else length // 4 + 1)

    return imm

# TODO
def generate_random_v_instruction():
    """
    Generates a random v instruction in assembly format.
    Returns:
        str: A randomly generated v instruction.
    """
    
    return None


def get_instruction_type(instruction):
    for instr_type, instr_dict in INSTRUCTION_FORMATS.items():
        if instruction in instr_dict:
            return instr_type
    return "Unknown"

def get_instruction_format(instruction):
    instr_type = get_instruction_type(instruction)
    return INSTRUCTION_FORMATS.get(instr_type, {}).get(instruction, {})


def _generate_memop_offset(instr_name, var, category):
    """
    Generate safe offset for load/store instructions.

    Args:
        instr_name: Name of the instruction (e.g., 'lw', 'sw', 'c.lwsp')
        var: Variable name (e.g., 'IMM_12', 'UIMM_8_4')
        category: Instruction category list

    Returns:
        String representation of the safe offset value
    """
    if 'LOAD' in category or 'STORE' in category or \
       'FLOAT_LOAD' in category or 'FLOAT_STORE' in category:
        # T6-based load/store: generate safe offset for IMM_12
        if var == 'IMM_12':
            imm = MemoryAccessManager.get_safe_offset_for_t6(instr_name)
            return str(imm)
        elif var == 'IMM_11':
            imm = MemoryAccessManager.get_safe_offset_for_t6_11(instr_name)
            return str(imm)
        else:
            # Other immediate types (shouldn't happen for standard load/store)
            # Fall back to normal generation
            return None
    else:
        # Not a memory operation
        return None

# Global settings for Vortex
_VORTEX_MAX_NUM_BARRIER = 2
_VORTEX_MAX_BARRIER_COUNT = 4
_VORTEX_NUM_WARPS_PER_CORE = 4
_VORTEX_NUM_THREADS_PER_WARP = 4
_VORTEX_NUM_CORES_PER_CLUSTER = 1

def init_generate(architecture, vortex_max_num_barrier=None, vortex_max_barrier_count=None, num_warps_per_core=None, num_threads_per_warp=None, num_cores_per_cluster=None):
    if architecture == 'ventus':
        variable_range['CSR'].extend([
            '0x800',  # CSR_TID - Thread ID
            '0x801',  # CSR_NUMW - Number of warps
            '0x802',  # CSR_NUMT - Number of threads
            '0x803',  # CSR_KNL - Kernel
            '0x804',  # CSR_WGID - Workgroup ID
            '0x805',  # CSR_WID - Warp ID
            '0x806',  # CSR_LDS - Local data share
            '0x807',  # CSR_PDS - Private data share
            '0x808',  # CSR_GIDX - Global ID X
            '0x809',  # CSR_GIDY - Global ID Y
            '0x80a',  # CSR_GIDZ - Global ID Z
            '0x80b',  # CSR_PRINT - Print
            '0x80c',  # CSR_RPC - Return PC
        ])
    if architecture == 'vortex':
        # Divefuzz currently does not distinguish between read-only and read-write CSRs
        # To avoid crashing the ISS, remove these CSRs:
        variable_range['CSR'].remove('mhartid')
        variable_range['CSR'].remove('misa')
        
        global _VORTEX_MAX_NUM_BARRIER, _VORTEX_MAX_BARRIER_COUNT, _VORTEX_NUM_WARPS_PER_CORE, _VORTEX_NUM_THREADS_PER_WARP, _VORTEX_NUM_CORES_PER_CLUSTER
        if vortex_max_num_barrier is not None:
            _VORTEX_MAX_NUM_BARRIER = vortex_max_num_barrier
        if vortex_max_barrier_count is not None:
            _VORTEX_MAX_BARRIER_COUNT = vortex_max_barrier_count
        if num_warps_per_core is not None:
            _VORTEX_NUM_WARPS_PER_CORE = num_warps_per_core
        if num_threads_per_warp is not None:
            _VORTEX_NUM_THREADS_PER_WARP = num_threads_per_warp
        if num_cores_per_cluster is not None:
            _VORTEX_NUM_CORES_PER_CLUSTER = num_cores_per_cluster

def generate_new_instr(new_instr_op, extension, rd_history, rs_history,\
                         frd_history, frs_history, vd_history, vs_history, architecture, arch_bits=64):

    new_instr_op = expand_dot_insn(new_instr_op)

    special_registers = ['ra', 'sp', 'gp', 'tp']

    instr_format = INSTRUCTION_FORMATS.get(extension, {}).get(new_instr_op, {})
    format_str = instr_format.get("format", "")
    variables = instr_format.get("variables", [])
    # ...

    instr_format = INSTRUCTION_FORMATS.get(extension, {}).get(new_instr_op, {})
    format_str = instr_format.get("format", "")
    variables = instr_format.get("variables", [])
    # ...

    # Parse the format string and create a mapping
    format_to_instr_map = {}
    format_parts = re.split(r'(\{[^}]*\})', format_str)  # Use regular expressions to split the string correctly

    part_index = 1

    for part in format_parts:
        if part.startswith('{') and part.endswith('}'):
            var_name = part[1:-1]
            if var_name in variables:
                if part_index < len(new_instr_op):
                    format_to_instr_map[var_name] = new_instr_op[part_index]
                else:
                    format_to_instr_map[var_name] = part
                part_index += 1

    prob_adjust = 0.8
    new_parts = {}
    
    # Handle vx_wmma tensor core formats (FMT1 and FMT2)
    # Based on logic from gen_tensor_core.cc
    fmt1_value = None
    fmt2_value = None
    if 'FMT1' in variables and 'FMT2' in variables:
        # Randomly select an output format (FMT1) from available output formats
        available_output_formats = list(VORTEX_TENSOR_INPUT_FORMATS.keys())
        fmt1_value = random.choice(available_output_formats)
        
        # Select a valid input format (FMT2) for the chosen output format
        valid_input_formats = VORTEX_TENSOR_INPUT_FORMATS[fmt1_value]
        fmt2_value = random.choice(valid_input_formats)
    
    for var in variables:
        if var == 'LABEL':
            # Always keep LABEL as placeholder for later replacement by label_manager
            # This handles all jump/branch instructions uniformly
            new_parts[var] = "{" + var + "}"
        elif var == 'FMT1':
            # Use the pre-selected FMT1 value for tensor core instructions
            new_parts[var] = str(fmt1_value) if fmt1_value is not None else '0'
        elif var == 'FMT2':
            # Use the pre-selected FMT2 value (compatible with FMT1)
            new_parts[var] = str(fmt2_value) if fmt2_value is not None else '0'
        elif var in variable_range and variable_range[var] is not None:
            # Randomly select one first and make a judgment later
            choices = bug_filter.filter_operand_choices(var, variable_range[var], new_instr_op, extension)
            new_parts[var] = random.choice(choices)
            # Make sliding windows of RD and RS
            # When RD is used as the destination register, the historical RD should be considered. 
            # 80% of the time, it is selected outside the historical RD, and 80% of the time, it is to avoid starvation register.
            if 'RD' in var:
                if 'FRD' in var:
                    temp_frd = new_parts[var]
                    if temp_frd in frd_history.get_history() and random.random() < prob_adjust:
                        available_frd = [frd for frd in choices if frd not in frd_history.get_history()]
                        if available_frd:
                            new_frd = random.choice(available_frd)
                            new_parts[var] = new_frd
                    # else:
                    #     new_parts[var]
                    frd_history.use_register(new_parts[var])
                # RD
                else:
                    temp_rd = new_parts[var]
                    if temp_rd in rd_history.get_history() and random.random() < prob_adjust:
                        available_rd = [rd for rd in choices if rd not in rd_history.get_history()]
                        if available_rd:
                            new_rd = random.choice(available_rd)
                            new_parts[var] = new_rd
                    rd_history.use_register(new_parts[var])
            # When choosing RS, try to choose in RD to make WAW, RAW, etc.
            elif 'FRS' in var:
                temp_frs = new_parts[var]
                    # 80% probability of using the nearest FRD register, RAW()
                if random.random() < prob_adjust:
                    available_frs = [frs for frs in choices if frs in frd_history.get_history()]
                    if available_frs:
                        new_frs = random.choice(available_frs)
                        new_parts[var] = new_frs
                frs_history.use_register(new_parts[var])
            elif 'RS' in var:
                temp_rs = new_parts[var]
                    # 80% probability of using the nearest FRD register, RAW()
                if random.random() < prob_adjust:
                    available_rs = [rs for rs in choices if rs in rd_history.get_history()]
                    if available_rs:
                        new_rs = random.choice(available_rs)
                        new_parts[var] = new_rs
                rs_history.use_register(new_parts[var])
            elif 'VD' in var:
                temp_vd = new_parts[var]
                if vd_history and temp_vd in vd_history.get_history() and random.random() < prob_adjust:
                    available_vd = [vd for vd in choices if vd not in vd_history.get_history()]
                    if available_vd:
                        new_vd = random.choice(available_vd)
                        new_parts[var] = new_vd
                if vd_history:
                    vd_history.use_register(new_parts[var])
            elif 'VS' in var:
                temp_vs = new_parts[var]
                if vd_history and random.random() < prob_adjust:
                    available_vs = [vs for vs in choices if vs in vd_history.get_history()]
                    if available_vs:
                        new_vs = random.choice(available_vs)
                        new_parts[var] = new_vs
                if vs_history:
                    vs_history.use_register(new_parts[var])
            elif var == 'VM':
                # For VENTUS arch: randomly choose whether to use vector mask
                # VM can be 'v0.t' (use mask) or '' (no mask)
                if architecture.upper() == 'VENTUS' or architecture.upper() == 'VENTUS_RVV':
                    #new_parts[var] = random.choice(variable_range[var])
                    # ventus not supports vector mask, so disable it for now
                    new_parts[var] = ''
                else:
                    # For other architectures, use default behavior
                    new_parts[var] = random.choice(choices) if choices else ''
            else:
                # Other special circumstances
                # For CSR, try not to choose SATP because it will cause cache inconsistency. The probability of choosing SATP is 1.7%.
                new_parts[var] = random.choice(choices)
                if 'satp' in new_parts[var]:
                    new_parts[var] = random.choice(choices)

        elif 'IMM' in var or 'UIMM' in var:
            # Check if this is a load/store instruction that needs safe offset
            category = instr_format.get('category', [])
            memop_offset = _generate_memop_offset(new_instr_op, var, category)

            if memop_offset is not None:
                # Load/store instruction: use safe offset
                new_parts[var] = memop_offset
            else:
                # Other instructions: use original immediate generation
                # DEBUG: Print var before calling gen_imm to trace SPUIMM issue
                if 'SP' in var and 'UIMM' in var:
                    print(f"DEBUG: Calling gen_imm with suspicious var='{var}' for instr={new_instr_op}")
                    print(f"DEBUG: format_str='{format_str}', variables={variables}")
                imm = gen_imm(var, 1, arch_bits=arch_bits)
                new_parts[var] = str(imm)
        else:
            new_parts[var] = format_to_instr_map.get(var, "{" + var + "}")


    new_instr = format_str
    if architecture.upper() == 'VENTUS' and (new_instr.startswith('fmv.w.x') or new_instr.startswith('fmv.x.w')):
        # ventus use mv instead of fmv.w.x (shared int & fp reg)
        new_instr = new_instr.replace('fmv.w.x', 'mv')
        new_instr = new_instr.replace('fmv.x.w', 'mv')

    for var, replacement in new_parts.items():
        if architecture.upper() == 'VENTUS':
            # replace riscv fp reg to x<idx> for ventus architecture
            # RISC-V floating-point registers (f0-f31) map to floating-point register bank
            # For VENTUS, we need to convert fp register names to corresponding indices
            fp_reg_map = {
                # ft0-ft7: indices 0-7
                'ft0': 'x0', 'ft1': 'x1', 'ft2': 'x2', 'ft3': 'x3',
                'ft4': 'x4', 'ft5': 'x5', 'ft6': 'x6', 'ft7': 'x7',
                # fs0-fs1, fa0-fa7: indices 8-15
                'fs0': 'x8', 'fs1': 'x9', 'fa0': 'x10', 'fa1': 'x11',
                'fa2': 'x12', 'fa3': 'x13', 'fa4': 'x14', 'fa5': 'x15',
                # fa6-fa7, fs2-fs7: indices 16-23
                'fa6': 'x16', 'fa7': 'x17', 'fs2': 'x18', 'fs3': 'x19',
                'fs4': 'x20', 'fs5': 'x21', 'fs6': 'x22', 'fs7': 'x23',
                # fs8-fs11, ft8-ft11: indices 24-31
                'fs8': 'x24', 'fs9': 'x25', 'fs10': 'x26', 'fs11': 'x27',
                'ft8': 'x28', 'ft9': 'x29', 'ft10': 'x30', 'ft11': 'x31'
            }
            if replacement in fp_reg_map:
                replacement = fp_reg_map[replacement]
            
        new_instr = new_instr.replace("{" + var + "}", replacement)

    # Clean up the instruction format for empty VM field
    # RISC-V vector assembly rules: VM field is either empty (no comma before it) or 'v0.t'
    # e.g., with mask: "vadd.vv v1, v2, v3, v0.t"
    #       no mask:   "vadd.vv v1, v2, v3"
    if 'VM' in variables and new_parts.get('VM', 'v0.t') == '':
        # Remove the comma and space before the empty VM field
        # Match patterns like ", " at the end of the instruction
        new_instr = re.sub(r',\s*$', '', new_instr)
        # Also clean up any multiple consecutive spaces that might result
        new_instr = re.sub(r'\s+', ' ', new_instr).strip()


# FOR CVA6 TEST
    # To avoid some spec different cva6 and spike ,
    # spike forbidden write senvcfg and scounteren
    if 'senvcfg' in new_instr or 'scounteren' in new_instr:

        parts = new_instr.split(' ')
        parts[1] = 'zero,'
        new_instr = ' '.join(parts)


# FOR VENTUS ARCH - Add regext prefix for floating-point instructions
    if architecture.upper() == 'VENTUS':
        # Check if the instruction has floating-point operands
        has_frd = 'FRD' in variables
        has_frs1 = 'FRS1' in variables
        has_frs2 = 'FRS2' in variables
        has_frs3 = 'FRS3' in variables
        
        if has_frd or has_frs1 or has_frs2 or has_frs3:
            # Calculate regext_imm based on which FP registers are present
            regext_imm = 0
            if has_frd:
                regext_imm |= (1 << 0)
            if has_frs1:
                regext_imm |= (1 << 3)
            if has_frs2:
                regext_imm |= (1 << 6)
            if has_frs3:
                regext_imm |= (1 << 9)
            
            # Add regext instruction before the main instruction
            regext_instr = f"regext x0,x0,{regext_imm}"
            new_instr = f"{regext_instr}\n{new_instr}"

    if False:
        print(f"# Generated instruction: '{new_instr}'")
        print(f"# extension {extension}")
        print(f"# new_instr_op: {new_instr_op}")
        print(f"# Instruction fmt: '{instr_format}'")
        print(f'# format string: "{format_str}" with variables {variables}')
    return new_instr

def expand_vx_bar(new_instr):
    entire_instrs = []
    
    vx_bar_info = None
    if 'vortex' in INSTRUCTION_FORMATS and 'vx_bar' in INSTRUCTION_FORMATS['vortex']:
        vx_bar_info = INSTRUCTION_FORMATS['vortex']['vx_bar']
    
    # Check if this is actually a vx_bar instruction before processing
    if vx_bar_info:
        fmt = vx_bar_info['format']
        # Format: .insn r 0x0B, 4, 0, x0, {RS1}, {RS2}
        
        # Verify that the instruction is vx_bar by checking if it matches the format pattern
        pattern = re.escape(fmt)
        
        # Replace escaped placeholders with capture groups
        # Note: re.escape('{RS1}') -> '\{RS1\}'
        pattern = pattern.replace(r'\{RS1\}', r'(?P<RS1>[\w\d]+)')
        pattern = pattern.replace(r'\{RS2\}', r'(?P<RS2>[\w\d]+)')
        
        # Only process if the instruction matches the vx_bar format
        if re.match(pattern, new_instr):
            while True:
                rs1 = random.choice(variable_range['RS1'])
                rs2 = random.choice(variable_range['RS2'])
                if rs1!=rs2 and rs1!='zero' and rs2!='zero': # bug fix when zero is used as rs1 or rs2
                    break

            bar_id = random.randint(0, _VORTEX_MAX_NUM_BARRIER-1)
            allow_global_barrier = not (
                _VORTEX_NUM_WARPS_PER_CORE == 1 and _VORTEX_NUM_THREADS_PER_WARP == 1
            )
            if allow_global_barrier and bug_filter.allow_global_barrier() and random.random() < 0.5:
                bar_id |= 0x80000000 
                max_count = min(_VORTEX_NUM_CORES_PER_CLUSTER, _VORTEX_MAX_BARRIER_COUNT)
            else:
                max_count = min(_VORTEX_NUM_WARPS_PER_CORE, _VORTEX_MAX_BARRIER_COUNT)
            count = random.randint(1, max_count)
            
            entire_instrs.append(f'li {rs1}, {bar_id}')
            entire_instrs.append(f'li {rs2}, {count}')

            # replace the rs1 and rs2 in the new_instr
            new_instr = fmt.replace('{RS1}', rs1).replace('{RS2}', rs2)

    entire_instrs.append(new_instr)
    return entire_instrs

def generate_instruction_probabilities(instruction_usage, probabilities, allowed_ext, special_probabilities):
    total_usage = sum(instruction_usage.values())
    if total_usage == 0:
        return probabilities  # If no instruction is used, keep the probability unchanged

    # Update probabilities: Convert instruction usage frequencies into a probability distribution
    new_probabilities = []
    for ext in allowed_ext:
        if ext in special_probabilities:  # If the instruction has a specific probability, do not update it.
            new_probabilities.append(special_probabilities[ext])
        else:
            # Distribute the remaining probability.
            usage_prob = instruction_usage.get(ext, 0) / total_usage
            new_probabilities.append(usage_prob)

    # Normalize the probabilities to ensure they sum to 1.
    sum_probs = sum(new_probabilities)
    normalized_probabilities = [prob / sum_probs for prob in new_probabilities]
    return normalized_probabilities
