#!/usr/bin/env python3
"""
Measure fuzz_gpu instruction throughput for different generator configurations.
"""

import argparse
import os
import signal
import subprocess
import sys
import threading
import time
from shutil import which
import statistics
import json
import datetime
from pathlib import Path
from tqdm import tqdm

GREEN = "\033[32m"
RED = "\033[31m"
YELLOW = "\033[33m"
RESET = "\033[0m"

# Constants
EMULATOR_TIMEOUT_SECONDS = 10 * 60  # 10 minutes timeout for emulator

# Shared state for running child processes so we can terminate them on Ctrl+C.
_procs_lock = threading.Lock()
_procs = set()  # set of subprocess.Popen objects
_stop_event = threading.Event()


def _handle_signal(signum, frame):
    try:
        print(f"{YELLOW}\nReceived signal {signum}, terminating children...{RESET}")
    except Exception:
        pass
    _stop_event.set()
    with _procs_lock:
        procs = list(_procs)
    for p in procs:
        try:
            # kill the whole process group
            os.killpg(p.pid, signal.SIGTERM)
        except Exception:
            try:
                p.terminate()
            except Exception:
                pass
    # give children a short time to exit, then force kill if necessary
    time.sleep(0.2)
    with _procs_lock:
        for p in list(_procs):
            if p.poll() is None:
                try:
                    os.killpg(p.pid, signal.SIGKILL)
                except Exception:
                    try:
                        p.kill()
                    except Exception:
                        pass


def run_command(cmd, logfile_path, timeout=None, env=None):
    """Run cmd and stream output to logfile_path while registering
    the child so signal handlers can terminate it. Returns (returncode, elapsed_time).
    
    Return codes:
        -1: Terminated due to stop event (Ctrl+C)
        -2: Terminated due to timeout
        Other: Actual process return code
    """
    with open(logfile_path, 'wb') as lf:
        # Write the command at the beginning of the log file
        cmd_str = ' '.join(cmd) + '\n' + '='*80 + '\n'
        lf.write(cmd_str.encode('utf-8'))
        lf.flush()
        
        # Start the process in a new session so we can signal its process group
        # (works on Unix/Linux).
        proc = subprocess.Popen(cmd, stdout=lf, stderr=subprocess.STDOUT, preexec_fn=os.setsid, env=env)
        with _procs_lock:
            _procs.add(proc)
        start = time.time()
        try:
            # Wait in a loop so we can respond to the stop event and terminate
            # the process group promptly if requested.
            while True:
                try:
                    rc = proc.wait(timeout=0.1)
                    break
                except subprocess.TimeoutExpired:
                    if _stop_event.is_set():
                        try:
                            os.killpg(proc.pid, signal.SIGTERM)
                        except Exception:
                            try:
                                proc.terminate()
                            except Exception:
                                pass
                        return -1, time.time() - start
                    if timeout and (time.time() - start) > timeout:
                        try:
                            os.killpg(proc.pid, signal.SIGTERM)
                        except Exception:
                            try:
                                proc.terminate()
                            except Exception:
                                pass
                        return -2, time.time() - start
                    # otherwise keep waiting
            elapsed = time.time() - start
            return rc, elapsed
        finally:
            with _procs_lock:
                _procs.discard(proc)


def find_executable(candidate):
    # If candidate is absolute/relative path and exists, use it. Otherwise try which().
    if os.path.exists(candidate) and os.access(candidate, os.X_OK):
        return candidate
    p = which(candidate)
    return p


def calculate_num_seeds_for_config(method, num_instrs, target_time_seconds=60):
    """Calculate number of seeds to run for a config to achieve target runtime.
    
    Args:
        method: 'fuzzgpu', 'cascade', or 'divefuzz'
        num_instrs: Number of instructions in the config
        target_time_seconds: Target total runtime in seconds (default 60)
    
    Returns:
        Number of seeds to run
    """
    # Effective throughput (instructions/second) for each method
    throughputs = {
        'fuzzgpu': 1000,
        'cascade': 1000,
        'divefuzz': 100
    }
    
    throughput = throughputs.get(method, 1000)
    
    # Estimated time per seed = num_instrs / throughput
    time_per_seed = num_instrs / throughput
    
    # Number of seeds = target_time / time_per_seed
    num_seeds = max(3, int(target_time_seconds / time_per_seed))
    
    # Cap at reasonable limits
    num_seeds = min(num_seeds, 1000)  # Max 1000 seeds per config
    
    return num_seeds


def get_arch_config(build_dir):
    """Run get_arch_config executable and return a dict of config values."""
    exe_path = build_dir / 'get_arch_config'
    exe = find_executable(str(exe_path))
    if not exe:
        return None

    cmd = [exe]
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        return None
        
    config = {}
    for line in result.stdout.splitlines():
        if '=' in line:
            key, value = line.split('=', 1)
            stripped_value = value.strip()
            try:
                config[key.strip()] = int(stripped_value)
            except ValueError:
                config[key.strip()] = stripped_value
    return config


def run_emulator_and_check_stats(emu_cmd, emu_log, stat_json, gen_num_instrs, disable_dft, timeout=None):
    rc_emu, emu_time = run_command(emu_cmd, emu_log, timeout=timeout)
    
    emu_num_instrs = 0
    emu_ok = False
    
    # Check for timeout (-2) or stop event (-1)
    if rc_emu == -2:
        return False, emu_time, 0  # Timeout - mark as failed
    if rc_emu == -1:
        return False, emu_time, 0  # Stop event - mark as failed
    
    # Try to read stat_json first - the emulator may exit with non-zero code
    # but still have saved valid stats (e.g., when std::terminate is called after saving)
    try:
        with open(stat_json, 'r') as f:
            stat_data = json.load(f)
            emu_num_instrs = stat_data.get('num_executed_instr_trace', 0)
            if emu_num_instrs > 0:
                emu_ok = True
    except Exception:
        pass
    
    # If we got valid instruction count, consider it success regardless of return code
    # This handles cases where emulator saves stats then calls std::terminate
    if emu_ok:
        return True, emu_time, emu_num_instrs

    # If no valid stats and emulator returned error, it's a real failure
    if rc_emu != 0:
        return False, emu_time, 0

    if disable_dft:
        emu_num_instrs = gen_num_instrs # suppose all finished since we have no observation
        emu_ok = True
        
    return emu_ok, emu_time, emu_num_instrs


