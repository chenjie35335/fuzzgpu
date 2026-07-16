#!/usr/bin/env python3
"""
Run baseline analysis:
1. Generate ELFs using do_genmanyelfs.py
2. Run emulator on generated ELFs
"""

import argparse
import os
import signal
import subprocess
import sys
import re
import threading
import time
import json
import random
from shutil import which
from pathlib import Path
from concurrent.futures import FIRST_COMPLETED, ThreadPoolExecutor, as_completed, wait
import math

EMULATOR_TIMEOUT_SECONDS = 5
# Grace window after SIGINT before we escalate to SIGKILL when timing out
SIGINT_GRACE_SECONDS = 1.0
LOG_ANALYSIS_MAX_BYTES = 80 * 1024 * 1024  # Guardrails for log triage

GREEN = "\033[32m"
RED = "\033[31m"
YELLOW = "\033[33m"
RESET = "\033[0m"

# Shared state for running child processes so we can terminate them on Ctrl+C.
_procs_lock = threading.Lock()
_procs = set()
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
            os.killpg(p.pid, signal.SIGTERM)
        except Exception:
            try:
                p.terminate()
            except Exception:
                pass
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

def run_command(cmd, logfile_path=None, env=None, echo=False, timeout=None):
    """Run `cmd`. If logfile_path is provided, stream output there.
    Returns (returncode, elapsed_time).
    """
    stdout_dest = subprocess.PIPE
    stderr_dest = subprocess.STDOUT
    f = None
    
    if logfile_path:
        f = open(logfile_path, 'wb')
        # Write command line at the beginning of log file
        cmd_line = ' '.join(cmd) + '\n'
        f.write(cmd_line.encode('utf-8'))
        f.write(b'# ' + b'=' * 78 + b'\n')
        f.flush()
        if not echo:
            stdout_dest = f
    
    try:
        # Start process in new session for signal handling
        # stderr is merged into stdout so we can capture/echo both
        proc = subprocess.Popen(cmd, stdout=stdout_dest, stderr=stderr_dest, 
                                preexec_fn=os.setsid, env=env)
        with _procs_lock:
            _procs.add(proc)
        start = time.time()
        
        if echo:
            while True:
                char = proc.stdout.read(1)
                if not char:
                    break
                sys.stdout.buffer.write(char)
                sys.stdout.flush()
                if f:
                    f.write(char)
                    f.flush()
            proc.wait()
            rc = proc.returncode
        else:
            sent_sigint = False
            sigint_sent_at = None
            sent_sigkill = False
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
                    
                    # handle the timeout
                    now = time.time()
                    if timeout and (now - start > timeout):
                        if not sent_sigint:
                            print(f"{YELLOW}Process timeout reached, sending SIGINT...{RESET}")
                            try:
                                os.killpg(proc.pid, signal.SIGINT)
                            except Exception:
                                pass
                            sent_sigint = True
                            sigint_sent_at = now
                        elif not sent_sigkill and sigint_sent_at and (now - sigint_sent_at > SIGINT_GRACE_SECONDS):
                            print(f"{YELLOW}Process still running after SIGINT, sending SIGKILL...{RESET}")
                            try:
                                os.killpg(proc.pid, signal.SIGKILL)
                            except Exception:
                                try:
                                    proc.kill()
                                except Exception:
                                    pass
                            sent_sigkill = True
        
        elapsed = time.time() - start
        return rc, elapsed
    finally:
        with _procs_lock:
            _procs.discard(proc)
        if f:
            f.close()

def find_executable(candidate):
    if os.path.exists(candidate) and os.access(candidate, os.X_OK):
        return candidate
    return which(candidate)

