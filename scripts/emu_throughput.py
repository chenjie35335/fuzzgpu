#!/usr/bin/env python3
"""Measure emulator throughput with and without DFT enabled.

This script generates test cases using fuzz_gpu and runs them through
the emulator, measuring instruction throughput under different DFT settings.

Usage examples:
  python3 scripts/emu_throughput.py --arch ventus --num-tests 100
  python3 scripts/emu_throughput.py --arch vortex --num-tests 50
"""

import argparse
import os
import signal
import subprocess
import sys
import time
import json
import datetime
from pathlib import Path
import traceback
from shutil import which
import random

GREEN = "\033[32m"
RED = "\033[31m"
YELLOW = "\033[33m"
BLUE = "\033[34m"
RESET = "\033[0m"

# Constants
EMULATOR_TIMEOUT_SECONDS = 30  # timeout for emulator (adjust via the (instr number / measured MIPS) as needed)

# Process tracking for cleanup
_procs = set()

# Global state for throughput measurements
_start_time = None
_throughput_data = []  # List of throughput measurement results


def _register_proc(proc):
    try:
        _procs.add(proc)
    except Exception:
        pass


def _discard_proc(proc):
    try:
        _procs.discard(proc)
    except Exception:
        pass


def _kill_all_procs():
    """Best-effort terminate and wait for any remaining subprocesses."""
    procs = list(_procs)
    # First try graceful termination
    for p in procs:
        try:
            if p.poll() is None:
                try:
                    os.killpg(p.pid, signal.SIGTERM)
                except Exception:
                    try:
                        p.terminate()
                    except Exception:
                        pass
        except Exception:
            pass

    # Wait briefly
    deadline = time.time() + 1.0
    while time.time() < deadline:
        if all((proc.poll() is not None) for proc in procs):
            break
        time.sleep(0.05)

    # Force kill any remaining
    for p in procs:
        try:
            if p.poll() is None:
                try:
                    os.killpg(p.pid, signal.SIGKILL)
                except Exception:
                    try:
                        p.kill()
                    except Exception:
                        pass
        except Exception:
            pass
        finally:
            _discard_proc(p)


def run_command(cmd, logfile_path, timeout=None, env=None, echo=False, record_logs=True):
    """Run command and stream output to logfile (or discard). Returns (returncode, elapsed_time)."""
    if echo:
        print(f"  Running: {' '.join(map(str, cmd))}")

    lf_handle = None
    stdout_target = subprocess.DEVNULL
    try:
        if record_logs:
            lf_handle = open(logfile_path, 'wb')
            stdout_target = lf_handle

        proc = subprocess.Popen(
            cmd,
            stdout=stdout_target,
            stderr=subprocess.STDOUT,
            preexec_fn=os.setsid,
            env=env or os.environ.copy()
        )
        _register_proc(proc)
        start = time.time()
        try:
            while True:
                try:
                    rc = proc.wait(timeout=0.1)
                    break
                except subprocess.TimeoutExpired:
                    if timeout and (time.time() - start) > timeout:
                        try:
                            os.killpg(proc.pid, signal.SIGTERM)
                        except Exception:
                            try:
                                proc.terminate()
                            except Exception:
                                pass
                        return -2, time.time() - start
            elapsed = time.time() - start
            return rc, elapsed
        except Exception:
            try:
                proc.terminate()
            except Exception:
                pass
            raise
        finally:
            _discard_proc(proc)
    finally:
        if lf_handle is not None:
            try:
                lf_handle.close()
            except Exception:
                pass


def find_executable(candidate):
    """Find executable in current directory or system PATH."""
    if os.path.exists(candidate) and os.access(candidate, os.X_OK):
        return candidate
    p = which(candidate)
    return p

