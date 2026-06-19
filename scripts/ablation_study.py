#!/usr/bin/env python3
"""
Ablation Study for FuzzGPU

Runs fuzzgpu with different configurations to evaluate the contribution of each component:
  - baseline (all enabled)
  - --disable-emu-guide
  - --disable-barrier-plan
  - --disable-taint
"""

from __future__ import annotations

import argparse
import datetime
import json
import os
import random
import signal
import subprocess
import sys
import threading
import time
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass, field, asdict
from pathlib import Path
from typing import Dict, List, Optional, Set, Tuple, Any

import numpy as np

# Script directory
script_dir = Path(__file__).resolve().parent
sys.path.insert(0, str(script_dir))

from bug_detector import scan_file, BugMatch

# Colors
GREEN = "\033[32m"
RED = "\033[31m"
YELLOW = "\033[33m"
BLUE = "\033[34m"
CYAN = "\033[36m"
RESET = "\033[0m"

# Constants
EMULATOR_TIMEOUT_SECONDS = 300
MAX_LOG_FILE_SIZE_BYTES = 50 * 1024 * 1024

# Ablation configurations: name -> list of extra args for fuzz_gpu
ABLATION_CONFIGS = {
    'baseline': [],  # All features enabled
    'no_emu_guide': ['--disable-emu-guide'],
    'no_barrier_plan': ['--disable-barrier-plan'],
    'no_taint': ['--disable-taint'],
}

# Arch configurations for --all mode
ALL_ARCH_CONFIGS = [
    {'arch': 'vortex', 'xlen': 32},
    {'arch': 'vortex', 'xlen': 64},
    {'arch': 'ventus', 'xlen': 32},
]


# =============================================================================
# Data Structures
# =============================================================================

@dataclass
class TestRecord:
    """Record of a single test case."""
    seed_id: int
    gen_num_instrs: int = 0
    emu_num_instrs: int = 0
    success: bool = True
    max_simt_stack_depth: int = 0
    thread_divergent_branches: int = 0
    thread_total_branches: int = 0
    local_barrier_success_count: int = 0
    global_barrier_success_count: int = 0
    gen_time: float = 0.0
    emu_time: float = 0.0
    # Bug discovery tracking
    bugs_found: List[str] = field(default_factory=list)
    test_start_time: float = 0.0
    test_end_time: float = 0.0


@dataclass
class BugDiscoveryRecord:
    """Record of when a bug was first discovered."""
    bug_name: str
    first_detection_time: float  # Elapsed time from start (seconds)
    first_detection_seed: int
    core_hours: float = 0.0