def main():
    script_dir = Path(__file__).resolve().parent
    fuzzgpu_dir = script_dir.parent

    parser = argparse.ArgumentParser(description='Measure fuzzing instruction throughput.')
    parser.add_argument('--method', type=str, choices=['fuzzgpu', 'divefuzz', 'cascade'], default=None,
                        help='Fuzzing method to test (default: scan all methods)')
    parser.add_argument('--arch', type=str, choices=['ventus', 'vortex'], default=None,
                        help='Target architecture (default: scan all)')
    parser.add_argument('--xlen', type=int, choices=[32, 64], default=None,
                        help='Target XLEN (default: scan all)')
    parser.add_argument('--num-seeds', type=int, default=None,
                        help='Number of seeds per configuration (overrides time-based mode)')
    parser.add_argument('--target-config-time', type=float, default=60.0,
                        help='Target runtime in seconds per configuration (default: 60s, min 3 successful runs)')
    parser.add_argument('--jobs', type=int, default=None,
                        help='(Deprecated) Number of parallel jobs - currently sequential execution is used')
    parser.add_argument('--gen-jobs', type=int, default=1,
                        help='(Deprecated) Currently sequential gen->emu execution is used')
    parser.add_argument('--emu-jobs', type=int, default=1,
                        help='(Deprecated) Currently sequential gen->emu execution is used')
    parser.add_argument('--out-dir', type=str, default='/tmp/fuzz_throughput',
                        help='Directory for generated files and logs')
    parser.add_argument('--summary-json', type=str, default=None,
                        help='Path to write JSON results')
    parser.add_argument('--cov-type', type=str, choices=['none', 'line', 'toggle'], default='none',
                        help='Coverage type for DUT build (default: none)')
    parser.add_argument('--config-mode', type=str, choices=['block_warp', 'max_instr'], default='max_instr',
                        help='Configuration mode (fuzzgpu only): block_warp (scan blocks/warps) or max_instr (scan max-num-instrs)')
    # DiveFuzz specific
    parser.add_argument('--divefuzz-dir', type=str, default=None,
                        help='Path to DiveFuzz directory (default: fuzzgpu/baseline/DiveFuzz)')
    parser.add_argument('--disable-eliminate', action='store_true', default=False,
                        help='Disable elimination optimization for divefuzz (enabled by default)')
    # Cascade specific
    parser.add_argument('--cascade-dir', type=str, default=None,
                        help='Path to Cascade directory (default: fuzzgpu/baseline/cascade-meta)')

    args = parser.parse_args()

    # install signal handlers so Ctrl+C / termination will kill children
    signal.signal(signal.SIGINT, _handle_signal)
    signal.signal(signal.SIGTERM, _handle_signal)

    # Determine which arch/xlen configurations to test
    if args.arch and args.xlen:
        arch_configs = [(args.arch, args.xlen)]
    elif args.arch:
        arch_configs = [(args.arch, 32), (args.arch, 64)]
    elif args.xlen:
        arch_configs = [('vortex', args.xlen), ('ventus', args.xlen)]
    else:
        # Default: scan all specified configurations
        arch_configs = [
            #('vortex', 32),
            ('vortex', 64),
            ('ventus', 32)
        ]

    # Determine which methods to test
    if args.method:
        methods_to_test = [args.method]
    else:
        methods_to_test = ['fuzzgpu', 'divefuzz', 'cascade']

    # Pre-validate and setup paths for divefuzz/cascade
    divefuzz_dir = None
    wrapper_script = None
    cascade_dir = None
    cascade_script = None
    
    if 'cascade' in methods_to_test:
        if args.cascade_dir:
            cascade_dir = Path(args.cascade_dir).resolve()
        else:
            cascade_dir = fuzzgpu_dir / 'baseline' / 'cascade-meta'
            
        if not cascade_dir.exists():
            print(f"{YELLOW}Warning: Cascade directory not found at {cascade_dir}, skipping cascade method{RESET}")
            methods_to_test = [m for m in methods_to_test if m != 'cascade']
        else:
            cascade_script = cascade_dir / 'fuzzer' / 'do_genmanyelfs.py'
            if not cascade_script.exists():
                print(f"{YELLOW}Warning: do_genmanyelfs.py not found at {cascade_script}, skipping cascade method{RESET}")
                methods_to_test = [m for m in methods_to_test if m != 'cascade']
    
    if 'divefuzz' in methods_to_test:
        if args.divefuzz_dir:
            divefuzz_dir = Path(args.divefuzz_dir).resolve()
        else:
            divefuzz_dir = fuzzgpu_dir / 'baseline' / 'DiveFuzz'
            
        if not divefuzz_dir.exists():
            print(f"{YELLOW}Warning: DiveFuzz directory not found at {divefuzz_dir}, skipping divefuzz method{RESET}")
            methods_to_test = [m for m in methods_to_test if m != 'divefuzz']
        else:
            wrapper_script = script_dir / 'divefuzz_wrapper.py'
            if not wrapper_script.exists():
                print(f"{YELLOW}Warning: divefuzz_wrapper.py not found at {wrapper_script}, skipping divefuzz method{RESET}")
                methods_to_test = [m for m in methods_to_test if m != 'divefuzz']
    
    if not methods_to_test:
        print(f"{RED}Error: No valid methods to test{RESET}")
        sys.exit(1)

    # Resolve gen_jobs and emu_jobs from args
    if args.jobs is not None:
        gen_jobs = args.jobs
        emu_jobs = args.jobs
    else:
        gen_jobs = args.gen_jobs
        emu_jobs = args.emu_jobs

    os.makedirs(args.out_dir, exist_ok=True)
    all_results = {}
    
    print(f"Starting throughput test")
    print(f"Methods to test: {methods_to_test}")
    if 'fuzzgpu' in methods_to_test:
        print(f"Configuration mode (fuzzgpu): {args.config_mode}")
    print(f"Architecture configurations: {arch_configs}")
    print(f"Execution: dual-pass (dft1 for instr count, dft0 for timing)")
    if args.num_seeds:
        print(f"Seeds per config: {args.num_seeds} (fixed count mode)")
    else:
        print(f"Target config time: {args.target_config_time}s (time-based mode, min 3 runs)")
    print(f"Execution mode: Sequential (gen -> emu per seed)")

    # Calculate total iterations for progress bar
    total_iterations = len(methods_to_test) * len(arch_configs)
    
    # Create progress bar for overall progress
    overall_pbar = tqdm(total=total_iterations, desc="Overall Progress", position=0, leave=True, 
                     bar_format='{l_bar}{bar}| {n_fmt}/{total_fmt} [{elapsed}<{remaining}]')
    
    for current_method in methods_to_test:
        if _stop_event.is_set():
            break
        
        num_instrs_configs = [(100,), (1000,), (10000,), (100000,), ] # (1000000,)]

        # Setup configurations based on current method
        if current_method == 'fuzzgpu':
            if args.config_mode == 'block_warp':
                configs = [(1, 1), (5, 2), (50, 2), (500, 2), (5000, 2)]
                config_label = 'Block/Warp configs (blocks, warps)'
            else: # max_instr
                num_genblocks = 4
                num_genwarps_per_block = 4
                configs = num_instrs_configs
                config_label = 'Instr configs (max-num-instrs)'
        elif current_method == 'cascade':
            configs = num_instrs_configs
            config_label = 'Instr configs (instr-number)'
        else: # divefuzz
            configs = num_instrs_configs
            config_label = 'Instr configs (instr-number)'
        
        tqdm.write(f"\n{GREEN}{'#'*60}{RESET}")
        tqdm.write(f"{GREEN}Method: {current_method.upper()}{RESET}")
        tqdm.write(f"{GREEN}{config_label}: {configs}{RESET}")
        tqdm.write(f"{GREEN}{'#'*60}{RESET}")
            
        for arch, xlen in arch_configs:
            if _stop_event.is_set():
                break
                
            fuzz_exe = None
            emu_exe_dft = None  # Emulator with dft enabled (for getting instr count)
            emu_exe_nodft = None  # Emulator without dft (for timing)
            if current_method == 'fuzzgpu':
                # Always use dft1 build for fuzz_gpu (generator doesn't depend on dft)
                # For emulator, we need both dft1 (for instr count) and dft0 (for timing)
                build_dir_dft1 = os.path.join(fuzzgpu_dir, f"build{xlen}-{arch}-cov{args.cov_type}-dft1")
                build_dir_dft0 = os.path.join(fuzzgpu_dir, f"build{xlen}-{arch}-cov{args.cov_type}-dft0")
                
                fuzz_exe_ = os.path.join(build_dir_dft1, 'fuzz_gpu')
                fuzz_exe = find_executable(fuzz_exe_)

                if not fuzz_exe:
                    tqdm.write(f"{RED}fuzz_gpu executable not found at '{fuzz_exe_}' for {arch}-{xlen}, skipping{RESET}")
                    overall_pbar.update(1)
                    continue
                
                # Get emulator paths for fuzzgpu method
                # dft1 emulator for getting instruction trace count
                emu_exe_dft_ = os.path.join(build_dir_dft1, 'emulator')
                emu_exe_dft = find_executable(emu_exe_dft_)
                if not emu_exe_dft:
                    tqdm.write(f"{RED}Emulator (dft1) not found at {emu_exe_dft_} for {arch}-{xlen}, skipping{RESET}")
                    overall_pbar.update(1)
                    continue
                
                # dft0 emulator for timing (faster, no difftest overhead)
                emu_exe_nodft_ = os.path.join(build_dir_dft0, 'emulator')
                emu_exe_nodft = find_executable(emu_exe_nodft_)
                if not emu_exe_nodft:
                    tqdm.write(f"{RED}Emulator (dft0) not found at {emu_exe_nodft_} for {arch}-{xlen}, skipping{RESET}")
                    overall_pbar.update(1)
                    continue
                
                build_info = build_dir_dft1
            elif current_method == 'cascade':
                build_info = str(cascade_dir)
            else:
                build_info = str(divefuzz_dir) # Just for info

            # Get arch_config for cascade/divefuzz methods
            arch_config = None
            if current_method in ('cascade', 'divefuzz'):
                # Always use dft1 build for arch config and emulator
                build_dir_dft1 = Path(fuzzgpu_dir) / f"build{xlen}-{arch}-cov{args.cov_type}-dft1"
                build_dir_dft0 = Path(fuzzgpu_dir) / f"build{xlen}-{arch}-cov{args.cov_type}-dft0"
                arch_config = get_arch_config(build_dir_dft1)
                if arch_config is None:
                    tqdm.write(f"{RED}Failed to get arch_config from {build_dir_dft1} for {arch}-{xlen}, skipping{RESET}")
                    overall_pbar.update(1)
                    continue
                
                # Get emulator paths - dft1 for instr count, dft0 for timing
                emu_exe_dft_ = build_dir_dft1 / 'emulator'
                emu_exe_dft = find_executable(str(emu_exe_dft_))
                if not emu_exe_dft:
                    tqdm.write(f"{RED}Emulator (dft1) not found at {emu_exe_dft_} for {arch}-{xlen}, skipping{RESET}")
                    overall_pbar.update(1)
                    continue
                
                emu_exe_nodft_ = build_dir_dft0 / 'emulator'
                emu_exe_nodft = find_executable(str(emu_exe_nodft_))
                if not emu_exe_nodft:
                    tqdm.write(f"{RED}Emulator (dft0) not found at {emu_exe_nodft_} for {arch}-{xlen}, skipping{RESET}")
                    overall_pbar.update(1)
                    continue
                
                # Determine num_warps_per_core based on arch
                if arch == 'ventus':
                    num_warps_per_core = 4  # Default for ventus, matching baseline.py's num_genwarps_per_block
                elif arch == 'vortex':
                    num_warps_per_core = arch_config['vortex_nr_warps_per_core']
                else:
                    num_warps_per_core = 4
                
                num_threads_per_warp = arch_config['nr_threads_per_warp']

            # Use method in key to distinguish different methods
            result_key = f"{current_method}_{arch}_xlen{xlen}"
            all_results[result_key] = {
                'method': current_method,
                'arch': arch,
                'xlen': xlen,
                'build_info': build_info,
                'tested_configs': configs,
                'configs': {}
            }
            
            overall_pbar.set_description(f"Testing {current_method}/{arch.upper()}/XLEN={xlen}")
            tqdm.write(f"\n{GREEN}{'='*60}{RESET}")
            tqdm.write(f"{GREEN}Testing {arch.upper()} XLEN={xlen} with {current_method}{RESET}")
            tqdm.write(f"{GREEN}{'='*60}{RESET}")

            # Create progress bar for configs
            config_pbar = tqdm(configs, desc=f"  {arch}-{xlen} Configs", position=1, leave=False,
                           bar_format='{l_bar}{bar}| {n_fmt}/{total_fmt}')
            
            for config in config_pbar:
                if _stop_event.is_set():
                    break
                
                # Record config start time
                config_start_time = time.time()
                
                # Prepare config details
                # Determine if we're in time-based mode or fixed seed count mode
                use_time_based_mode = args.num_seeds is None
                min_runs = 3  # Minimum number of successful runs
                target_config_time = args.target_config_time
                
                if current_method == 'fuzzgpu':
                    if args.config_mode == 'block_warp':
                        num_blocks, num_warps = config
                        config_desc = f"blocks={num_blocks} warps={num_warps}"
                        config_key = f"b{num_blocks}_w{num_warps}"
                        num_seeds = args.num_seeds if args.num_seeds is not None else 10000  # Large upper bound for time-based
                    else:
                        max_num_instr = config[0]
                        config_desc = f"max-num-instrs={max_num_instr}"
                        config_key = f"maxinstr{max_num_instr}"
                        num_seeds = args.num_seeds if args.num_seeds is not None else 10000  # Large upper bound for time-based
                else:
                    instr_num = config[0]
                    config_desc = f"instr-number={instr_num}"
                    config_key = f"inst{instr_num}"
                    num_seeds = args.num_seeds if args.num_seeds is not None else 10000  # Large upper bound for time-based
                
                config_pbar.set_description(f"  {arch}-{xlen} {config_desc}")
                if use_time_based_mode:
                    tqdm.write(f"\n{YELLOW}Running configuration: {arch}-xlen{xlen} {config_desc} (time-based: target={target_config_time}s, min_runs={min_runs}){RESET}")
                else:
                    tqdm.write(f"\n{YELLOW}Running configuration: {arch}-xlen{xlen} {config_desc} (num_seeds={num_seeds}){RESET}")
                
                config_results = []
                
                # GenResult: (seed, gen_ok, gen_time, gen_num_instrs, emu_context)
                # emu_context is a dict with info needed to run emulation, or None if gen failed
                
                def gen_worker(seed):
                    """Generation worker - only does generation, returns gen result + emu context"""
                    if _stop_event.is_set():
                        return (seed, False, 0.0, 0, None)

                    prefix = f"{current_method}_{arch}_{xlen}_{config_key}_seed{seed}"
                    
                    if current_method == 'fuzzgpu':
                        if args.config_mode == 'block_warp':
                            num_blocks, num_warps = config
                            fuzz_cmd = [fuzz_exe,
                                        '--seed', str(seed),
                                        '--num-genblocks', str(num_blocks),
                                        '--num-genwarps-per-block', str(num_warps)]
                        else:  # max_instr
                            max_num_instr = config[0]
                            fuzz_cmd = [fuzz_exe,
                                        '--seed', str(seed),
                                        '--num-genblocks', str(num_genblocks),
                                        '--num-genwarps-per-block', str(num_genwarps_per_block),
                                        '--max-num-instrs', str(max_num_instr)]
                        
                        instr = os.path.join(args.out_dir, f'{prefix}.instr.bin')
                        data = os.path.join(args.out_dir, f'{prefix}.data.bin')
                        aux = os.path.join(args.out_dir, f'{prefix}.aux.bin')
                        jsonf = os.path.join(args.out_dir, f'{prefix}.json')
                        stat_json = os.path.join(args.out_dir, f'{prefix}.stat.json')
                        fuzz_log = os.path.join(args.out_dir, f'{prefix}.fuzz.log')
                        emu_log = os.path.join(args.out_dir, f'{prefix}.emu.log')
                        
                        fuzz_cmd.extend(['--log-level', 'warn'])
                        fuzz_cmd.extend([instr, data, aux, jsonf])
                        
                        # Run fuzz_gpu to generate test case
                        rc_gen, gen_time = run_command(fuzz_cmd, fuzz_log)
                        
                        # If gen fails, discard the entire test result
                        if rc_gen != 0:
                            return (seed, False, gen_time, 0, None)
                        
                        # Read num_instrs and file_write_time_us from meta JSON for gen throughput
                        gen_num_instrs = 0
                        file_write_time_us = 0
                        try:
                            with open(jsonf, 'r') as f:
                                meta = json.load(f)
                                gen_num_instrs = meta.get('num_instrs', 0)
                                file_write_time_us = meta.get('file_write_time_us', 0)
                        except Exception:
                            return (seed, False, gen_time, 0, None, None)
                        
                        if gen_num_instrs == 0:
                            return (seed, False, gen_time, 0, None, None)
                        
                        # Build base emulator arguments for emu_worker
                        # The actual emulator executable will be chosen by emu_worker based on dft mode
                        emu_base_args = [
                            '--instr-bin', str(instr),
                            '--data-bin', str(data),
                            '--aux', str(aux),
                            '--meta-json', str(jsonf),
                            '--stat-jsonfile', str(stat_json)
                        ]
                        
                        emu_context = {
                            'emu_base_args': emu_base_args,
                            'emu_exe_dft': emu_exe_dft,
                            'emu_exe_nodft': emu_exe_nodft,
                            'emu_log': emu_log,
                            'stat_json': stat_json,
                            'file_write_time_us': file_write_time_us,
                        }
                        
                        return (seed, True, gen_time, gen_num_instrs, emu_context)

                    elif current_method == 'cascade':
                        instr_num = config[0]
                        # Create subdirs
                        out_dir = Path(args.out_dir) / f"{prefix}_out"
                        os.makedirs(out_dir, exist_ok=True)
                        gen_log = Path(args.out_dir) / f"{prefix}.log"

                        # For cascade, vortex design_name includes xlen (e.g., vortex32, vortex64)
                        design_name = f"{arch}{xlen}" if arch == 'vortex' else arch

                        cmd = [
                            sys.executable, str(cascade_script),
                            '1', # num_elfs
                            str(out_dir),
                            '--arch', design_name,
                            '--num-instrs', str(instr_num),
                            '--num-warps-per-core', str(num_warps_per_core),
                            '--num-threads-per-warp', str(num_threads_per_warp),
                            '--emulator-path', str(emu_exe_nodft),
                            '--seed-id', str(seed)
                        ]
                        
                        if arch == 'vortex':
                            cmd += [
                                '--vortex-max-num-barrier', str(arch_config['num_barriers']),
                                '--vortex-max-barrier-count', str(max(arch_config['vortex_nr_warps_per_core'], arch_config['vortex_nr_cores_per_cluster'])),
                                '--vortex-cores-per-clusters', str(arch_config['vortex_nr_cores_per_cluster'])
                            ]
                        
                        rc, gen_time = run_command(cmd, str(gen_log))

                        if rc != 0:
                            return (seed, False, gen_time, 0, None)

                        # Find generated instructions count
                        try:
                            elf_files = list(out_dir.glob('*.elf'))
                            if not elf_files:
                                return (seed, False, gen_time, 0, None)
                            
                            elf_name = elf_files[0].stem
                            numinstrs_file = out_dir / f'{elf_name}_numinstrs.txt'
                            elf_path = elf_files[0]
                            
                            if not numinstrs_file.exists():
                                return (seed, False, gen_time, 0, None)
                                 
                            with open(numinstrs_file, 'r') as f:
                                num_instrs_hex = f.read().strip()
                                gen_num_instrs = int(num_instrs_hex, 16)
                            
                            stat_json = out_dir / f'{elf_name}.stat.json'
                            emu_log = out_dir / f'{elf_name}.emu.log'
                            
                            emu_base_args = [
                                '--elf', str(elf_path),
                                '--elf-num-genblocks', '4',
                                '--elf-num-genwarps-per-block', '4',
                                '--elf-num-threads-per-warp', str(num_threads_per_warp),
                                '--stat-jsonfile', str(stat_json)
                            ]

                            emu_context = {
                                'emu_base_args': emu_base_args,
                                'emu_exe_dft': emu_exe_dft,
                                'emu_exe_nodft': emu_exe_nodft,
                                'emu_log': str(emu_log),
                                'stat_json': str(stat_json),
                            }

                            return (seed, True, gen_time, gen_num_instrs, emu_context)
                        except Exception:
                            return (seed, False, gen_time, 0, None)

                    else: # divefuzz
                        instr_num = config[0]
                        # Create subdirs
                        elf_dir = os.path.join(args.out_dir, f"{prefix}_elf")
                        out_dir = os.path.join(args.out_dir, f"{prefix}_out")
                        os.makedirs(elf_dir, exist_ok=True)
                        os.makedirs(out_dir, exist_ok=True)
                        
                        xor_cache = os.path.join(args.out_dir, f"{prefix}_xor_cache.json")
                        output_json = os.path.join(args.out_dir, f"{prefix}.json")
                        gen_log = os.path.join(args.out_dir, f"{prefix}.log")
                        
                        cmd = [
                            sys.executable, str(wrapper_script),
                            '--divefuzz-dir', str(divefuzz_dir),
                            '--seed-id', str(seed),
                            '--instr-number', str(instr_num),
                            '--arch', arch,
                            '--xlen', str(xlen),
                            '--out-dir', out_dir,
                            '--elf-dir', elf_dir,
                            '--xor-cache-file', xor_cache,
                            '--output-json', output_json,
                            '--num-warps-per-core', str(num_warps_per_core),
                            '--num-threads-per-warp', str(num_threads_per_warp)
                        ]
                        
                        if arch == 'vortex':
                            cmd += [
                                '--vortex-max-num-barrier', str(arch_config['num_barriers']),
                                '--vortex-max-barrier-count', str(max(arch_config['vortex_nr_warps_per_core'], arch_config['vortex_nr_cores_per_cluster'])),
                                '--vortex-cores-per-clusters', str(arch_config['vortex_nr_cores_per_cluster'])
                            ]
                        
                        if args.disable_eliminate:
                            cmd.append('--disable-eliminate')
                        
                        rc, gen_time = run_command(cmd, gen_log)
                        
                        if rc != 0:
                            return (seed, False, gen_time, 0, None)
                        
                        try:
                            with open(output_json, 'r') as f:
                                res = json.load(f)
                                if not res.get('success'): 
                                    return (seed, False, gen_time, 0, None)
                                gen_num_instrs = res.get('instr_count', 0)
                            
                            stat_json = os.path.join(out_dir, f"{prefix}.stat.json")
                            emu_log = os.path.join(out_dir, f"{prefix}.emu.log")

                            elf_files = list(Path(elf_dir).glob('*.elf'))
                            if not elf_files:
                                # Gen succeeded but no elf file - count as gen success but emu will fail
                                return (seed, True, gen_time, gen_num_instrs, None)
                            
                            elf_path = elf_files[0]

                            emu_base_args = [
                                '--elf', str(elf_path),
                                '--elf-num-genblocks', '4',
                                '--elf-num-genwarps-per-block', '4',
                                '--elf-num-threads-per-warp', str(num_threads_per_warp),
                                '--stat-jsonfile', str(stat_json)
                            ]
                            
                            emu_context = {
                                'emu_base_args': emu_base_args,
                                'emu_exe_dft': emu_exe_dft,
                                'emu_exe_nodft': emu_exe_nodft,
                                'emu_log': emu_log,
                                'stat_json': stat_json,
                            }

                            return (seed, True, gen_time, gen_num_instrs, emu_context)
                        except Exception:
                            return (seed, False, gen_time, 0, None)

                def emu_worker(gen_result):
                    """Emulation worker - takes gen result and runs emulation.
                    
                    Dual-pass execution:
                      1. First run with dft emulator to get num_executed_instr_trace (reliable)
                      2. Then run with non-dft emulator for timing measurement (faster)
                    """
                    seed, gen_ok, gen_time, gen_num_instrs, emu_context = gen_result
                    
                    # Initialize time breakdown values (in milliseconds)
                    file_write_time_ms = 0.0
                    program_construction_time_ms = gen_time * 1000.0
                    memory_check_time_ms = 0.0
                    rtl_emu_time_ms = 0.0
                    
                    if _stop_event.is_set():
                        return (seed, gen_ok, False, gen_time, 0.0, gen_num_instrs, 0,
                                file_write_time_ms, program_construction_time_ms, memory_check_time_ms, rtl_emu_time_ms)
                    
                    if not gen_ok or emu_context is None:
                        # No emu context - return gen result with emu failed
                        return (seed, gen_ok, False, gen_time, 0.0, gen_num_instrs, 0,
                                file_write_time_ms, program_construction_time_ms, memory_check_time_ms, rtl_emu_time_ms)
                    
                    emu_base_args = emu_context['emu_base_args']
                    emu_exe_dft = emu_context['emu_exe_dft']
                    emu_exe_nodft = emu_context['emu_exe_nodft']
                    emu_log = emu_context['emu_log']
                    stat_json = emu_context['stat_json']
                    
                    # Extract file_write_time_us from emu_context (fuzzgpu only)
                    file_write_time_us = emu_context.get('file_write_time_us', 0)
                    file_write_time_ms = file_write_time_us / 1000.0  # Convert us to ms
                    program_construction_time_ms = max(0, gen_time * 1000.0 - file_write_time_ms)
                    
                    emu_num_instrs = 0
                    emu_time = 0.0
                    emu_ok = False
                    
                    # Dual-pass execution:
                    # Step 1: Run with dft emulator to get num_executed_instr_trace (reliable)
                    # Step 2: Run with non-dft emulator for timing (faster)
                    
                    # Step 1: Run with dft emulator to get num_executed_instr_trace
                    emu_log_dft = emu_log + '.dft' if isinstance(emu_log, str) else str(emu_log) + '.dft'
                    stat_json_dft = stat_json + '.dft' if isinstance(stat_json, str) else str(stat_json) + '.dft'
                    emu_cmd_dft = [emu_exe_dft] + emu_base_args.copy()
                    # Replace stat_json path in args for dft run
                    for i, arg in enumerate(emu_cmd_dft):
                        if arg == '--stat-jsonfile' and i + 1 < len(emu_cmd_dft):
                            emu_cmd_dft[i + 1] = stat_json_dft
                            break
                    
                    dft_ok, dft_time, emu_num_instrs = run_emulator_and_check_stats(
                        emu_cmd_dft, emu_log_dft, stat_json_dft, gen_num_instrs, disable_dft=False,
                        timeout=EMULATOR_TIMEOUT_SECONDS)
                    
                    if not dft_ok or emu_num_instrs == 0:
                        # DFT run failed (timeout, stop event, non-zero return code, or no instruction trace)
                        return (seed, gen_ok, False, gen_time, dft_time, dft_time, gen_num_instrs, 0,
                                file_write_time_ms, program_construction_time_ms, memory_check_time_ms, rtl_emu_time_ms)
                    
                    if _stop_event.is_set():
                        return (seed, gen_ok, False, gen_time, dft_time, dft_time, gen_num_instrs, emu_num_instrs,
                                file_write_time_ms, program_construction_time_ms, memory_check_time_ms, rtl_emu_time_ms)
                    
                    # Step 2: Run with non-dft emulator for timing (use emu_num_instrs from dft run)
                    emu_cmd_nodft = [emu_exe_nodft] + emu_base_args
                    # For non-dft run, we only care about timing. We already have emu_num_instrs from dft run.
                    # The emulator may exit with non-zero code (e.g., std::terminate after saving stats)
                    # but as long as it didn't timeout or get stopped, we consider it successful.
                    rc_emu, nodft_time = run_command(emu_cmd_nodft, emu_log, timeout=EMULATOR_TIMEOUT_SECONDS)
                    
                    if rc_emu == -2:
                        emu_ok = False  # Timeout
                    elif rc_emu == -1:
                        emu_ok = False  # Stop event
                    else:
                        # For non-dft timing run, we don't care about return code
                        # as long as it completed (not timeout/stopped)
                        emu_ok = True
                    
                    # Extract memory checker timing stats from stat_json_dft
                    build_graph_time_us = 0
                    verification_time_us = 0
                    try:
                        with open(stat_json_dft, 'r') as f:
                            stat_data = json.load(f)
                            mem_checker_stats = stat_data.get('mem_checker_stats', {})
                            build_graph_time_us = mem_checker_stats.get('build_graph_time_us', 0)
                            verification_time_us = mem_checker_stats.get('verification_time_us', 0)
                    except Exception:
                        pass
                    
                    memory_check_time_ms = (build_graph_time_us + verification_time_us) / 1000.0  # Convert us to ms
                    rtl_emu_time_ms = max(0, nodft_time * 1000.0 - memory_check_time_ms)
                    
                    # Return dft_time and nodft_time separately for comparison
                    # Return signature: (seed, gen_ok, emu_ok, gen_time, dft_time, nodft_time, gen_num_instrs, emu_num_instrs, ...)
                    return (seed, gen_ok, emu_ok, gen_time, dft_time, nodft_time, gen_num_instrs, emu_num_instrs,
                            file_write_time_ms, program_construction_time_ms, memory_check_time_ms, rtl_emu_time_ms)

                # Sequential gen->emu execution per seed for real-time feedback
                # Track cumulative gen+emu time for time-based mode
                current_seed = 0
                success_count = 0
                cumulative_gen_emu_time = 0.0  # Total gen+emu time (for time-based termination)
                
                # Create progress bar for successful seeds
                if use_time_based_mode:
                    seed_pbar = tqdm(total=None, desc=f"    Seeds", position=2, leave=False,
                                    bar_format='{desc}: {n} runs | gen+emu time: {postfix}')
                else:
                    seed_pbar = tqdm(total=num_seeds, desc=f"    Seeds", position=2, leave=False,
                                    bar_format='{l_bar}{bar}| {n_fmt}/{total_fmt} [{rate_fmt}]')
                
                try:
                    # Determine termination condition based on mode
                    def should_continue():
                        if use_time_based_mode:
                            # Use cumulative gen+emu time instead of wall clock time
                            time_condition_met = cumulative_gen_emu_time >= target_config_time
                            min_runs_met = success_count >= min_runs
                            # Stop only when both conditions are met
                            return not (time_condition_met and min_runs_met)
                        else:
                            return success_count < num_seeds
                    
                    while should_continue() and not _stop_event.is_set():
                        seed = current_seed
                        current_seed += 1
                        
                        # Step 1: Generate test case
                        gen_result = gen_worker(seed)
                        seed_from_result, gen_ok, gen_time, gen_num_instrs, emu_context = gen_result
                        
                        if not gen_ok:
                            # Gen failed, log and continue with next seed
                            prefix = f"{current_method}_{arch}_{xlen}_{config_key}_seed{seed}"
                            if current_method == 'fuzzgpu':
                                log_path = os.path.join(args.out_dir, f'{prefix}.fuzz.log')
                            else:
                                log_path = os.path.join(args.out_dir, f'{prefix}.log')
                            tqdm.write(f"  {YELLOW}Seed {seed} gen failed, trying next seed. Log: {log_path}{RESET}")
                            continue
                        
                        # Step 2: Immediately emulate the generated test
                        emu_result = emu_worker(gen_result)
                        (seed, gen_ok, emu_ok, gen_time, dft_time, nodft_time, gen_num_instrs, emu_num_instrs,
                         file_write_time_ms, program_construction_time_ms, memory_check_time_ms, rtl_emu_time_ms) = emu_result
                        
                        # Use nodft_time for overall timing calculation (faster, without difftest)
                        emu_time = nodft_time
                        
                        # Accumulate gen+emu time regardless of success (for time tracking)
                        total_time = gen_time + emu_time
                        cumulative_gen_emu_time += total_time
                        
                        if not emu_ok:
                            # Emu failed, log and continue with next seed
                            prefix = f"{current_method}_{arch}_{xlen}_{config_key}_seed{seed}"
                            if current_method == 'fuzzgpu':
                                emu_log_path = os.path.join(args.out_dir, f'{prefix}.emu.log')
                            else:
                                emu_log_path = os.path.join(args.out_dir, f'{prefix}.log')
                            tqdm.write(f"  {RED}Seed {seed} emu failed, trying next seed. Log: {emu_log_path}{RESET}")
                            continue
                        
                        # Both gen and emu succeeded - record result
                        # gen_throughput = gen_num_instrs / gen_time (generated instructions per second)
                        gen_throughput = gen_num_instrs / gen_time if gen_time > 0 else 0
                        # emu_throughput = emu_num_instrs / emu_time (executed instructions per second)
                        emu_throughput = emu_num_instrs / emu_time if emu_time > 0 else 0
                        # dft_throughput = emu_num_instrs / dft_time (with difftest)
                        dft_throughput = emu_num_instrs / dft_time if dft_time > 0 else 0
                        # nodft_throughput = emu_num_instrs / nodft_time (without difftest)
                        nodft_throughput = emu_num_instrs / nodft_time if nodft_time > 0 else 0
                        # effective_throughput = gen_num_instrs / (gen_time + emu_time)
                        effective_throughput = gen_num_instrs / total_time if total_time > 0 else 0
                        
                        config_results.append({
                            'seed': seed,
                            'gen_ok': gen_ok,
                            'emu_ok': emu_ok,
                            'gen_time': gen_time,
                            'emu_time': emu_time,
                            'dft_time': dft_time,  # DFT (with difftest) timing for comparison
                            'nodft_time': nodft_time,  # Non-DFT (without difftest) timing for comparison
                            'total_time': total_time,
                            'gen_num_instrs': gen_num_instrs,
                            'emu_num_instrs': emu_num_instrs,
                            'gen_throughput': gen_throughput,
                            'emu_throughput': emu_throughput,
                            'dft_throughput': dft_throughput,
                            'nodft_throughput': nodft_throughput,
                            'effective_throughput': effective_throughput,
                            # Time breakdown fields (in milliseconds)
                            'file_write_time_ms': file_write_time_ms,
                            'program_construction_time_ms': program_construction_time_ms,
                            'memory_check_time_ms': memory_check_time_ms,
                            'rtl_emu_time_ms': rtl_emu_time_ms
                        })
                        success_count += 1
                        seed_pbar.update(1)
                        
                        if use_time_based_mode:
                            remaining = max(0, target_config_time - cumulative_gen_emu_time)
                            seed_pbar.set_postfix_str(f'{cumulative_gen_emu_time:.1f}s / {target_config_time:.0f}s (remaining: {remaining:.1f}s, eff: {effective_throughput:.0f} i/s)')
                        else:
                            seed_pbar.set_postfix({'eff': f'{effective_throughput:.0f} i/s'})
                        
                except KeyboardInterrupt:
                    _handle_signal(signal.SIGINT, None)
                finally:
                    seed_pbar.close()

                # Calculate stats for this config
                config_elapsed_time = time.time() - config_start_time
                
                if config_results:
                    # Filter valid gen results (gen_num_instrs > 0)
                    valid_gen_results = [r for r in config_results if r['gen_num_instrs'] is not None and r['gen_num_instrs'] > 0]
                    # Filter valid emu results (emu_ok and emu_num_instrs > 0)
                    valid_emu_results = [r for r in config_results if r['emu_ok'] and r['emu_num_instrs'] is not None and r['emu_num_instrs'] > 0]
                    
                    total_gen_instrs = sum(r['gen_num_instrs'] for r in valid_gen_results)
                    total_emu_instrs = sum(r['emu_num_instrs'] for r in valid_emu_results)
                    median_gen_instrs = statistics.median(r['gen_num_instrs'] for r in valid_gen_results) if valid_gen_results else 0
                    total_gen_time = sum(r['gen_time'] for r in valid_gen_results if r['gen_time'] is not None and r['gen_time'] > 0)
                    total_emu_time = sum(r['emu_time'] for r in valid_emu_results if r['emu_time'] is not None and r['emu_time'] > 0)
                    total_time = sum(r['total_time'] for r in valid_emu_results if r['total_time'] is not None and r['total_time'] > 0)
                    
                    # Time breakdown aggregation (in milliseconds)
                    total_file_write_time_ms = sum(r.get('file_write_time_ms', 0) for r in valid_gen_results)
                    total_program_construction_time_ms = sum(r.get('program_construction_time_ms', 0) for r in valid_gen_results)
                    total_memory_check_time_ms = sum(r.get('memory_check_time_ms', 0) for r in valid_emu_results)
                    total_rtl_emu_time_ms = sum(r.get('rtl_emu_time_ms', 0) for r in valid_emu_results)
                    
                    # Count successful gen and emu
                    gen_success_count = len(valid_gen_results)
                    emu_success_count = len(valid_emu_results)
                    
                    # Calculate throughput statistics for each type (skip None and <= 0 values)
                    gen_throughputs = [r['gen_throughput'] for r in config_results if r['gen_throughput'] is not None and r['gen_throughput'] > 0]
                    emu_throughputs = [r['emu_throughput'] for r in config_results if r['emu_throughput'] is not None and r['emu_throughput'] > 0]
                    dft_throughputs = [r['dft_throughput'] for r in config_results if r.get('dft_throughput') is not None and r['dft_throughput'] > 0]
                    nodft_throughputs = [r['nodft_throughput'] for r in config_results if r.get('nodft_throughput') is not None and r['nodft_throughput'] > 0]
                    effective_throughputs = [r['effective_throughput'] for r in config_results if r['effective_throughput'] is not None and r['effective_throughput'] > 0]
                    
                    mean_gen_throughput = statistics.geometric_mean(gen_throughputs) if gen_throughputs else 0
                    mean_emu_throughput = statistics.geometric_mean(emu_throughputs) if emu_throughputs else 0
                    mean_dft_throughput = statistics.geometric_mean(dft_throughputs) if dft_throughputs else 0
                    mean_nodft_throughput = statistics.geometric_mean(nodft_throughputs) if nodft_throughputs else 0
                    mean_effective_throughput = statistics.geometric_mean(effective_throughputs) if effective_throughputs else 0
                    
                    config_dict = {}
                    if current_method == 'fuzzgpu':
                        if args.config_mode == 'block_warp':
                            num_blocks, num_warps = config
                            config_dict = {'num_genblocks': num_blocks, 'num_genwarps_per_block': num_warps}
                        else:
                            config_dict = {'max_num_instrs': config[0],
                                           'num_genblocks': num_genblocks,
                                           'num_genwarps_per_block': num_genwarps_per_block}
                    else:
                        config_dict = {'instr_number': config[0]}
                    
                    tqdm.write(f"{GREEN}Summary for {current_method}/{arch}-xlen{xlen} {config_desc}:{RESET}")
                    tqdm.write(f"  Config Runtime: {config_elapsed_time:.2f}s")
                    tqdm.write(f"  Gen Success: {gen_success_count}/{num_seeds}, Emu Success: {emu_success_count}/{num_seeds}")
                    tqdm.write(f"  Total Gen Instrs: {total_gen_instrs}, Total Emu Instrs: {total_emu_instrs}")
                    tqdm.write(f"  Total Gen Time: {total_gen_time*1000:.3f}ms, Total Emu Time: {total_emu_time*1000:.3f}ms")
                    tqdm.write(f"    Gen Time Breakdown: Program Construction: {total_program_construction_time_ms:.3f}ms, File Write: {total_file_write_time_ms:.3f}ms")
                    tqdm.write(f"    Emu Time Breakdown: RTL Emulation: {total_rtl_emu_time_ms:.3f}ms, Memory Check: {total_memory_check_time_ms:.3f}ms")
                    tqdm.write(f"  Total Time (gen+emu for emu_ok): {total_time*1000:.3f}ms")
                    tqdm.write(f"  Median Gen Instrs: {median_gen_instrs}")
                    tqdm.write(f"  Mean Gen Throughput: {mean_gen_throughput:.2f} instrs/s ({len(gen_throughputs)} samples)")
                    tqdm.write(f"  Mean Emu Throughput: {mean_emu_throughput:.2f} instrs/s ({len(emu_throughputs)} samples)")
                    tqdm.write(f"    DFT (with difftest) Throughput: {mean_dft_throughput:.2f} instrs/s ({len(dft_throughputs)} samples)")
                    tqdm.write(f"    Non-DFT (timing only) Throughput: {mean_nodft_throughput:.2f} instrs/s ({len(nodft_throughputs)} samples)")
                    tqdm.write(f"  Mean Effective Throughput: {mean_effective_throughput:.2f} instrs/s ({len(effective_throughputs)} samples)")
                    
                    config_dict.update({
                        'config_elapsed_time': config_elapsed_time,
                        'num_seeds': num_seeds,
                        'gen_success_count': gen_success_count,
                        'emu_success_count': emu_success_count,
                        'samples': len(config_results),
                        'total_gen_instrs': total_gen_instrs,
                        'total_emu_instrs': total_emu_instrs,
                        'median_gen_instrs': median_gen_instrs,
                        'total_gen_time': total_gen_time,
                        'total_emu_time': total_emu_time,
                        'total_time': total_time,
                        # Time breakdown (in milliseconds)
                        'total_file_write_time_ms': total_file_write_time_ms,
                        'total_program_construction_time_ms': total_program_construction_time_ms,
                        'total_memory_check_time_ms': total_memory_check_time_ms,
                        'total_rtl_emu_time_ms': total_rtl_emu_time_ms,
                        'mean_gen_throughput': mean_gen_throughput,
                        'mean_emu_throughput': mean_emu_throughput,
                        'mean_dft_throughput': mean_dft_throughput,
                        'mean_nodft_throughput': mean_nodft_throughput,
                        'mean_effective_throughput': mean_effective_throughput,
                        'all_gen_num_instrs': [r['gen_num_instrs'] for r in config_results if r['gen_num_instrs'] is not None],
                        'all_emu_num_instrs': [r['emu_num_instrs'] for r in config_results if r['emu_num_instrs'] is not None],
                        'all_gen_times': [r['gen_time'] for r in config_results if r['gen_time'] is not None],
                        'all_emu_times': [r['emu_time'] for r in config_results if r['emu_time'] is not None],
                        'all_total_times': [r['total_time'] for r in config_results if r['total_time'] is not None],
                        # Time breakdown arrays (in milliseconds)
                        'all_file_write_times_ms': [r.get('file_write_time_ms', 0) for r in config_results],
                        'all_program_construction_times_ms': [r.get('program_construction_time_ms', 0) for r in config_results],
                        'all_memory_check_times_ms': [r.get('memory_check_time_ms', 0) for r in config_results],
                        'all_rtl_emu_times_ms': [r.get('rtl_emu_time_ms', 0) for r in config_results],
                        'details': config_results
                    })
                    all_results[result_key]['configs'][config_key] = config_dict
                else:
                    tqdm.write(f"{RED}Config {current_method}/{arch}-xlen{xlen} {config_desc} failed: No successful runs (elapsed: {config_elapsed_time:.2f}s){RESET}")
                    all_results[result_key]['configs'][config_key] = {'error': 'No successful runs', 'config_elapsed_time': config_elapsed_time}
            
            config_pbar.close()
            overall_pbar.update(1)
    
    overall_pbar.close()

    # Write final JSON to results directory (separate from intermediate files)
    results_dir = fuzzgpu_dir / 'results' / 'fuzz_instr_throughput'
    results_dir.mkdir(parents=True, exist_ok=True)
    
    if args.summary_json:
        summary_json_path = Path(args.summary_json)
    else:
        ts = datetime.datetime.now().strftime('%Y%m%dT%H%M%S')
        method_suffix = args.method if args.method else 'all'
        summary_json_path = results_dir / f"throughput_results_{method_suffix}_{ts}.json"
        
    try:
        with open(summary_json_path, 'w') as f:
            json.dump(all_results, f, indent=2)
        print(f"\n{GREEN}Wrote results to {summary_json_path}{RESET}")
        
        # Print summary comparison
        print(f"\n{GREEN}{'='*60}{RESET}")
        print(f"{GREEN}Summary Comparison:{RESET}")
        print(f"{GREEN}{'='*60}{RESET}")
        for result_key, result_data in all_results.items():
            if 'arch' in result_data:
                print(f"\n{YELLOW}{result_data['method'].upper()} / {result_data['arch'].upper()} / XLEN={result_data['xlen']}:{RESET}")
                for cfg_key, cfg_data in result_data['configs'].items():
                    if 'mean_effective_throughput' in cfg_data:
                        gen_tp = cfg_data.get('mean_gen_throughput', 0)
                        emu_tp = cfg_data.get('mean_emu_throughput', 0)
                        eff_tp = cfg_data.get('mean_effective_throughput', 0)
                        total_instrs = cfg_data.get('total_emu_instrs', cfg_data.get('total_gen_instrs', 0))
                        print(f"  {cfg_key:12s}: gen={gen_tp:10.2f}, emu={emu_tp:10.2f}, eff={eff_tp:10.2f} instrs/s ({cfg_data['samples']} samples, {total_instrs} total instrs)")
                    else:
                        print(f"  {cfg_key:12s}: Failed")
    except Exception as e:
        print(f"{RED}Failed to write JSON results: {e}{RESET}")