def read_executed_instr_count(stat_json_path, dft_enabled : bool):
    """Read executed instruction count from emulator stat json.

    Prefer 'num_executed_instr_trace' if present, otherwise fallback to
    'num_executed_instr'. Return 0 on failure.
    """
    try:
        with open(stat_json_path, 'r') as f:
            payload = json.load(f)
        if isinstance(payload, dict):
            if dft_enabled:
                # the instr count is from the ISA emualtor, which is available only when DFT is enabled
                if 'num_executed_instr_trace' in payload:
                    return int(payload['num_executed_instr_trace'])
            else:
                return int(payload['total'])
    except Exception as e:
        print(f"{YELLOW}Failed to read executed instr count from {stat_json_path}: {e}{RESET}")

    return 0


def _handle_signal(signum, frame):
    """Handle termination signals."""
    try:
        print(f"{YELLOW}\nReceived signal {signum}, shutting down...{RESET}")
    except Exception:
        pass
    _kill_all_procs()
    sys.exit(1)


def generate_tests(args, fuzz_exe, method_dir):
    """Generate all tests once (shared by dft0 and dft1) and return metadata list."""
    tests = []
    for test_id in range(args.num_tests):
        test_dir = method_dir / f'test_{test_id}'
        os.makedirs(test_dir, exist_ok=True)

        instr = test_dir / 'instr.bin'
        data = test_dir / 'data.bin'
        aux = test_dir / 'aux.bin'
        jsonf = test_dir / 'meta.json'
        fuzz_log = test_dir / 'gen.log'

        fuzz_cmd = [
            fuzz_exe,
            '--seed', str(test_id),
            '--num-genblocks', str(args.num_genblocks),
            '--num-genwarps-per-block', str(args.num_genwarps_per_block),
            '--max-num-fuzzing-instrs-per-bb', str(args.max_instrs_per_bb),
            '--min-num-fuzzing-instrs-per-bb', str(args.min_instrs_per_bb),
            '--instr-prob-json', '',
            str(instr), str(data), str(aux), str(jsonf)
        ]

        cmd_log = test_dir / 'fuzz_cmd.sh'
        with open(cmd_log, 'w') as f:
            f.write('#!/bin/bash\n')
            f.write(' '.join(fuzz_cmd) + '\n')

        rc, gen_time = run_command(fuzz_cmd, str(fuzz_log), record_logs=args.record_logs)
        if rc != 0:
            print(f"  {RED}Test {test_id}: fuzz failed, see {fuzz_log}{RESET}")
            continue

        tests.append({
            'test_id': test_id,
            'dir': test_dir,
            'instr': instr,
            'data': data,
            'aux': aux,
            'jsonf': jsonf,
            'gen_time': gen_time
        })

    return tests


