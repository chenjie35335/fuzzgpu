#!/usr/bin/env python3
"""Baseline comparison of fuzzgpu, cascade, and divefuzz fuzzing methods.

Usage examples:
  python3 scripts/baseline.py --method fuzzgpu --jobs 4 --target-instr-count 1000000
  python3 scripts/baseline.py --method cascade --jobs 4 --target-time 3600
  python3 scripts/baseline.py --method divefuzz --jobs 2 --target-instr-count 500000

Compares coverage over time for different fuzzing methods.
"""

import argparse
import os
import signal
import subprocess
import sys
import threading
import time
import shutil
from shutil import which
import statistics
import json
import datetime
import re
import random
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor, as_completed
import math
import bisect
import traceback

# Import count_instructions from DiveFuzz mutate_instructions module
sys.path.insert(0, str(Path(__file__).parent.parent / 'baseline' / 'DiveFuzz' / 'fuzzer'))
from generator.core.mutator.mutate_instructions import count_instructions

# Import bug detector
from bug_detector import scan_file

GREEN = "\033[32m"
RED = "\033[31m"
YELLOW = "\033[33m"
BLUE = "\033[34m"
RESET = "\033[0m"

# Constants
EMULATOR_TIMEOUT_SECONDS = 300  # 5 minutes timeout for emulator
BUG_DETECTOR_THRESHOLD = 1  # Threshold to add bug to filter list
MAX_LOG_FILE_SIZE_BYTES = 200 * 1024 * 1024  # 200MB max log file size
SUPPORTED_GENERATOR_BUG_FILTERS = {
    "amo_dec",
    "var_thread_num",
    "csr_tid",
    "vmv_x_s",
    "vmask",
    "vec_nfield",
    "vftta_vv_tmask",
    "inactive_warp_barrier",
    "vx_wmma",
    "wb_fp_rd_zero",
    "wb_zero",
    "jalr_align",
    "global_bar",
}

# Shared state for running child processes
_procs_lock = threading.Lock()
_procs = set()
_stop_event = threading.Event()

# Shared state for instruction counting and coverage
_stats_lock = threading.Lock()
_total_instrs = 0
_start_time = None
_coverage_data = []  # List of coverage snapshots ordered by timestamp
_coverage_files = []  # List of {'path': Path, 'timestamp': float}
_coverage_paths = set()
_coverage_merge_lock = threading.Lock()
_merged_results = []  # Accumulated merged coverage entries
_merged_latest_path = None
_merged_latest_prefix = None
_merge_failed_paths = set()  # Set of paths that failed to merge (to avoid infinite retries)
_seed_counter = 0  # Global seed counter for fuzzgpu
_run_counter = 0  # Global run counter for cascade/divefuzz
_runtime_stats = {
    'gen_time': 0.0,
    'emu_time': 0.0,
    'iterations': 0
}

# Directory separation:
#   _results_dir: for logs, coverage data, results.json (persistent, in project/results/)
#   _work_dir: for temp files like instr.bin/data.bin/aux.bin/elf (in /tmp, avoids SSD wear)
_results_dir = None
_work_dir = None

# Shared state for bug detection and filtering
_bug_detector_lock = threading.Lock()
_bug_counter = {}  # Dict mapping bug_name -> count
_bug_filter_list = set()  # Set of bug names to filter
_bug_stats_file = None  # Path to bug stats JSON file
_bug_filter_file = None  # Path to bug filter JSON file


def _initialize_bug_detector_files(method_dir):
    """Initialize bug detector state files."""
    global _bug_stats_file, _bug_filter_file
    _bug_stats_file = method_dir / 'bug_stats.json'
    _bug_filter_file = method_dir / 'bug_filter.json'
    
    # Load existing bug stats if available
    if _bug_stats_file.exists():
        try:
            with open(_bug_stats_file, 'r') as f:
                _bug_counter.update(json.load(f))
        except Exception as e:
            print(f"  {YELLOW}Warning: failed to load bug stats: {e}{RESET}")
    
    # Load existing bug filter if available
    if _bug_filter_file.exists():
        try:
            with open(_bug_filter_file, 'r') as f:
                data = json.load(f)
                _bug_filter_list.update(
                    bug_name for bug_name in data.get('filtered_bugs', [])
                    if bug_name in SUPPORTED_GENERATOR_BUG_FILTERS
                )
        except Exception as e:
            print(f"  {YELLOW}Warning: failed to load bug filter: {e}{RESET}")


def _save_bug_detector_state():
    """Save current bug detector state to JSON files."""
    if _bug_stats_file is None or _bug_filter_file is None:
        return
    
    try:
        with open(_bug_stats_file, 'w') as f:
            json.dump(_bug_counter, f, indent=2)
    except Exception as e:
        print(f"  {YELLOW}Warning: failed to save bug stats: {e}{RESET}")
    
    try:
        with open(_bug_filter_file, 'w') as f:
            json.dump({'filtered_bugs': sorted(_bug_filter_list)}, f, indent=2)
    except Exception as e:
        print(f"  {YELLOW}Warning: failed to save bug filter: {e}{RESET}")


def _cascade_elf_num_threads(elf_out_dir: Path, elf_name: str, default_num_threads: int) -> int:
    active_threads_file = elf_out_dir / f'{elf_name}_active_threads.txt'
    if not active_threads_file.exists():
        return default_num_threads
    try:
        active_threads = int(active_threads_file.read_text().strip())
    except Exception:
        return default_num_threads
    return active_threads if active_threads > 0 else default_num_threads


def _process_bug_detections(emu_log_path):
    """Run bug detector on emulator log and update bug statistics."""
    global _bug_counter, _bug_filter_list
    
    try:
        found_bugs, _ = scan_file(Path(emu_log_path))
        if not found_bugs:
            return
        
        with _bug_detector_lock:
            for match in found_bugs:
                bug_name = match.bug_name
                _bug_counter[bug_name] = _bug_counter.get(bug_name, 0) + 1
                current_count = _bug_counter[bug_name]
                if bug_name not in SUPPORTED_GENERATOR_BUG_FILTERS:
                    continue
                
                # Check if we should add to filter list
                if current_count >= BUG_DETECTOR_THRESHOLD and bug_name not in _bug_filter_list:
                    _bug_filter_list.add(bug_name)
                    print(f"    {GREEN}Bug '{bug_name}' detected {current_count} times, adding to filter list{RESET}")
                elif current_count < BUG_DETECTOR_THRESHOLD:
                    print(f"    {GREEN}Bug '{bug_name}' detected ({current_count}/{BUG_DETECTOR_THRESHOLD}){RESET}")
            
            _save_bug_detector_state()
    except Exception as e:
        print(f"  {YELLOW}Warning: bug detection failed: {e}{RESET}")


def _format_eta(seconds):
    if seconds is None:
        return "N/A"
    if seconds == 0:
        return "0s"
    if seconds < 0 or math.isinf(seconds):
        return "N/A"
    mins, secs = divmod(int(seconds + 0.5), 60)
    hours, mins = divmod(mins, 60)
    if hours:
        return f"{hours}h{mins:02d}m"
    if mins:
        return f"{mins}m{secs:02d}s"
    return f"{secs}s"


def _record_and_summarize(gen_time, emu_time, target_instr_count):
    """Accumulate timing stats and estimate remaining time to target instructions."""
    with _stats_lock:
        _runtime_stats['gen_time'] += max(gen_time, 0.0)
        _runtime_stats['emu_time'] += max(emu_time, 0.0)
        _runtime_stats['iterations'] += 1

        iters = _runtime_stats['iterations']
        avg_gen = _runtime_stats['gen_time'] / iters if iters else 0.0
        avg_emu = _runtime_stats['emu_time'] / iters if iters else 0.0

        elapsed = time.time() - _start_time if _start_time else 0.0
        instrs = _total_instrs
        ips = instrs / elapsed if elapsed > 0 else 0.0

        remaining_instrs = None
        remaining_time = None
        if target_instr_count:
            remaining_instrs = max(target_instr_count - instrs, 0)
            if ips > 0 and remaining_instrs > 0:
                remaining_time = remaining_instrs / ips
            elif remaining_instrs == 0:
                remaining_time = 0.0

        return {
            'iterations': iters,
            'avg_gen': avg_gen,
            'avg_emu': avg_emu,
            'ips': ips,
            'elapsed': elapsed,
            'remaining_instrs': remaining_instrs,
            'remaining_time': remaining_time,
        }


