#!/usr/bin/env python3
"""
Ablation Study for FuzzGPU - Coverage Metrics

Runs fuzzgpu with different configurations and measures RTL coverage:
  - baseline (all enabled)
  - --disable-emu-guide
  - --disable-barrier-plan
  - --disable-taint

Coverage is measured using verilator_coverage after RTL simulation.
"""

from __future__ import annotations

import argparse
import bisect
import datetime
import json
import os
import random
import re
import shutil
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


# =============================================================================
# Data Structures
# =============================================================================

@dataclass
class CoverageRecord:
    """Record of coverage at a point in time."""
    seed_id: int
    timestamp: float  # Elapsed time from start (seconds)
    covered: int = 0
    total: int = 0
    percent: float = 0.0
    merged_file: str = ""
    source_file: str = ""


@dataclass
class TestRecord:
    """Record of a single test case with coverage."""
    seed_id: int
    gen_num_instrs: int = 0
    emu_num_instrs: int = 0
    success: bool = True
    gen_time: float = 0.0
    emu_time: float = 0.0
    test_start_time: float = 0.0
    test_end_time: float = 0.0
    # Coverage data
    coverage_covered: int = 0
    coverage_total: int = 0
    coverage_percent: float = 0.0


@dataclass
class AblationCoverageResults:
    """Results for a single ablation configuration with coverage."""
    config_name: str
    arch: str
    xlen: int
    cov_type: str  # 'line', 'toggle', or 'expr'
    tests: List[TestRecord] = field(default_factory=list)
    coverage_timeline: List[CoverageRecord] = field(default_factory=list)
    start_time: float = 0.0
    end_time: float = 0.0
    
    def to_dict(self) -> Dict:
        return {
            'config_name': self.config_name,
            'arch': self.arch,
            'xlen': self.xlen,
            'cov_type': self.cov_type,
            'num_tests': len(self.tests),
            'start_time': self.start_time,
            'end_time': self.end_time,
            'duration_seconds': self.end_time - self.start_time if self.end_time else 0,
            'tests': [asdict(t) for t in self.tests],
            'coverage_timeline': [asdict(c) for c in self.coverage_timeline],
        }
    
    def compute_stats(self) -> Dict:
        """Compute summary statistics from test records."""
        if not self.tests:
            return {}
        
        successful_tests = [t for t in self.tests if t.success]
        if not successful_tests:
            return {'num_tests': len(self.tests), 'num_successful': 0}
        
        stats = {
            'num_tests': len(self.tests),
            'num_successful': len(successful_tests),
            'success_rate': len(successful_tests) / len(self.tests) if self.tests else 0,
        }
        
        # Final coverage (last merged value)
        if self.coverage_timeline:
            final_cov = self.coverage_timeline[-1]
            stats['final_coverage'] = {
                'covered': final_cov.covered,
                'total': final_cov.total,
                'percent': final_cov.percent,
            }
        
        return stats
    
    def compute_coverage_over_time(self) -> Tuple[List[float], List[float]]:
        """Compute cumulative coverage over time.
        
        Returns:
            Tuple of (time_points, coverage_percents)
        """
        if not self.coverage_timeline:
            return [], []
        
        time_points = [c.timestamp for c in self.coverage_timeline]
        coverage_percents = [c.percent for c in self.coverage_timeline]
        
        return time_points, coverage_percents


# =============================================================================
# Shared State
# =============================================================================

_procs_lock = threading.Lock()
_procs: Set[subprocess.Popen] = set()
_stop_event = threading.Event()

_stats_lock = threading.Lock()
_seed_counter = 0

# Coverage merge state
_coverage_merge_lock = threading.Lock()
_coverage_files: List[Dict] = []  # List of {'path': Path, 'timestamp': float}
_coverage_paths: Set[Path] = set()
_merged_results: List[Dict] = []
_merged_latest_path: Optional[str] = None
_merge_failed_paths: Set[str] = set()


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


def _get_verilator_cwd(arch: str, fuzzgpu_dir: Path) -> Path:
    """Return verilator working directory based on architecture."""
    if arch == 'ventus':
        return fuzzgpu_dir / 'targets' / 'ventus-env' / 'gpgpu' / 'sim-verilator'
    return fuzzgpu_dir / 'targets' / 'vortex' / 'hw' / 'rtl'