def run_throughput_single(args, tests, fuzzgpu_dir, dft_enabled):
    """Run emulator on pre-generated tests for a given DFT setting."""
    for t in tests:
        test_id = t['test_id']
        test_dir = t['dir']
        # Executed instruction count will be read from emulator stat json

        # Per-DFT outputs kept separate
        dft_suffix = f"dft{int(dft_enabled)}"
        emu_log = test_dir / f'emu_{dft_suffix}.log'
        stat_json = test_dir / f'stat_{dft_suffix}.json'

        # Build directory with appropriate DFT setting
        xlen = args.xlen
        arch = args.arch
        cov_type = 'none'  # Always use cov-none for throughput measurement
        dft_flag = '1' if dft_enabled else '0'
        build_dir = fuzzgpu_dir / f"build{xlen}-{arch}-cov{cov_type}-dft{dft_flag}"
        emu_exe_for_dft = find_executable(str(build_dir / 'emulator'))

        if not emu_exe_for_dft:
            print(f"  {RED}Test {test_id}: emulator not found for dft={dft_flag} at {build_dir}/emulator{RESET}")
            continue

        # Run emulator with stat output
        emu_cmd = [
            emu_exe_for_dft,
            '--instr-bin', str(t['instr']),
            '--data-bin', str(t['data']),
            '--aux', str(t['aux']),
            '--meta-json', str(t['jsonf']),
            '--stat-jsonfile', str(stat_json)
        ]

        if args.emulate:
            emu_cmd.append('--emulate')

        emu_cmd_log = test_dir / f'emu_cmd_{dft_suffix}.sh'
        with open(emu_cmd_log, 'w') as f:
            f.write('#!/bin/bash\n')
            f.write(' '.join(emu_cmd) + '\n')

        # Write start timestamp if first test
        if not (test_dir / 'start.time').exists():
            with open(test_dir / 'start.time', 'w') as f:
                f.write(f"{time.time()}\n")

        rc2, t_emu = run_command(emu_cmd, str(emu_log), timeout=EMULATOR_TIMEOUT_SECONDS, record_logs=args.record_logs)
        if rc2 == -2:
            print(f"  {RED}Test {test_id}: emulator timed out after {EMULATOR_TIMEOUT_SECONDS}s, see {emu_log}{RESET}, not included in results")
            continue  # Timed out, skip
        if rc2 != 0:
            print(f"  {YELLOW}Test {test_id}: emulator returns error msg (may be just DFT reports), see {emu_log}{RESET}")

        # Read executed instruction count from stat json (required source of truth)
        num_instrs = read_executed_instr_count(str(stat_json), dft_enabled)
        if num_instrs <= 0:
            print(f"  {YELLOW}Test {test_id}: executed instruction count is 0 from stats {stat_json}{RESET}")
        
        throughput = num_instrs / t_emu if t_emu > 0 else 0.0

        elapsed = time.time() - _start_time if _start_time else 0.0
        result = {
            'dft_enabled': dft_enabled,
            'test_id': test_id,
            'timestamp': elapsed,
            'instrs': num_instrs,
            'gen_time': (t.get('gen_time', 0.0) if not dft_enabled else 0.0),
            'emu_time': t_emu,
            'throughput': throughput  # instrs/second
        }
        _throughput_data.append(result)

        print(f"  Test {test_id} (dft={int(dft_enabled)}): {num_instrs} instrs in {t_emu:.3f}s = {throughput:.1f} instrs/s")


def _cleanup_emu_processes():
    """Kill any existing instances of simulator processes."""
    targets = ['emulator']
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