def _print_progress(tag, summary):
    eta = _format_eta(summary.get('remaining_time'))
    ips = summary.get('ips', 0.0)
    avg_gen = summary.get('avg_gen', 0.0)
    avg_emu = summary.get('avg_emu', 0.0)
    rem_instrs = summary.get('remaining_instrs')
    rem_txt = f", remaining instrs {rem_instrs}" if rem_instrs is not None else ""
    print(f"  {BLUE}{tag}: avg gen {avg_gen:.2f}s, avg emu {avg_emu:.2f}s, throughput {ips:.1f} instr/s, ETA {eta}{rem_txt}{RESET}")

def _wait_for_processes(timeout=30):
    """Wait for all child processes to terminate."""
    import time as time_module
    start = time_module.time()
    while time_module.time() - start < timeout:
        with _procs_lock:
            if not _procs:
                return True
            active_procs = [p for p in _procs if p.poll() is None]
        if not active_procs:
            return True
        time_module.sleep(0.1)
    return False


def _handle_signal(signum, frame):
    try:
        print(f"{YELLOW}\nReceived signal {signum}, terminating children...{RESET}")
    except Exception:
        pass
    _stop_event.set()
    
    # Terminate all child processes gracefully first
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
    
    # Wait briefly for graceful termination
    time.sleep(0.5)
    
    # Force kill any remaining processes
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
    
    # Wait for processes to actually exit
    _wait_for_processes(timeout=5)
    
    try:
        print(f"{YELLOW}All child processes terminated{RESET}")
    except Exception:
        pass
    
    sys.exit(1)


def run_command(cmd, logfile_path, timeout=None, env=None, echo=False, record_logs=True, max_log_size=None):
    """Run command and stream output to logfile (or discard). Returns (returncode, elapsed_time).
    
    Args:
        cmd: Command to run
        logfile_path: Path to write log output
        timeout: Timeout in seconds (None = no timeout)
        env: Environment variables
        echo: Whether to print the command
        record_logs: Whether to record logs to file
        max_log_size: Maximum log file size in bytes (None = use global MAX_LOG_FILE_SIZE_BYTES)
    
    Returns:
        Tuple of (returncode, elapsed_time) where:
        - returncode: 0 = success, -1 = stopped, -2 = timeout, -3 = process vanished, -4 = log size exceeded
    """
    if echo:
        print(f"  Running: {' '.join(map(str, cmd))}")
    
    if max_log_size is None:
        max_log_size = MAX_LOG_FILE_SIZE_BYTES

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
        with _procs_lock:
            _procs.add(proc)
        start = time.time()
        try:
            while True:
                # Use poll() - non-blocking, returns None if still running
                rc = proc.poll()
                if rc is not None:
                    break
                # Check if process still exists (handles external kills/zombies)
                try:
                    os.kill(proc.pid, 0)
                except ProcessLookupError:
                    # Process gone, force collect status
                    rc = proc.poll()
                    if rc is None:
                        rc = -3  # Process vanished
                    break
                except PermissionError:
                    pass  # Process exists but we can't signal it
                
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
                # Check log file size
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
                            # Append truncation notice to log
                            lf_handle.write(f"\n\n{'='*80}\n# LOG TRUNCATED: File size exceeded {max_log_size / (1024*1024):.0f}MB limit\n".encode('utf-8'))
                            return -4, time.time() - start
                    except Exception:
                        pass  # Ignore errors checking file size
                time.sleep(0.1)
            elapsed = time.time() - start
            return rc, elapsed
        finally:
            with _procs_lock:
                _procs.discard(proc)
    finally:
        if lf_handle is not None:
            try:
                lf_handle.close()
            except Exception:
                pass


def find_executable(candidate):
    if os.path.exists(candidate) and os.access(candidate, os.X_OK):
        return candidate
    p = which(candidate)
    return p


def _get_verilator_cwd(arch, fuzzgpu_dir: Path):
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
            print(f"{RED}stdout: {result.stdout[:200]}{RESET}")
            print(f"{RED}stderr: {result.stderr[:200]}{RESET}")
            return None

        combined_output = f"{result.stdout}\n{result.stderr}"
        match = re.search(r"Total coverage\s*\(\s*(\d+)\s*/\s*(\d+)\s*\)\s*([0-9]+\.?[0-9]*)%", combined_output)
        if not match:
            print(f"{YELLOW}Could not find total coverage line in annotate output for {coverage_dat_path}{RESET}")
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
        print(f"{RED}Failed to annotate coverage for {coverage_dat_path}: {e}{RESET}")
        import traceback
        traceback.print_exc()
        return None


def _cleanup_previous_merged(prev_path, prev_prefix):
    """Remove previous merged coverage artifacts except the latest one."""
    if prev_path and Path(prev_path).exists():
        try:
            resolved_prev = Path(prev_path).resolve()
            if str(resolved_prev).startswith('/tmp'):
                resolved_prev.unlink()
            else:
                print(f"{YELLOW}Skip deleting merged file outside /tmp: {resolved_prev}{RESET}")
        except Exception:
            pass
    if prev_prefix:
        ann_dir = Path(prev_prefix)
        # Safety: only remove annotate directory if under /tmp to avoid accidental deletion
        if ann_dir.exists():
            try:
                if str(ann_dir.resolve()).startswith('/tmp'):
                    shutil.rmtree(ann_dir)
                else:
                    print(f"{YELLOW}Skip deleting annotate dir outside /tmp: {ann_dir}{RESET}")
            except Exception:
                pass


