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
from typing import Optional
from ...instr_generator import (
    INSTRUCTION_FORMATS,
    expand_dot_insn,
    special_instr,
    variable_range,
    gen_imm,
)
from ...instr_generator.generator import VORTEX_TENSOR_INPUT_FORMATS
from ...bug_filter import bug_filter


def _select_tensor_formats(source_instr: Optional[str] = None):
    """Pick a compatible (FMT1, FMT2) pair for vx_wmma."""
    fmt1_value = None
    fmt2_value = None

    if source_instr:
        fmt_matches = re.findall(r'x(\d+)', source_instr)
        if len(fmt_matches) >= 2:
            try:
                fmt1_value = int(fmt_matches[0], 0)
                fmt2_value = int(fmt_matches[1], 0)
            except ValueError:
                fmt1_value = None
                fmt2_value = None

    if fmt1_value not in VORTEX_TENSOR_INPUT_FORMATS:
        fmt1_value = random.choice(list(VORTEX_TENSOR_INPUT_FORMATS.keys()))

    valid_inputs = VORTEX_TENSOR_INPUT_FORMATS[fmt1_value]
    if fmt2_value not in valid_inputs:
        fmt2_value = random.choice(valid_inputs)

    return fmt1_value, fmt2_value


def _operand_choices(var_name: str, instr_name: str, extension: str):
    return bug_filter.filter_operand_choices(
        var_name, variable_range[var_name], instr_name, extension
    )

def modify_instruction_dec(ori_instr, rand_instr, increase_prob, extension, architecture, arch_bits=32):
    #extension = extension.upper()

    special_registers = ['ra', 'sp', 'gp', 'tp']
    contains_special = any(reg in ori_instr for reg in special_registers)

    if contains_special and random.random() >= 0.05:
        return ori_instr

    instr_parts = rand_instr.split()
    if '.insn' in instr_parts[0]:
        instr_key = expand_dot_insn(rand_instr)
    else:
        instr_key = instr_parts[0]
    instr_format = INSTRUCTION_FORMATS.get(extension, {}).get(instr_key, {})
    format_str = instr_format.get("format", "")
    variables = instr_format.get("variables", [])
    # ...
    # Parse format strings and build mapping
    format_to_instr_map = {}
    format_parts = re.split(r'(\{[^}]*\})', format_str)  
    part_index = 1

    fmt1_value = None
    fmt2_value = None
    if 'FMT1' in variables and 'FMT2' in variables:
        fmt1_value, fmt2_value = _select_tensor_formats(rand_instr)

    for part in format_parts:
        if part.startswith('{') and part.endswith('}'):
            var_name = part[1:-1]
            if var_name in variables:
                if part_index < len(instr_parts):
                    format_to_instr_map[var_name] = instr_parts[part_index]
                else:
                    format_to_instr_map[var_name] = part
                part_index += 1

    new_parts = {}
    for var in variables:
        if var == 'LABEL':
            new_parts[var] = format_to_instr_map.get(var, "{" + var + "}")
        elif var == 'FMT1':
            new_parts[var] = str(fmt1_value)
        elif var == 'FMT2':
            new_parts[var] = str(fmt2_value)
        elif var == 'VM':
            # VM is optional for vector instructions
            new_parts[var] = ''
        elif var in variable_range and variable_range[var] is not None:
            new_parts[var] = random.choice(_operand_choices(var, instr_key, extension))
        elif 'IMM' in var:
            # Handle all immediate types: IMM, UIMM, NZIMM, NZUIMM, ZIMM, IMM_x, UIMM_x, etc.
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

        # Handle optional parameters like VM - if empty, remove the placeholder AND preceding comma/space
        if replacement == '' and var == 'VM':
            # Remove ", {VM}" pattern (comma, optional space, and the placeholder)
            new_instr = re.sub(r',\s*\{' + var + r'\}', '', new_instr)
        else:
            new_instr = new_instr.replace("{" + var + "}", replacement)
    
    # Clean up any trailing commas and extra spaces
    new_instr = re.sub(r',\s*$', '', new_instr)
    new_instr = re.sub(r'\s+', ' ', new_instr).strip()

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

    print(f"[DEBUG] Final new_instr: '{new_instr}', rand_instr {rand_instr} ext {extension}")
    return new_instr



