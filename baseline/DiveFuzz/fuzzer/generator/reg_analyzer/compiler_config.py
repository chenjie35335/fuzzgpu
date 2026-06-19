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

"""
Compiler configuration for different architectures.
Defines assembler, linker, and assembler flags for each supported architecture.
"""

import os
from pathlib import Path


def _repo_root() -> Path:
    override = os.environ.get('FUZZGPU_HOME') or os.environ.get('PRJ_HOME')
    if override:
        return Path(override).expanduser().resolve()

    path = Path(__file__).resolve()
    for parent in path.parents:
        if (parent / 'targets' / 'ventus-env').exists():
            return parent
    return path.parents[5]


_FUZZGPU_HOME = _repo_root()
_VENTUS_INSTALL = Path(os.environ.get(
    'VENTUS_INSTALL_PREFIX',
    str(_FUZZGPU_HOME / 'targets' / 'ventus-env' / 'install'),
)).expanduser()
_RISCV32_TOOLCHAIN = Path(os.environ.get(
    'RISCV32_TOOLCHAIN_ROOT',
    os.path.expandvars('$HOME/tools/riscv32-gnu-toolchain'),
)).expanduser()
_RISCV64_TOOLCHAIN = Path(os.environ.get(
    'RISCV_TOOLCHAIN_ROOT',
    os.path.expandvars('$HOME/tools/riscv64-gnu-toolchain'),
)).expanduser()