@dataclass
class AblationResults:
    """Results for a single ablation configuration."""
    config_name: str
    arch: str
    xlen: int
    tests: List[TestRecord] = field(default_factory=list)
    start_time: float = 0.0
    end_time: float = 0.0
    # Bug discovery tracking
    bug_discoveries: Dict[str, BugDiscoveryRecord] = field(default_factory=dict)
    
    def to_dict(self) -> Dict:
        return {
            'config_name': self.config_name,
            'arch': self.arch,
            'xlen': self.xlen,
            'num_tests': len(self.tests),
            'start_time': self.start_time,
            'end_time': self.end_time,
            'duration_seconds': self.end_time - self.start_time if self.end_time else 0,
            'tests': [asdict(t) for t in self.tests],
            'bug_discoveries': {k: asdict(v) for k, v in self.bug_discoveries.items()},
        }
    
    def compute_stats(self) -> Dict:
        """Compute summary statistics from test records."""
        if not self.tests:
            return {}
        
        # Filter successful tests
        successful_tests = [t for t in self.tests if t.success]
        if not successful_tests:
            return {'num_tests': len(self.tests), 'num_successful': 0}
        
        # Instruction completion rate
        completion_rates = []
        for t in successful_tests:
            if t.gen_num_instrs > 0:
                completion_rates.append(t.emu_num_instrs / t.gen_num_instrs)
        
        # Max SIMT stack depths
        simt_depths = [t.max_simt_stack_depth for t in successful_tests]
        
        # Thread divergent branch rates
        divergence_rates = []
        for t in successful_tests:
            if t.thread_total_branches > 0:
                divergence_rates.append(t.thread_divergent_branches / t.thread_total_branches)
        
        # Barrier success counts
        barrier_counts = [
            t.local_barrier_success_count + t.global_barrier_success_count 
            for t in successful_tests
        ]
        
        stats = {
            'num_tests': len(self.tests),
            'num_successful': len(successful_tests),
            'success_rate': len(successful_tests) / len(self.tests) if self.tests else 0,
        }
        
        if completion_rates:
            arr = np.array(completion_rates)
            stats['instr_completion_rate'] = {
                'mean': float(np.mean(arr)),
                'std': float(np.std(arr)),
                'min': float(np.min(arr)),
                'max': float(np.max(arr)),
                'median': float(np.median(arr)),
            }
        
        if simt_depths:
            arr = np.array(simt_depths)
            stats['max_simt_stack_depth'] = {
                'mean': float(np.mean(arr)),
                'std': float(np.std(arr)),
                'min': float(np.min(arr)),
                'max': float(np.max(arr)),
                'median': float(np.median(arr)),
            }
        
        if divergence_rates:
            arr = np.array(divergence_rates)
            stats['thread_divergence_rate'] = {
                'mean': float(np.mean(arr)),
                'std': float(np.std(arr)),
                'min': float(np.min(arr)),
                'max': float(np.max(arr)),
                'median': float(np.median(arr)),
            }
        
        if barrier_counts:
            arr = np.array(barrier_counts)
            stats['barrier_success_count'] = {
                'mean': float(np.mean(arr)),
                'std': float(np.std(arr)),
                'min': float(np.min(arr)),
                'max': float(np.max(arr)),
                'median': float(np.median(arr)),
            }
        
        # Bug discovery stats
        if self.bug_discoveries:
            stats['num_bugs_found'] = len(self.bug_discoveries)
            stats['bug_names'] = list(self.bug_discoveries.keys())
        
        return stats
    
    def compute_bug_discovery_timeline(self) -> Tuple[List[float], List[int]]:
        """Compute cumulative bug discovery timeline.
        
        Returns:
            Tuple of (time_points, bug_counts) where:
            - time_points[i] is the elapsed time (seconds) when bug i+1 was discovered
            - bug_counts[i] is i+1
        """
        if not self.bug_discoveries:
            return [], []
        
        # Sort bugs by first detection time
        sorted_bugs = sorted(
            self.bug_discoveries.values(),
            key=lambda b: b.first_detection_time
        )
        
        time_points = []
        bug_counts = []
        for i, bug in enumerate(sorted_bugs):
            time_points.append(bug.first_detection_time)
            bug_counts.append(i + 1)
        
        return time_points, bug_counts


# =============================================================================
# Shared State
# =============================================================================

_procs_lock = threading.Lock()
_procs: Set[subprocess.Popen] = set()
_stop_event = threading.Event()

_stats_lock = threading.Lock()
_seed_counter = 0

# Bug filter state (for real-time bug filtering during generation)
_bug_filter_lock = threading.Lock()
_bug_filter_set: Set[str] = set()


# =============================================================================
# Utility Functions
# =============================================================================