def _build_annotate_prefix(coverage_dat_path: str) -> str:
    """Derive annotate output prefix from coverage dat file path."""
    if coverage_dat_path.endswith('.coverage.dat'):
        return coverage_dat_path[:-len('.coverage.dat')] + '_coverage'
    return f"{coverage_dat_path}_coverage"


def run_coverage_annotate(coverage_dat_path, arch, fuzzgpu_dir, verilator_bin='verilator_coverage', annotate_min=10):
    """Run verilator_coverage --annotate and parse total coverage values."""
    coverage_dat_path = str(coverage_dat_path)
    annotate_prefix = _build_annotate_prefix(coverage_dat_path)
    try:
        result = subprocess.run(
            [verilator_bin, '--annotate', annotate_prefix, '--annotate-min', str(annotate_min), coverage_dat_path],
            capture_output=True,
            text=True,
            cwd=_get_verilator_cwd(arch, fuzzgpu_dir)
        )

        if result.returncode != 0:
            print(f"{RED}verilator_coverage annotate failed with code {result.returncode}{RESET}")
            return None

        combined_output = f"{result.stdout}\n{result.stderr}"
        match = re.search(r"Total coverage\s*\(\s*(\d+)\s*/\s*(\d+)\s*\)\s*([0-9]+\.?[0-9]*)%", combined_output)
        if not match:
            print(f"{YELLOW}Could not find total coverage line in annotate output{RESET}")
            return None

        covered = int(match.group(1))
        total = int(match.group(2))
        percent = float(match.group(3))
        return {
            'covered': covered,
            'total': total,
            'percent': percent,
            'annotate_prefix': annotate_prefix
        }
    except Exception as e:
        print(f"{RED}Failed to annotate coverage: {e}{RESET}")
        return None


def merge_coverage_files(sorted_items, arch, fuzzgpu_dir, merged_dir, verilator_bin='verilator_coverage'):
    """Incrementally merge coverage dat files in timestamp order and return annotated results."""
    global _merged_latest_path, _merged_results

    os.makedirs(merged_dir, exist_ok=True)

    # Build a set of source files that have already been merged
    merged_sources = set(item.get('source_file') for item in _merged_results)
    
    # Find items that need to be merged
    items_to_merge = [
        item for item in sorted_items 
        if str(item['path']) not in merged_sources and str(item['path']) not in _merge_failed_paths
    ]
    
    if not items_to_merge:
        return _merged_results

    prev_merged = _merged_latest_path

    for item in items_to_merge:
        source_path = Path(item['path'])
        source_str = str(source_path)
        
        merge_idx = len(_merged_results) + 1
        merged_path = merged_dir / f'merged_seed_{merge_idx}.coverage.dat'
        
        # Verify source coverage file exists and has content
        if not source_path.exists():
            print(f"{YELLOW}Coverage file does not exist: {source_path}{RESET}")
            _merge_failed_paths.add(source_str)
            continue
        if source_path.stat().st_size == 0:
            print(f"{YELLOW}Coverage file is empty: {source_path}{RESET}")
            _merge_failed_paths.add(source_str)
            continue
        
        inputs = [item['path']] if prev_merged is None else [prev_merged, item['path']]

        try:
            merge_run = subprocess.run(
                [verilator_bin, '--write', str(merged_path)] + [str(p) for p in inputs],
                capture_output=True,
                text=True,
                cwd=_get_verilator_cwd(arch, fuzzgpu_dir)
            )

            if merge_run.returncode != 0:
                print(f"{RED}Failed to merge coverage into {merged_path}{RESET}")
                _merge_failed_paths.add(source_str)
                continue

            ann = run_coverage_annotate(merged_path, arch, fuzzgpu_dir, verilator_bin=verilator_bin)
            if ann:
                _merged_results.append({
                    'timestamp': item['timestamp'],
                    'covered': ann['covered'],
                    'total': ann['total'],
                    'percent': ann['percent'],
                    'merged_file': str(merged_path),
                    'source_file': str(item['path'])
                })
                print(f"  {BLUE}Merged coverage seed {merge_idx}: {ann['covered']}/{ann['total']} ({ann['percent']:.2f}%){RESET}")

                prev_merged = str(merged_path)
                _merged_latest_path = prev_merged
            else:
                _merge_failed_paths.add(source_str)
        except Exception as e:
            print(f"{YELLOW}Error merging coverage: {e}{RESET}")
            _merge_failed_paths.add(source_str)

    return _merged_results