# Architecture-specific compiler configurations
COMPILER_CONFIG = {
    'default': {
        'as': 'riscv64-unknown-elf-as',
        'ld': 'riscv64-unknown-elf-ld',
        'objcopy': 'riscv64-unknown-elf-objcopy',
        'as_flags': '-march=rv64g_c_v_zicsr_zifencei_zfh_zba_zbb_zbkc_zbc_zbkb_zbs_zmmul_zknh_zkne_zknd_zbkx_zfhmin',
        'as_flags_32': '-march=rv32g_c_v_zicsr_zifencei_zfh_zba_zbb_zbkc_zbc_zbkb_zbs_zmmul_zknh_zkne_zknd_zbkx_zfhmin',
        'as_output_flag': '-o',  # Flag for output file in assembler
        'as_input_flag': None,   # Most assemblers take input as positional argument
        'ld_flags': '',  # Default linker has no special flags
    },
    'ventus': {
        'as': str(_VENTUS_INSTALL / 'bin' / 'clang'),
        'ld': str(_VENTUS_INSTALL / 'bin' / 'clang'),
        'objcopy': str(_VENTUS_INSTALL / 'bin' / 'llvm-objcopy'),
        'as_flags': ['--target=riscv32', '-mcpu=ventus-gpgpu', '-c'],
        'as_flags_32': ['--target=riscv32', '-mcpu=ventus-gpgpu', '-c'],
        'as_output_flag': '-o',  # clang uses -o for output
        'as_input_flag': None,   # Input is positional
        'ld_flags': ['--target=riscv32', '-mcpu=ventus-gpgpu', '-nostdlib'],
    },
    'vortex32': {
        'as': str(_RISCV32_TOOLCHAIN / 'bin' / 'riscv32-unknown-elf-as'),
        'ld': str(_RISCV32_TOOLCHAIN / 'bin' / 'riscv32-unknown-elf-ld'),
        'objcopy': str(_RISCV32_TOOLCHAIN / 'bin' / 'riscv32-unknown-elf-objcopy'),
        'as_flags': ['-march=rv32ifmd_zicsr_zifencei'],
        'as_flags_32': ['-march=rv32ifmd_zicsr_zifencei'],
        'as_output_flag': '-o',
        'as_input_flag': None,
        'ld_flags': [],
    },
    'vortex64': {
        'as': str(_RISCV64_TOOLCHAIN / 'bin' / 'riscv64-unknown-elf-as'),
        'ld': str(_RISCV64_TOOLCHAIN / 'bin' / 'riscv64-unknown-elf-ld'),
        'objcopy': str(_RISCV64_TOOLCHAIN / 'bin' / 'riscv64-unknown-elf-objcopy'),
        'as_flags': ['-march=rv64ifmd_zicsr_zifencei'],
        'as_flags_32': ['-march=rv64ifmd_zicsr_zifencei'],
        'as_output_flag': '-o',
        'as_input_flag': None,
        'ld_flags': [],
    },
    'xs': {
        'as': 'riscv64-unknown-elf-as',
        'ld': 'riscv64-unknown-elf-ld',
        'objcopy': 'riscv64-unknown-elf-objcopy',
        'as_flags': '-march=rv64g_c_v_zicsr_zifencei_zfh_zba_zbb_zbkc_zbc_zbkb_zbs_zmmul_zknh_zkne_zknd_zbkx_zfhmin',
        'as_flags_32': '-march=rv32g_c_v_zicsr_zifencei_zfh_zba_zbb_zbkc_zbc_zbkb_zbs_zmmul_zknh_zkne_zknd_zbkx_zfhmin',
        'as_output_flag': '-o',
        'as_input_flag': None,
        'ld_flags': '',
    },
    'nts': {
        'as': 'riscv64-unknown-elf-as',
        'ld': 'riscv64-unknown-elf-ld',
        'objcopy': 'riscv64-unknown-elf-objcopy',
        'as_flags': '-march=rv64g_c_v_zicsr_zifencei_zfh_zba_zbb_zbkc_zbc_zbkb_zbs_zmmul_zknh_zkne_zknd_zbkx_zfhmin',
        'as_flags_32': '-march=rv32g_c_v_zicsr_zifencei_zfh_zba_zbb_zbkc_zbc_zbkb_zbs_zmmul_zknh_zkne_zknd_zbkx_zfhmin',
        'as_output_flag': '-o',
        'as_input_flag': None,
        'ld_flags': '',
    },
    'rkt': {
        'as': 'riscv64-unknown-elf-as',
        'ld': 'riscv64-unknown-elf-ld',
        'objcopy': 'riscv64-unknown-elf-objcopy',
        'as_flags': '-march=rv64g_c_v_zicsr_zifencei_zfh_zba_zbb_zbkc_zbc_zbkb_zbs_zmmul_zknh_zkne_zknd_zbkx_zfhmin',
        'as_flags_32': '-march=rv32g_c_v_zicsr_zifencei_zfh_zba_zbb_zbkc_zbc_zbkb_zbs_zmmul_zknh_zkne_zknd_zbkx_zfhmin',
        'as_output_flag': '-o',
        'as_input_flag': None,
        'ld_flags': '',
    },
}

def get_compiler_config(architecture: str = 'default', arch_bits: int = 64):
    """
    Get compiler configuration for the specified architecture.
    
    Args:
        architecture: Architecture name ('ventus', 'xs', 'nts', 'rkt', or 'default')
        arch_bits: Architecture bit width (32 or 64)
    
    Returns:
        Dictionary with compiler configuration including:
        - 'as': Assembler command
        - 'ld': Linker command
        - 'objcopy': objcopy command
        - 'as_flags': Assembler flags
        - 'as_output_flag': Flag for output file in assembler (default: '-o')
        - 'as_input_flag': Flag for input file in assembler (default: None for positional)
        - 'ld_flags': Linker flags (default: '')
    """
    if architecture == 'vortex':
        architecture = f'vortex{arch_bits}'
    config = COMPILER_CONFIG.get(architecture, COMPILER_CONFIG['default'])
    
    # Create a copy to avoid modifying the original
    result = config.copy()
    
    # Select appropriate flags based on arch_bits
    if arch_bits == 32:
        result['as_flags'] = config['as_flags_32']
    
    # Ensure all required keys have defaults
    result.setdefault('as_output_flag', '-o')
    result.setdefault('as_input_flag', None)
    result.setdefault('ld_flags', '')
    
    return result