def run_command(cmd, logfile_path, timeout=None, env=None, record_logs=True, max_log_size=None):
    """Run command and stream output to logfile. Returns (returncode, elapsed_time)."""
    if max_log_size is None:
        max_log_size = MAX_LOG_FILE_SIZE_BYTES
    
    lf_handle = None
    stdout_target = subprocess.DEVNULL
    try:
        if record_logs:
            lf_handle = open(logfile_path, 'wb')
            cmd_header = f"# Command: {' '.join(cmd)}\n{'='*80}\n"
            lf_handle.write(cmd_header.encode('utf-8'))
            lf_handle.flush()
            stdout_target = lf_handle

        proc = subprocess.Popen(
            cmd,
            stdout=stdout_target,
            stderr=subprocess.STDOUT,
            preexec_fn=os.setsid,
            env=env or os.environ.copy()
        )
        with _procs_lock:
            _procs.add(proc)
        start = time.time()
        try:
            while True:
                rc = proc.poll()
                if rc is not None:
                    break
                try:
                    os.kill(proc.pid, 0)
                except ProcessLookupError:
                    rc = proc.poll()
                    if rc is None:
                        rc = -3
                    break
                except PermissionError:
                    pass
                
                if _stop_event.is_set():
                    try:
                        os.killpg(proc.pid, signal.SIGTERM)
                    except Exception:
                        proc.terminate()
                    proc.wait(timeout=1)
                    return -1, time.time() - start
                if timeout and (time.time() - start) > timeout:
                    try:
                        os.killpg(proc.pid, signal.SIGTERM)
                    except Exception:
                        proc.terminate()
                    try:
                        proc.wait(timeout=1)
                    except subprocess.TimeoutExpired:
                        proc.kill()
                    return -2, time.time() - start
                if record_logs and max_log_size and lf_handle:
                    try:
                        current_size = lf_handle.tell()
                        if current_size > max_log_size:
                            try:
                                os.killpg(proc.pid, signal.SIGTERM)
                            except Exception:
                                proc.terminate()
                            try:
                                proc.wait(timeout=1)
                            except subprocess.TimeoutExpired:
                                proc.kill()
                            lf_handle.write(f"\n\n{'='*80}\n# LOG TRUNCATED\n".encode('utf-8'))
                            return -4, time.time() - start
                    except Exception:
                        pass
                time.sleep(0.1)
            return rc, time.time() - start
        finally:
            with _procs_lock:
                _procs.discard(proc)
    finally:
        if lf_handle:
            lf_handle.close()


def find_executable(candidate):
    """Find executable path."""
    if os.path.exists(candidate) and os.access(candidate, os.X_OK):
        return candidate
    from shutil import which
    return which(candidate)


def save_bug_filter_json(work_dir: Path) -> Path:
    """Save bug_filter.json atomically for fuzz_gpu consumption.
    
    Uses atomic write (write to temp file + rename) to avoid race conditions
    where fuzz_gpu may read incomplete JSON.
    """
    import tempfile
    filter_file = work_dir / 'bug_filter.json'
    
    with _bug_filter_lock:
        data = {'filtered_bugs': list(_bug_filter_set)}
    
    # Atomic write: write to temp file, then rename
    fd, tmp_path = tempfile.mkstemp(dir=work_dir, suffix='.json.tmp')
    try:
        with os.fdopen(fd, 'w') as f:
            json.dump(data, f, indent=2)
        os.replace(tmp_path, filter_file)  # Atomic on POSIX
    except Exception:
        if os.path.exists(tmp_path):
            os.unlink(tmp_path)
        raise
    return filter_file


def get_bug_filter_path_if_exists(work_dir: Path) -> Optional[str]:
    """Thread-safe check for bug_filter.json existence.
    
    Returns the path string if file exists, None otherwise.
    """
    filter_file = work_dir / 'bug_filter.json'
    return str(filter_file) if filter_file.exists() else None


def _handle_signal(signum, frame):
    """Handle termination signals."""
    print(f"{YELLOW}\nReceived signal {signum}, terminating...{RESET}")
    _stop_event.set()
    with _procs_lock:
        for p in list(_procs):
            try:
                os.killpg(p.pid, signal.SIGTERM)
            except Exception:
                pass
    time.sleep(0.5)
    sys.exit(1)


# =============================================================================
# Ablation Worker
# =============================================================================

