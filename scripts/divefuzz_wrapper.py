#!/usr/bin/env python3
"""Wrapper script for running DiveFuzz generation in a separate process.

This allows parallel execution without stdout/stderr redirection conflicts.

Note: Any exceptions will be dumped to the output JSON file (no echo!!!)
"""

import sys
import os
import json
import argparse
from pathlib import Path
import re
import subprocess
import tempfile
import shutil

_expand_dot_insn = None

class LiCostAnalyzer:
    def __init__(self, compile_fn, get_compiler_config_fn, arch, xlen):
        self.compile_fn = compile_fn
        self.compiler_cfg = get_compiler_config_fn(arch, xlen)
        self.arch = arch
        self.xlen = xlen
        self.memo = {}
        self._analyze()

    def _resolve_objdump(self):
        cfg = self.compiler_cfg
        candidates = []

        if cfg.get('objdump'):
            candidates.append(cfg['objdump'])

        for key in ('objcopy', 'as', 'ld'):
            tool = cfg.get(key)
            if not tool:
                continue
            tool_path = Path(tool)
            name = tool_path.name

            if 'objcopy' in name:
                candidates.append(str(tool_path.with_name(name.replace('objcopy', 'objdump'))))
            if name.startswith('llvm-') or name == 'clang' or name.startswith('clang-'):
                candidates.append(str(tool_path.with_name('llvm-objdump')))
            if name.endswith('-as') or name.endswith('-ld') or name.endswith('-gcc'):
                candidates.append(str(tool_path.with_name(name.rsplit('-', 1)[0] + '-objdump')))

        candidates.append('objdump')

        # Resolve to an existing executable
        tried = []
        for cand in candidates:
            tried.append(cand)
            cand_path = Path(cand)
            if cand_path.is_absolute() and cand_path.exists():
                return cand
            resolved = shutil.which(cand)
            if resolved:
                return resolved

        raise FileNotFoundError(f"objdump not found for toolchain: tried {tried}")

    def _compile_and_count_la(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            p = Path(temp_dir)
            asm_file = p / "test_la.S"
            with open(asm_file, 'w') as f:
                f.write(
                    ".section .text\n"
                    ".global _start\n"
                    "_start:\n"
                    "\tla x1, target\n"
                    ".section .data\n"
                    "target:\n"
                    + ("\t.dword 0\n" if self.xlen == 64 else "\t.word 0\n")
                )

            self.compile_fn(
                filename="test_la.S",
                folder_path=str(p),
                img_folder=str(p),
                elf_folder=str(p),
                architecture=self.arch,
                arch_bits=self.xlen
            )

            elf_file = p / "test_la.elf"
            if not elf_file.exists():
                raise FileNotFoundError(f"ELF not generated: {elf_file}")

            objdump = self._resolve_objdump()
            cmd = [objdump, '-d', str(elf_file)]
            result = subprocess.run(cmd, capture_output=True, text=True)
            if result.returncode != 0:
                raise RuntimeError(f"objdump failed: {objdump}\n{result.stderr}")
            if not result.stdout:
                raise RuntimeError(f"objdump produced no output: {objdump}")
            output = result.stdout

            count = 0
            for line in output.splitlines():
                if re.match(r'^\s*[0-9a-fA-F]+:\s+[0-9a-fA-F]+', line):
                    count += 1
            if count <= 0:
                raise RuntimeError("No instructions found in objdump output")
            return count

    def _compile_and_count(self, val):
        with tempfile.TemporaryDirectory() as temp_dir:
            p = Path(temp_dir)
            asm_file = p / "test_li.S"
            with open(asm_file, 'w') as f:
                f.write(f".section .text\n.global _start\n_start:\n\tli x1, {val}\n")

            # Use the same toolchain as generate_elf
            self.compile_fn(
                filename="test_li.S",
                folder_path=str(p),
                img_folder=str(p),
                elf_folder=str(p),
                architecture=self.arch,
                arch_bits=self.xlen
            )

            elf_file = p / "test_li.elf"
            if not elf_file.exists():
                raise FileNotFoundError(f"ELF not generated: {elf_file}")

            objdump = self._resolve_objdump()
            cmd = [objdump, '-d', str(elf_file)]
            result = subprocess.run(cmd, capture_output=True, text=True)
            if result.returncode != 0:
                raise RuntimeError(f"objdump failed: {objdump}\n{result.stderr}")
            if not result.stdout:
                raise RuntimeError(f"objdump produced no output: {objdump}")
            output = result.stdout

            count = 0
            # Simple parsing of objdump output
            # 10000:	4581                	li	a1,0
            # Matches lines starting with hex address
            for line in output.splitlines():
                if re.match(r'^\s*[0-9a-fA-F]+:\s+[0-9a-fA-F]+', line):
                     count += 1
            if count <= 0:
                raise RuntimeError("No instructions found in objdump output")
            return count

    def _analyze(self):
        # Probe a few points to establish baseline costs
        self.cost_0 = self._compile_and_count(0)
        self.cost_2048 = self._compile_and_count(2048)
        self.cost_la = self._compile_and_count_la()
        # For 64-bit, probe a 32-bit+ value
        if self.xlen == 64:
            self.cost_huge = self._compile_and_count("0x123456789")
        else:
            self.cost_huge = self.cost_2048
        print('cost_0:', self.cost_0, 'cost_2048:', self.cost_2048, 'cost_huge:', self.cost_huge, 'cost_la:', self.cost_la)

    def get_la_cost(self):
        return self.cost_la

    def get_cost(self, imm_str):
        if imm_str in self.memo:
            return self.memo[imm_str]

        try:
            val = int(imm_str, 0)
        except Exception as exc:
            raise ValueError(f"Invalid li immediate: {imm_str}") from exc

        cost = 1
        # Use probed logic
        if -2048 <= val <= 2047:
            cost = self.cost_0
        elif self.xlen == 32:
            cost = self.cost_2048
        else: # 64-bit
            if -2147483648 <= val <= 2147483647:
                cost = self.cost_2048
            else:
                # For very large values, verify cache or assume cost_huge
                # Or lazily compile if not efficient to guess
                # To follow "test different ranges... then consider", using probe results for ranges is consistent.
                # However, 64-bit li construction can vary (3, 4, 5 instrs).
                # For high accuracy, we compile.
                cost = self._compile_and_count(imm_str)

        self.memo[imm_str] = cost
        return cost

def count_instructions(processed_data, modules=None):
    freq_counter = {}
    expanded_count = 0
    li_cost_analyzer = modules['li_analyzer']

    # Update the regular expression to match instructions containing numbers and decimal points
    # instruction_pattern = re.compile(r'^\s*[^:\s]*:\s*([\w.]+)|^\s*([\w.]+)', re.MULTILINE) 
    #instruction_pattern = re.compile(r'^\s*_[a-zA-Z0-9]+:\s*([^\s].*?)(?=\s\s)', re.MULTILINE) # difuzz-rtl
    #instruction_pattern = re.compile(r'^\s*([\w.]+)\s*|(^\s*$)', re.MULTILINE) # difuzz-rtl

    for _, content in processed_data.items():
        for line in content.splitlines():
            line = line.strip()
            # Remove comments
            if '#' in line:
                line = line.split('#')[0].strip()
            if not line: continue
            
            # Remove labels
            if ':' in line:
                line = line.split(':')[-1].strip()
            if not line: continue
            
            # Parse instruction
            parts = line.split(None, 1)
            instr = parts[0]
            if instr == '.insn':
                expanded = modules['expand_dot_insn'](line)
                if expanded.strip().startswith('.insn'):
                    raise ValueError(f"Failed to expand .insn: {line}")
                instr = expanded
            
            # Skip directives and empty, but keep .insn
            if not instr: continue
            if instr.startswith('.') and not instr.startswith('.insn'):
                 continue
                 
            freq_counter[instr] = freq_counter.get(instr, 0) + 1
            
            # Calculate cost
            cost = 1
            if instr == 'li':
                args = parts[1] if len(parts) > 1 else ""
                # li rd, imm
                ops = [op.strip() for op in args.split(',')]
                if len(ops) >= 2:
                    cost = li_cost_analyzer.get_cost(ops[-1])
            elif instr == 'la' and li_cost_analyzer:
                cost = li_cost_analyzer.get_la_cost()
            
            expanded_count += cost

    return freq_counter, expanded_count

def run_mutation_mode(args, modules, arch_config):
    """Run DiveFuzz in mutation mode.
    
    Args:
        args: Parsed command line arguments
        modules: Dictionary containing imported DiveFuzz modules
        arch_config: Dictionary with architecture configuration
    
    Returns:
        Output data dictionary with mutation results
    """
    process_file = modules['process_file']
    process_content = modules['process_content']
    compile_asm_to_elf = modules['compile_asm_to_elf']
    #count_instructions = modules['count_instructions']
    arch = arch_config['arch']
    template_type = arch_config['template_type']
    is_rv32 = arch_config['is_rv32']
    
    # Validate mutation mode parameters
    if not args.input_asm or not args.mutate_dir or not args.mutate_elf_dir or args.mutation_idx is None:
        return {
            'success': False,
            'error': 'Mutation mode requires --input-asm, --mutate-dir, --mutate-elf-dir, and --mutation-idx'
        }
    
    input_asm = Path(args.input_asm)
    mutate_dir = Path(args.mutate_dir)
    mutate_elf_dir = Path(args.mutate_elf_dir)
    
    if not input_asm.exists():
        return {
            'success': False,
            'error': f'Input assembly file not found: {input_asm}'
        }
    
    # Read input assembly content
    input_asm, asm_content = process_file(input_asm)

    # Perform mutation
    result_msg = process_content(
        file_path=str(input_asm),
        file_content=asm_content,
        directory_path=str(input_asm.parent),
        mutate_directory=str(mutate_dir),
        enable_ext=True,
        exclude_extensions=[],
        eliminate_enable=not args.disable_eliminate,
        arch=arch,
        template_type=template_type,
        is_rv32=is_rv32,
        architecture=args.arch,
        num_warps=args.num_warps_per_core,
        num_threads=args.num_threads_per_warp,
        bug_filter_json=args.bug_filter_json,
        disable_default_bug_filtering=args.disable_default_bug_filtering,
        vortex_max_num_barrier=args.vortex_max_num_barrier,
        vortex_max_barrier_count=args.vortex_max_barrier_count,
        num_warps_per_core=args.num_warps_per_core,
        num_cores_per_cluster=args.vortex_cores_per_clusters
    )
    
    # The mutated file was written by process_content with suffix _mutated
    # DiveFuzz writes to: mutate_directory / f'{basename_without_ext}__mutated.S'
    basename = input_asm.stem  # e.g., 'seeds_0_'
    mut_asm_file = mutate_dir / f'{basename}_mutated.S'
    
    # Rename to include mutation index for uniqueness
    mut_asm_file_renamed = mutate_dir / f'{basename}mut_{args.mutation_idx}_.S'
    if mut_asm_file.exists():
        os.rename(str(mut_asm_file), str(mut_asm_file_renamed))
    else:
        return {
            'success': False,
            'error': f'Mutated file not created: {mut_asm_file}'
        }
    
    # Read mutated content
    with open(mut_asm_file_renamed, 'r') as f:
        mutated_content = f.read()
    
    # Compile mutated assembly
    compile_asm_to_elf(
        filename=mut_asm_file_renamed.name,
        folder_path=str(mutate_dir),
        img_folder=str(mutate_dir),
        elf_folder=str(mutate_elf_dir),
        architecture=args.arch,
        arch_bits=args.xlen
    )
    
    # Count instructions
    instr_freq, total_count = count_instructions(
        {str(mut_asm_file_renamed): mutated_content}, 
        modules=modules
    )
    
    # Check if elf was created
    elf_path = mutate_elf_dir / f'{mut_asm_file_renamed.stem}.elf'
    elf_exists = elf_path.exists()
    
    # Return output data
    return {
        'success': elf_exists,
        'asm_file': str(mut_asm_file_renamed),
        'elf_file': str(elf_path) if elf_exists else None,
        'elf_exists': elf_exists,
        'instr_count': total_count,
        'instr_freq': instr_freq,
        'result_msg': result_msg
    }


def run_generation_mode(args, modules, arch_config, xor_cache, xor_cache_file):
    """Run DiveFuzz in generation mode.
    
    Args:
        args: Parsed command line arguments
        modules: Dictionary containing imported DiveFuzz modules
        arch_config: Dictionary with architecture configuration
        xor_cache: XOR cache dictionary
        xor_cache_file: Path to XOR cache file
    
    Returns:
        Output data dictionary with generation results
    """
    generate_instructions = modules['generate_instructions']
    compile_asm_to_elf = modules['compile_asm_to_elf']
    #count_instructions = modules['count_instructions']
    arch = arch_config['arch']
    template_type = arch_config['template_type']
    is_rv32 = arch_config['is_rv32']
    out_dir = Path(args.out_dir)
    elf_dir = Path(args.elf_dir)
    
    # Generate instructions
    result = generate_instructions(
        instr_number=args.instr_number,
        seed_times=args.seed_id,
        eliminate_enable=not args.disable_eliminate,
        is_cva6=False,
        is_rv32=is_rv32,
        arch=arch,
        template_type=template_type,
        out_dir=str(out_dir),
        shared_xor_cache=xor_cache,
        architecture=args.arch,
        num_warps=args.num_warps_per_core,
        num_threads=args.num_threads_per_warp,
        bug_filter_json=args.bug_filter_json,
        disable_default_bug_filtering=args.disable_default_bug_filtering,
        vortex_max_num_barrier=args.vortex_max_num_barrier,
        vortex_max_barrier_count=args.vortex_max_barrier_count,
        num_warps_per_core=args.num_warps_per_core,
        num_cores_per_cluster=args.vortex_cores_per_clusters
    )
    
    if result is None:
        return {
            'success': False,
            'error': 'generate_instructions returned None'
        }
    
    # The assembly file was written by generate_instructions
    asm_file = out_dir / f'seeds_{args.seed_id}_.S'
    if not asm_file.exists():
        return {
            'success': False,
            'error': f'Assembly file not found: {asm_file}'
        }
    
    # Read assembly content for instruction counting
    with open(asm_file, 'r') as f:
        asm_content = f.read()
    
    # Compile to elf
    compile_asm_to_elf(
        filename=f'seeds_{args.seed_id}_.S',
        folder_path=str(out_dir),
        img_folder=str(out_dir),
        elf_folder=str(elf_dir),
        architecture=args.arch,
        arch_bits=args.xlen
    )
    
    # Count instructions
    instr_freq, total_count = count_instructions(
        {str(asm_file): asm_content}, 
        modules=modules
    )
    
    # Check if elf was created
    elf_path = elf_dir / f'seeds_{args.seed_id}_.elf'
    elf_exists = elf_path.exists()
    
    # Update XOR cache file
    if xor_cache:
        cache_dict = {k: list(v) for k, v in xor_cache.items()}
        with open(xor_cache_file, 'w') as f:
            json.dump(cache_dict, f)
    
    # Return output data
    return {
        'success': elf_exists,
        'asm_file': str(asm_file),
        'elf_file': str(elf_path) if elf_exists else None,
        'elf_exists': elf_exists,
        'instr_count': total_count,
        'instr_freq': instr_freq
    }


def main():
    parser = argparse.ArgumentParser(description='DiveFuzz wrapper for parallel execution')
    parser.add_argument('--divefuzz-dir', type=str, required=True)
    parser.add_argument('--seed-id', type=int, required=True)
    parser.add_argument('--instr-number', type=int, default=200)
    parser.add_argument('--arch', type=str, choices=['ventus', 'vortex'], required=True)
    parser.add_argument('--xlen', type=int, choices=[32, 64], required=True)
    parser.add_argument('--out-dir', type=str, required=True)
    parser.add_argument('--elf-dir', type=str, required=True)
    parser.add_argument('--xor-cache-file', type=str, required=True)
    parser.add_argument('--output-json', type=str, required=True, help='Output JSON with results')
    parser.add_argument('--num-warps-per-core', type=int, default=2, help='Number of warps per core')
    parser.add_argument('--num-threads-per-warp', type=int, default=4, help='Number of threads per warp')
    parser.add_argument('--bug-filter-json', type=str, default=None,
                        help='fuzzgpu-compatible generator bug filter JSON')
    parser.add_argument('--disable-default-bug-filtering', action='store_true', default=False,
                        help='Use fuzzgpu disable-default-bug-filtering semantics when --bug-filter-json is supplied')
    
    # Optional Vortex-specific parameters
    parser.add_argument('--vortex-max-num-barrier', type=int, help='Vortex max num barrier')
    parser.add_argument('--vortex-max-barrier-count', type=int, help='Vortex max barrier count')
    parser.add_argument('--vortex-cores-per-clusters', type=int, help='Vortex cores per cluster (unused, for compatibility)')
    
    # Mutation mode parameters
    parser.add_argument('--mutate', action='store_true', help='Run in mutation mode')
    parser.add_argument('--input-asm', type=str, help='Input assembly file for mutation')
    parser.add_argument('--mutate-dir', type=str, help='Output directory for mutated files')
    parser.add_argument('--mutate-elf-dir', type=str, help='Output directory for mutated elf files')
    parser.add_argument('--mutation-idx', type=int, help='Mutation index for naming')
    
    # Eliminate enable parameter (enabled by default, use --disable-eliminate to turn off)
    parser.add_argument('--disable-eliminate', action='store_true', default=False,
                        help='Disable elimination optimization (enabled by default)')
    
    args = parser.parse_args()
    
    divefuzz_dir = Path(args.divefuzz_dir)
    
    # Setup Python path
    sys.path.insert(0, str(divefuzz_dir / 'fuzzer'))
    
    # Import DiveFuzz modules
    from generator.instr_generator.formats import expand_dot_insn
    from generator.core.generator.generate_instrs import generate_instructions
    from generator.core.mutator.preprocess import process_file
    from generator.core.mutator.mutate_instructions import process_content
    from generator.asm_template_manager.riscv_asm_syntex import ArchConfig
    from generator.main import process_file as compile_asm_to_elf
    from generator.asm_template_manager.ext_list import allowed_ext
    from generator.bug_filter import bug_filter
    from generator.reg_analyzer.compiler_config import get_compiler_config

    li_analyzer = LiCostAnalyzer(compile_asm_to_elf, get_compiler_config, args.arch, args.xlen)
    
    # Package modules for passing to functions
    modules = {
        "process_file": process_file,
        'generate_instructions': generate_instructions,
        'process_content': process_content,
        #'count_instructions': count_instructions,
        'compile_asm_to_elf': compile_asm_to_elf,
        'li_analyzer': li_analyzer,
        'expand_dot_insn': expand_dot_insn
    }
    
    # Setup architecture-specific configurations
    is_rv32 = (args.xlen == 32)
    
    # Initialize allowed_ext based on architecture
    if args.arch == 'ventus':
        allowed_ext_name = 'ventus'
    elif args.arch == 'vortex':
        allowed_ext_name = 'vortex32' if is_rv32 else 'vortex64'
    else:
        allowed_ext_name = 'general'
    
    allowed_ext.setup_ext(allowed_ext_name)
    
    # Initialize bug_filter
    bug_filter.set_architecture(args.arch)
    bug_filter.load_generator_filter_json(
        args.bug_filter_json,
        default_filtering=not args.disable_default_bug_filtering,
    )
    
    # Setup architecture config
    arch_bits = 32 if is_rv32 else 64
    arch = ArchConfig(arch_bits)
    template_type = args.arch
    
    arch_config = {
        'arch': arch,
        'template_type': template_type,
        'is_rv32': is_rv32
    }
    
    # Load XOR cache if exists (only for generation mode)
    xor_cache = {}
    xor_cache_file = Path(args.xor_cache_file)
    if not args.mutate and xor_cache_file.exists():
        try:
            with open(xor_cache_file, 'r') as f:
                cache_data = json.load(f)
                xor_cache = {k: tuple(v) for k, v in cache_data.items()}
        except Exception as e:
            print(f"Warning: Failed to load XOR cache: {e}", file=sys.stderr)
    
    try:
        # Run appropriate mode
        if args.mutate:
            output_data = run_mutation_mode(args, modules, arch_config)
        else:
            output_data = run_generation_mode(args, modules, arch_config, xor_cache, xor_cache_file)
        
        # Write output
        with open(args.output_json, 'w') as f:
            json.dump(output_data, f, indent=2)
        
        # Exit with appropriate code
        sys.exit(0 if output_data.get('success', False) else 1)
        
    except Exception as e:
        import traceback
        output_data = {
            'success': False,
            'error': str(e),
            'traceback': traceback.format_exc()
        }
        print(traceback.format_exc())
        with open(args.output_json, 'w') as f:
            json.dump(output_data, f, indent=2)
        sys.exit(1)


if __name__ == '__main__':
    main()
