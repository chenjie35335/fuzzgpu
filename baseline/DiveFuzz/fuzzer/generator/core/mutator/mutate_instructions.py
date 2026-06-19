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
import os
import random
import numpy as np
from typing import List
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor
from ...instr_generator import (
    init_formats,
    INSTRUCTION_FORMATS,
    expand_dot_insn,
    special_instr,
    init_generate,
    get_instruction_format,
    get_instruction_type,
    classify_instructions,
    find_instruction_extension,
)
from .modify_instr import (
    modify_instruction_dec,
    modify_instruction_inc,
)
from ...asm_template_manager import create_template_instance, TemplateInstance
from ...asm_template_manager.riscv_asm_syntex import ArchConfig
from ...asm_template_manager.ext_list import allowed_ext
from ...utils import list2str_without_indent
from ...reg_analyzer.nop_template_gen import generate_nop_elf
from ...reg_analyzer.spike_session import SpikeSession, is_spike_engine_available
from ...reg_analyzer.instruction_validator import InstructionValidator
from ...config.config_manager import MAX_MUTATE_TIME
from ...bug_filter import bug_filter
from ...reg_analyzer.hybrid_encoder import HybridEncoder


def extract_instruction_name(line: str) -> str:
    """Return the instruction mnemonic on a single line (expands .insn when possible)."""
    parts = line.split()
    if not parts:
        return ""

    name = parts[0]
    if ':' in name and len(parts) > 1:
        name = parts[1]

    if name == '.insn':
        expanded = expand_dot_insn(line)
        name = expanded if expanded else '.insn'

    return name


def peek_next_instruction(lines: List[str], current_index: int) -> str:
    """Peek the mnemonic of the next logical instruction (skips regext prefix)."""
    if current_index + 1 >= len(lines):
        return ""

    next_line = lines[current_index + 1]
    next_instr = extract_instruction_name(next_line)

    if next_instr == 'regext' and current_index + 2 < len(lines):
        next_instr = extract_instruction_name(lines[current_index + 2])

    return next_instr


# Relocation symbols that must be preserved as pairs
RELOCATION_PAIRS = {
    '%pcrel_hi', '%pcrel_lo',
    '%hi', '%lo',
    '%tprel_hi', '%tprel_lo',
    '%gottprel', '%tls_ie_pcrel_hi',
    '%tls_gd_pcrel_hi',
    '%got_pcrel_hi',
}


def contains_relocation(line: str) -> bool:
    """Check if a line contains any relocation symbols that must be preserved."""
    return any(reloc in line for reloc in RELOCATION_PAIRS)


def should_skip_mutation(instr_name: str, next_instr_name: str, line: str = "", next_line: str = "") -> bool:
    """
    Determine if an instruction should be skipped during mutation.
    
    Skip mutation when:
    - Current instruction is vx_bar
    - Current is li followed by li/vx_bar
    - Line contains relocation symbols (%pcrel_hi, %pcrel_lo, %hi, %lo, etc.)
      These must be preserved as pairs to avoid assembly errors.
    """
    # Skip if line contains relocation symbols (they must remain paired)
    if contains_relocation(line) or contains_relocation(next_line):
        return True
    
    # Do not mutate when current is li followed by li/vx_bar (.insn), or when current is vx_bar (.insn).
    return instr_name == 'vx_bar' or (instr_name == 'li' and next_instr_name in {'li', 'vx_bar'})