def run_ablation_worker(
    args,
    config_name: str,
    config_extra_args: List[str],
    fuzz_exe: str,
    emu_exe: str,
    work_dir: Path,
    results: AblationResults,
    results_lock: threading.Lock,
):
    """Worker for running ablation tests."""
    global _seed_counter
    
    logs_dir = work_dir / 'logs'
    os.makedirs(logs_dir, exist_ok=True)
    
    while not _stop_event.is_set():
        # Check max_time limit
        if args.max_time is not None:
            elapsed = time.time() - results.start_time
            if elapsed >= args.max_time:
                break  # Time limit reached
        
        # Get next seed atomically
        with _stats_lock:
            seed = _seed_counter
            if seed >= args.start_seed + args.num_seeds:
                break  # All seeds assigned
            _seed_counter += 1
        
        # Test directory
        test_dir = work_dir / f'testcase_{seed}'
        instr = test_dir / 'instr.bin'
        data = test_dir / 'data.bin'
        aux = test_dir / 'aux.bin'
        jsonf = test_dir / 'meta.json'
        stat_json = test_dir / 'stat.json'
        
        fuzz_log = logs_dir / f'testcase_{seed}_gen.log'
        emu_log = logs_dir / f'testcase_{seed}_emu.log'
        
        os.makedirs(test_dir, exist_ok=True)
        
        max_num_instrs = args.force_max_num_instrs or random.randrange(100, 10000)
        
        # Get bug filter file path (thread-safe check to avoid TOCTOU race)
        bug_filter_json_str = get_bug_filter_path_if_exists(work_dir)
        
        # Build fuzz command
        fuzz_cmd = [
            fuzz_exe,
            '--seed', str(seed),
            '--num-genblocks', str(args.num_genblocks),
            '--num-genwarps-per-block', str(args.num_genwarps_per_block),
            '--max-num-instrs', str(max_num_instrs),
            '--max-num-fuzzing-instrs-per-bb', str(args.max_instrs_per_bb),
            '--min-num-fuzzing-instrs-per-bb', str(args.min_instrs_per_bb),
            '--log-level', 'warn',
            '--disable-default-bug-filtering',
            str(instr), str(data), str(aux), str(jsonf)
        ]
        
        # Add bug filter if exists
        if bug_filter_json_str:
            fuzz_cmd.extend(['--bug-filter-json', bug_filter_json_str])
        
        # Add config-specific args
        fuzz_cmd.extend(config_extra_args)
        
        print(f"{BLUE}gen test case {seed} ({config_name})...{RESET}")
        # Run generation
        test_start_time = time.time()
        rc1, gen_time = run_command(fuzz_cmd, str(fuzz_log), record_logs=args.record_logs)
        if rc1 != 0:
            continue
        
        # Read meta.json
        gen_num_instrs = 0
        try:
            with open(jsonf, 'r') as f:
                meta_data = json.load(f)
                gen_num_instrs = meta_data.get('num_instrs', 0)
        except Exception:
            pass
        
        print(f"{BLUE}emu test case {seed} ({config_name})...{RESET}")

        # Run emulator
        emu_cmd = [
            emu_exe,
            '--instr-bin', str(instr),
            '--data-bin', str(data),
            '--aux', str(aux),
            '--meta-json', str(jsonf),
            '--stat-jsonfile', str(stat_json)
        ]
        if args.emulate:
            emu_cmd.append('--emulate')
        
        rc2, emu_time = run_command(emu_cmd, str(emu_log), timeout=EMULATOR_TIMEOUT_SECONDS,
                                     record_logs=args.record_logs)
        
        test_end_time = time.time()
        
        # Read stat.json and extract metrics
        record = TestRecord(
            seed_id=seed,
            gen_num_instrs=gen_num_instrs,
            gen_time=gen_time,
            emu_time=emu_time,
            test_start_time=test_start_time,
            test_end_time=test_end_time,
        )
        
        try:
            with open(stat_json, 'r') as f:
                stat_data = json.load(f)
                record.emu_num_instrs = stat_data.get('num_executed_instr', 0)
                record.success = record.emu_num_instrs > 0
                
                # Extract isa_emulator_stats
                isa_stats = stat_data.get('isa_emulator_stats', {})
                record.max_simt_stack_depth = isa_stats.get('max_simt_stack_depth', 0)
                record.thread_divergent_branches = isa_stats.get('thread_divergent_branches', 0)
                record.thread_total_branches = isa_stats.get('thread_total_branches', 0)
                record.local_barrier_success_count = isa_stats.get('local_barrier_success_count', 0)
                record.global_barrier_success_count = isa_stats.get('global_barrier_success_count', 0)
        except Exception:
            record.success = False
        
        # Bug detection (if enabled and log exists)
        bugs_found = []
        if args.detect_bugs and emu_log.exists():
            try:
                known_matches, unknown_matches = scan_file(emu_log)
                for match in known_matches:
                    bug_name = match.bug_name
                    bugs_found.append(bug_name)
                    
                    # Add to global bug filter set (for real-time filtering)
                    with _bug_filter_lock:
                        is_new_bug = bug_name not in _bug_filter_set
                        if is_new_bug:
                            _bug_filter_set.add(bug_name)
                    
                    # Track first detection time for each bug
                    # match.timestamp is absolute timestamp from emulator log
                    # Calculate discovery time relative to config start_time
                    # Use test_end_time as fallback when bug.timestamp is 0
                    with results_lock:
                        if bug_name not in results.bug_discoveries:
                            bug_ts = float(match.timestamp) if match.timestamp > 0 else test_end_time
                            discovery_time = bug_ts - results.start_time if results.start_time > 0 else bug_ts
                            core_hours = discovery_time / 3600.0  # Convert seconds to hours
                            results.bug_discoveries[bug_name] = BugDiscoveryRecord(
                                bug_name=bug_name,
                                first_detection_time=discovery_time,
                                first_detection_seed=seed,
                                core_hours=core_hours,
                            )
                            print(f"{GREEN}Bug found: {bug_name} at seed {seed} (ts={discovery_time:.2f}s, {core_hours:.4f} ch) -> filter{RESET}")
                    
                    # Save bug filter JSON (atomically) for next fuzz_gpu invocation
                    if is_new_bug:
                        try:
                            save_bug_filter_json(work_dir)
                        except Exception as e:
                            print(f"{YELLOW}Warning: Failed to save bug_filter.json: {e}{RESET}")
            except Exception as e:
                print(f"{YELLOW}Warning: Bug detection failed for seed {seed}: {e}{RESET}")
        
        record.bugs_found = bugs_found
        
        with results_lock:
            results.tests.append(record)