def merge_coverage_files(sorted_items, arch, fuzzgpu_dir, method_dir, verilator_bin='verilator_coverage', force_start_idx=None):
    """Incrementally merge coverage dat files in timestamp order and return annotated results.
    
    Args:
        force_start_idx: If provided, invalidate merged results from this index and re-merge.
                        Used when a file is inserted before the current merge position.
    """
    global _merged_latest_path, _merged_latest_prefix, _merged_results

    merged_dir = Path(method_dir) / 'merged_coverage'
    os.makedirs(merged_dir, exist_ok=True)

    # Handle re-merge from an earlier position
    if force_start_idx is not None and force_start_idx < len(_merged_results):
        # Invalidate merged results from force_start_idx onwards
        # Keep the results before force_start_idx to preserve coverage history
        _merged_results = _merged_results[:force_start_idx]
        # Reset latest merged path to the one before force_start_idx
        if force_start_idx > 0:
            _merged_latest_path = _merged_results[force_start_idx - 1].get('merged_file')
            _merged_latest_prefix = _build_annotate_prefix(_merged_latest_path) if _merged_latest_path else None
            # Verify the file still exists
            if not _merged_latest_path or not Path(_merged_latest_path).exists():
                print(f"  {RED}Error: Previous merged file at position {force_start_idx-1} not found: {_merged_latest_path}{RESET}")
                print(f"  {RED}This should not happen if merged files are preserved.{RESET}")
                return _merged_results
        else:
            _merged_latest_path = None
            _merged_latest_prefix = None

    # Build a set of source files that have already been merged
    merged_sources = set(item.get('source_file') for item in _merged_results)
    
    # Find items that need to be merged (not yet in merged_sources and not failed)
    items_to_merge = [
        item for item in sorted_items 
        if str(item['path']) not in merged_sources and str(item['path']) not in _merge_failed_paths
    ]
    
    if not items_to_merge:
        return _merged_results

    prev_merged = _merged_latest_path
    prev_prefix = _merged_latest_prefix

    for item in items_to_merge:
        source_path = Path(item['path'])
        source_str = str(source_path)
        
        # Use a unique merge file name based on current results count to avoid gaps
        merge_idx = len(_merged_results) + 1
        merged_path = merged_dir / f'merged_seed_{merge_idx}.coverage.dat'
        
        # Verify source coverage file exists and has content
        if not source_path.exists():
            print(f"{YELLOW}Coverage file does not exist: {source_path}, marking as failed{RESET}")
            _merge_failed_paths.add(source_str)
            continue
        if source_path.stat().st_size == 0:
            print(f"{YELLOW}Coverage file is empty: {source_path}, marking as failed{RESET}")
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
                print(f"{RED}Failed to merge coverage into {merged_path} (seed idx {merge_idx}){RESET}")
                print(f"{RED}stdout: {merge_run.stdout[:200]}{RESET}")
                print(f"{RED}stderr: {merge_run.stderr[:200]}{RESET}")
                _merge_failed_paths.add(source_str)
                continue

            ann = run_coverage_annotate(merged_path, arch, fuzzgpu_dir, verilator_bin=verilator_bin, annotate_min=10)
            if ann:
                _merged_results.append({
                    'timestamp': item['timestamp'],
                    'covered': ann['covered'],
                    'total': ann['total'],
                    'percent': ann['percent'],
                    'merged_file': str(merged_path),
                    'source_file': str(item['path'])
                })
                print(f"  {BLUE}Merged coverage seed {merge_idx}: {ann['covered']}/{ann['total']} ({ann['percent']:.2f}%) at {item['timestamp']:.2f}s{RESET}")

                # Do NOT clean up previous merged files to support re-merging and ensure monotonic coverage
                # These files are in /tmp and will be cleaned up on system restart
                
                prev_merged = str(merged_path)
                prev_prefix = ann.get('annotate_prefix')
                _merged_latest_path = prev_merged
                _merged_latest_prefix = prev_prefix
            else:
                # Annotation failed
                print(f"{YELLOW}Failed to annotate merged coverage for {source_path}{RESET}")
                _merge_failed_paths.add(source_str)
        except Exception as e:
            print(f"{YELLOW}Error merging coverage for seed idx {merge_idx}: {e}{RESET}")
            _merge_failed_paths.add(source_str)

    return _merged_results


def update_coverage(coverage_dat_path, method_dir, arch, fuzzgpu_dir):
    """Annotate and merge coverage, tracking absolute counts and percentage over time."""
    coverage_dat = Path(coverage_dat_path)
    time_file = f"{coverage_dat_path}.time"
    if not os.path.exists(time_file):
        print(f"{YELLOW}Timestamp file missing for {coverage_dat}{RESET}")
        return

    try:
        with open(time_file, 'r') as f:
            timestamp = float(f.read().strip())
    except Exception as e:
        print(f"{YELLOW}Failed to read timestamp for {coverage_dat}: {e}{RESET}")
        return

    ########## Merge Coverage Critical Begin ##########
    with _coverage_merge_lock:
        # Skip if already marked as failed
        if str(coverage_dat) in _merge_failed_paths:
            return
        
        already_recorded = coverage_dat in _coverage_paths
        existing_entry = next((item for item in _merged_results if item.get('source_file') == str(coverage_dat)), None)

        inserted = False
        insert_idx = None
        force_start_idx = None
        if not already_recorded:
            ts_list = [item['timestamp'] for item in _coverage_files]
            insert_idx = bisect.bisect_left(ts_list, timestamp)
            _coverage_files.insert(insert_idx, {'path': coverage_dat, 'timestamp': timestamp})
            _coverage_paths.add(coverage_dat)
            inserted = True
            
            # Check if we inserted before the current merge position
            # If so, we need to re-merge from the insertion point
            if insert_idx < len(_merged_results):
                force_start_idx = insert_idx
                print(f"  {YELLOW}Coverage file inserted at position {insert_idx}, triggering re-merge from that point{RESET}")

        if already_recorded and existing_entry:
            print(f"{BLUE}Coverage already merged for {coverage_dat.name}, skipping re-merge{RESET}")
            return

        merged_results = merge_coverage_files(_coverage_files, arch, fuzzgpu_dir, method_dir, force_start_idx=force_start_idx)

        entry = next((item for item in merged_results if item.get('source_file') == str(coverage_dat)), None)

        # Check if file was marked as failed during merge
        if str(coverage_dat) in _merge_failed_paths:
            # Remove from tracking since it's now marked as permanently failed
            _coverage_paths.discard(coverage_dat)
            for i, item in enumerate(_coverage_files):
                if item['path'] == coverage_dat:
                    _coverage_files.pop(i)
                    break
            return
        
        if entry is None and inserted:
            # Merge failed but not permanently; roll back the inserted coverage entry to allow retry later
            _coverage_paths.discard(coverage_dat)
            for i, item in enumerate(_coverage_files):
                if item['path'] == coverage_dat:
                    _coverage_files.pop(i)
                    break
        if entry:
            coverage_val_file = f"{coverage_dat_path}.val"
            with open(coverage_val_file, 'w') as f:
                json.dump({
                    'covered': entry['covered'],
                    'total': entry['total'],
                    'percent': entry['percent']
                }, f)
            print(f"  {BLUE}Coverage at {timestamp:.2f}s: {entry['covered']}/{entry['total']} ({entry['percent']:.2f}%) -> merged {Path(entry['merged_file']).name}{RESET}")
        else:
            print(f"{YELLOW}No merged entry found for {coverage_dat}{RESET}")

        with _stats_lock:
            global _coverage_data
            _coverage_data = merged_results
    ########## Merge Coverage Critical End ##########

def check_stop_conditions(target_instr_count, target_time):
    """Check if we should stop fuzzing based on instruction count or time."""
    with _stats_lock:
        if target_instr_count and _total_instrs >= target_instr_count:
            _stop_event.set()
            return True, f"instruction count {_total_instrs} >= {target_instr_count}"
        if target_time and _start_time:
            elapsed = time.time() - _start_time
            if elapsed >= target_time:
                _stop_event.set()
                return True, f"time {elapsed:.1f}s >= {target_time}s"
    return False, None