def modify_instruction_inc(ori_instr, increase_prob, extension, architecture, arch_bits=32):
    # Only modify operand registers and immediates, etc.

    #extension = extension.upper()
    special_registers = ['ra', 'sp', 'gp', 'tp']
    contains_special = any(reg in ori_instr for reg in special_registers)

    if contains_special and random.random() >= 0.05:
        return ori_instr

    instr_parts = ori_instr.split()
    # Check if there is a section label
    instr_key_index = 1 if ':' in instr_parts[0] else 0
    if '.insn' in instr_parts[instr_key_index]:
        instr_key = expand_dot_insn(' '.join(instr_parts[instr_key_index:]))
    else:
        instr_key = instr_parts[instr_key_index]

    instr_format = INSTRUCTION_FORMATS.get(extension, {}).get(instr_key, {})
    
    # Debug: Check if instruction format exists
    if not instr_format:
        print(f"[DEBUG] No format found for instruction '{instr_key}' in extension '{extension}'")
        return ori_instr
    format_str = instr_format.get("format", "")
    variables = instr_format.get("variables", [])
    # Parse format strings and build mapping
    format_to_instr_map = {}
    format_parts = re.split(r'(\{[^}]*\})', format_str) 
    part_index = 1 + instr_key_index # Filter out section labels
    for part in format_parts:
        if part.startswith('{') and part.endswith('}'):
            var_name = part[1:-1]
            if var_name in variables:
                # Check if part_index is within the range of instr_parts
                if part_index < len(instr_parts):
                    # Special handling for cases with parentheses
                    if '(' in instr_parts[part_index] and ')' in instr_parts[part_index]:
                        imm_rs1_part = instr_parts[part_index]
                        match_result = re.match(r'([^(]*)\(([^)]+)\)', imm_rs1_part)
                        if match_result:
                            imm, rs1 = match_result.groups()
                            # Check if imm is empty; if so, set it to 0
                            imm = imm if imm else '0'
                        else: # Never run
                            # If no match is found, print an error or handle the exception accordingly
                            print(f"Unexpected format encountered in instruction: {instr_parts}")
                            # Or set a default value and continue
                            imm = '0'
                            rs1 = 'UNDEFINED'  # Need to decide how to handle this case
                        # Assigning values ​​for IMM and RS1
                        if var_name == variables[0]:  # Assume IMM always comes before RS1
                            format_to_instr_map[var_name] = imm
                            continue  # Continue with the next loop iteration to avoid part_index being incorrectly incremented
                        elif var_name == variables[1]:
                            format_to_instr_map[var_name] = rs1
                    else:
                        format_to_instr_map[var_name] = instr_parts[part_index]
                else:
                    # If part_index is out of range, assign a default value or perform error handling
                    # For optional parameters like VM, use empty string if not present in original instruction
                    format_to_instr_map[var_name] = None
                part_index += 1

    fmt1_value = None
    fmt2_value = None
    if 'FMT1' in variables and 'FMT2' in variables:
        fmt1_value, fmt2_value = _select_tensor_formats(ori_instr)

    new_parts = {}
    for var in variables:
        if instr_key in special_instr and var == 'LABEL':
            new_parts[var] = format_to_instr_map.get(var, "{" + var + "}")
        elif var == 'FMT1':
            new_parts[var] = str(fmt1_value)
        elif var == 'FMT2':
            new_parts[var] = str(fmt2_value)
        elif var in variable_range and variable_range[var] is not None:
            # If the variable was not in original instruction (None), still generate a value
            if format_to_instr_map.get(var) is None:
                # For VM (vector mask), use empty string to make it optional
                if var == 'VM':
                    new_parts[var] = ''  # VM is optional in vector instructions
                else:
                    new_parts[var] = random.choice(_operand_choices(var, instr_key, extension))
            else:
                new_parts[var] = random.choice(_operand_choices(var, instr_key, extension))
        elif 'IMM' in var:
            imm = gen_imm(var, 1, arch_bits=arch_bits)
            new_parts[var] = str(imm)
        else:
            value = format_to_instr_map.get(var, "{" + var + "}")
            # Handle None values
            if value is None:
                if var == 'VM':
                    new_parts[var] = ''  # VM is optional
                else:
                    new_parts[var] = "{" + var + "}"
            else:
                new_parts[var] = value

    new_instr = format_str
    if architecture.upper() == 'VENTUS' and (new_instr.startswith('fmv.w.x') or new_instr.startswith('fmv.x.w')):
        # ventus use mv instead of fmv.w.x (shared int & fp reg)
        new_instr = new_instr.replace('fmv.w.x', 'mv')
        new_instr = new_instr.replace('fmv.x.w', 'mv')
    
    # Debug: Print new_parts before replacement
    print(f"[DEBUG] new_parts for '{instr_key}': {new_parts}")
    print(f"[DEBUG] format_str: {format_str}")
    
    for var, replacement in new_parts.items():
        # Skip None replacements
        if replacement is None:
            print(f"[DEBUG] Warning: replacement for '{var}' is None, skipping")
            continue
            
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
        
        # Handle optional parameters like VM - if empty, remove the placeholder AND preceding comma/space
        if replacement == '' and var == 'VM':
            # Remove ", {VM}" pattern (comma, optional space, and the placeholder)
            new_instr = re.sub(r',\s*\{' + var + r'\}', '', new_instr)
        else:
            new_instr = new_instr.replace("{" + var + "}", replacement)

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
    
    # Clean up any remaining unreplaced placeholders (shouldn't happen, but safety check)
    if '{' in new_instr and '}' in new_instr:
        print(f"[DEBUG] Warning: Unreplaced placeholders in instruction: {new_instr}")
    
    # Clean up trailing commas and extra spaces (handle multiple lines for regext prefix)
    lines = new_instr.split('\n')
    cleaned_lines = []
    for line in lines:
        # Remove trailing commas and spaces
        line = re.sub(r',\s*$', '', line)
        # Remove double commas (e.g., "v1, , v2" -> "v1, v2")
        line = re.sub(r',\s*,', ',', line)
        # Remove comma followed by nothing before end of line or before spaces at end
        line = re.sub(r',\s*$', '', line)
        # Clean up extra spaces
        line = re.sub(r'\s+', ' ', line).strip()
        cleaned_lines.append(line)
    new_instr = '\n'.join(cleaned_lines)

    print(f"[DEBUG] Final new_instr: '{new_instr}'")
    return new_instr