def _cleanup_gen_emu_processes():
    """Kill any existing instances of simulator/fuzzer processes."""
    targets = ['fuzz_gpu', 'emulator', 'verilator_coverage']
    found = False
    
    for t in targets:
        try:
            # Use regex to match process name allowing for path prefix
            # (^|/) matches start of command or after a path separator
            # ($| ) matches end of command or space separator (args)
            pattern = f"(^|/){t}($| )"
            if subprocess.run(['pgrep', '-f', pattern], stdout=subprocess.DEVNULL).returncode == 0:
                found = True
                break
        except Exception:
            pass
            
    if found:
        for t in targets:
            print(f"{YELLOW}Cleaning up gen/emulator process {t}...{RESET}")
            pattern = f"(^|/){t}($| )"
            subprocess.run(['pkill', '-9', '-f', pattern], stderr=subprocess.DEVNULL, stdout=subprocess.DEVNULL)
            
        start = time.time()
        while time.time() - start < 10:
            still_running = False
            for t in targets:
                pattern = f"(^|/){t}($| )"
                if subprocess.run(['pgrep', '-f', pattern], stdout=subprocess.DEVNULL).returncode == 0:
                    still_running = True
                    break
            if not still_running:
                break
            time.sleep(0.2)
        print(f"{GREEN}Cleanup finished{RESET}")

if __name__ == '__main__':
    _cleanup_gen_emu_processes()
    main()