def run_ablation_config(
    args,
    config_name: str,
    config_extra_args: List[str],
    arch: str,
    xlen: int,
    fuzzgpu_dir: Path,
) -> AblationResults:
    """Run ablation study for a single configuration."""
    global _seed_counter, _bug_filter_set
    
    # Reset state
    _seed_counter = args.start_seed
    _stop_event.clear()
    
    # Reset bug filter set for each config run
    with _bug_filter_lock:
        _bug_filter_set.clear()
    
    build_dir = fuzzgpu_dir / f"build{xlen}-{arch}-covnone-dft1"
    fuzz_exe = find_executable(str(build_dir / 'fuzz_gpu'))
    emu_exe = find_executable(str(build_dir / 'emulator'))
    
    if not fuzz_exe:
        print(f"{RED}fuzz_gpu not found in {build_dir}{RESET}")
        return None
    if not emu_exe:
        print(f"{RED}emulator not found in {build_dir}{RESET}")
        return None
    
    # Work directory
    timestamp = datetime.datetime.now().strftime('%Y%m%d_%H%M%S')
    work_dir = Path(args.work_dir) / f"ablation_{arch}_x{xlen}" / f"{config_name}_{timestamp}"
    os.makedirs(work_dir, exist_ok=True)
    
    results = AblationResults(
        config_name=config_name,
        arch=arch,
        xlen=xlen,
        start_time=time.time(),
    )
    results_lock = threading.Lock()
    
    print(f"\n{BLUE}=== Running: {config_name} ({arch}/x{xlen}) ==={RESET}")
    print(f"  Work dir: {work_dir}")
    print(f"  Extra args: {config_extra_args or '(none)'}")
    
    try:
        if args.jobs == 1:
            run_ablation_worker(
                args, config_name, config_extra_args,
                fuzz_exe, emu_exe, work_dir, results, results_lock
            )
        else:
            with ThreadPoolExecutor(max_workers=args.jobs) as executor:
                futures = []
                for worker_id in range(args.jobs):
                    fut = executor.submit(
                        run_ablation_worker,
                        args, config_name, config_extra_args,
                        fuzz_exe, emu_exe, work_dir, results, results_lock
                    )
                    futures.append(fut)
                for fut in futures:
                    try:
                        fut.result()
                    except Exception as e:
                        print(f"{RED}Worker error: {e}{RESET}")
    except KeyboardInterrupt:
        _stop_event.set()
    
    results.end_time = time.time()
    return results