def run_fuzzgpu_worker(args, worker_id, fuzz_exe, emu_exe, work_dir, results_dir, fuzzgpu_dir):
    """Worker for fuzzgpu method.
    
    File locations:
      - Temp files (instr.bin, data.bin, aux.bin, meta.json, stat.json, logs, coverage.dat): work_dir (in /tmp)
      - Persistent data (results.json, merged coverage): results_dir
    """
    global _seed_counter
    
    # Create logs directory in work_dir (temp, in /tmp)
    logs_dir = work_dir / 'logs'
    os.makedirs(logs_dir, exist_ok=True)
    
    while not _stop_event.is_set():
        should_stop, reason = check_stop_conditions(args.target_instr_count, args.target_time)
        if should_stop:
            print(f"  {YELLOW}Worker {worker_id}: stopping ({reason}){RESET}")
            break
        
        with _stats_lock:
            seed = _seed_counter
            _seed_counter += 1
        
        # Temp files in work_dir (avoid SSD wear)
        test_dir = work_dir / f'testcase_{seed}'
        instr = test_dir / 'instr.bin'
        data = test_dir / 'data.bin'
        aux = test_dir / 'aux.bin'
        jsonf = test_dir / 'meta.json'
        stat_json = test_dir / 'stat.json'
        coverage_dat = test_dir / 'coverage.dat'  # Temp coverage in work_dir
        
        # Persistent logs in results_dir/logs/
        fuzz_log = logs_dir / f'testcase_{seed}_gen.log'
        emu_log = logs_dir / f'testcase_{seed}_emu.log'
        
        os.makedirs(test_dir, exist_ok=True)
        
        max_num_instrs = random.randrange(2000, 10000)

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
        
        # Add bug filter JSON if available
        if _bug_filter_file and Path(_bug_filter_file).exists():
            fuzz_cmd.extend(['--bug-filter-json', str(_bug_filter_file)])
        
        if args.disable_emu_guide:
            fuzz_cmd.append('--disable-emu-guide')
        
        # Save command line
        cmd_log = logs_dir / f'testcase_{seed}_fuzz_cmd.sh'
        with open(cmd_log, 'w') as f:
            f.write('#!/bin/bash\n')
            f.write(' '.join(fuzz_cmd) + '\n')
        
        rc1, t1 = run_command(fuzz_cmd, str(fuzz_log), record_logs=args.record_logs)
        if rc1 != 0:
            print(f"  {RED}Worker {worker_id}: fuzz failed for seed {seed}, see {fuzz_log}{RESET}")
            continue
        
        # Count generated instructions from metadata
        try:
            with open(jsonf, 'r') as f:
                meta = json.load(f)
                num_instrs = meta.get('num_instrs', 0)
                with _stats_lock:
                    global _total_instrs
                    _total_instrs += num_instrs
        except Exception as e:
            print(f"  {YELLOW}Worker {worker_id}: failed to read metadata: {e}{RESET}")
        
        # Run emulator
        emu_cmd = [
            emu_exe,
            '--instr-bin', str(instr),
            '--data-bin', str(data),
            '--aux', str(aux),
            '--meta-json', str(jsonf),
            '--coverage-dat-file', str(coverage_dat),
            '--stat-jsonfile', str(stat_json)
        ]
        if args.emulate:
            emu_cmd.append('--emulate')

        emu_cmd_log = logs_dir / f'testcase_{seed}_emu_cmd.sh'
        with open(emu_cmd_log, 'w') as f:
            f.write('#!/bin/bash\n')
            f.write(' '.join(emu_cmd) + '\n')
        
        # Write start timestamp before first simulation
        if not (results_dir / 'start.time').exists():
            with _stats_lock:
                global _start_time
                if _start_time is None:
                    _start_time = time.time()
                    with open(results_dir / 'start.time', 'w') as f:
                        f.write(f"{_start_time}\n")
        
        rc2, t2 = run_command(emu_cmd, str(emu_log), timeout=EMULATOR_TIMEOUT_SECONDS, record_logs=args.record_logs)
        
        # Write timestamp for coverage
        with open(f"{coverage_dat}.time", 'w') as f:
            f.write(f"{time.time()}\n")
        
        # Update coverage (merge in work_dir)
        update_coverage(str(coverage_dat), work_dir, args.arch, fuzzgpu_dir)

        # Run bug detector on emulator log
        _process_bug_detections(str(emu_log))

        print(f"  {GREEN}Worker {worker_id}: completed seed {seed}{RESET}")

        summary = _record_and_summarize(t1, t2, args.target_instr_count)
        if summary:
            _print_progress(f"Worker {worker_id} seed {seed}", summary)
        
        # Cleanup temp files for this iteration (logs, coverage source, test files)
        if not args.no_cleanup_temp:
            try:
                # Remove test directory (instr.bin, data.bin, aux.bin, meta.json, stat.json, coverage.dat)
                if test_dir.exists():
                    shutil.rmtree(test_dir)
                # Remove log files for this seed
                for log_pattern in [f'testcase_{seed}_*.log', f'testcase_{seed}_*.sh']:
                    for log_file in logs_dir.glob(log_pattern):
                        log_file.unlink(missing_ok=True)
            except Exception as e:
                print(f"  {YELLOW}Worker {worker_id}: cleanup failed for seed {seed}: {e}{RESET}")


def run_cascade_worker(args, worker_id, emu_exe, work_dir, results_dir, gen_script, arch_config, fuzzgpu_dir):
    """Worker for cascade method.
    
    File locations:
      - Temp files (elf, gen.log, emu.log, stat.json, coverage.dat): work_dir (in /tmp)
      - Persistent data (results.json, merged coverage): results_dir
    """
    global _run_counter
    
    # Create logs directory in work_dir (temp, in /tmp)
    logs_dir = work_dir / 'logs'
    os.makedirs(logs_dir, exist_ok=True)
    
    while not _stop_event.is_set():
        should_stop, reason = check_stop_conditions(args.target_instr_count, args.target_time)
        if should_stop:
            print(f"  {YELLOW}Worker {worker_id}: stopping ({reason}){RESET}")
            break
        
        with _stats_lock:
            run_id = _run_counter
            _run_counter += 1
        
        # Temp files in work_dir
        run_dir = work_dir / f'run_{run_id}'
        elf_out_dir = run_dir / 'elf'
        gen_log = logs_dir / f'run_{run_id}_gen.log'
        os.makedirs(elf_out_dir, exist_ok=True)
        total_emu_time = 0.0

        if args.arch == 'ventus':
            num_warps_per_core = args.num_genwarps_per_block
        elif args.arch == 'vortex':
            num_warps_per_core = arch_config['vortex_nr_warps_per_core']
        
        # For cascade, vortex design_name includes xlen (e.g., vortex32, vortex64)
        design_name = f"{args.arch}{args.xlen}" if args.arch == 'vortex' else args.arch

        gen_cmd = [
            sys.executable, str(gen_script),
            str(args.cascade_num_seeds),
            str(elf_out_dir),
            '--num-warps-per-core', str(num_warps_per_core),
            '--num-threads-per-warp', str(arch_config['nr_threads_per_warp']),
            '--arch', design_name,
            '--emulator-path', str(emu_exe),
            '--seed-id', str(run_id)
        ]
        if _bug_filter_file and Path(_bug_filter_file).exists():
            gen_cmd.extend(['--bug-filter-json', str(_bug_filter_file)])
        
        if args.arch == 'vortex':
            gen_cmd += [
                '--vortex-max-num-barrier', str(arch_config['num_barriers']),
                '--vortex-max-barrier-count', str(max(arch_config['vortex_nr_warps_per_core'], arch_config['vortex_nr_cores_per_cluster'])),
                '--vortex-cores-per-clusters', str(arch_config['vortex_nr_cores_per_cluster'])
            ]
        
        # Save command line
        cascade_cmd_log = logs_dir / f'run_{run_id}_gen_cmd.sh'
        with open(cascade_cmd_log, 'w') as f:
            f.write('#!/bin/bash\n')
            f.write(' '.join(gen_cmd) + '\n')
        
        gen_env = os.environ.copy()
        rc1, t1 = run_command(gen_cmd, str(gen_log), env=gen_env, echo=False, record_logs=args.record_logs)
        if rc1 != 0:
            print(f"  {RED}Worker {worker_id}: cascade generation failed for run {run_id}, see {gen_log}{RESET}")
            continue
        
        # Write start timestamp before first simulation
        if not (results_dir / 'start.time').exists():
            with _stats_lock:
                global _start_time
                if _start_time is None:
                    _start_time = time.time()
                    with open(results_dir / 'start.time', 'w') as f:
                        f.write(f"{_start_time}\n")
        
        # Run emulator on each generated elf
        elf_files = list(elf_out_dir.glob('*.elf'))
        for elf_path in elf_files:
            if _stop_event.is_set():
                break
            
            elf_name = elf_path.stem
            # Persistent logs in results_dir/logs/, temp coverage in work_dir
            emu_log = logs_dir / f'run_{run_id}_{elf_name}_emu.log'
            coverage_dat = run_dir / f'{elf_name}.coverage.dat'  # Temp coverage in work_dir
            stat_json = run_dir / f'{elf_name}.stat.json'
            
            emu_cmd = [
                emu_exe,
                '--elf', str(elf_path),
                '--elf-num-genblocks', str(args.num_genblocks),
                '--elf-num-genwarps-per-block', str(args.num_genwarps_per_block),
                '--elf-num-threads-per-warp', str(_cascade_elf_num_threads(elf_out_dir, elf_name, arch_config['nr_threads_per_warp'])),
                '--stat-jsonfile', str(stat_json),
                '--coverage-dat-file', str(coverage_dat)
            ]
            if args.emulate:
                emu_cmd.append('--emulate')

            cascade_emu_cmd_log = logs_dir / f'run_{run_id}_{elf_name}_emu_cmd.sh'
            with open(cascade_emu_cmd_log, 'w') as f:
                f.write('#!/bin/bash\n')
                f.write(' '.join(emu_cmd) + '\n')
            
            rc2, t2 = run_command(emu_cmd, str(emu_log), timeout=EMULATOR_TIMEOUT_SECONDS, record_logs=args.record_logs)
            total_emu_time += t2
            
            # Write timestamp for coverage
            with open(f"{coverage_dat}.time", 'w') as f:
                f.write(f"{time.time()}\n")
            
            # Count instructions from _numinstrs.txt file
            numinstrs_file = elf_out_dir / f'{elf_name}_numinstrs.txt'
            try:
                assert numinstrs_file.exists()
                with open(numinstrs_file, 'r') as f:
                    num_instrs_hex = f.read().strip()
                    num_instrs = int(num_instrs_hex, 16)  # Parse hex value
                    with _stats_lock:
                        global _total_instrs
                        _total_instrs += num_instrs
            except Exception as e:
                print(f"  {YELLOW}Worker {worker_id}: failed to read instruction count for {elf_name}: {e}{RESET}")
            
            # Update coverage (merge in work_dir)
            update_coverage(str(coverage_dat), work_dir, args.arch, fuzzgpu_dir)
            
            # Run bug detector on emulator log
            _process_bug_detections(str(emu_log))
        
        print(f"  {GREEN}Worker {worker_id}: completed run {run_id} ({len(elf_files)} tests){RESET}")

        summary = _record_and_summarize(t1, total_emu_time, args.target_instr_count)
        if summary:
            _print_progress(f"Worker {worker_id} run {run_id}", summary)
        
        # Cleanup temp files for this iteration (run_dir, logs)
        if not args.no_cleanup_temp:
            try:
                # Remove run directory (elf files, coverage.dat, stat.json)
                if run_dir.exists():
                    shutil.rmtree(run_dir)
                # Remove log files for this run
                for log_pattern in [f'run_{run_id}_*.log', f'run_{run_id}_*.sh']:
                    for log_file in logs_dir.glob(log_pattern):
                        log_file.unlink(missing_ok=True)
            except Exception as e:
                print(f"  {YELLOW}Worker {worker_id}: cleanup failed for run {run_id}: {e}{RESET}")