def crash_triage(log_path):
    """
    Analyze emulator log to determine crash reason based on rules.
    """
    if _stop_event.is_set():
        return "interrupted"

    if not os.path.exists(log_path):
        return "log_not_found"
        
    try:
        if os.path.getsize(log_path) > LOG_ANALYSIS_MAX_BYTES:
            return "deadloop"

        warp_last_instr = {} # { wid: mnemonic }
        last_active_wid = None
        
        # Regex setup
        instr_pattern = re.compile(r".*\[info\]\s+Disasm:\s+(.+?)\s+wid=(\d+)")
        wid_pattern = re.compile(r"(?:Warp GID|wid=)\s*(\d+)")
        
        has_divergent_branch_msg = False
        has_ipdom_stack_underflow = False
        possible_deadlock = False
        wmma_invalid_tmask = False
        
        # Read file line by line
        with open(log_path, 'r', errors='replace') as f:
            for line in f:
                if _stop_event.is_set():
                    return "interrupted"

                # Rule 2 check
                if "divergent branch! PC=0x" in line:
                    has_divergent_branch_msg = True
                
                if "IPDOM stack is empty!" in line:
                    has_ipdom_stack_underflow = True
                
                # Rule 1 check (part 1: detect deadlock msg)
                if "No schedulable warps found" in line and "Possible deadlock" in line:
                    possible_deadlock = True
                
                # WMMA requires tmask
                if "Assertion `warp.tmask.count() == num_threads' failed." in line:
                    wmma_invalid_tmask = True

                # Track active warp
                m_wid = wid_pattern.search(line)
                if m_wid:
                    last_active_wid = int(m_wid.group(1))
                
                # Track instructions
                m_instr = instr_pattern.search(line)
                if m_instr:
                    instr_str = m_instr.group(1).strip()
                    wid = int(m_instr.group(2))
                    warp_last_instr[wid] = instr_str
                    last_active_wid = wid

        # Apply Rules
        
        # Rule 2
        if has_divergent_branch_msg:
            return "thread-divergent branch"

        if has_ipdom_stack_underflow:
            return "SIMT stack underflow"
        
        if wmma_invalid_tmask:
            return "WMMA invalid tmask"
            
        # Rule 1
        if possible_deadlock:
            # Check if any warp's last instruction is BAR
            for instr in warp_last_instr.values():
                if "vx.bar" in instr:
                    return "Barrier Dead lock"
                if "vx.wspawn" in instr:
                    return "Wspawn Dead lock"
                    
        # Rule 3
        if last_active_wid is not None and last_active_wid in warp_last_instr:
            last_instr = warp_last_instr[last_active_wid]
            mnemonic = last_instr.split()[0].upper()
            #print(last_instr)
            if mnemonic in ['BEQ', 'BNE', 'BLT', 'BGE', 'BLTU', 'BGEU',
                            'BEQZ', 'BNEZ', 'BLTZ', 'BGEZ', 'BLTUZ', 'BGEUZ',
                            'JALR', 'JAL', 'JR', 'CALL', 'RET', 'VX.WSPAWN']:
                return "mis-steered branch"
            else:
                print(f'Unknown last insn {mnemonic}')
        # Rule 4
        return "others"

    except Exception as e:
        return f"analysis_error: {str(e)}"

def triage_failures(failures, results_dir, jobs, write_log=True):
    if not failures:
        print(f"{YELLOW}No failure logs to triage{RESET}")
        return

    print(f"\n{GREEN}Triaging failures...{RESET}")
    triage_results = {}
    executor = ThreadPoolExecutor(max_workers=jobs)
    futures = {}
    try:
        futures = {
            executor.submit(crash_triage, log_path): display_id
            for display_id, rc, log_path in failures
        }
        pending = set(futures)
        while pending:
            if _stop_event.is_set():
                raise KeyboardInterrupt
            done, pending = wait(pending, timeout=0.2,
                                 return_when=FIRST_COMPLETED)
            for future in done:
                display_id = futures[future]
                triage_results[display_id] = future.result()
    except KeyboardInterrupt:
        _stop_event.set()
        for future in futures:
            future.cancel()
        executor.shutdown(wait=False, cancel_futures=True)
        print(f"{YELLOW}Interrupted failure triage{RESET}")
        return
    finally:
        if not _stop_event.is_set():
            executor.shutdown(wait=True)

    if write_log:
        triage_log_path = os.path.join(results_dir, 'triage.log')
        with open(triage_log_path, 'w') as f:
            for display_id, rc, log_path in failures:
                reason = triage_results.get(display_id, "interrupted")
                f.write(f"Seed {display_id}: {reason}\n")
                print(f"  Seed {display_id}: {reason}")
        print(f"Triage results saved to {triage_log_path}")
    else:
        for display_id, rc, log_path in failures:
            reason = triage_results.get(display_id, "interrupted")
            print(f"  Seed {display_id}: {reason}")

def get_arch_config(build_dir):
    """
    Run get_arch_config executable and return a dict of config values.
    """
    exe_path = build_dir / 'get_arch_config'
    exe = find_executable(str(exe_path))
    if not exe:
        return {}

    try:
        cmd = [exe]
        result = subprocess.run(cmd, capture_output=True, text=True)
        if result.returncode != 0:
             return {}
             
        config = {}
        for line in result.stdout.splitlines():
            if '=' in line:
                key, value = line.split('=', 1)
                try:
                    config[key.strip()] = int(value.strip())
                except ValueError:
                    pass
        return config
    except Exception:
        return {}

