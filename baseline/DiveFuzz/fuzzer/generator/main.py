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

import time
import logging
import os
import subprocess
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor
from tqdm import tqdm
from .config.cli_parser import parse_args
from .config.config_manager import setup_config
from .core.generator import generate_instructions_parallel
from .core.mutator import mutate_instructions_parallel
from .reg_analyzer.compiler_config import get_compiler_config

logger = logging.getLogger(__name__)

# When processing a large number of files and performing compute-intensive operations 
# on the file contents (such as instruction counting, probability calculation, etc.), 
# the optimal choice is usually to use multiprocessing to parallelize the work, 
# in order to fully utilize the computational power of multi-core processors. 
# This is because Python's Global Interpreter Lock (GIL) restricts only one thread 
# from executing Python bytecode at a time. Therefore, for compute-intensive tasks, 
# multithreading does not provide significant performance improvements. 
# In contrast, using multiprocessing can bypass the GIL limitation, 
# since each Python process has its own interpreter and memory space, 
# thus enabling true parallel execution of tasks.



def main():
    args = parse_args()
    config = setup_config(args)

    if config.generate_enable:
        generate_instructions_parallel(
            config.instr_number,
            config.seed_times,
            config.eliminate_enable,
            config.is_cva6,
            config.is_rv32,
            config.max_workers,
            config.arch,
            config.template_type,
            str(config.out_dir),
            config.architecture,
            bug_filter_json=config.bug_filter_json,
            disable_default_bug_filtering=config.disable_default_bug_filtering
        )
        asm_out_dir = str(config.out_dir)

    # Whether to enable out-of-order mutation, considering previously unseen extension instructions
    if config.mutation_enable:
        mutate_instructions_parallel(
            config.directory_path, # seed-dir
            config.mutate_directory, # (args.mutate_out or (self.directory_path / 'mutate')
            config.max_workers,
            config.enable_ext,
            config.exclude_extensions,
            config.eliminate_enable,
            config.arch,
            config.template_type,
            config.is_rv32,
            config.architecture,
            bug_filter_json=config.bug_filter_json,
            disable_default_bug_filtering=config.disable_default_bug_filtering
        )
        asm_out_dir = str(config.mutate_directory)
    
    process_divefuzz_asm(asm_out_dir, logger, args.architecture, 32 if args.rv32 else 64)


def process_file(filename, folder_path, img_folder, elf_folder, architecture='default', arch_bits=64):
    """
    Process a single assembly file: .S -> .o -> .elf -> .img

    Parameters:
    filename: Assembly file name
    folder_path: Path to the folder where the original file is located
    img_folder: Directory where the generated img file will be stored
    elf_folder: Directory where the generated elf file will be stored
    architecture: Target architecture ('ventus', 'xs', 'nts', 'rkt', or 'default')
    arch_bits: Architecture bit width (32 or 64)
    """
    base_name = os.path.splitext(filename)[0]
    source_path = os.path.join(folder_path, filename)

    object_file = os.path.join(folder_path, base_name + '.o')
    elf_file = os.path.join(elf_folder, base_name + '.elf')
    img_file = os.path.join(img_folder, base_name + '.img')

    # Get linker script path
    link_dir = Path(__file__).parent / 'reg_analyzer' / 'linker' / 'link.ld'
    if arch_bits == 32:
        link_dir = Path(__file__).parent / 'reg_analyzer' / 'linker' / 'link32.ld'
    
    # Get compiler configuration
    compiler_cfg = get_compiler_config(architecture, arch_bits)
    
    # Step 1: Assemble .S to .o
    print(compiler_cfg['as'], compiler_cfg['as_flags'], source_path, object_file)
    subprocess.run([compiler_cfg['as']] + compiler_cfg['as_flags'] + ['-c', source_path, '-o', object_file])
    
    # Step 2: Link .o to .elf
    ld_cmd = [compiler_cfg['ld']]
    if 'ld_flags' in compiler_cfg and compiler_cfg['ld_flags']:
        ld_cmd.extend(compiler_cfg['ld_flags'])
    ld_cmd.extend(['-T', str(link_dir), object_file, '-o', elf_file])
    subprocess.run(ld_cmd)
    
    # Step 3: Convert .elf to .img (binary)
    subprocess.run([compiler_cfg['objcopy'], '-O', 'binary', elf_file, img_file])

    # Clean up intermediate .o files
    if os.path.exists(object_file):
        os.remove(object_file)


def process_assembly_files(folder_path, architecture='default', arch_bits=64):
    """
    Process all assembly files (.S) in the specified folder.

    Parameters:
    folder_path: Path to the folder to be processed
    architecture: Target architecture ('ventus', 'xs', 'nts', 'rkt', or 'default')
    arch_bits: Architecture bit width (32 or 64)
    """
    # Create output directories
    img_folder = os.path.join(folder_path, 'img_file')
    os.makedirs(img_folder, exist_ok=True)

    elf_folder = os.path.join(folder_path, 'elf_file')
    os.makedirs(elf_folder, exist_ok=True)

    # Find all assembly files
    assembly_files = [f for f in os.listdir(folder_path) if f.endswith('.S')]

    if not assembly_files:
        logger.warning(f"No assembly files (.S) found in {folder_path}")
        return

    # Use thread pool to process files in parallel
    with ThreadPoolExecutor() as executor:
        list(tqdm(
            executor.map(
                process_file, 
                assembly_files, 
                [folder_path] * len(assembly_files), 
                [img_folder] * len(assembly_files), 
                [elf_folder] * len(assembly_files), 
                [architecture] * len(assembly_files), 
                [arch_bits] * len(assembly_files)
            ), 
            total=len(assembly_files), 
            desc="Converting assembly to elf/img"
        ))


def process_divefuzz_asm(seeds_output, seed_config_logger, architecture='default', arch_bits=64):
    """
    Process DiveFuzz generated assembly files and convert them to elf/img format.

    Parameters:
    seed_config: Seed configuration object with divefuzz.seeds_output path
    seed_config_logger: Logger instance for logging
    architecture: Target architecture ('ventus', 'xs', 'nts', 'rkt', or 'default')
    arch_bits: Architecture bit width (32 or 64)

    Returns:
    List of generated .img file paths
    """
    # Use the configured seeds_output directory
    generator_output_dir = Path(seeds_output)

    # Convert assembly files to elf and img
    seed_config_logger.info("Converting assembly to elf files...")
    process_assembly_files(str(generator_output_dir), architecture, arch_bits)

    # Find img files (binary format for DUT execution)
    # All DUTs (NutShell, Rocket, XiangShan) use .img format
    img_dir = generator_output_dir / 'img_file'
    if not img_dir.exists():
        seed_config_logger.error(f"IMG directory not found: {img_dir}")
        return []

    seed_files = []
    for file in os.listdir(img_dir):
        if file.endswith(".img"):
            seed_files.append(os.path.join(img_dir, file))

    seed_config_logger.info(
        f"Found {len(seed_files)} generated seed files (.img)")

    return seed_files

def test_main():
    start_time = time.time()
    main()
    end_time = time.time()
    print("# Execution time: %.2f seconds" % (end_time - start_time))

if __name__ == "__main__":
    test_main()
