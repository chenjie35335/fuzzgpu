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

import os
import subprocess
from pathlib import Path
from ..asm_template_manager import temp_file_manager
from .compiler_config import get_compiler_config
import traceback

def generate_elf(source_path: str, spike_args: str, architecture: str, arch_bits: int = 64):
    # For spike resolution RS value
    """
    Process a single assembly file.

    Parameters:
    source_path: Path to the source assembly file
    spike_args: Additional arguments for the assembler (e.g., -march=rv64gc)
    arch_bits: Architecture bit width (32 or 64)
    architecture: Target architecture ('ventus', 'xs', 'nts', 'rkt', or 'default')
    """

    filename = os.path.basename(source_path)
    base_name = os.path.splitext(filename)[0]
    folder_path = os.path.dirname(source_path)

    object_file = os.path.join(folder_path, base_name + '.o')
    elf_file = os.path.join(folder_path, base_name + '.elf')


    # Register temporary file to the manager
    temp_file_manager.register_temp_file(object_file)
    temp_file_manager.register_temp_file(elf_file)

    # Get compiler configuration based on architecture
    print('architecture', architecture)
    compiler_cfg = get_compiler_config(architecture, arch_bits)
    compiler_as = compiler_cfg['as']
    compiler_ld = compiler_cfg['ld']

    # Linking in reg_analyzer/liner directory
    link_dir = Path(__file__).parent / 'linker/link.ld'
    if arch_bits == 32:
        link_dir = Path(__file__).parent / 'linker/link32.ld'
            
    try:
        compile_cmd = [compiler_as] + compiler_cfg['as_flags'] + spike_args.split() + [source_path, '-o', object_file]
        result = subprocess.run(compile_cmd)
        if result.returncode != 0:
            print(f"Compilation failed.")
            return None

        link_cmd = [compiler_ld] + compiler_cfg['ld_flags'] + ['-T', str(link_dir), object_file, '-o', elf_file]
        result = subprocess.run(link_cmd)
        if result.returncode != 0:
            print(f"Linking failed.")
            return None
        
        return elf_file

    except Exception as e:
        traceback.print_exc()
        print(f"Error occurred during compilation: {e}")
        return None