def main():
    script_dir = Path(__file__).resolve().parent
    fuzzgpu_dir = script_dir.parent
    
    parser = argparse.ArgumentParser(description='Run motivation analysis.')
    parser.add_argument('--jobs', type=int, default=30, help='Number of parallel jobs')
    parser.add_argument('--num-seeds', type=int, default=10, help='Number of seeds/ELFs to generate')
    parser.add_argument('--out-log-dir', type=str, default=None, help='Directory for logs')
    parser.add_argument('--out-elf-dir', type=str, default=None, help='Directory for output ELFs')
    parser.add_argument('--arch', type=str, choices=['ventus', 'vortex'], default='vortex', help='Target architecture')
    parser.add_argument('--xlen', type=int, choices=[32, 64], default=32, help='Target XLEN')
    parser.add_argument('--method', type=str, choices=['cascade', 'divefuzz', 'fuzzgpu'], default='cascade', help='Fuzzing method')
    parser.add_argument('--mutation-rounds', type=int, default=1, help='Number of mutation rounds per seed (divefuzz only)')
    parser.add_argument('--disable-eliminate', action='store_true', default=False,
                        help='Disable elimination optimization for divefuzz (enabled by default)')
    
    # fuzzgpu specific arguments
    parser.add_argument('--max-instrs-per-bb', type=int, default=500, help='Max instructions per basic block (fuzzgpu)')
    parser.add_argument('--min-instrs-per-bb', type=int, default=1, help='Min instructions per basic block (fuzzgpu)')
    parser.add_argument('--force-max-num-instrs', type=int, default=None, help='Force max number of instructions (fuzzgpu)')
    parser.add_argument('--emulate', action='store_true', help='Enable emulation mode for emulator')
    
    args = parser.parse_args()
    
    # Setup paths
    # do_genmanyelfs.py path
    gen_script = os.path.join( fuzzgpu_dir , 'baseline/cascade-meta/fuzzer/do_genmanyelfs.py')
    if args.method == 'cascade' and not os.path.exists(gen_script):
        print(f"{RED}Error: {gen_script} not found{RESET}")
        sys.exit(1)

    # divefuzz paths
    divefuzz_wrapper = os.path.join(script_dir, 'divefuzz_wrapper.py')
    divefuzz_dir = os.path.join(fuzzgpu_dir, 'baseline', 'DiveFuzz')
    if args.method == 'divefuzz' and not os.path.exists(divefuzz_dir):
         print(f"{RED}Error: {divefuzz_dir} not found{RESET}")
         sys.exit(1)
        
    # Emulator path
    build_dir = fuzzgpu_dir / f"build{args.xlen}-{args.arch}-covnone-dft{0 if args.emulate else 1}"
    # Vortex crash triage needs assertions enabled, so use the debug build.
    if args.arch == 'vortex':
        build_dir = Path(f"{build_dir}-debug")
    
    # fuzzgpu executable path
    fuzz_exe_path = build_dir / 'fuzz_gpu'
    fuzz_exe = find_executable(str(fuzz_exe_path))
    if args.method == 'fuzzgpu' and not fuzz_exe:
        print(f"{RED}fuzz_gpu executable not found at '{fuzz_exe_path}'{RESET}")
        sys.exit(2)

    if args.out_log_dir is None:
        args.out_log_dir = os.path.join('/tmp', f'instr_complete_rate_{args.method}_logs_{args.arch}_{args.xlen}')
    if args.out_elf_dir is None:
        args.out_elf_dir = os.path.join('/tmp', f'{args.method}_elfs_{args.arch}_{args.xlen}')
    
    # Results directory for key final results (coverage.json, triage.log)
    # Intermediate files (logs, elfs) stay in /tmp
    results_base_dir = fuzzgpu_dir / 'results' / 'program_metric' / f'{args.method}_{args.arch}_{args.xlen}'

    # Get Arch Config
    arch_config = get_arch_config(build_dir)
    print(f"{GREEN}Arch Config:{RESET}")
    for k, v in arch_config.items():
        print(f"  {k}: {v}")
    
    emu_exe_path = build_dir / 'emulator'
    emu_exe = find_executable(str(emu_exe_path))
    if not emu_exe:
        print(f"{RED}emulator executable not found at '{emu_exe_path}'{RESET}")
        sys.exit(2)
        
    # Create log dir and results dir
    os.makedirs(args.out_log_dir, exist_ok=True)
    os.makedirs(args.out_elf_dir, exist_ok=True)
    os.makedirs(results_base_dir, exist_ok=True)
    
    print(f"{GREEN}Output directories:{RESET}")
    print(f"  Log dir: {args.out_log_dir}")
    print(f"  ELF dir: {args.out_elf_dir}")
    print(f"  Results dir: {results_base_dir}")
    
    # Install signal handlers
    signal.signal(signal.SIGINT, _handle_signal)
    signal.signal(signal.SIGTERM, _handle_signal)
    
    # Step 1: Generation
    print(f"{GREEN}Starting generation with {args.num_seeds} seeds...{RESET}")
    gen_log = os.path.join(args.out_log_dir, 'generation.log')
    
    # Command: CASCADE_JOBS={args.jobs} python do_genmanyelfs.py {args.num_seeds} {elf_out_dir}
    gen_env = os.environ.copy()
    gen_env['CASCADE_JOBS'] = str(args.jobs)
    
    if args.arch == 'vortex':
        max_threads_per_warp = int(arch_config.get('nr_threads_per_warp'))
        vortex_nr_warps_per_core = int(arch_config.get('vortex_nr_warps_per_core'))
        vortex_nr_total_warps = int(arch_config.get('vortex_nr_total_warps'))

        # Config name                       | (cur_num_threads_per_warp, execute_warps)
        #    single                         | (1,1)
        #    single-warp-multiple-thread    | (1, vortex_nr_total_warps)
        #    multi-warp-multiple-thread     | (max_threads_per_warp, vortex_nr_total_warps)
        configs = []
        if args.method != 'fuzzgpu': # FuzzGPU is naturally multi-threaded
            configs.append(("single", 1, 1))
        configs.extend([
            #("single-warp-multiple-thread", 1, vortex_nr_total_warps),
            ("multi-warp-multiple-thread", max_threads_per_warp, vortex_nr_total_warps)
        ])
    elif args.arch == 'ventus':
        max_threads_per_warp = int(arch_config.get('nr_threads_per_warp'))
        if args.method != 'fuzzgpu': # FuzzGPU is naturally multi-threaded
            configs.append(("single", 1, 1))
        configs = [
            ("multi-warp-multiple-thread", max_threads_per_warp, 4) # 8 allowed warps, but having writeback bugs in some cases
        ]
    else:
        print(f"{RED}Unsupported architecture: {args.arch}{RESET}")
        sys.exit(3)

    if args.arch == 'vortex':
        num_genblocks = 1 # vortex does not support CTA scheduler yet, so this is just a mathical convenience (1 * conf_warps = conf_warps)
    elif args.arch == 'ventus':
        num_genblocks = 2 # ventus can handle multiple blocks via CTA scheduler

    original_out_log_dir = args.out_log_dir
    original_elf_out_dir = Path(args.out_elf_dir)
    original_results_base_dir = results_base_dir

    for conf_name, conf_threads, conf_warps in configs:
        print(f"{GREEN}Running config: {conf_name}{RESET}")
        
        # Update paths for this config
        out_log_dir = os.path.join(original_out_log_dir, conf_name)
        elf_out_dir = original_elf_out_dir / conf_name
        results_dir = original_results_base_dir / conf_name
        
        os.makedirs(out_log_dir, exist_ok=True)
        os.makedirs(elf_out_dir, exist_ok=True)
        os.makedirs(results_dir, exist_ok=True)

        cur_num_threads_per_warp = conf_threads
        execute_warps = conf_warps
        cur_num_warps_per_core = conf_warps # Assuming single core focus for these tests

        warps_limit = []
        if args.arch == 'vortex':
            for warp_id in range(execute_warps):
                core_gid = warp_id // cur_num_warps_per_core
                warp_gid = (warp_id % cur_num_warps_per_core) + core_gid * vortex_nr_warps_per_core
                warps_limit.append(str(warp_gid))
            print(warps_limit)

        if args.method == 'cascade':
            # For cascade, vortex design_name includes xlen (e.g., vortex32, vortex64)
            design_name = f"{args.arch}{args.xlen}" if args.arch == 'vortex' else args.arch

            gen_cmd = [sys.executable, str(gen_script), str(args.num_seeds), str(elf_out_dir),
                       '--num-warps-per-core', str(cur_num_warps_per_core),
                       '--num-threads-per-warp', str(cur_num_threads_per_warp),
                       '--arch', design_name,
                       '--emulator-path', str(emu_exe),
                       '--seed-id', '0']
            if args.arch == 'vortex':
                gen_cmd += [
                       '--vortex-max-num-barrier', str(arch_config.get('num_barriers')),
                       '--vortex-max-barrier-count', str(max(arch_config.get('vortex_nr_warps_per_core'), arch_config.get('vortex_nr_cores_per_cluster'))),
                       '--vortex-cores-per-clusters', str(arch_config.get('vortex_nr_cores_per_cluster'))]
            
            with open(os.path.join(out_log_dir, 'generation_cmd.txt'), 'w') as f:
                f.write(' '.join(gen_cmd))
            
            print(f"Running: {' '.join(gen_cmd)}")
            gen_log = os.path.join(out_log_dir, 'generation.log')
            rc, elapsed = run_command(gen_cmd, logfile_path=gen_log, env=gen_env, echo=True)
            
            if rc != 0:
                print(f"{RED}Generation failed with return code {rc}. See {gen_log}{RESET}")
                continue
            
            print(f"{GREEN}Generation successful in {elapsed:.2f}s{RESET}")

        elif args.method == 'divefuzz':
            print(f"{GREEN}Starting DiveFuzz generation for {args.num_seeds} seeds...{RESET}")
            xor_cache_file = os.path.join(out_log_dir, 'xor_cache.json')
            mutate_dir = elf_out_dir / "mutants"
            os.makedirs(mutate_dir, exist_ok=True)
            
            def run_divefuzz_gen(seed_id):
                output_json = os.path.join(out_log_dir, f'gen_{seed_id}.json')
                
                cmd = [sys.executable, str(divefuzz_wrapper),
                       '--divefuzz-dir', str(divefuzz_dir),
                       '--seed-id', str(seed_id),
                       '--arch', args.arch,
                       '--xlen', str(args.xlen),
                       '--out-dir', str(elf_out_dir),
                       '--elf-dir', str(elf_out_dir),
                       '--xor-cache-file', str(xor_cache_file),
                       '--output-json', str(output_json),
                       '--num-warps-per-core', str(cur_num_warps_per_core),
                       '--num-threads-per-warp', str(cur_num_threads_per_warp),
                       '--instr-number', '200']

                if args.arch == 'vortex':
                    cmd += ['--vortex-max-num-barrier', str(arch_config.get('num_barriers')),
                            '--vortex-max-barrier-count', str(max(arch_config.get('vortex_nr_warps_per_core'), arch_config.get('vortex_nr_cores_per_cluster'))),
                            '--vortex-cores-per-clusters', str(arch_config.get('vortex_nr_cores_per_cluster'))]
                
                if args.disable_eliminate:
                    cmd.append('--disable-eliminate')
                
                log_file = os.path.join(out_log_dir, f'gen_{seed_id}.log')
                
                # Save command
                with open(os.path.join(out_log_dir, f'gen_{seed_id}_cmd.txt'), 'w') as f:
                    f.write(' '.join(cmd))
                    
                rc, elapsed = run_command(cmd, logfile_path=log_file)
                
                if rc == 0:
                    # To reuse the emulator logic, we rename elf and create numinstrs file
                    src_elf = elf_out_dir / f'seeds_{seed_id}_.elf'
                    dst_elf = elf_out_dir / f"{args.arch}_{seed_id}.elf"
                    src_asm = elf_out_dir / f'seeds_{seed_id}_.S'
                    dst_asm = elf_out_dir / f"{args.arch}_{seed_id}.S"
                    try:
                        if src_elf.exists():
                            if dst_elf.exists():
                                dst_elf.unlink()
                            src_elf.rename(dst_elf)
                            if src_asm.exists():
                                src_asm.rename(dst_asm)
                            
                            if os.path.exists(output_json):
                                with open(output_json, 'r') as f:
                                    res = json.load(f)
                                    with open(elf_out_dir / f"{args.arch}_{seed_id}_numinstrs.txt", 'w') as nf:
                                        nf.write(str(res.get('instr_count', 0)))
                            
                            # Perform mutation rounds if requested
                            current_asm = dst_asm
                            for r in range(1, args.mutation_rounds + 1):
                                mut_output_json = os.path.join(out_log_dir, f'gen_{seed_id}_mut{r}.json')
                                mut_cmd = [sys.executable, str(divefuzz_wrapper),
                                           '--divefuzz-dir', str(divefuzz_dir),
                                           '--seed-id', str(seed_id),
                                           '--arch', args.arch,
                                           '--xlen', str(args.xlen),
                                           '--out-dir', str(elf_out_dir), # Not used in mutate mode but required arg
                                           '--elf-dir', str(elf_out_dir), # Not used in mutate mode but required arg
                                           '--xor-cache-file', str(xor_cache_file),
                                           '--output-json', str(mut_output_json),
                                           '--num-warps-per-core', str(cur_num_warps_per_core),
                                           '--num-threads-per-warp', str(cur_num_threads_per_warp),
                                           '--mutate', 
                                           '--input-asm', str(current_asm),
                                           '--mutate-dir', str(mutate_dir),
                                           '--mutate-elf-dir', str(elf_out_dir),
                                           '--mutation-idx', str(r)]
                                
                                if args.arch == 'vortex':
                                    mut_cmd += ['--vortex-max-num-barrier', str(arch_config.get('num_barriers')),
                                            '--vortex-max-barrier-count', str(max(arch_config.get('vortex_nr_warps_per_core'), arch_config.get('vortex_nr_cores_per_cluster'))),
                                            '--vortex-cores-per-clusters', str(arch_config.get('vortex_nr_cores_per_cluster'))]
                                
                                if args.disable_eliminate:
                                    mut_cmd.append('--disable-eliminate')
                                
                                mut_log_file = os.path.join(out_log_dir, f'gen_{seed_id}_mut{r}.log')
                                
                                rc_mut, _ = run_command(mut_cmd, logfile_path=mut_log_file)
                                if rc_mut == 0:
                                    # divefuzz_wrapper writes mutants to --mutate-elf-dir
                                    # Named via {basename}mut_{idx}_.S -> {basename}mut_{idx}_.elf if compiled
                                    # But basename of dst_asm is `{arch}_{seed_id}`.
                                    # So wrapper output is `{arch}_{seed_id}mut_{r}_.elf`
                                    
                                    # Read result to get exact path
                                    if os.path.exists(mut_output_json):
                                        with open(mut_output_json, 'r') as f:
                                            mut_res = json.load(f)
                                            mut_asm = Path(mut_res.get('asm_file'))
                                            mut_elf = Path(mut_res.get('elf_file')) if mut_res.get('elf_file') else None
                                            
                                            # Update current_asm for next round to chain mutations
                                            if mut_asm and mut_asm.exists():
                                                current_asm = mut_asm
                                                
                                            # Standardize name to {arch}_{seed_id}_m{r}.elf
                                            target_elf = elf_out_dir / f"{args.arch}_{seed_id}_m{r}.elf"
                                            if mut_elf and mut_elf.exists():
                                                if target_elf.exists():
                                                    target_elf.unlink()
                                                mut_elf.rename(target_elf)
                                                
                                                with open(elf_out_dir / f"{args.arch}_{seed_id}_m{r}_numinstrs.txt", 'w') as nf:
                                                    nf.write(str(mut_res.get('instr_count', 0)))
                                            else:
                                                 print(f"  {RED}Seed {seed_id} mutant {r} compilation failed{RESET}")
                                                 break # Stop mutation chain if compilation fails
                                else:
                                    print(f"  {RED}Seed {seed_id} mutant {r} generation failed. See {mut_log_file}{RESET}")
                                    break # Stop mutation chain
                                    
                        else:
                            rc = 1 # Failed to produce elf
                    except Exception as e:
                        print(f"Error post-processing seed {seed_id}: {e}")
                        rc = 1
                return seed_id, rc, elapsed

            gen_start_time = time.time()
            failed_seeds = 0
            with ThreadPoolExecutor(max_workers=args.jobs) as executor:
                futures = {executor.submit(run_divefuzz_gen, i): i for i in range(args.num_seeds)}
                for future in as_completed(futures):
                    seed_id, rc, elapsed = future.result()
                    if rc != 0:
                        failed_seeds += 1
                        log_file = os.path.join(out_log_dir, f'gen_{seed_id}.log')
                        print(f"  {RED}Seed {seed_id} generation failed. See {log_file}{RESET}")
            
            gen_elapsed = time.time() - gen_start_time
            print(f"{GREEN}DiveFuzz generation completed in {gen_elapsed:.2f}s. Failures: {failed_seeds}/{args.num_seeds}{RESET}")
        
        elif args.method == 'fuzzgpu':
            print(f"{GREEN}Starting fuzzgpu generation for {args.num_seeds} seeds...{RESET}")
            
            def run_fuzzgpu_gen(seed_id):
                test_dir = elf_out_dir / f'testcase_{seed_id}'
                os.makedirs(test_dir, exist_ok=True)
                
                instr_bin = test_dir / 'instr.bin'
                data_bin = test_dir / 'data.bin'
                aux_bin = test_dir / 'aux.bin'
                meta_json = test_dir / 'meta.json'
                gen_log = test_dir / 'gen.log'
                
                max_num_instrs = args.force_max_num_instrs or random.randrange(100, 10000)
                
                fuzz_cmd = [
                    fuzz_exe,
                    '--seed', str(seed_id),
                    '--num-genblocks', str(num_genblocks),
                    '--num-genwarps-per-block', str(math.ceil(conf_warps / num_genblocks)),
                    '--max-num-instrs', str(max_num_instrs),
                    '--max-num-fuzzing-instrs-per-bb', str(args.max_instrs_per_bb),
                    '--min-num-fuzzing-instrs-per-bb', str(args.min_instrs_per_bb),
                    '--log-level', 'debug',
                    str(instr_bin), str(data_bin), str(aux_bin), str(meta_json)
                ]
                
                with open(test_dir / 'fuzz_cmd.sh', 'w') as f:
                    f.write('#!/bin/bash\n' + ' '.join(fuzz_cmd) + '\n')
                
                rc, elapsed = run_command(fuzz_cmd, logfile_path=str(gen_log))
                
                num_instrs = 0
                if rc == 0 and meta_json.exists():
                    try:
                        with open(meta_json, 'r') as f:
                            meta = json.load(f)
                            num_instrs = meta.get('num_instrs', 0)
                    except Exception:
                        pass
                    
                    # Create numinstrs file for compatibility with emulation step
                    with open(elf_out_dir / f"{args.arch}_{seed_id}_numinstrs.txt", 'w') as nf:
                        nf.write(str(num_instrs))
                
                return seed_id, rc, elapsed, num_instrs
            
            gen_start_time = time.time()
            failed_seeds = 0
            with ThreadPoolExecutor(max_workers=args.jobs) as executor:
                futures = {executor.submit(run_fuzzgpu_gen, i): i for i in range(args.num_seeds)}
                for future in as_completed(futures):
                    seed_id, rc, elapsed, num_instrs = future.result()
                    if rc != 0:
                        failed_seeds += 1
                        test_dir = elf_out_dir / f'testcase_{seed_id}'
                        gen_log = test_dir / 'gen.log'
                        print(f"  {RED}Seed {seed_id} generation failed. See {gen_log}{RESET}")
                    else:
                        print(f"  {GREEN}Seed {seed_id} generated ({num_instrs} instrs){RESET}")
            
            gen_elapsed = time.time() - gen_start_time
            print(f"{GREEN}fuzzgpu generation completed in {gen_elapsed:.2f}s. Failures: {failed_seeds}/{args.num_seeds}{RESET}")
        
        # Step 2: Emulation
        print(f"{GREEN}Starting emulation...{RESET}")
        
        def run_emulator(i, mutation_id=None):
            # Check if this is fuzzgpu method (uses bin files instead of elf)
            is_fuzzgpu = args.method == 'fuzzgpu'
            
            # For cascade method with vortex, the ELF naming includes xlen (e.g., vortex64)
            if args.method == 'cascade' and args.arch == 'vortex':
                elf_arch_name = f"{args.arch}{args.xlen}"
            else:
                elf_arch_name = args.arch
            
            if is_fuzzgpu:
                test_dir = elf_out_dir / f'testcase_{i}'
                instr_bin = test_dir / 'instr.bin'
                data_bin = test_dir / 'data.bin'
                aux_bin = test_dir / 'aux.bin'
                meta_json = test_dir / 'meta.json'
                log_prefix = f"emulator_{i}"
                numinstrs_path = elf_out_dir / f"{args.arch}_{i}_numinstrs.txt"
                display_id = str(i)
                
                log_path = os.path.join(out_log_dir, f"{log_prefix}.log")
                stat_path = os.path.join(out_log_dir, f"{log_prefix}.json")
                
                if not instr_bin.exists():
                    empty_metrics = {
                        'thread_divergent_branches': 0,
                        'thread_total_branches': 0,
                        'max_simt_stack_depth': 0,
                        'local_barrier_success_count': 0,
                        'global_barrier_success_count': 0
                    }
                    return display_id, 1, 0.0, log_path, 0.0, 0, 0, empty_metrics
                
                emu_cmd = [
                    emu_exe,
                    '--instr-bin', str(instr_bin),
                    '--data-bin', str(data_bin),
                    '--aux', str(aux_bin),
                    '--meta-json', str(meta_json),
                    '--stat-jsonfile', stat_path,
                    '--disasm-trace'
                ]
                if args.emulate:
                    emu_cmd.append('--emulate')
            else:
                if mutation_id is not None:
                    elf_path = elf_out_dir / f"{elf_arch_name}_{i}_m{mutation_id}.elf"
                    log_prefix = f"emulator_{i}_m{mutation_id}"
                    numinstrs_path = elf_out_dir / f"{elf_arch_name}_{i}_m{mutation_id}_numinstrs.txt"
                    display_id = f"{i}_m{mutation_id}"
                else:
                    elf_path = elf_out_dir / f"{elf_arch_name}_{i}.elf"
                    log_prefix = f"emulator_{i}"
                    numinstrs_path = elf_out_dir / f"{elf_arch_name}_{i}_numinstrs.txt"
                    display_id = str(i)
                    
                log_path = os.path.join(out_log_dir, f"{log_prefix}.log")
                stat_path = os.path.join(out_log_dir, f"{log_prefix}.json")
                
                if not elf_path.exists():
                    empty_metrics = {
                        'thread_divergent_branches': 0,
                        'thread_total_branches': 0,
                        'max_simt_stack_depth': 0,
                        'local_barrier_success_count': 0,
                        'global_barrier_success_count': 0
                    }
                    return display_id, 1, 0.0, log_path, 0.0, 0, 0, empty_metrics
                
                emu_cmd = [emu_exe, '--elf', str(elf_path), '--stat-jsonfile', stat_path,
                       '--warps-limit', ','.join(warps_limit),
                       "--elf-num-genblocks", str(num_genblocks),
                       "--elf-num-genwarps-per-block", str(math.ceil(conf_warps / num_genblocks)),
                       "--elf-num-threads-per-warp", str(cur_num_threads_per_warp),
                       "--disasm-trace"]
                if args.emulate:
                    emu_cmd.append('--emulate')
            
            with open(os.path.join(out_log_dir, f"{log_prefix}_cmd.txt"), 'w') as f:
                f.write(' '.join(emu_cmd))
            
            rc, elapsed = run_command(emu_cmd, logfile_path=log_path, timeout=EMULATOR_TIMEOUT_SECONDS)
            
            # Calculate coverage and read metrics
            coverage = 0.0
            num_executed = 0
            total_instrs = 0
            metrics = {
                'thread_divergent_branches': 0,
                'thread_total_branches': 0,
                'max_simt_stack_depth': 0,
                'local_barrier_success_count': 0,
                'global_barrier_success_count': 0
            }
            
            try:
                # Read executed instructions and ISA emulator stats
                if os.path.exists(stat_path):
                    with open(stat_path, 'r') as f:
                        stats = json.load(f)
                        num_executed = stats.get('num_executed_instr', 0)
                        
                        # Read ISA emulator statistics
                        isa_stats = stats.get('isa_emulator_stats', {})
                        metrics['thread_divergent_branches'] = isa_stats.get('thread_divergent_branches', 0)
                        metrics['thread_total_branches'] = isa_stats.get('thread_total_branches', 0)
                        metrics['max_simt_stack_depth'] = isa_stats.get('max_simt_stack_depth', 0)
                        metrics['local_barrier_success_count'] = isa_stats.get('local_barrier_success_count', 0)
                        metrics['global_barrier_success_count'] = isa_stats.get('global_barrier_success_count', 0)
                
                # Read total instructions
                if os.path.exists(numinstrs_path):
                    with open(numinstrs_path, 'r') as f:
                        content = f.read().strip()
                        # Handle hex or decimal
                        if content.startswith('0x'):
                            total_instrs = int(content, 16)
                        else:
                            total_instrs = int(content)
                
                if total_instrs > 0:
                    coverage = min(100, (num_executed / total_instrs) * 100.0)
                    
            except Exception as e:
                print(f"Error calculating metrics for seed {display_id}: {e}")

            return display_id, rc, elapsed, log_path, coverage, num_executed, total_instrs, metrics

        results = []
        coverage_data = []
        tasks = []
        # Add tasks for base seeds
        for i in range(args.num_seeds):
            tasks.append((i, None))
            # Add tasks for mutants
            if args.method == 'divefuzz' and args.mutation_rounds > 0:
                for m in range(1, args.mutation_rounds + 1):
                    tasks.append((i, m))

        with ThreadPoolExecutor(max_workers=args.jobs) as executor:
            futures = {executor.submit(run_emulator, i, m): (i, m) for (i, m) in tasks}
            
            try:
                for future in as_completed(futures):
                    display_id, rc, elapsed, log_path, cov, exec_n, total_n, metrics = future.result()
                    
                    # Calculate thread_divergent_rate
                    thread_divergent_rate = 0.0
                    if metrics['thread_total_branches'] > 0:
                        thread_divergent_rate = (metrics['thread_divergent_branches'] / metrics['thread_total_branches']) * 100.0
                    
                    if rc == 0:
                        print(f"  {GREEN}Seed {display_id}: Success ({elapsed:.2f}s) Coverage: {cov:.2f}% ({exec_n}/{total_n}) Divergent: {thread_divergent_rate:.2f}%{RESET}")
                    else:
                        print(f"  {YELLOW}Seed {display_id}: Completed (rc={rc}). Coverage: {cov:.2f}% ({exec_n}/{total_n}) See {log_path}{RESET}")
                    results.append((display_id, rc, log_path))
                    coverage_data.append({
                        'seed': display_id,
                        'instr_completion_rate': cov,
                        'executed_instrs': exec_n,
                        'total_instrs': total_n,
                        'thread_divergent_rate': thread_divergent_rate,
                        'thread_divergent_branches': metrics['thread_divergent_branches'],
                        'thread_total_branches': metrics['thread_total_branches'],
                        'max_simt_stack_depth': metrics['max_simt_stack_depth'],
                        'local_barrier_success_count': metrics['local_barrier_success_count'],
                        'global_barrier_success_count': metrics['global_barrier_success_count'],
                        'rc': rc
                    })
            except KeyboardInterrupt:
                _handle_signal(signal.SIGINT, None)
                print(f"{YELLOW}Interrupted{RESET}")
                # Cancel remaining futures
                for f in futures:
                    f.cancel()
                
        # Summary
        failures = [r for r in results if r[1] != 0]
        
        print(f"\n{GREEN}Summary for {conf_name}:{RESET}")
        print(f"  Reported: {len(tasks)}")
        print(f"  Counted: {len(coverage_data)}")

        # Save metrics data to results directory (for plotting)
        metrics_log_path = os.path.join(results_dir, 'metrics.json')
        with open(metrics_log_path, 'w') as f:
            json.dump(coverage_data, f, indent=2)
        print(f"Metrics results saved to {metrics_log_path}")

        if failures:
            triage_failures(failures, results_dir, args.jobs)

if __name__ == '__main__':
    main()