#TODO If you refer to the historical write data when generating instructions, you can change the following code.
# def modify_instruction_inc(ori_instr, increase_prob, extension):
   

#     extension = extension.upper()

#     special_registers = ['ra', 'sp', 'gp', 'tp']
#     contains_special = any(reg in ori_instr for reg in special_registers)

#     if contains_special and random.random() >= 0.05:
#         return ori_instr

#     instr_parts = ori_instr.split()

#     instr_key_index = 1 if ':' in instr_parts[0] else 0
#     instr_key = instr_parts[instr_key_index]
#     # if ':' in instr_parts[0]:
#     #     instr_key = instr_parts[1]
#     # else:
#     #     instr_key = instr_parts[0]
#     instr_format = INSTRUCTION_FORMATS.get(extension, {}).get(instr_key, {})
#     format_str = instr_format.get("format", "")
#     variables = instr_format.get("variables", [])


#     format_to_instr_map = {}
#     format_parts = re.split(r'(\{[^}]*\})', format_str)  
#     part_index = 1 + instr_key_index

#     for part in format_parts:
#         if part.startswith('{') and part.endswith('}'):
#             var_name = part[1:-1]
#             if var_name in variables:
#                 if part_index < len(instr_parts):
#                     if '(' in instr_parts[part_index] and ')' in instr_parts[part_index]:
#                         imm_rs1_part = instr_parts[part_index]
#                         # repair imm maybe 0 ,so no 0 at ()
#                         # imm, rs1 = re.match(r'([^(]+)\(([^)]+)\)', imm_rs1_part).groups()
#                         match_result = re.match(r'([^(]*)\(([^)]+)\)', imm_rs1_part)
#                         if match_result:
#                             imm, rs1 = match_result.groups()
#                             imm = imm if imm else '0'
#                         else: # Never run
#                           
#                             print(f"Unexpected format encountered in instruction: {instr_parts}")
#             
#                             imm = '0'
#                             rs1 = 'UNDEFINED'  


#                       
#                         if var_name == variables[0]: 
#                             format_to_instr_map[var_name] = imm
#                             continue  
#                         elif var_name == variables[1]:
#                             format_to_instr_map[var_name] = rs1
#                     else:
#                         format_to_instr_map[var_name] = instr_parts[part_index]
#                 else:
#                    
#                     format_to_instr_map[var_name] = None
#                 part_index += 1


#     new_parts = {}
#     for var in variables:
#             if instr_key in special_instr and var == 'LABEL':
#                 new_parts[var] = format_to_instr_map.get(var, "{" + var + "}")
#             elif var in variable_range and variable_range[var] is not None:
#                 new_parts[var] = random.choice(variable_range[var])
#             elif 'IMM' in var:
#                 imm = gen_imm(var, 1)  # 1 作为占位符
#                 new_parts[var] = str(imm)
#             else:
#                 new_parts[var] = format_to_instr_map.get(var, "{" + var + "}")

#     new_instr = format_str
#     for var, replacement in new_parts.items():
#         new_instr = new_instr.replace("{" + var + "}", replacement)


#     # print(f"New parts: {new_parts}")
#     #print(f"2222Generated instruction: {new_instr}")

#     return new_instr