# Initial mutation, search within the current extension set.
def process_content(file_path: str,
                    file_content: str,
                    directory_path: Path,
                    mutate_directory: Path,
                    enable_ext: bool,
                    exclude_extensions: List[str],
                    eliminate_enable: bool,
                    arch: ArchConfig,
                    template_type: str,
                    is_rv32 : bool,
                    architecture: str = 'default',
                    **kwargs):
    """
    Process and mutate instructions in a single file.

    Args:
        file_path: Path to the source file
        file_content: Content of the source file
        directory_path: Path to source directory
        mutate_directory: Path to output directory
        enable_ext: Allow introducing new extension instructions
        exclude_extensions: Extensions to exclude from mutation
        eliminate_enable: Enable conflict elimination via Spike
        arch: Architecture configuration for template creation
        **kwargs: Additional arguments for template creation
    """
    if 'bug_filter_json' in kwargs:
        bug_filter.load_generator_filter_json(
            kwargs.get('bug_filter_json'),
            default_filtering=not kwargs.get('disable_default_bug_filtering', False),
        )

    init_formats(architecture, kwargs.get('num_threads'))
    init_generate(architecture, 
                  vortex_max_num_barrier=kwargs.get('vortex_max_num_barrier'),
                  vortex_max_barrier_count=kwargs.get('vortex_max_barrier_count'),
                  num_warps_per_core=kwargs.get('num_warps_per_core'),
                  num_threads_per_warp=kwargs.get('num_threads'),
                  num_cores_per_cluster=kwargs.get('num_cores_per_cluster'))

    # Create fresh template instance for this mutated file with random type and values
    template = create_template_instance(arch, template_type, **kwargs)
    
    # Get architecture bit width from arch configuration
    arch_bits = arch.get_arch_bits()

    # Initialize Spike session for eliminate mode (similar to generate_instructions)
    spike_session = None
    validator = None
    shared_xor_cache = {}  # Local XOR cache for mutation

    # Check if spike_engine is available for this architecture (will load if needed)
    spike_available = is_spike_engine_available(architecture, arch_bits)
    if eliminate_enable and spike_available:
        try:
            # Estimate instruction count from file content
            instr_count = len([line for line in file_content.split('\n')
                             if line.strip() and not line.strip().startswith('#')
                             and ':' not in line.strip()[0:1]])

            # Generate NOP ELF template
            import tempfile
            with tempfile.NamedTemporaryFile(suffix='.elf', delete=False) as tmp:
                elf_path = tmp.name
            elf_path = generate_nop_elf(template, instr_count, architecture, elf_path)

            spike_session = SpikeSession(
                elf_path,
                template.isa,
                instr_count,
                shared_xor_cache
            )

            if spike_session.initialize():
                arch_bits = 32 if is_rv32 else 64
                encoder = HybridEncoder(quiet=True, architecture=architecture, arch_bits=arch_bits)
                validator = InstructionValidator(spike_session, encoder)
            else:
                spike_session = None
                validator = None

        except Exception as e:
            print(f"[Mutate] Failed to initialize Spike session: {e}")
            spike_session = None
            validator = None

    instruction_freq = count_instructions({file_path: file_content})
    increase_queue, decrease_queue, classified_instructions, geometric_means, missing_ext = classify_instructions(instruction_freq)

    updated_content = []  # Store the mutated content
    instr_probabilities = {}  # Create a dictionary for storing probabilities
    
    for extension, instr_data in classified_instructions.items():
        counts = list(instr_data['instructions'].values())
        if counts:  
            mean = np.mean(counts)
            std = np.std(counts)
            p_decs, p_incs, valid_counts = calculate_probabilities_z_score_full(counts, mean, std, min_prob=0.3, max_prob=0.9)

            for instr, count in instr_data['instructions'].items():
                prob = p_decs.get(count, 0.3) if count > mean else p_incs.get(count, 0.9)
                instr_probabilities[instr] = prob
    # Instructions in the mutated file
    lines = file_content.split('\n')
    i = 0
    while i < len(lines):
        line = lines[i]
        line_parts = line.split()
        if not line_parts:
            updated_content.append(line)
            i += 1
            continue
        # Check whether the line contains a section description (i.e., whether it contains a colon)
        if ':' in line_parts[0]:
            # If there is a section description, then the instruction name should be the first word after the colon
            if len(line_parts) > 1:
                instr_name = line_parts[1]
                segment_label = line_parts[0] + " "  # Keep the section identifier
            else:
                updated_content.append(line)
                i += 1
                continue  # Skip lines that contain only a section description without instructions
        else:
            instr_name = line_parts[0]
            segment_label = ""
        if instr_name == '.insn':
            instr_name = expand_dot_insn(line) if expand_dot_insn(line) else '.insn'
        print('instr_name: ', instr_name)
        # Handle UNKNOWN instructions
        if instr_name == 'la':
            updated_content.append(line)
            i += 1
            continue

        # Check if current instruction is regext and needs to be combined with next line
        is_regext = (instr_name == 'regext')
        next_line = ""
        combined_line = line
        if is_regext and i + 1 < len(lines):
            next_line = lines[i + 1]
            # Combine current line and next line for processing
            combined_line = line + '\n' + next_line

        # Get the instruction type and check whether there is a label tag in the format
        if is_regext:
            instr_parts = next_line.split()
            if not instr_parts:
                updated_content.append(line)
                i += 1
                continue
            instr_name = instr_parts[0] # main instr
        instr_type = get_instruction_type(instr_name)

        next_instr_name = peek_next_instruction(lines, i)
        if should_skip_mutation(instr_name, next_instr_name, line, next_line):
            updated_content.append(line)
            if is_regext:
                updated_content.append(next_line)
                i += 1
            i += 1
            continue
        
        # Skip unknown instruction types
        if instr_type == 'Unknown':
            updated_content.append(line)
            if is_regext:
                updated_content.append(next_line)
                i += 1  # Skip next line
            i += 1
            continue
        
        instr_format = INSTRUCTION_FORMATS.get(instr_type.upper(), {}).get(instr_name, {})
        
        # TODO: Add register value checking!
        if instr_name in increase_queue or is_regext:
            prob = instr_probabilities.get(instr_name, 0.5)
            if random.random() < prob:
                # Determine which line to process (combined for regext, single for others)
                line_to_process = combined_line.split('\n')[1] if is_regext else line
                
                if eliminate_enable and validator is not None:
                    # Check if instruction needs special handling (skip validation)
                    if  'LABEL' in get_instruction_format(instr_name).get('variables', []) or 'LOAD' in get_instruction_format(instr_name).get('category', []) \
                        or 'STORE' in get_instruction_format(instr_name).get('category', []) or 'JUMP' in get_instruction_format(instr_name).get('category', []) \
                        or 'BRANCH' in get_instruction_format(instr_name).get('category', []) or 'LOAD_SP' in get_instruction_format(instr_name).get('category', []) \
                        or 'STORE_SP' in get_instruction_format(instr_name).get('category', []) or instr_name in ['vx_bar']:
                        updated_content.append(line)
                        if is_regext:
                            updated_content.append(next_line)
                            i += 1  # Skip next line since we already processed it
                    else:
                        # Use validator for instruction validation (similar to generate_instructions)
                        is_diff_rs = 0
                        mutate_time = 0

                        while is_diff_rs == 0 and mutate_time < MAX_MUTATE_TIME:
                            modified_instr = modify_instruction_inc(line_to_process, prob, get_instruction_type(instr_name), architecture, arch_bits)

                            # Validate with checkpoint-based validator
                            # NEW: validator returns (xor_value, dest_values, source_values)
                            print('line_to_process:', line_to_process)
                            print('ext:', get_instruction_type(instr_name))
                            print('modified_instr:', modified_instr)
                            xor_value, dest_values, source_values, machine_code = validator.validate_instruction(modified_instr)
                            if xor_value is not None:
                                # Unique XOR, check bug filter
                                opcode = modified_instr.strip().split()[0] if modified_instr.strip() else "unknown"

                                # Check for known bugs using actual register values
                                bug_name = bug_filter.filter_known_bug(opcode, dest_values, source_values)
                                if bug_name:
                                    # Triggers known bug, restore and retry
                                    spike_session.restore_checkpoint_and_reset()
                                    mutate_time += 1
                                else:
                                    # try the instruction to see if there is any exception
                                    try:
                                        spike_session.execute_instruction(machine_code)
                                    except Exception as e:
                                        # Instruction caused exception, restore checkpoint and retry
                                        spike_session.restore_checkpoint_and_reset()
                                        mutate_time += 1
                                        continue

                                    # Valid instruction, accept it
                                    spike_session.confirm_instruction(xor_value, modified_instr, opcode=opcode)
                                    # Handle regext: modified result might be single or multiple lines
                                    if is_regext:
                                        # Split modified result and append each line
                                        modified_lines = modified_instr.split('\n')
                                        for mod_line in modified_lines:
                                            if mod_line.strip():  # Skip empty lines
                                                updated_content.append(segment_label + mod_line)
                                        i += 1  # Skip next line since we processed it with regext
                                    else:
                                        updated_content.append(segment_label + modified_instr)
                                    is_diff_rs = 1
                            else:
                                # Duplicate XOR, retry
                                mutate_time += 1

                        if mutate_time >= MAX_MUTATE_TIME:
                            # Failed to find unique instruction, keep original
                            updated_content.append(line)
                            if is_regext:
                                updated_content.append(next_line)
                                i += 1  # Skip next line
                else:
                    modified_instr = modify_instruction_inc(line_to_process, prob, get_instruction_type(instr_name), architecture, arch_bits)
                    # Handle regext: modified result might be single or multiple lines
                    if is_regext:
                        # Split modified result and append each line
                        modified_lines = modified_instr.split('\n')
                        for mod_line in modified_lines:
                            if mod_line.strip():  # Skip empty lines
                                updated_content.append(segment_label + mod_line)
                        i += 1  # Skip next line since we processed it with regext
                    else:
                        updated_content.append(segment_label + modified_instr)

            else:
                updated_content.append(line)
                if is_regext:
                    updated_content.append(next_line)
                    i += 1  # Skip next line
        # Instructions to be reduced
        elif instr_name in decrease_queue:
            # regext should not be in decrease_queue, but if it is, keep it unchanged
            if is_regext:
                updated_content.append(line)
                updated_content.append(next_line)
                i += 1  # Skip next line
            # Check whether the instruction contains the 'LABEL' variable
            elif 'LABEL' not in instr_format.get('variables', []):
                prob = instr_probabilities.get(instr_name, 0.5)
                if random.random() < prob:
                    # With a 50% probability, replace with NOP or randomly select an instruction from the addition queue that does not contain LABEL
                    prob_choose_stgy = random.random()
                    if prob_choose_stgy < 0.001:  # Originally 0.5, for experimental
                        # TODO: Originally replaced with nop
                        updated_content.append(segment_label + "")
                        pass
                    elif (prob_choose_stgy < 0.1 and enable_ext) or not enable_ext:
                        # Randomly select an instruction from the addition queue that does not contain LABEL
                        # Randomly select an instruction from the addition queue that does not contain STORE
                        # Randomly select an instruction from the addition queue that does not contain LOAD
                        # TODO Two strategies: should we add it to a specific extension that has not appeared yet?
                        no_label_increase_queue = [instr for instr in increase_queue if 'LABEL' not in get_instruction_format(instr).get('variables', []) \
                                                                                and 'LOAD' not in get_instruction_format(instr).get('category', []) \
                                                                                and 'STORE' not in get_instruction_format(instr).get('category', []) \
                                                                                and 'SYSTEM' not in get_instruction_format(instr).get('category', [])\
                                                                                and 'JUMP' not in get_instruction_format(instr).get('category', []) \
                                                                                and instr != 'vx_bar' \
                                                                                and not bug_filter.should_filter_instruction(instr, get_instruction_type(instr), get_instruction_format(instr), architecture) \
                                                                                    ]# 为了做实验

                        if no_label_increase_queue:
                            random_increase_instr = random.choice(no_label_increase_queue)
                            while random_increase_instr in special_instr:
                                random_increase_instr = random.choice(no_label_increase_queue)
                            modified_instr = modify_instruction_dec(line, random_increase_instr, 1, get_instruction_type(random_increase_instr), architecture, arch_bits)
                            updated_content.append(segment_label + modified_instr)
                    # Enable extension
                    else:
                        # filter missing_ext with allowed_ext
                        allowed_missing_ext = [instr for instr in missing_ext if find_instruction_extension(instr) in allowed_ext.allowed_ext]
                        
                        # Filter instructions excluding certain categories and extensions
                        no_label_increase_queue = [instr for instr in allowed_missing_ext if 'LABEL' not in get_instruction_format(instr).get('variables', []) \
                                                                                and 'LOAD' not in get_instruction_format(instr).get('category', []) \
                                                                                and 'STORE' not in get_instruction_format(instr).get('category', []) \
                                                                                and 'SYSTEM' not in get_instruction_format(instr).get('category', [])\
                                                                                and 'JUMP' not in get_instruction_format(instr).get('category', [])\
                                                                                and instr != 'vx_bar' \
                                                                                and not bug_filter.should_filter_instruction(instr, find_instruction_extension(instr), get_instruction_format(instr), architecture) \
                                                                                and find_instruction_extension(instr) not in exclude_extensions]# 为了做实验

                        if no_label_increase_queue:
                            random_increase_instr = random.choice(no_label_increase_queue)
                            while random_increase_instr in special_instr:
                                random_increase_instr = random.choice(no_label_increase_queue)
                            modified_instr = modify_instruction_dec(line, random_increase_instr, 1, get_instruction_type(random_increase_instr), architecture, arch_bits)
                            updated_content.append(segment_label + modified_instr)
                else:
                    updated_content.append(line)
            else:
                updated_content.append(line)
        else:
            # Instructions not in increase_queue or decrease_queue
            updated_content.append(line)
            # If it's regext, also append the next line to keep them together
            if is_regext:
                updated_content.append(next_line)
                i += 1  # Skip next line
        i += 1  # Move to next line
    base_filename = os.path.basename(file_path)
    new_filename = base_filename.replace('.S', '_mutated.S')

    new_file_path = os.path.join(mutate_directory, new_filename)
    
    # TODO use template rather than replace content
    # replace_content(file_path, updated_content, new_file_path)  # Assume updated_content is a list of strings
    write_instructions_to_file(new_file_path, list2str_without_indent(updated_content), template, architecture)

    # Cleanup Spike session
    if spike_session is not None:
        spike_session.cleanup()

    return f"Processed {base_filename}"