def update_coverage(coverage_dat_path: str, work_dir: Path, arch: str, fuzzgpu_dir: Path, 
                    results: AblationCoverageResults, results_lock: threading.Lock,
                    config_start_time: float, seed_id: int):
    """Annotate and merge coverage, tracking coverage over time."""
    global _coverage_files, _coverage_paths, _merged_results
    
    coverage_dat = Path(coverage_dat_path)
    if not coverage_dat.exists():
        return
    
    timestamp = time.time() - config_start_time  # Relative to config start
    
    merged_dir = work_dir / 'merged_coverage'
    
    with _coverage_merge_lock:
        # Skip if already processed
        if coverage_dat in _coverage_paths:
            return
        
        # Insert in timestamp order
        ts_list = [item['timestamp'] for item in _coverage_files]
        insert_idx = bisect.bisect_left(ts_list, timestamp)
        _coverage_files.insert(insert_idx, {'path': coverage_dat, 'timestamp': timestamp})
        _coverage_paths.add(coverage_dat)
        
        # Merge coverage files
        merged_results = merge_coverage_files(_coverage_files, arch, fuzzgpu_dir, merged_dir)
        
        # Find the entry for this coverage file
        entry = next((item for item in merged_results if item.get('source_file') == str(coverage_dat)), None)
        
        if entry:
            cov_record = CoverageRecord(
                seed_id=seed_id,
                timestamp=entry['timestamp'],
                covered=entry['covered'],
                total=entry['total'],
                percent=entry['percent'],
                merged_file=entry['merged_file'],
                source_file=entry['source_file'],
            )
            with results_lock:
                results.coverage_timeline.append(cov_record)


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


def reset_coverage_state():
    """Reset global coverage merge state for a new configuration."""
    global _coverage_files, _coverage_paths, _merged_results, _merged_latest_path, _merge_failed_paths
    with _coverage_merge_lock:
        _coverage_files = []
        _coverage_paths = set()
        _merged_results = []
        _merged_latest_path = None
        _merge_failed_paths = set()


# =============================================================================
# Ablation Worker
# =============================================================================