def main():
    global _start_time, _throughput_data
    
    script_dir = Path(__file__).resolve().parent
    fuzzgpu_dir = script_dir.parent
    
    parser = argparse.ArgumentParser(description='Measure emulator throughput with different DFT settings.')
    parser.add_argument('--arch', type=str, choices=['ventus', 'vortex'], default='ventus',
                        help='Target architecture')
    parser.add_argument('--xlen', type=int, choices=[32, 64], default=32,
                        help='XLEN (32 or 64 bit)')
    parser.add_argument('--num-tests', type=int, default=100,
                        help='Number of test cases to generate and run')
    parser.add_argument('--out-dir', type=str, default='/tmp/emu_throughput_results',
                        help='Output directory for results')
    parser.add_argument('--emulate', action='store_true',
                        help='Emulate only (no RTL simulation)')
    parser.add_argument('--no-log', action='store_false', dest='record_logs',
                        help='Disable writing generator/emulator log files')
    
    # FuzzGPU parameters
    parser.add_argument('--num-genblocks', type=int, default=2,
                        help='Number of blocks to generate')
    parser.add_argument('--num-genwarps-per-block', type=int, default=8,
                        help='Number of warps per block')
    parser.add_argument('--max-instrs-per-bb', type=int, default=500,
                        help='Maximum instructions per basic block')
    parser.add_argument('--min-instrs-per-bb', type=int, default=1,
                        help='Minimum instructions per basic block')
    
    args = parser.parse_args()
    
    # Setup paths
    results_dir = Path(args.out_dir)
    timestamp = datetime.datetime.now().strftime('%Y%m%d_%H%M%S')
    method_dir = results_dir / f"emu_throughput_{args.arch}_{timestamp}"
    os.makedirs(method_dir, exist_ok=True)
    
    print(f"{BLUE}Starting emulator throughput measurement{RESET}")
    print(f"  Architecture: {args.arch}")
    print(f"  XLEN: {args.xlen}")
    print(f"  Number of tests: {args.num_tests}")
    print(f"  Output directory: {method_dir}")
    print(f"  Generator parameters: {args.num_genblocks} blocks, {args.num_genwarps_per_block} warps/block")
    if not args.record_logs:
        print(f"  Logging disabled: generator/emulator stdout will be suppressed")
    print()

    _cleanup_emu_processes()
    
    # Find executables
    build_dir_none_dft0 = fuzzgpu_dir / f"build{args.xlen}-{args.arch}-covnone-dft0"
    fuzz_exe = find_executable(str(build_dir_none_dft0 / 'fuzz_gpu'))
    emu_exe_dft0 = find_executable(str(build_dir_none_dft0 / 'emulator'))

    if not fuzz_exe:
        print(f"{RED}fuzz_gpu executable not found at {build_dir_none_dft0}/fuzz_gpu{RESET}")
        sys.exit(2)

    if not emu_exe_dft0:
        print(f"{RED}emulator (dft0) executable not found at {build_dir_none_dft0}/emulator{RESET}")
        sys.exit(2)

    # Verify dft=1 build exists
    build_dir_none_dft1 = fuzzgpu_dir / f"build{args.xlen}-{args.arch}-covnone-dft1"
    emu_exe_dft1 = find_executable(str(build_dir_none_dft1 / 'emulator'))

    if not emu_exe_dft1:
        print(f"{RED}emulator (dft1) executable not found at {build_dir_none_dft1}/emulator{RESET}")
        print(f"{RED}Please build with DFT enabled: build32-{args.arch}-covnone-dft1 or build64-{args.arch}-covnone-dft1{RESET}")
        sys.exit(2)

    print(f"{BLUE}Found executables:{RESET}")
    print(f"  fuzz_gpu: {fuzz_exe}")
    print(f"  emulator (dft=0): {emu_exe_dft0}")
    print(f"  emulator (dft=1): {emu_exe_dft1}")
    print()
    
    # Install signal handlers
    signal.signal(signal.SIGINT, _handle_signal)
    signal.signal(signal.SIGTERM, _handle_signal)
    
    # Generate all tests once (shared for both DFT modes)
    print(f"{BLUE}Generating tests...{RESET}")
    tests = generate_tests(args, fuzz_exe, method_dir)
    if not tests:
        print(f"{RED}No tests generated. Exiting.{RESET}")
        sys.exit(1)
    print(f"  Generated {len(tests)} tests")

    _start_time = time.time()

    # Run tests with DFT disabled and enabled
    try:
        print(f"{BLUE}Phase 1: Testing with DFT disabled (dft=0){RESET}")
        run_throughput_single(args, tests, fuzzgpu_dir, False)
        
        print()
        print(f"{BLUE}Phase 2: Testing with DFT enabled (dft=1){RESET}")
        run_throughput_single(args, tests, fuzzgpu_dir, True)
    
    except KeyboardInterrupt:
        print(f"{YELLOW}Interrupted by user, shutting down...{RESET}")
        _handle_signal(signal.SIGINT, None)
    
    except Exception as e:
        print(f"{RED}Error during execution: {e}{RESET}")
        traceback.print_exc()
        _handle_signal(signal.SIGTERM, None)
    
    # Compute statistics
    elapsed_total = time.time() - _start_time
    
    # Separate results by DFT setting
    dft0_results = [r for r in _throughput_data if not r['dft_enabled']]
    dft1_results = [r for r in _throughput_data if r['dft_enabled']]

    # Compute stats for each group
    def compute_stats(results):
        if not results:
            return None
        throughputs = [r['throughput'] for r in results]
        gen_times = [r['gen_time'] for r in results]
        emu_times = [r['emu_time'] for r in results]
        total_instrs = sum(r['instrs'] for r in results)
        total_time = sum(r['emu_time'] for r in results)
        
        return {
            'count': len(results),
            'total_instructions': total_instrs,
            'total_emu_time': total_time,
            'average_throughput': sum(throughputs) / len(throughputs),
            'min_throughput': min(throughputs),
            'max_throughput': max(throughputs),
            'median_throughput': sorted(throughputs)[len(throughputs)//2],
            'average_gen_time': sum(gen_times) / len(gen_times),
            'average_emu_time': sum(emu_times) / len(emu_times),
            'aggregate_throughput': total_instrs / total_time if total_time > 0 else 0
        }

    dft0_stats = compute_stats(dft0_results)
    dft1_stats = compute_stats(dft1_results)
    
    # Compute comparison
    comparison = None
    if dft0_stats and dft1_stats:
        throughput_ratio = dft1_stats['average_throughput'] / dft0_stats['average_throughput'] if dft0_stats['average_throughput'] > 0 else 0
        aggregate_ratio = dft1_stats['aggregate_throughput'] / dft0_stats['aggregate_throughput'] if dft0_stats['aggregate_throughput'] > 0 else 0
        
        comparison = {
            'dft_enabled_throughput_ratio': throughput_ratio,  # dft1 / dft0
            'aggregate_throughput_ratio': aggregate_ratio,  # dft1 / dft0
            'summary': f"DFT overhead: {throughput_ratio:.2%} throughput reduction, {time_overhead:+.1f}% time overhead"
        }
    
    # Build results object
    results = {
        'timestamp': timestamp,
        'arch': args.arch,
        'xlen': args.xlen,
        'total_elapsed_time': elapsed_total,
        'args': vars(args),
        'dft_disabled': dft0_stats,
        'dft_enabled': dft1_stats,
        'comparison': comparison,
        'all_measurements': _throughput_data
    }
    
    # Save results
    results_json = method_dir / 'results.json'
    with open(results_json, 'w') as f:
        json.dump(results, f, indent=2)
    
    print()
    print(f"{GREEN}Results saved to: {results_json}{RESET}")
    print()
    print(f"{BLUE}Summary:{RESET}")
    print(f"  Total elapsed time: {elapsed_total:.1f}s")
    print(f"  Tests completed: {len(_throughput_data)}")
    
    if dft0_stats:
        print()
        print(f"{BLUE}DFT Disabled (dft=0):{RESET}")
        print(f"  Tests: {dft0_stats['count']}")
        print(f"  Total instructions: {dft0_stats['total_instructions']}")
        print(f"  Total emulation time: {dft0_stats['total_emu_time']:.2f}s")
        print(f"  Average throughput: {dft0_stats['average_throughput']:.1f} instrs/s")
        print(f"  Aggregate throughput: {dft0_stats['aggregate_throughput']:.1f} instrs/s")
        print(f"  Throughput range: {dft0_stats['min_throughput']:.1f} - {dft0_stats['max_throughput']:.1f} instrs/s")
    
    if dft1_stats:
        print()
        print(f"{BLUE}DFT Enabled (dft=1):{RESET}")
        print(f"  Tests: {dft1_stats['count']}")
        print(f"  Total instructions: {dft1_stats['total_instructions']}")
        print(f"  Total emulation time: {dft1_stats['total_emu_time']:.2f}s")
        print(f"  Average throughput: {dft1_stats['average_throughput']:.1f} instrs/s")
        print(f"  Aggregate throughput: {dft1_stats['aggregate_throughput']:.1f} instrs/s")
        print(f"  Throughput range: {dft1_stats['min_throughput']:.1f} - {dft1_stats['max_throughput']:.1f} instrs/s")
    
    if comparison:
        print()
        print(f"{BLUE}Comparison (DFT Enabled vs Disabled):{RESET}")
        print(f"  {comparison['summary']}")
        print(f"  Average throughput ratio: {comparison['dft_enabled_throughput_ratio']:.4f}x")
        print(f"  Aggregate throughput ratio: {comparison['aggregate_throughput_ratio']:.4f}x")
    
    print()
    print(f"{BLUE}To visualize results, run:{RESET}")
    print(f"  python3 scripts/plot_emu_throughput.py --results-json {results_json}")

    # Best-effort cleanup of any stray subprocesses
    _kill_all_procs()


if __name__ == '__main__':
    main()