def write_instructions_to_file(new_filename: str, instructions: str, template: TemplateInstance, architecture: str = 'default'):
    """
    Write mutated instructions to file with template wrapper.

    Args:
        new_filename: Output file path
        instructions: Mutated instruction sequence
        template: Template instance to wrap instructions
    """
    lines = template.get_complete_template(instructions, architecture)

    os.makedirs(os.path.dirname(new_filename), exist_ok=True)

    with open(new_filename, 'w') as file:
        file.writelines(lines)


def count_instructions(processed_data):
    freq_counter = {}
    # Update the regular expression to match instructions containing numbers and decimal points
    instruction_pattern = re.compile(r'^\s*[^:\s]*:\s*([\w.]+)|^\s*([\w.]+)', re.MULTILINE) 
    #instruction_pattern = re.compile(r'^\s*_[a-zA-Z0-9]+:\s*([^\s].*?)(?=\s\s)', re.MULTILINE) # difuzz-rtl
    #instruction_pattern = re.compile(r'^\s*([\w.]+)\s*|(^\s*$)', re.MULTILINE) # difuzz-rtl

    for _, content in processed_data.items():
        matches = instruction_pattern.findall(content)

        for match in matches:
            # Extract instructions from the matching results
            instr = next((x for x in match if x), None)

            # Skip regext instruction prefix
            if instr and instr != 'regext':
                freq_counter[instr] = freq_counter.get(instr, 0) + 1

    return freq_counter

# Compute z-score normalization
def calculate_probabilities_z_score_full(counts, mean, std, min_prob=0.3, max_prob=0.9):
    p_decs = {}
    p_incs = {}
    valid_counts = [count for count in counts if count > 0]
    for count in counts:
        z_score = (count - mean) / std if std != 0 else 0
        p_decs[count] = np.clip(1 / (1 + np.exp(-z_score)), min_prob, max_prob) if count > 0 else 0.3
        p_incs[count] = np.clip(1 / (1 + np.exp(z_score)), min_prob, max_prob) if count > 0 else 0.9

    return p_decs, p_incs, valid_counts

def get_label_from_instruction(instruction, extension):
    instr_parts = instruction.split()
    instr_key = instr_parts[0]  # The keyword of the instruction, such as 'c.j'

    instr_format = INSTRUCTION_FORMATS.get(extension, {}).get(instr_key, {})
    format_str = instr_format.get("format", "")
    variables = instr_format.get("variables", [])
    # Check if LABEL is in a variable and find its position
    if "LABEL" in variables:
        label_position = format_str.split().index("{" + "LABEL" + "}")
        if label_position < len(instr_parts):
            return instr_parts[label_position]
    
    return None