def run_ablation_coverage_worker(
    args,
    config_name: str,
    config_extra_args: List[str],
    fuzz_exe: str,
    emu_exe: str,
    work_dir: Path,
    fuzzgpu_dir: Path,
    results: AblationCoverageResults,
    results_lock: threading.Lock,
):
    """Worker for running ablation tests with coverage."""
    global _seed_counter
    
    logs_dir = work_dir / 'logs'
    os.makedirs(logs_dir, exist_ok=True)
    
    while not _stop_event.is_set():
        # Check max_time limit
        if args.max_time is not None:
            elapsed = time.time() - results.start_time
            if elapsed >= args.max_time:
                break
        
        # Get next seed atomically
        with _stats_lock:
            seed = _seed_counter
            if seed >= args.start_seed + args.num_seeds:
                break
            _seed_counter += 1
        
        # Test directory
        test_dir = work_dir / f'testcase_{seed}'
        instr = test_dir / 'instr.bin'
        data = test_dir / 'data.bin'
        aux = test_dir / 'aux.bin'
        jsonf = test_dir / 'meta.json'
        stat_json = test_dir / 'stat.json'
        coverage_dat = test_dir / 'coverage.dat'
        
        fuzz_log = logs_dir / f'testcase_{seed}_gen.log'
        emu_log = logs_dir / f'testcase_{seed}_emu.log'
        
        os.makedirs(test_dir, exist_ok=True)
        
        max_num_instrs = args.force_max_num_instrs or random.randrange(100, 10000)
        
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
            str(instr), str(data), str(aux), str(jsonf)
        ]
        
        # Add config-specific args
        fuzz_cmd.extend(config_extra_args)
        
        print(f"{BLUE}[{config_name}] gen seed {seed}...{RESET}")
        
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
        
        print(f"{BLUE}[{config_name}] emu seed {seed}...{RESET}")

        # Run emulator with coverage
        emu_cmd = [
            emu_exe,
            '--instr-bin', str(instr),
            '--data-bin', str(data),
            '--aux', str(aux),
            '--meta-json', str(jsonf),
            '--stat-jsonfile', str(stat_json),
            '--coverage-dat-file', str(coverage_dat),
        ]
        
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
        except Exception:
            record.success = False
        
        # Update coverage
        if coverage_dat.exists() and record.success:
            update_coverage(
                str(coverage_dat), work_dir, args.arch, fuzzgpu_dir,
                results, results_lock, results.start_time, seed
            )
            
            # Get latest coverage for this test
            with results_lock:
                if results.coverage_timeline:
                    latest = results.coverage_timeline[-1]
                    record.coverage_covered = latest.covered
                    record.coverage_total = latest.total
                    record.coverage_percent = latest.percent
        
        with results_lock:
            results.tests.append(record)
            
        # Print progress
        elapsed = time.time() - results.start_time
        with results_lock:
            num_tests = len(results.tests)
            if results.coverage_timeline:
                latest_cov = results.coverage_timeline[-1]
                print(f"  {GREEN}[{config_name}] seed {seed}: {latest_cov.covered}/{latest_cov.total} ({latest_cov.percent:.2f}%) @ {elapsed:.1f}s{RESET}")