def print_summary(all_results: Dict[str, AblationResults]):
    """Print summary comparison of all ablation configurations."""
    print(f"\n{GREEN}{'='*80}{RESET}")
    print(f"{GREEN}ABLATION STUDY SUMMARY{RESET}")
    print(f"{GREEN}{'='*80}{RESET}\n")
    
    # Table header
    header = f"{'Config':<20} {'Tests':>8} {'Success':>8} {'Compl.Rate':>12} {'SIMT Depth':>12} {'Diverg.Rate':>12} {'Barriers':>12}"
    print(header)
    print("-" * len(header))
    
    for config_name, results in all_results.items():
        if results is None:
            continue
        
        stats = results.compute_stats()
        num_tests = stats.get('num_tests', 0)
        num_success = stats.get('num_successful', 0)
        
        # Get median values
        compl_rate = stats.get('instr_completion_rate', {}).get('median', 0)
        simt_depth = stats.get('max_simt_stack_depth', {}).get('median', 0)
        diverg_rate = stats.get('thread_divergence_rate', {}).get('median', 0)
        barrier_count = stats.get('barrier_success_count', {}).get('median', 0)
        
        row = f"{config_name:<20} {num_tests:>8} {num_success:>8} {compl_rate:>12.4f} {simt_depth:>12.2f} {diverg_rate:>12.4f} {barrier_count:>12.2f}"
        print(row)

    print()


def save_results(all_results: Dict[str, AblationResults], output_dir: Path, arch: str, xlen: int):
    """Save all results to JSON files."""
    os.makedirs(output_dir, exist_ok=True)
    
    timestamp = datetime.datetime.now().strftime('%Y%m%d_%H%M%S')
    
    # Save combined results
    combined_file = output_dir / f"ablation_{arch}_x{xlen}_{timestamp}.json"
    combined_data = {
        'timestamp': timestamp,
        'arch': arch,
        'xlen': xlen,
        'configs': {},
    }
    
    for config_name, results in all_results.items():
        if results is None:
            continue
        
        # Compute bug discovery timeline
        time_points, bug_counts = results.compute_bug_discovery_timeline()
        
        combined_data['configs'][config_name] = {
            'results': results.to_dict(),
            'stats': results.compute_stats(),
            'bug_discovery_timeline': {
                'time_points': time_points,
                'bug_counts': bug_counts,
            },
        }
    
    with open(combined_file, 'w') as f:
        json.dump(combined_data, f, indent=2)
    
    print(f"{GREEN}Results saved to: {combined_file}{RESET}")
    return combined_file