def run_divefuzz_worker(args, worker_id, emu_exe, work_dir, results_dir, divefuzz_dir, xor_cache_file, wrapper_script, arch_config, fuzzgpu_dir):
    """Worker for divefuzz method - uses subprocess to generate seeds in parallel.
    
    File locations:
      - Temp files (seeds, elf, gen.json, stat.json, logs, coverage.dat): work_dir (in /tmp)
      - Persistent data (results.json, merged coverage): results_dir
    """
    global _run_counter
    
    # Create logs directory in work_dir (temp, in /tmp)
    logs_dir = work_dir / 'logs'
    os.makedirs(logs_dir, exist_ok=True)
    
    # Shared output directory for all workers (temp files in work_dir)
    shared_out_dir = work_dir / 'seeds'
    shared_elf_dir = work_dir / 'elf_file'
    os.makedirs(shared_out_dir, exist_ok=True)
    os.makedirs(shared_elf_dir, exist_ok=True)
    
    # Shared mutation directory (temp files in work_dir)
    shared_mutate_dir = work_dir / 'mutate'
    shared_mutate_elf_dir = work_dir / 'mutate' / 'elf_file'
    os.makedirs(shared_mutate_dir, exist_ok=True)
    os.makedirs(shared_mutate_elf_dir, exist_ok=True)

    if args.arch == 'ventus':
        num_warps_per_core = args.num_genwarps_per_block
    elif args.arch == 'vortex':
        num_warps_per_core = arch_config['vortex_nr_warps_per_core']
    
    while not _stop_event.is_set():
        should_stop, reason = check_stop_conditions(args.target_instr_count, args.target_time)
        if should_stop:
            print(f"  {YELLOW}Worker {worker_id}: stopping ({reason}){RESET}")
            break
        
        with _stats_lock:
            seed_id = _run_counter
            _run_counter += 1
        emu_time_total = 0.0
        
        # Write start timestamp before first simulation
        if not (results_dir / 'start.time').exists():
            with _stats_lock:
                global _start_time
                if _start_time is None:
                    _start_time = time.time()
                    with open(results_dir / 'start.time', 'w') as f:
                        f.write(f"{_start_time}\n")
        
        try:
            # Generate seed using subprocess wrapper (allows parallel execution)
            gen_log = logs_dir / f'seed_{seed_id}_gen.log'
            output_json = work_dir / f'gen_{seed_id}.json'
            
            wrapper_cmd = [
                sys.executable, str(wrapper_script),
                '--divefuzz-dir', str(divefuzz_dir),
                '--seed-id', str(seed_id),
                '--instr-number', str(args.divefuzz_seed_size),
                '--arch', args.arch,
                '--xlen', str(args.xlen),
                '--out-dir', str(shared_out_dir),
                '--elf-dir', str(shared_elf_dir),
                '--xor-cache-file', str(xor_cache_file),
                '--output-json', str(output_json),
                '--num-warps-per-core', str(num_warps_per_core),
                '--num-threads-per-warp', str(arch_config['nr_threads_per_warp'])
            ]
            
            if args.arch == 'vortex':
                wrapper_cmd += [
                    '--vortex-max-num-barrier', str(arch_config['num_barriers']),
                    '--vortex-max-barrier-count', str(max(arch_config['vortex_nr_warps_per_core'], arch_config['vortex_nr_cores_per_cluster'])),
                    '--vortex-cores-per-clusters', str(arch_config['vortex_nr_cores_per_cluster'])
                ]

            if _bug_filter_file and Path(_bug_filter_file).exists():
                wrapper_cmd.extend([
                    '--disable-default-bug-filtering',
                    '--bug-filter-json', str(_bug_filter_file),
                ])
            
            if args.disable_eliminate:
                wrapper_cmd.append('--disable-eliminate')
            
            # Save command line
            divefuzz_cmd_log = logs_dir / f'seed_{seed_id}_gen_cmd.sh'
            with open(divefuzz_cmd_log, 'w') as f:
                f.write('#!/bin/bash\n')
                f.write(' '.join(wrapper_cmd) + '\n')
            
            rc_gen, t_gen = run_command(wrapper_cmd, str(gen_log), timeout=EMULATOR_TIMEOUT_SECONDS, record_logs=args.record_logs)
            
            if rc_gen != 0 or not output_json.exists():
                print(f"  {RED}Worker {worker_id}: wrapper failed for seed {seed_id}, see {gen_log}{RESET}")
                continue
            
            # Read generation results
            try:
                with open(output_json, 'r') as f:
                    gen_result = json.load(f)
                
                if not gen_result.get('success', False):
                    print(f"  {RED}Worker {worker_id}: generation failed for seed {seed_id}: {gen_result.get('error', 'unknown')}{RESET}")
                    continue
                
                asm_file = Path(gen_result['asm_file'])
                elf_path = Path(gen_result['elf_file']) if gen_result.get('elf_file') else None
                total_count = gen_result.get('instr_count', 0)
                instr_freq = gen_result.get('instr_freq', {})
                
                with _stats_lock:
                    global _total_instrs
                    _total_instrs += total_count
                
            except Exception as e:
                print(f"  {RED}Worker {worker_id}: failed to read generation results for seed {seed_id}: {e}{RESET}")
                continue
            
            # Run emulator on the generated elf
            if elf_path and elf_path.exists():
                emu_log = logs_dir / f'seed_{seed_id}_emu.log'
                coverage_dat = work_dir / f'seed_{seed_id}.coverage.dat'  # Temp coverage in work_dir
                stat_json = work_dir / f'seed_{seed_id}.stat.json'
                
                emu_cmd = [
                    emu_exe,
                    '--elf', str(elf_path),
                    '--elf-num-genblocks', str(args.num_genblocks),
                    '--elf-num-genwarps-per-block', str(args.num_genwarps_per_block),
                    '--elf-num-threads-per-warp', str(arch_config['nr_threads_per_warp']),
                    '--stat-jsonfile', str(stat_json),
                    '--coverage-dat-file', str(coverage_dat)
                ]
                if args.emulate:
                    emu_cmd.append('--emulate')

                emu_cmd_log = logs_dir / f'seed_{seed_id}_emu_cmd.sh'
                with open(emu_cmd_log, 'w') as f:
                    f.write('#!/bin/bash\n')
                    f.write(' '.join(emu_cmd) + '\n')
                
                rc_emu, t_emu = run_command(emu_cmd, str(emu_log), timeout=EMULATOR_TIMEOUT_SECONDS, record_logs=args.record_logs)
                emu_time_total += t_emu
                
                # Write timestamp for coverage
                with open(f"{coverage_dat}.time", 'w') as f:
                    f.write(f"{time.time()}\n")
                
                # Update coverage (merge in work_dir)
                update_coverage(str(coverage_dat), work_dir, args.arch, fuzzgpu_dir)
                
                # Run bug detector on emulator log
                _process_bug_detections(str(emu_log))
            
            # Perform mutation if requested
            if args.divefuzz_mutation_times > 0:
                for mut_idx in range(args.divefuzz_mutation_times):
                    if _stop_event.is_set():
                        break
                    
                    try:
                        # Use wrapper for mutation (parallel execution)
                        mut_log = logs_dir / f'seed_{seed_id}_mut_{mut_idx}_gen.log'
                        mut_output_json = work_dir / f'mutate_{seed_id}_mut_{mut_idx}.json'
                        
                        mut_wrapper_cmd = [
                            sys.executable, str(wrapper_script),
                            '--divefuzz-dir', str(divefuzz_dir),
                            '--seed-id', str(seed_id),
                            '--arch', args.arch,
                            '--xlen', str(args.xlen),
                            '--out-dir', str(shared_out_dir),
                            '--elf-dir', str(shared_elf_dir),
                            '--xor-cache-file', str(xor_cache_file),
                            '--output-json', str(mut_output_json),
                            '--mutate',
                            '--input-asm', str(asm_file),
                            '--mutate-dir', str(shared_mutate_dir),
                            '--mutate-elf-dir', str(shared_mutate_elf_dir),
                            '--mutation-idx', str(mut_idx),
                            '--num-warps-per-core', str(num_warps_per_core),
                            '--num-threads-per-warp', str(arch_config['nr_threads_per_warp'])
                        ]
                        
                        if args.arch == 'vortex':
                            mut_wrapper_cmd += [
                                '--vortex-max-num-barrier', str(arch_config['num_barriers']),
                                '--vortex-max-barrier-count', str(max(arch_config['vortex_nr_warps_per_core'], arch_config['vortex_nr_cores_per_cluster'])),
                                '--vortex-cores-per-clusters', str(arch_config['vortex_nr_cores_per_cluster'])
                            ]

                        if _bug_filter_file and Path(_bug_filter_file).exists():
                            mut_wrapper_cmd.extend([
                                '--disable-default-bug-filtering',
                                '--bug-filter-json', str(_bug_filter_file),
                            ])
                        
                        if args.disable_eliminate:
                            mut_wrapper_cmd.append('--disable-eliminate')
                        
                        # Save command line
                        divefuzz_mut_cmd_log = logs_dir / f'seed_{seed_id}_mut_{mut_idx}_gen_cmd.sh'
                        with open(divefuzz_mut_cmd_log, 'w') as f:
                            f.write('#!/bin/bash\n')
                            f.write(' '.join(mut_wrapper_cmd) + '\n')
                        
                        rc_mut, t_mut = run_command(mut_wrapper_cmd, str(mut_log), timeout=EMULATOR_TIMEOUT_SECONDS, record_logs=args.record_logs)
                        
                        if rc_mut != 0 or not mut_output_json.exists():
                            print(f"  {YELLOW}Worker {worker_id}: mutation {mut_idx} wrapper failed for seed {seed_id}, see {mut_log}{RESET}")
                            continue
                        
                        # Read mutation results
                        try:
                            with open(mut_output_json, 'r') as f:
                                mut_result = json.load(f)
                            
                            if not mut_result.get('success', False):
                                print(f"  {YELLOW}Worker {worker_id}: mutation {mut_idx} failed for seed {seed_id}: {mut_result.get('error', 'unknown')}{RESET}")
                                continue
                            
                            mut_asm_file_renamed = Path(mut_result['asm_file'])
                            mut_elf_path = Path(mut_result['elf_file']) if mut_result.get('elf_file') else None
                            mut_count = mut_result.get('instr_count', 0)
                            
                            with _stats_lock:
                                _total_instrs += mut_count
                            
                        except Exception as e:
                            print(f"  {YELLOW}Worker {worker_id}: failed to read mutation results for seed {seed_id} mutation {mut_idx}: {e}{RESET}")
                            continue
                        
                        # Run emulator on mutated elf
                        if mut_elf_path and mut_elf_path.exists():
                            mut_emu_log = logs_dir / f'seed_{seed_id}_mut_{mut_idx}_emu.log'
                            mut_coverage_dat = work_dir / f'seed_{seed_id}_mut_{mut_idx}.coverage.dat'  # Temp coverage
                            mut_stat_json = work_dir / f'seed_{seed_id}_mut_{mut_idx}.stat.json'
                            
                            mut_emu_cmd = [
                                emu_exe,
                                '--elf', str(mut_elf_path),
                                '--elf-num-genblocks', str(args.num_genblocks),
                                '--elf-num-genwarps-per-block', str(args.num_genwarps_per_block),
                                '--elf-num-threads-per-warp', str(arch_config['nr_threads_per_warp']),
                                '--stat-jsonfile', str(mut_stat_json),
                                '--coverage-dat-file', str(mut_coverage_dat)
                            ]
                            if args.emulate:
                                mut_emu_cmd.append('--emulate')

                            mut_emu_cmd_log = logs_dir / f'seed_{seed_id}_mut_{mut_idx}_emu_cmd.sh'
                            with open(mut_emu_cmd_log, 'w') as f:
                                f.write('#!/bin/bash\n')
                                f.write(' '.join(mut_emu_cmd) + '\n')
                            
                            rc_mut_emu, t_mut_emu = run_command(mut_emu_cmd, str(mut_emu_log), timeout=EMULATOR_TIMEOUT_SECONDS, record_logs=args.record_logs)
                            emu_time_total += t_mut_emu
                            
                            # Write timestamp for coverage
                            with open(f"{mut_coverage_dat}.time", 'w') as f:
                                f.write(f"{time.time()}\n")
                            
                            # Update coverage (merge in work_dir)
                            update_coverage(str(mut_coverage_dat), work_dir, args.arch, fuzzgpu_dir)
                            
                            # Run bug detector on emulator log
                            _process_bug_detections(str(mut_emu_log))
                    
                    except Exception as e:
                        print(f"  {YELLOW}Worker {worker_id}: mutation {mut_idx} for seed {seed_id} failed: {e}{RESET}")
            
            print(f"  {GREEN}Worker {worker_id}: completed seed {seed_id} (base + {args.divefuzz_mutation_times} mutations){RESET}")

            summary = _record_and_summarize(t_gen, emu_time_total, args.target_instr_count)
            if summary:
                _print_progress(f"Worker {worker_id} seed {seed_id}", summary)
            
            # Cleanup temp files for this iteration (seed files, logs, coverage)
            if not args.no_cleanup_temp:
                try:
                    # Remove generation output json
                    if output_json.exists():
                        output_json.unlink(missing_ok=True)
                    # Remove base coverage files
                    for cov_file in work_dir.glob(f'seed_{seed_id}.coverage.dat*'):
                        cov_file.unlink(missing_ok=True)
                    for stat_file in work_dir.glob(f'seed_{seed_id}.stat.json'):
                        stat_file.unlink(missing_ok=True)
                    # Remove mutation coverage and stat files
                    for mut_cov in work_dir.glob(f'mutate_{seed_id}_mut_*.json'):
                        mut_cov.unlink(missing_ok=True)
                    for mut_cov in work_dir.glob(f'seed_{seed_id}_mut_*.coverage.dat*'):
                        mut_cov.unlink(missing_ok=True)
                    for mut_stat in work_dir.glob(f'seed_{seed_id}_mut_*.stat.json'):
                        mut_stat.unlink(missing_ok=True)
                    # Remove log files for this seed
                    for log_pattern in [f'seed_{seed_id}_*.log', f'seed_{seed_id}_*.sh']:
                        for log_file in logs_dir.glob(log_pattern):
                            log_file.unlink(missing_ok=True)
                    # Remove asm/elf files (from shared directories)
                    for asm_file_pat in shared_out_dir.glob(f'seed_{seed_id}*.S'):
                        asm_file_pat.unlink(missing_ok=True)
                    for elf_file_pat in shared_elf_dir.glob(f'seed_{seed_id}*.elf'):
                        elf_file_pat.unlink(missing_ok=True)
                    for mut_asm in shared_mutate_dir.glob(f'seed_{seed_id}*.S'):
                        mut_asm.unlink(missing_ok=True)
                    for mut_elf in shared_mutate_elf_dir.glob(f'seed_{seed_id}*.elf'):
                        mut_elf.unlink(missing_ok=True)
                except Exception as e:
                    print(f"  {YELLOW}Worker {worker_id}: cleanup failed for seed {seed_id}: {e}{RESET}")
        
        except Exception as e:
            print(f"  {RED}Worker {worker_id}: error processing seed {seed_id}: {e}{RESET}")
            import traceback
            traceback.print_exc()