def run_ablation_coverage_config(
    args,
    config_name: str,
    config_extra_args: List[str],
    arch: str,
    xlen: int,
    cov_type: str,
    fuzzgpu_dir: Path,
) -> AblationCoverageResults:
    """Run ablation study for a single configuration with coverage."""
    global _seed_counter
    
    # Reset state
    _seed_counter = args.start_seed
    _stop_event.clear()
    reset_coverage_state()
    
    # Build directory with coverage enabled
    build_dir = fuzzgpu_dir / f"build{xlen}-{arch}-cov{cov_type}-dft1"
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
    work_dir = Path(args.work_dir) / f"ablation_{arch}_x{xlen}_cov{cov_type}" / f"{config_name}_{timestamp}"
    os.makedirs(work_dir, exist_ok=True)
    
    results = AblationCoverageResults(
        config_name=config_name,
        arch=arch,
        xlen=xlen,
        cov_type=cov_type,
        start_time=time.time(),
    )
    results_lock = threading.Lock()
    
    print(f"\n{CYAN}=== Running: {config_name} ({arch}/x{xlen}, {cov_type} coverage) ==={RESET}")
    print(f"  Work dir: {work_dir}")
    print(f"  Extra args: {config_extra_args or '(none)'}")
    
    try:
        if args.jobs == 1:
            run_ablation_coverage_worker(
                args, config_name, config_extra_args,
                fuzz_exe, emu_exe, work_dir, fuzzgpu_dir,
                results, results_lock
            )
        else:
            with ThreadPoolExecutor(max_workers=args.jobs) as executor:
                futures = []
                for worker_id in range(args.jobs):
                    fut = executor.submit(
                        run_ablation_coverage_worker,
                        args, config_name, config_extra_args,
                        fuzz_exe, emu_exe, work_dir, fuzzgpu_dir,
                        results, results_lock
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


def print_summary(all_results: Dict[str, AblationCoverageResults], cov_type: str):
    """Print summary comparison of all ablation configurations."""
    print(f"\n{GREEN}{'='*80}{RESET}")
    print(f"{GREEN}ABLATION COVERAGE SUMMARY ({cov_type} coverage){RESET}")
    print(f"{GREEN}{'='*80}{RESET}\n")
    
    # Table header
    header = f"{'Config':<20} {'Tests':>8} {'Success':>8} {'Covered':>12} {'Total':>12} {'Percent':>12}"
    print(header)
    print("-" * len(header))
    
    for config_name, results in all_results.items():
        if results is None:
            continue
        
        stats = results.compute_stats()
        num_tests = stats.get('num_tests', 0)
        num_success = stats.get('num_successful', 0)
        
        final_cov = stats.get('final_coverage', {})
        covered = final_cov.get('covered', 0)
        total = final_cov.get('total', 0)
        percent = final_cov.get('percent', 0.0)
        
        row = f"{config_name:<20} {num_tests:>8} {num_success:>8} {covered:>12} {total:>12} {percent:>12.2f}%"
        print(row)
    
    print()


def save_results(all_results: Dict[str, AblationCoverageResults], output_dir: Path, 
                 arch: str, xlen: int, cov_type: str):
    """Save all results to JSON files."""
    os.makedirs(output_dir, exist_ok=True)
    
    timestamp = datetime.datetime.now().strftime('%Y%m%d_%H%M%S')
    
    # Save combined results
    combined_file = output_dir / f"ablation_coverage_{arch}_x{xlen}_{cov_type}_{timestamp}.json"
    combined_data = {
        'timestamp': timestamp,
        'arch': arch,
        'xlen': xlen,
        'cov_type': cov_type,
        'configs': {},
    }
    
    for config_name, results in all_results.items():
        if results is None:
            continue
        
        # Compute coverage over time
        time_points, coverage_percents = results.compute_coverage_over_time()
        
        combined_data['configs'][config_name] = {
            'results': results.to_dict(),
            'stats': results.compute_stats(),
            'coverage_over_time': {
                'time_points': time_points,
                'coverage_percents': coverage_percents,
            },
        }
    
    with open(combined_file, 'w') as f:
        json.dump(combined_data, f, indent=2)
    
    print(f"{GREEN}Results saved to: {combined_file}{RESET}")
    return combined_file


def main():
    parser = argparse.ArgumentParser(
        description='Ablation study for FuzzGPU - Coverage Metrics'
    )
    
    # Architecture selection
    parser.add_argument('--arch', type=str, choices=['ventus', 'vortex'], default='ventus',
                        help='Target architecture (default: ventus)')
    parser.add_argument('--xlen', type=int, choices=[32, 64], default=32,
                        help='XLEN (default: 32)')
    
    # Coverage type
    parser.add_argument('--cov-type', type=str, choices=['line', 'toggle', 'expr'], required=True,
                        help='Coverage type to measure')
    
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
    parser.add_argument('--jobs', type=int, default=4,
                        help='Number of parallel jobs (default: 4)')
    parser.add_argument('--work-dir', type=str, default='/tmp/ablation_coverage',
                        help='Work directory for temp files (default: /tmp/ablation_coverage)')
    parser.add_argument('--output-dir', type=str, default=None,
                        help='Output directory for results (default: results/ablation_coverage)')
    parser.add_argument('--no-log', action='store_false', dest='record_logs',
                        help='Do not record logs')
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
        output_dir = fuzzgpu_dir / 'results' / 'ablation_coverage'
    
    # Setup signal handlers
    signal.signal(signal.SIGINT, _handle_signal)
    signal.signal(signal.SIGTERM, _handle_signal)
    
    print(f"{CYAN}FuzzGPU Ablation Study - Coverage Metrics{RESET}")
    print(f"  Configs: {list(configs_to_run.keys())}")
    print(f"  Arch: {args.arch}, XLEN: {args.xlen}")
    print(f"  Coverage type: {args.cov_type}")
    print(f"  Num seeds: {args.num_seeds}")
    print(f"  Start seed: {args.start_seed}")
    
    all_results = {}
    
    for config_name, config_extra_args in configs_to_run.items():
        if _stop_event.is_set():
            break
        
        results = run_ablation_coverage_config(
            args, config_name, config_extra_args,
            args.arch, args.xlen, args.cov_type, fuzzgpu_dir
        )
        all_results[config_name] = results
    
    # Print and save summary
    print_summary(all_results, args.cov_type)
    save_results(all_results, output_dir, args.arch, args.xlen, args.cov_type)
    
    print(f"\n{GREEN}Ablation coverage study complete!{RESET}")


if __name__ == '__main__':
    main()