def main():
    parser = argparse.ArgumentParser(
        description='Ablation study for FuzzGPU components'
    )
    
    # Architecture selection
    parser.add_argument('--arch', type=str, choices=['ventus', 'vortex'], default='ventus',
                        help='Target architecture (default: ventus)')
    parser.add_argument('--xlen', type=int, choices=[32, 64], default=32,
                        help='XLEN (default: 32)')
    parser.add_argument('--all', action='store_true',
                        help='Run all arch/xlen: vortex/32, vortex/64, ventus/32')
    
    # Number of test cases to generate
    parser.add_argument('--num-seeds', type=int, required=True,
                        help='Number of test cases to generate per configuration')
    
    # Ablation configuration selection
    parser.add_argument('--configs', type=str, nargs='*', default=None,
                        help=f'Configs to run (default: all). Options: {list(ABLATION_CONFIGS.keys())}')
    
    # Generation parameters
    parser.add_argument('--start-seed', type=int, default=0,
                        help='Starting seed (default: 0)')
    parser.add_argument('--force-max-num-instrs', type=int, default=None,
                        help='Force specific max number of instructions')
    parser.add_argument('--num-genblocks', type=int, default=2,
                        help='Number of blocks to generate (default: 2)')
    parser.add_argument('--num-genwarps-per-block', type=int, default=2,
                        help='Number of warps per block (default: 2)')
    parser.add_argument('--max-instrs-per-bb', type=int, default=500,
                        help='Max instructions per basic block (default: 500)')
    parser.add_argument('--min-instrs-per-bb', type=int, default=1,
                        help='Min instructions per basic block (default: 1)')
    
    # Execution parameters
    parser.add_argument('--jobs', type=int, default=30,
                        help='Number of parallel jobs (default: 1)')
    parser.add_argument('--work-dir', type=str, default='/tmp/ablation_work',
                        help='Work directory for temp files (default: /tmp/ablation_work)')
    parser.add_argument('--output-dir', type=str, default=None,
                        help='Output directory for results (default: results/ablation_study)')
    parser.add_argument('--emulate', action='store_true',
                        help='Use emulation-only mode (no RTL)')
    parser.add_argument('--no-log', action='store_false', dest='record_logs',
                        help='Do not record logs')
    
    # Bug discovery mode
    parser.add_argument('--detect-bugs', action='store_true',
                        help='Enable bug detection and track discovery times')
    parser.add_argument('--max-time', type=float, default=None,
                        help='Maximum time (seconds) to run each config (overrides --num-seeds)')
    
    args = parser.parse_args()
    
    # Determine which configs to run
    if args.configs:
        configs_to_run = {k: v for k, v in ABLATION_CONFIGS.items() if k in args.configs}
        if not configs_to_run:
            parser.error(f'Invalid config names. Options: {list(ABLATION_CONFIGS.keys())}')
    else:
        configs_to_run = ABLATION_CONFIGS
    
    # Setup paths
    fuzzgpu_dir = script_dir.parent
    if args.output_dir:
        output_dir = Path(args.output_dir)
    else:
        output_dir = fuzzgpu_dir / 'results' / 'ablation_study'
    
    # Setup signal handlers
    signal.signal(signal.SIGINT, _handle_signal)
    signal.signal(signal.SIGTERM, _handle_signal)
    
    # Determine arch configurations
    arch_configs = ALL_ARCH_CONFIGS if args.all else [{'arch': args.arch, 'xlen': args.xlen}]
    
    print(f"{CYAN}FuzzGPU Ablation Study{RESET}")
    print(f"  Configs: {list(configs_to_run.keys())}")
    print(f"  Arch configs: {arch_configs}")
    print(f"  Num seeds: {args.num_seeds}")
    print(f"  Start seed: {args.start_seed}")
    
    for arch_cfg in arch_configs:
        arch = arch_cfg['arch']
        xlen = arch_cfg['xlen']
        
        print(f"\n{CYAN}{'='*60}{RESET}")
        print(f"{CYAN}Architecture: {arch} / x{xlen}{RESET}")
        print(f"{CYAN}{'='*60}{RESET}")
        
        all_results = {}
        
        for config_name, config_extra_args in configs_to_run.items():
            if _stop_event.is_set():
                break
            
            results = run_ablation_config(
                args, config_name, config_extra_args,
                arch, xlen, fuzzgpu_dir
            )
            all_results[config_name] = results
        
        # Print and save summary
        print_summary(all_results)
        save_results(all_results, output_dir, arch, xlen)
    
    print(f"\n{GREEN}Ablation study complete!{RESET}")


if __name__ == '__main__':
    main()