def get_arch_config(build_dir):
    """Run get_arch_config executable and return a dict of config values."""
    exe_path = build_dir / 'get_arch_config'
    exe = find_executable(str(exe_path))
    assert exe

    cmd = [exe]
    result = subprocess.run(cmd, capture_output=True, text=True)
    assert not result.returncode
        
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

def _cleanup_gen_emu_processes():
    """Kill any existing instances of simulator/fuzzer processes."""
    targets = ['fuzz_gpu', 'emulator', 'verilator_coverage', 'spike', 'divefuzz_wrapper.py', 'do_genmanyelfs.py']
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
    global _start_time, _total_instrs, _coverage_data
    
    _cleanup_gen_emu_processes()
    
    script_dir = Path(__file__).resolve().parent
    fuzzgpu_dir = script_dir.parent
    
    parser = argparse.ArgumentParser(description='Baseline comparison of fuzzing methods.')
    parser.add_argument('--arch', type=str, choices=['ventus', 'vortex'], default='ventus')
    parser.add_argument('--xlen', type=int, choices=[32, 64], default=32)
    parser.add_argument('--method', type=str, choices=['fuzzgpu', 'cascade', 'divefuzz'], required=True)
    parser.add_argument('--jobs', type=int, default=1, help='Number of parallel workers')
    parser.add_argument('--target-instr-count', type=int, default=None, 
                        help='Stop after generating this many instructions')
    parser.add_argument('--target-time', type=int, default=None,
                        help='Stop after this many seconds')
    parser.add_argument('--results-dir', type=str, default=None,
                        help='Directory for logs, coverage, results.json (default: scripts/../results/baseline_results)')
    parser.add_argument('--work-dir', type=str, default='/tmp/baseline_work',
                        help='Directory for temp files like instr.bin/data.bin/aux.bin/elf (default: /tmp/baseline_work)')
    parser.add_argument('--emulate', action='store_true', help='Emulate only (no RTL)')
    parser.add_argument('--no-log', action='store_false', dest='record_logs',
                        help='Disable writing generator/emulator log files')
    parser.add_argument('--cov-type', type=str, choices=['none', 'line', 'toggle', 'expr'], default='line',
                        help='Coverage type for DUT build (default: none)')
    parser.add_argument('--dft-enabled', action='store_true', default=True,
                        help='Enable difftest for DUT (default: disabled)')
    
    # FuzzGPU parameters
    parser.add_argument('--num-genblocks', type=int, default=2)
    parser.add_argument('--num-genwarps-per-block', type=int, default=2)
    parser.add_argument('--max-instrs-per-bb', type=int, default=500)
    parser.add_argument('--min-instrs-per-bb', type=int, default=1)
    
    # Cascade parameters
    parser.add_argument('--cascade-dir', type=str, default=None)
    parser.add_argument('--cascade-num-seeds', type=int, default=10)
    
    # DiveFuzz parameters
    parser.add_argument('--divefuzz-dir', type=str, default=None)
    parser.add_argument('--divefuzz-seed-size', type=int, default=200)
    parser.add_argument('--divefuzz-mutation-times', type=int, default=3)
    parser.add_argument('--disable-eliminate', action='store_true', default=False,
                        help='Disable elimination optimization for divefuzz (enabled by default)')

    parser.add_argument('--disable-emu-guide', action='store_true',
                        help='Disable emulator-guided generation')
    parser.add_argument('--no-cleanup-temp', action='store_true', default=False,
                        help='Disable cleanup of temp files after each iteration (cleanup enabled by default)')
    
    args = parser.parse_args()
    
    if not args.target_instr_count and not args.target_time:
        parser.error('Must specify either --target-instr-count or --target-time')
    
    # Setup paths
    dft_enabled = '1' if args.dft_enabled else '0'
    build_dir = fuzzgpu_dir / f"build{args.xlen}-{args.arch}-cov{args.cov_type}-dft{dft_enabled}"
    fuzz_exe = find_executable(str(build_dir / 'fuzz_gpu'))
    emu_exe = find_executable(str(build_dir / 'emulator'))
    
    if not emu_exe:
        print(f"{RED}emulator executable not found at {build_dir / 'emulator'}{RESET}")
        sys.exit(2)
    
    # Create output directory structure
    # Include arch, xlen, and cov_type in directory hierarchy for better organization
    global _results_dir, _work_dir
    
    timestamp = datetime.datetime.now().strftime('%Y%m%d_%H%M%S')
    if args.method == 'fuzzgpu':
        method_name = f"{args.method}_vanilla"
    else:
        method_name = args.method
    
    arch_xlen_cov_subdir = f"{args.arch}_x{args.xlen}_cov{args.cov_type}"
    run_name = f"{method_name}_{timestamp}"
    
    # Results directory: for logs, coverage data, results.json (persistent)
    if args.results_dir:
        results_base_dir = Path(args.results_dir)
    else:
        results_base_dir = script_dir.parent / 'results' / 'baseline_results'
    results_dir = results_base_dir / arch_xlen_cov_subdir / run_name
    os.makedirs(results_dir, exist_ok=True)
    _results_dir = results_dir
    
    # Work directory: for temp files like instr.bin/data.bin/aux.bin/elf (in /tmp)
    work_dir = Path(args.work_dir) / arch_xlen_cov_subdir / run_name
    os.makedirs(work_dir, exist_ok=True)
    _work_dir = work_dir
    
    # Initialize bug detector (store in work_dir for fuzzgpu feedback)
    _initialize_bug_detector_files(work_dir)
    
    # Get architecture configuration
    arch_config = get_arch_config(build_dir)
    print(f"{GREEN}Architecture Configuration:{RESET}")
    for k, v in arch_config.items():
        print(f"  {k}: {v}")
    
    print(f"{BLUE}Starting baseline test: {method_name}{RESET}")
    print(f"  Results directory: {results_dir}")
    print(f"  Work directory (temp): {work_dir}")
    print(f"  Jobs: {args.jobs}")
    if args.target_instr_count:
        print(f"  Target instructions: {args.target_instr_count}")
    if args.target_time:
        print(f"  Target time: {args.target_time}s")
    if not args.record_logs:
        print(f"  Logging disabled: generator/emulator stdout will be suppressed")
    if args.no_cleanup_temp:
        print(f"  Temp cleanup disabled: logs and coverage files will be preserved in work_dir")
    else:
        print(f"  Temp cleanup enabled: logs and coverage files will be deleted after each iteration")
    
    # Install signal handlers
    signal.signal(signal.SIGINT, _handle_signal)
    signal.signal(signal.SIGTERM, _handle_signal)
    
    # Run workers
    try:
        if args.method == 'fuzzgpu':
            if not fuzz_exe:
                print(f"{RED}fuzz_gpu executable not found{RESET}")
                sys.exit(2)
            
            with ThreadPoolExecutor(max_workers=args.jobs) as executor:
                futures = [
                    executor.submit(run_fuzzgpu_worker, args, i, fuzz_exe, emu_exe, work_dir, results_dir, fuzzgpu_dir)
                    for i in range(args.jobs)
                ]
                for fut in as_completed(futures):
                    try:
                        fut.result()
                    except Exception as e:
                        print(f"{RED}Worker error: {e}{RESET}")
        
        elif args.method == 'cascade':
            cascade_dir = Path(args.cascade_dir) if args.cascade_dir else (fuzzgpu_dir / 'baseline' / f'cascade-meta')
            gen_script = cascade_dir / 'fuzzer' / 'do_genmanyelfs.py'
            if not gen_script.exists():
                print(f"{RED}Cascade generator not found at {gen_script}{RESET}")
                sys.exit(2)
            
            with ThreadPoolExecutor(max_workers=args.jobs) as executor:
                futures = [
                    executor.submit(run_cascade_worker, args, i, emu_exe, work_dir, results_dir, gen_script, arch_config, fuzzgpu_dir)
                    for i in range(args.jobs)
                ]
                for fut in as_completed(futures):
                    try:
                        fut.result()
                    except Exception as e:
                        print(f"{RED}Worker error: {e}{RESET}")
        
        elif args.method == 'divefuzz':
            divefuzz_dir = Path(args.divefuzz_dir) if args.divefuzz_dir else (fuzzgpu_dir / 'baseline' / 'DiveFuzz')
            if not divefuzz_dir.exists():
                print(f"{RED}DiveFuzz not found at {divefuzz_dir}{RESET}")
                sys.exit(2)
            
            # Wrapper script for parallel execution
            wrapper_script = script_dir / 'divefuzz_wrapper.py'
            if not wrapper_script.exists():
                print(f"{RED}Wrapper script not found at {wrapper_script}{RESET}")
                sys.exit(2)
            
            # XOR cache file (shared across workers via file system, temp in work_dir)
            cache_file = work_dir / 'xor_cache.json'
            
            # Initialize empty cache file if it doesn't exist
            if not cache_file.exists():
                with open(cache_file, 'w') as f:
                    json.dump({}, f)
            
            with ThreadPoolExecutor(max_workers=args.jobs) as executor:
                futures = [
                    executor.submit(run_divefuzz_worker, args, i, emu_exe, work_dir, results_dir, divefuzz_dir, cache_file, wrapper_script, arch_config, fuzzgpu_dir)
                    for i in range(args.jobs)
                ]
                for fut in as_completed(futures):
                    try:
                        fut.result()
                    except Exception as e:
                        print(f"{RED}Worker error: {e}{RESET}")
                        import traceback
                        traceback.print_exc()
    
    except KeyboardInterrupt:
        print(f"{YELLOW}Interrupted by user, shutting down gracefully...{RESET}")
        _stop_event.set()
        # Wait for threads to complete
        time.sleep(1)
        _handle_signal(signal.SIGINT, None)
    
    except Exception as e:
        print(f"{RED}Error during execution: {e}{RESET}")
        import traceback
        traceback.print_exc()
        _stop_event.set()
        _handle_signal(signal.SIGTERM, None)
    
    finally:
        # Final cleanup - wait for any remaining processes
        if not _wait_for_processes(timeout=10):
            print(f"{YELLOW}Warning: Some child processes did not terminate within timeout{RESET}")
            with _procs_lock:
                remaining = [p for p in _procs if p.poll() is None]
                if remaining:
                    print(f"{YELLOW}Forcefully killing {len(remaining)} remaining processes{RESET}")
                    for p in remaining:
                        try:
                            p.kill()
                        except Exception:
                            pass
    
    # Save results to results_dir (persistent)
    with _stats_lock:
        results = {
            'method': method_name,
            'arch': args.arch,
            'xlen': args.xlen,
            'cov_type': args.cov_type,
            'timestamp': timestamp,
            'args': vars(args),
            'total_instructions': _total_instrs,
            'coverage_data': _coverage_data,
            'elapsed_time': time.time() - _start_time if _start_time else 0,
            'results_dir': str(results_dir),
            'work_dir': str(work_dir)
        }
    
    results_json = results_dir / 'results.json'
    with open(results_json, 'w') as f:
        json.dump(results, f, indent=2)
    
    # Clean up intermediate merged coverage files in work_dir, keep only the latest
    # Note: Coverage files are NOT copied to results_dir due to large size
    print(f"{BLUE}Cleaning up intermediate coverage files in work_dir...{RESET}")
    merged_coverage_src = work_dir / 'merged_coverage'
    if merged_coverage_src.exists():
        try:
            # Clean up intermediate merged files, keep only the latest one
            merged_files = sorted(merged_coverage_src.glob('merged_seed_*.coverage.dat'))
            if len(merged_files) > 1:
                # Keep the latest merged file, remove others
                for f in merged_files[:-1]:
                    try:
                        f.unlink()
                        # Also remove corresponding annotate directories
                        ann_prefix = _build_annotate_prefix(str(f))
                        ann_dir = Path(ann_prefix)
                        if ann_dir.exists():
                            shutil.rmtree(ann_dir)
                    except Exception as e:
                        print(f"{YELLOW}Warning: failed to remove {f}: {e}{RESET}")
                print(f"{GREEN}Cleaned up {len(merged_files)-1} intermediate merged coverage files{RESET}")
        except Exception as e:
            print(f"{YELLOW}Warning: failed to cleanup merged coverage directory: {e}{RESET}")
    
    # Clean up source coverage files in work_dir (testcase_*/coverage.dat)
    try:
        source_coverage_files = list(work_dir.glob('testcase_*/coverage.dat'))
        source_coverage_files += list(work_dir.glob('run_*/seed_*.coverage.dat'))
        source_coverage_files += list(work_dir.glob('run_*/*.coverage.dat'))
        source_coverage_files += list(work_dir.glob('seed_*.coverage.dat'))
        if source_coverage_files:
            for f in source_coverage_files:
                try:
                    f.unlink()
                    # Also remove timestamp and value files
                    time_file = Path(f"{f}.time")
                    val_file = Path(f"{f}.val")
                    if time_file.exists():
                        time_file.unlink()
                    if val_file.exists():
                        val_file.unlink()
                except Exception as e:
                    print(f"{YELLOW}Warning: failed to remove {f}: {e}{RESET}")
            print(f"{GREEN}Cleaned up {len(source_coverage_files)} source coverage files from work_dir{RESET}")
    except Exception as e:
        print(f"{YELLOW}Warning: failed to cleanup source coverage files: {e}{RESET}")
    
    # Also save a config.json with key paths for reference
    config_json = results_dir / 'config.json'
    with open(config_json, 'w') as f:
        json.dump({
            'method': method_name,
            'arch': args.arch,
            'xlen': args.xlen,
            'cov_type': args.cov_type,
            'results_dir': str(results_dir),
            'work_dir': str(work_dir),
        }, f, indent=2)
    
    print(f"\n{GREEN}Results saved to: {results_json}{RESET}")
    print(f"  Results directory: {results_dir}")
    print(f"  Work directory (temp): {work_dir}")
    print(f"Total instructions: {_total_instrs}")
    print(f"Coverage data points: {len(_coverage_data)}")
    print(f"\n{BLUE}To generate comparison plot, run:{RESET}")
    print(f"  python3 plot_baseline.py --results-dir {results_dir.parent}")


if __name__ == '__main__':
    main()
