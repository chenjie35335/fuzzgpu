#!/usr/bin/env python3
"""Fuzzing loop for GPGPU testing with bug detection and analysis.

Two-phase architecture:
  Phase 1 (gen_sim): Generation & simulation only. Records timestamps, maintains
                     --bug-filter-json for generators that support it. No detailed bug analysis.
  Phase 2 (analyze): Bug detection & analysis. Idempotent - same Phase1 results
                     + same bug_detector = same output.

This ensures reanalyze is exactly consistent with original analysis.

Directory structure:
  Results (JSON): scripts/../results/fuzzing_results/{method}_{arch}_{xlen}_seed{N}/
    - phase1_results.json, phase2_results.json, config.json
  Work (temp files): --work-dir/{arch}_x{xlen}/{run_name}/
    - testcase_*/instr.bin, data.bin, aux.bin, emu.log, etc.

The --analyze mode works with only the results directory content.
If logs are available (work_dir exists), re-analyzes them.
If logs are deleted, loads cached phase2_results.json.

Simplified from baseline.py:
- No CEM/OpcodeBalance optimizer
- --cov-type fixed to 'none'
- --dft-enabled fixed to True

Additional features:
- Configurable starting seed ID (--start-seed)
- --all mode: auto-scan vortex/32, vortex/64, ventus/32
- --force-max-num-instrs for fuzzgpu method
- Core hours tracking for bug discovery
- Unknown bug tracking by instruction
"""

from __future__ import annotations

import argparse
import json
import os
import signal
import subprocess
import sys
import threading
import time
import datetime
import random
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor, ProcessPoolExecutor, as_completed
from dataclasses import dataclass, field, asdict
from typing import Dict, List, Optional, Set, Tuple, Any
from multiprocessing import cpu_count

# Import bug detector
script_dir = Path(__file__).resolve().parent
sys.path.insert(0, str(script_dir))
from bug_detector import scan_file, BugMatch

# Import Random selection and helper functions
from random_sel import RandomSelection, get_instr_info

# Colors
GREEN = "\033[32m"
RED = "\033[31m"
YELLOW = "\033[33m"
BLUE = "\033[34m"
RESET = "\033[0m"

# Constants
EMULATOR_TIMEOUT_SECONDS = 300
NUM_CORES = 1  # FIXME: remove this hardcoding
MAX_LOG_FILE_SIZE_BYTES = 50 * 1024 * 1024  # max log file size (avoiding bug analysis DoS)
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

# Arch configurations for --all mode
ALL_ARCH_CONFIGS = [
    {'arch': 'vortex', 'xlen': 32},
    {'arch': 'vortex', 'xlen': 64},
    {'arch': 'ventus', 'xlen': 32},
]


# =============================================================================
# Phase 1 Data Structures: Generation & Simulation Records
# =============================================================================

@dataclass
class TestRecord:
    """Record of a single test case (Phase 1 output).
    
    Metrics stored for each seed (for later analysis/plotting):
    - gen_num_instrs (num_instrs): Number of instructions generated
    - emu_num_instrs: Number of instructions actually executed (from stat.json)
    - gen_time: Generation time in seconds
    - emu_time: Emulation time in seconds (wall-clock)
    - file_write_time_ms: Time to write generated files (ms)
    - program_construction_time_ms: Time for program construction excluding file write (ms)
    - memory_check_time_ms: Memory checker verification time (ms)
    - rtl_emu_time_ms: RTL emulation time excluding memory check (ms)
    - stat_json_data: Full stat.json content for detailed analysis
    """
    seed_id: int
    emu_log_path: str  # Relative path from method_dir
    sim_start_time: float  # Absolute timestamp when simulation started
    sim_end_time: float  # Absolute timestamp when simulation ended
    num_instrs: int = 0  # gen_num_instrs: Number of instructions generated
    emu_num_instrs: int = 0  # Number of instructions actually executed (from stat.json)
    success: bool = True
    error_msg: str = ""
    # Full stat.json content from emulator for later analysis
    stat_json_data: Optional[Dict] = None
    # Time breakdown (in seconds)
    gen_time: float = 0.0  # Generation time
    emu_time: float = 0.0  # Emulation time (total wall-clock time)
    # Detailed timing breakdown (in milliseconds, extracted from stat.json)
    program_construction_time_ms: float = 0.0
    file_write_time_ms: float = 0.0
    memory_check_time_ms: float = 0.0
    rtl_emu_time_ms: float = 0.0


@dataclass
class Phase1Results:
    """Phase 1 results: generation & simulation records.
    
    Note: bug_filter_list is ONLY used for fuzzgpu generation feedback
    (--bug-filter-json). Phase 2 will independently re-analyze all logs
    using bug_detector, NOT restore from this list.
    """
    method: str
    arch: str
    xlen: int
    start_seed: int
    max_num_instrs: Optional[int]
    first_gen_start_time: float  # Timestamp of first generation start
    last_sim_end_time: float = 0.0
    total_instrs: int = 0
    tests: List[TestRecord] = field(default_factory=list)
    # Bugs found so far, used to generate --bug-filter-json for supported generators.
    # This is NOT used by Phase 2 analysis
    bug_filter_list: List[str] = field(default_factory=list)
    # Path to work directory where logs/test files are stored (may be in /tmp)
    work_dir: Optional[str] = None
    # Fuzzing loop timing: when run_phase1 started and ended
    loop_start_time: float = 0.0  # Absolute timestamp when fuzzing loop started
    loop_end_time: float = 0.0  # Absolute timestamp when fuzzing loop ended
    
    def to_dict(self) -> Dict:
        return {
            'method': self.method,
            'arch': self.arch,
            'xlen': self.xlen,
            'start_seed': self.start_seed,
            'max_num_instrs': self.max_num_instrs,
            'first_gen_start_time': self.first_gen_start_time,
            'last_sim_end_time': self.last_sim_end_time,
            'total_instrs': self.total_instrs,
            'tests': [asdict(t) for t in self.tests],
            'bug_filter_list': self.bug_filter_list,
            'work_dir': self.work_dir,
            'loop_start_time': self.loop_start_time,
            'loop_end_time': self.loop_end_time,
        }
    
    @classmethod
    def from_dict(cls, d: Dict) -> 'Phase1Results':
        tests = []
        for t in d.get('tests', []):
            # Handle both old (without stat_json_data) and new format
            record = TestRecord(
                seed_id=t['seed_id'],
                emu_log_path=t['emu_log_path'],
                sim_start_time=t['sim_start_time'],
                sim_end_time=t['sim_end_time'],
                num_instrs=t.get('num_instrs', 0),
                emu_num_instrs=t.get('emu_num_instrs', 0),
                success=t.get('success', True),
                error_msg=t.get('error_msg', ''),
                stat_json_data=t.get('stat_json_data'),
                gen_time=t.get('gen_time', 0.0),
                emu_time=t.get('emu_time', 0.0),
                program_construction_time_ms=t.get('program_construction_time_ms', 0.0),
                file_write_time_ms=t.get('file_write_time_ms', 0.0),
                memory_check_time_ms=t.get('memory_check_time_ms', 0.0),
                rtl_emu_time_ms=t.get('rtl_emu_time_ms', 0.0),
            )
            tests.append(record)
        return cls(
            method=d['method'],
            arch=d['arch'],
            xlen=d['xlen'],
            start_seed=d['start_seed'],
            max_num_instrs=d.get('max_num_instrs'),
            first_gen_start_time=d['first_gen_start_time'],
            last_sim_end_time=d.get('last_sim_end_time', 0.0),
            total_instrs=d.get('total_instrs', 0),
            tests=tests,
            bug_filter_list=d.get('bug_filter_list', []),
            work_dir=d.get('work_dir'),
            loop_start_time=d.get('loop_start_time', 0.0),
            loop_end_time=d.get('loop_end_time', 0.0),
        )


# =============================================================================
# Phase 2 Data Structures: Bug Detection & Analysis Results
# =============================================================================

@dataclass
class BugRecord:
    """Record of a detected bug (Phase 2 output)."""
    bug_name: str
    first_detection_time: float
    first_detection_seed: int
    detection_count: int = 1
    core_hours: float = 0.0
    first_occurrence: Optional[Dict] = None


@dataclass
class UnknownBugRecord:
    """Record of an unknown bug grouped by instruction (Phase 2 output)."""
    instruction: str
    first_detection_time: float
    first_detection_seed: int
    occurrence_count: int = 0
    first_occurrence: Optional[Dict] = None


@dataclass
class Phase2Results:
    """Phase 2 results: bug detection & analysis."""
    method: str
    arch: str
    xlen: int
    start_seed: int
    max_num_instrs: Optional[int]
    first_gen_start_time: float
    last_sim_end_time: float
    total_tests: int
    total_instrs: int
    known_bugs: Dict[str, BugRecord] = field(default_factory=dict)
    unknown_bugs_by_instr: Dict[str, UnknownBugRecord] = field(default_factory=dict)
    analysis_timestamp: float = 0.0
    
    def to_dict(self) -> Dict:
        return {
            'method': self.method,
            'arch': self.arch,
            'xlen': self.xlen,
            'start_seed': self.start_seed,
            'max_num_instrs': self.max_num_instrs,
            'first_gen_start_time': self.first_gen_start_time,
            'last_sim_end_time': self.last_sim_end_time,
            'total_tests': self.total_tests,
            'total_instrs': self.total_instrs,
            'elapsed_seconds': self.last_sim_end_time - self.first_gen_start_time,
            'known_bugs': {k: asdict(v) for k, v in self.known_bugs.items()},
            'unknown_bugs_by_instr': {k: asdict(v) for k, v in self.unknown_bugs_by_instr.items()},
            'analysis_timestamp': self.analysis_timestamp,
        }


# =============================================================================
# Shared State for Phase 1 Workers
# =============================================================================

_procs_lock = threading.Lock()
_procs: Set[subprocess.Popen] = set()
_stop_event = threading.Event()

_stats_lock = threading.Lock()
_seed_counter = 0
_run_counter = 0
_first_gen_start_time: Optional[float] = None
_total_instrs = 0

_phase1_lock = threading.Lock()
_phase1_results: Optional[Phase1Results] = None
_bug_filter_set: Set[str] = set()
# Directories:
#   results_dir: for JSON results AND emu logs (persistent, in project/results/)
#   work_dir: for temp files like instr.bin/data.bin/aux.bin (in /tmp, avoids SSD wear)
_results_dir: Optional[Path] = None
_work_dir: Optional[Path] = None

# Shared Random selection (always enabled for fuzzgpu method)
_random_selection = None

# Expected bugs for --bug-complete-list mode
_expected_bugs: Optional[Set[str]] = None


# =============================================================================
# Utility Functions
# =============================================================================

def run_command(cmd, logfile_path, timeout=None, env=None, record_logs=True, max_log_size=None):
    """Run command and stream output to logfile. Returns (returncode, elapsed_time).
    
    Args:
        cmd: Command to run
        logfile_path: Path to write log output
        timeout: Timeout in seconds (None = no timeout)
        env: Environment variables
        record_logs: Whether to record logs to file
        max_log_size: Maximum log file size in bytes (None = use global MAX_LOG_FILE_SIZE_BYTES)
    
    Returns:
        Tuple of (returncode, elapsed_time) where:
        - returncode: 0 = success, -1 = stopped, -2 = timeout, -3 = process vanished, -4 = log size exceeded
    """
    if max_log_size is None:
        max_log_size = MAX_LOG_FILE_SIZE_BYTES

    if _stop_event.is_set():
        return -1, 0.0
    
    lf_handle = None
    stdout_target = subprocess.DEVNULL
    try:
        if record_logs:
            lf_handle = open(logfile_path, 'wb')
            # Write command at the beginning of log file
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
                    try:
                        proc.wait(timeout=1)
                    except subprocess.TimeoutExpired:
                        try:
                            os.killpg(proc.pid, signal.SIGKILL)
                        except Exception:
                            proc.kill()
                        proc.wait()
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
            return rc, time.time() - start
        finally:
            with _procs_lock:
                _procs.discard(proc)
    finally:
        if lf_handle:
            lf_handle.close()


def find_executable(candidate):
    if os.path.exists(candidate) and os.access(candidate, os.X_OK):
        return candidate
    from shutil import which
    return which(candidate)


def check_stop_conditions(target_instr_count, target_time):
    with _stats_lock:
        if target_instr_count and _total_instrs >= target_instr_count:
            _stop_event.set()
            return True
        if target_time and _first_gen_start_time:
            if time.time() - _first_gen_start_time >= target_time:
                _stop_event.set()
                return True
    # Check if all expected bugs have been found
    if _expected_bugs is not None:
        with _phase1_lock:
            if _expected_bugs.issubset(_bug_filter_set):
                print(f"{GREEN}All expected bugs found! Stopping...{RESET}")
                _stop_event.set()
                return True
    return False


def _handle_signal(signum, frame):
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


def get_arch_config(build_dir):
    exe = find_executable(str(build_dir / 'get_arch_config'))
    if not exe:
        return {}
    result = subprocess.run([exe], capture_output=True, text=True)
    if result.returncode:
        return {}
    config = {}
    for line in result.stdout.splitlines():
        if '=' in line:
            k, v = line.split('=', 1)
            try:
                config[k.strip()] = int(v.strip())
            except ValueError:
                config[k.strip()] = v.strip()
    return config


def check_emulator_success(stat_json_path: Path) -> bool:
    """Check if emulator succeeded by reading stat.json.
    
    Returns True only if stat.json exists and num_executed_instr > 0.
    """
    try:
        with open(stat_json_path, 'r') as f:
            stat_data = json.load(f)
            num_executed = stat_data.get('num_executed_instr', 0)
            return num_executed > 0
    except Exception:
        return False


# =============================================================================
# Phase 1: Quick Bug Detection for Generator Feedback
# =============================================================================

def phase1_quick_bug_detect(emu_log_path: Path, seed_id: int, method_dir: Path, sim_end_time: float = 0.0):
    """Quick bug detect for --bug-filter-json maintenance.
    
    This is used ONLY during Phase 1 to provide generation feedback.
    Phase 2 will independently re-analyze all logs for final results.
    
    Args:
        sim_end_time: Fallback timestamp when bug.timestamp is 0
    """
    global _bug_filter_set
    try:
        found_bugs, _ = scan_file(emu_log_path)
    except Exception:
        return
    
    with _phase1_lock:
        for bug in found_bugs:
            if bug.bug_name not in SUPPORTED_GENERATOR_BUG_FILTERS:
                continue
            if bug.bug_name not in _bug_filter_set:
                _bug_filter_set.add(bug.bug_name)
                # Calculate discovery time relative to phase1 start
                # Use sim_end_time as fallback when bug.timestamp is 0
                first_start = _phase1_results.first_gen_start_time if _phase1_results else 0

                # some bug timestamps cannot be accurately located, use sim_end_time instead
                bug_ts = bug.timestamp if bug.timestamp > 0 else sim_end_time
                discovery_time = bug_ts - first_start if first_start > 0 else bug_ts
                core_hours = (discovery_time / 3600.0) * NUM_CORES
                print(f"  {GREEN}Bug '{bug.bug_name}' @ seed {seed_id}, ts={discovery_time:.2f}s ({core_hours:.4f} ch) -> filter{RESET}")
                _save_bug_filter_json_unlocked(method_dir)


def _save_bug_filter_json_unlocked(method_dir: Path) -> Path:
    """Internal version - caller must hold _phase1_lock.
    
    Uses atomic write (write to temp file + rename) to avoid race conditions
    where external processes (fuzz_exe) may read incomplete JSON.
    """
    import tempfile
    filter_file = method_dir / 'bug_filter.json'
    data = {'filtered_bugs': sorted(_bug_filter_set)}
    
    # Atomic write: write to temp file, then rename
    fd, tmp_path = tempfile.mkstemp(dir=method_dir, suffix='.json.tmp')
    try:
        with os.fdopen(fd, 'w') as f:
            json.dump(data, f, indent=2)
        os.replace(tmp_path, filter_file)  # Atomic on POSIX
    except Exception:
        raise
    return filter_file


def save_bug_filter_json(method_dir: Path) -> Path:
    """Thread-safe version for external callers."""
    with _phase1_lock:
        return _save_bug_filter_json_unlocked(method_dir)


def get_bug_filter_path_if_exists(method_dir: Path) -> Optional[str]:
    """Thread-safe check for bug_filter.json existence.
    
    Returns the path string if file exists, None otherwise.
    Must be called to avoid TOCTOU race conditions.
    """
    with _phase1_lock:
        filter_file = method_dir / 'bug_filter.json'
        return str(filter_file) if filter_file.exists() else None


def snapshot_bug_filter_json(method_dir: Path, test_dir: Path) -> Optional[str]:
    """Snapshot the current bug filter for one testcase.

    The instruction-info query and fuzzgpu invocation must observe identical
    filter contents because the probability vector is indexed by the resulting
    registered instruction list.
    """
    with _phase1_lock:
        filter_file = method_dir / 'bug_filter.json'
        if not filter_file.exists():
            return None

        snapshot_file = test_dir / 'bug_filter.json'
        snapshot_file.write_bytes(filter_file.read_bytes())
        return str(snapshot_file)


def cascade_elf_num_threads(elf_out_dir: Path, elf_name: str, default_num_threads: int) -> int:
    active_threads_file = elf_out_dir / f'{elf_name}_active_threads.txt'
    if not active_threads_file.exists():
        return default_num_threads
    try:
        active_threads = int(active_threads_file.read_text().strip())
    except Exception:
        return default_num_threads
    return active_threads if active_threads > 0 else default_num_threads


def save_phase1_results(results_dir: Path):
    """Save phase1_results.json to the results directory (not work_dir)."""
    with _phase1_lock:
        if _phase1_results is None:
            return
        _phase1_results.bug_filter_list = list(_bug_filter_set)
        with open(results_dir / 'phase1_results.json', 'w') as f:
            json.dump(_phase1_results.to_dict(), f, indent=2)


def add_test_record(record: TestRecord):
    """Add a test record to phase1 results. Uses global _phase1_results."""
    with _phase1_lock:
        if _phase1_results:
            _phase1_results.tests.append(record)
            _phase1_results.total_instrs += record.num_instrs
            if record.sim_end_time > _phase1_results.last_sim_end_time:
                _phase1_results.last_sim_end_time = record.sim_end_time


@dataclass
class EmulatorResult:
    """Result from running the emulator."""
    emu_time: float  # Wall-clock emulation time in seconds
    emu_success: bool  # Whether emulation produced num_executed_instr > 0
    emu_num_instrs: int  # Number of instructions actually executed
    stat_json_data: Optional[Dict]  # Full stat.json content
    memory_check_time_ms: float  # Memory checker time in ms
    rtl_emu_time_ms: float  # RTL emulation time in ms


def run_emulator_with_stats(
    emu_cmd: List[str],
    emu_log: Path,
    stat_json: Path,
    record_logs: bool = True,
    timeout: int = EMULATOR_TIMEOUT_SECONDS,
    save_cmd_script: Optional[Path] = None,
) -> EmulatorResult:
    """Run emulator and extract timing stats from stat.json.
    
    This is a shared helper for all fuzzing methods (fuzzgpu, cascade, divefuzz).
    
    Args:
        emu_cmd: Emulator command to run
        emu_log: Path to write emulator log
        stat_json: Path where emulator writes stat.json
        record_logs: Whether to record logs
        timeout: Timeout in seconds
        save_cmd_script: Optional path to save command as shell script
    
    Returns:
        EmulatorResult with timing stats and stat.json content
    """
    # Optionally save command as shell script for debugging
    if save_cmd_script:
        with open(save_cmd_script, 'w') as f:
            f.write('#!/bin/bash\n' + ' '.join(emu_cmd) + '\n')
    
    # Run emulator
    rc, emu_time = run_command(emu_cmd, str(emu_log), timeout=timeout, record_logs=record_logs)
    
    # Read stat.json content and extract timing breakdown
    stat_json_data = None
    memory_check_time_ms = 0.0
    rtl_emu_time_ms = 0.0
    emu_success = False
    emu_num_instrs = 0
    
    try:
        with open(stat_json, 'r') as f:
            stat_json_data = json.load(f)
            emu_num_instrs = stat_json_data.get('num_executed_instr', 0)
            emu_success = emu_num_instrs > 0
            # Extract memory checker timing stats
            mem_checker_stats = stat_json_data.get('mem_checker_stats', {})
            build_graph_time_us = mem_checker_stats.get('build_graph_time_us', 0)
            verification_time_us = mem_checker_stats.get('verification_time_us', 0)
            memory_check_time_ms = (build_graph_time_us + verification_time_us) / 1000.0
            rtl_emu_time_ms = max(0, emu_time * 1000.0 - memory_check_time_ms)
    except Exception:
        pass
    
    return EmulatorResult(
        emu_time=emu_time,
        emu_success=emu_success,
        emu_num_instrs=emu_num_instrs,
        stat_json_data=stat_json_data,
        memory_check_time_ms=memory_check_time_ms,
        rtl_emu_time_ms=rtl_emu_time_ms,
    )


# =============================================================================
# Phase 1 Workers: Generation & Simulation
# =============================================================================

def run_fuzzgpu_worker(args, worker_id, fuzz_exe, emu_exe, work_dir, fuzzgpu_dir):
    """Phase 1 worker for fuzzgpu method.
    
    File locations:
      - Temp files (instr.bin, data.bin, aux.bin, meta.json, gen.log, stat.json): work_dir
      - Persistent logs (emu.log): _results_dir/logs/
    """
    global _seed_counter, _total_instrs, _first_gen_start_time, _random_selection
    
    # Create logs directory in results_dir for persistent emu logs
    logs_dir = _results_dir / 'logs'
    os.makedirs(logs_dir, exist_ok=True)
    
    while not _stop_event.is_set():
        if check_stop_conditions(args.target_instr_count, args.target_time):
            break
        
        with _stats_lock:
            seed = _seed_counter
            _seed_counter += 1

        sim_start = time.time()
        with _stats_lock:
            if _first_gen_start_time is None:
                _first_gen_start_time = sim_start
                with _phase1_lock:
                    if _phase1_results:
                        _phase1_results.first_gen_start_time = sim_start
        
        # Temp files in work_dir (avoid SSD wear)
        test_dir = work_dir / f'testcase_{seed}'
        instr = test_dir / 'instr.bin'
        data = test_dir / 'data.bin'
        aux = test_dir / 'aux.bin'
        jsonf = test_dir / 'meta.json'
        stat_json = test_dir / 'stat.json'
        
        # Persistent logs in results_dir/logs/
        fuzz_log = logs_dir / f'testcase_{seed}_gen.log'
        emu_log = logs_dir / f'testcase_{seed}_emu.log'
        fuzz_cmd_sh = logs_dir / f'testcase_{seed}_fuzz_cmd.sh'
        emu_cmd_sh = logs_dir / f'testcase_{seed}_emu_cmd.sh'
        
        os.makedirs(test_dir, exist_ok=True)
        
        max_num_instrs = args.force_max_num_instrs or random.randrange(100, 10000)
        
        # Freeze the filter contents for both instruction discovery and generation.
        # The shared filter may be updated by another worker between subprocesses.
        bug_filter_json_str = snapshot_bug_filter_json(work_dir, test_dir)
        
        # Re-fetch instruction info before each fuzz run with the same bug filter
        # This ensures instruction set is consistent with bug filtering
        instr_prob_json_file = ''
        try:
            instr_info = get_instr_info(fuzz_exe, bug_filter_json_str, False)
            instr_names = [info['name'] for info in instr_info]
            n_instrs = len(instr_info)
            
            # Create new RandomSelection for this run with seed for reproducibility
            with _stats_lock:
                _random_selection = RandomSelection(
                    n_instrs=n_instrs,
                    instr_names=instr_names,
                    instr_info=instr_info,
                    seed=seed
                )
                instr_prob_json_file = str(test_dir / 'instr_prob.json')
                _random_selection.save_prob_json(Path(instr_prob_json_file))
        except Exception as e:
            print(f"  {YELLOW}Worker {worker_id}: get_instr_info failed: {e}{RESET}")
            continue
        
        fuzz_cmd = [
            fuzz_exe,
            '--seed', str(seed),
            '--num-genblocks', str(args.num_genblocks),
            '--num-genwarps-per-block', str(args.num_genwarps_per_block),
            '--max-num-instrs', str(max_num_instrs),
            '--max-num-fuzzing-instrs-per-bb', str(args.max_instrs_per_bb),
            '--min-num-fuzzing-instrs-per-bb', str(args.min_instrs_per_bb),
            '--instr-prob-json', instr_prob_json_file,
            '--log-level', 'warn',
            '--disable-default-bug-filtering',
            str(instr), str(data), str(aux), str(jsonf)
        ]
        
        if bug_filter_json_str:
            fuzz_cmd.extend(['--bug-filter-json', bug_filter_json_str])
        
        with open(fuzz_cmd_sh, 'w') as f:
            f.write('#!/bin/bash\n' + ' '.join(fuzz_cmd) + '\n')
        
        gen_start = time.time()
        rc1, gen_time = run_command(fuzz_cmd, str(fuzz_log), record_logs=args.record_logs)
        if rc1 != 0:
            print(f"  {YELLOW}Worker {worker_id}: gen failed seed {seed}{RESET}")
            continue
        
        num_instrs = 0
        file_write_time_us = 0
        try:
            with open(jsonf, 'r') as f:
                meta_data = json.load(f)
                num_instrs = meta_data.get('num_instrs', 0)
                # Extract file_write_time_us if available (fuzzgpu meta.json)
                file_write_time_us = meta_data.get('file_write_time_us', 0)
                with _stats_lock:
                    _total_instrs += num_instrs
        except Exception:
            pass
        
        # Calculate program construction time (excluding file write)
        file_write_time_ms = file_write_time_us / 1000.0
        program_construction_time_ms = max(0, gen_time * 1000.0 - file_write_time_ms)
        
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
        
        # Run emulator and extract stats
        emu_result = run_emulator_with_stats(
            emu_cmd, emu_log, stat_json,
            record_logs=args.record_logs,
            save_cmd_script=emu_cmd_sh
        )
        sim_end = time.time()
        
        # emu_log_path is relative to results_dir (where logs are stored)
        rel_path = str(emu_log.relative_to(_results_dir))
        record = TestRecord(
            seed_id=seed,
            emu_log_path=rel_path,
            sim_start_time=sim_start,
            sim_end_time=sim_end,
            num_instrs=num_instrs,
            emu_num_instrs=emu_result.emu_num_instrs,
            success=emu_result.emu_success,
            stat_json_data=emu_result.stat_json_data,
            gen_time=gen_time,
            emu_time=emu_result.emu_time,
            program_construction_time_ms=program_construction_time_ms,
            file_write_time_ms=file_write_time_ms,
            memory_check_time_ms=emu_result.memory_check_time_ms,
            rtl_emu_time_ms=emu_result.rtl_emu_time_ms,
        )
        add_test_record(record)
        
        # Quick bug detect for filter (bug_filter.json is in work_dir)
        phase1_quick_bug_detect(emu_log, seed, work_dir, sim_end_time=sim_end)
        
        # Update Random selection (for statistics tracking)
        if _random_selection is not None:
            with _stats_lock:
                _random_selection.update()
                
                # Periodically save selection state
                if seed % 10 == 0:
                    _random_selection.save_state(_results_dir / 'random_state.json')
                    stats = _random_selection.get_statistics()
                    with open(_results_dir / 'random_stats.json', 'w') as f:
                        json.dump(stats, f, indent=2)
        
        if emu_result.emu_success:
            print(f"  {GREEN}Worker {worker_id}: seed {seed} done{RESET}")
        
        if seed % 10 == 0:
            save_phase1_results(_results_dir)


def run_cascade_worker(args, worker_id, emu_exe, work_dir, gen_script, arch_config, fuzzgpu_dir):
    """Phase 1 worker for cascade method.
    
    File locations:
      - Temp files (elf, gen.log, stat.json): work_dir
      - Persistent logs (emu.log): _results_dir/logs/
    """
    global _run_counter, _total_instrs, _first_gen_start_time
    
    # Create logs directory in results_dir for persistent emu logs
    logs_dir = _results_dir / 'logs'
    os.makedirs(logs_dir, exist_ok=True)
    
    while not _stop_event.is_set():
        if check_stop_conditions(args.target_instr_count, args.target_time):
            break
        
        with _stats_lock:
            run_id = _run_counter
            _run_counter += 1
        
        sim_start = time.time()
        with _stats_lock:
            if _first_gen_start_time is None:
                _first_gen_start_time = sim_start
                with _phase1_lock:
                    if _phase1_results:
                        _phase1_results.first_gen_start_time = sim_start
        
        # Temp files in work_dir
        run_dir = work_dir / f'run_{run_id}'
        elf_out_dir = run_dir / 'elf'
        os.makedirs(elf_out_dir, exist_ok=True)
        
        # Persistent logs in results_dir/logs/
        gen_log = logs_dir / f'run_{run_id}_gen.log'
        
        num_warps = args.num_genwarps_per_block if args.arch == 'ventus' else arch_config.get('vortex_nr_warps_per_core', 4)
        design_name = f"{args.arch}{args.xlen}" if args.arch == 'vortex' else args.arch
        bug_filter_json_str = get_bug_filter_path_if_exists(work_dir)
        
        gen_cmd = [
            sys.executable, str(gen_script),
            str(args.cascade_num_seeds), str(elf_out_dir),
            '--num-warps-per-core', str(num_warps),
            '--num-threads-per-warp', str(arch_config.get('nr_threads_per_warp', 4)),
            '--arch', design_name,
            '--emulator-path', str(emu_exe),
            '--seed-id', str(run_id)
        ]
        if bug_filter_json_str:
            gen_cmd.extend(['--bug-filter-json', bug_filter_json_str])
        
        if args.arch == 'vortex':
            gen_cmd += ['--vortex-max-num-barrier', str(arch_config.get('num_barriers', 4)),
                        '--vortex-max-barrier-count', str(max(arch_config.get('vortex_nr_warps_per_core', 4), arch_config.get('vortex_nr_cores_per_cluster', 1)))]
        
        gen_start = time.time()
        rc1, gen_time = run_command(gen_cmd, str(gen_log), record_logs=args.record_logs)
        if rc1 != 0:
            print(f"  {RED}Worker {worker_id}: cascade gen failed run {run_id}{RESET}")
            continue
        
        # For cascade, gen_time is the total generation time for all seeds
        gen_time_per_seed = gen_time / max(args.cascade_num_seeds, 1)
        
        elf_files = list(elf_out_dir.glob('*.elf'))
        for idx, elf_path in enumerate(elf_files):
            if _stop_event.is_set():
                break
            
            elf_name = elf_path.stem
            seed_id = run_id * args.cascade_num_seeds + idx
            
            # Persistent emu log in results_dir/logs/
            emu_log = logs_dir / f'run_{run_id}_{elf_name}_emu.log'
            # Temp stat.json in work_dir
            stat_json = run_dir / f'{elf_name}.stat.json'
            
            emu_cmd = [
                emu_exe, '--elf', str(elf_path),
                '--elf-num-genblocks', str(args.num_genblocks),
                '--elf-num-genwarps-per-block', str(args.num_genwarps_per_block),
                '--elf-num-threads-per-warp', str(cascade_elf_num_threads(elf_out_dir, elf_name, arch_config.get('nr_threads_per_warp', 4))),
                '--stat-jsonfile', str(stat_json)
            ]
            if args.emulate:
                emu_cmd.append('--emulate')
            
            # Run emulator and extract stats
            emu_result = run_emulator_with_stats(
                emu_cmd, emu_log, stat_json,
                record_logs=args.record_logs
            )
            sim_end = time.time()
            
            num_instrs = 0
            numinstrs_file = elf_out_dir / f'{elf_name}_numinstrs.txt'
            try:
                if numinstrs_file.exists():
                    num_instrs = int(numinstrs_file.read_text().strip(), 16)
                    with _stats_lock:
                        _total_instrs += num_instrs
            except Exception:
                pass
            
            # emu_log_path is relative to results_dir
            rel_path = str(emu_log.relative_to(_results_dir))
            record = TestRecord(
                seed_id=seed_id,
                emu_log_path=rel_path,
                sim_start_time=sim_start,
                sim_end_time=sim_end,
                num_instrs=num_instrs,
                emu_num_instrs=emu_result.emu_num_instrs,
                success=emu_result.emu_success,
                stat_json_data=emu_result.stat_json_data,
                gen_time=gen_time_per_seed,
                emu_time=emu_result.emu_time,
                program_construction_time_ms=gen_time_per_seed * 1000.0,  # For cascade, all gen time is program construction
                file_write_time_ms=0.0,
                memory_check_time_ms=emu_result.memory_check_time_ms,
                rtl_emu_time_ms=emu_result.rtl_emu_time_ms,
            )
            add_test_record(record)
            
            # Quick bug detect (no filter maintenance for cascade)
            phase1_quick_bug_detect(emu_log, seed_id, work_dir, sim_end_time=sim_end)
        
        print(f"  {GREEN}Worker {worker_id}: run {run_id} done ({len(elf_files)} tests){RESET}")
        if run_id % 5 == 0:
            save_phase1_results(_results_dir)


def run_divefuzz_worker(args, worker_id, emu_exe, work_dir, divefuzz_dir, wrapper_script, arch_config, fuzzgpu_dir):
    """Phase 1 worker for divefuzz method.
    
    File locations:
      - Temp files (seeds, elf, gen.log, stat.json): work_dir
      - Persistent logs (emu.log): _results_dir/logs/
    """
    global _run_counter, _total_instrs, _first_gen_start_time
    
    # Create logs directory in results_dir for persistent emu logs
    logs_dir = _results_dir / 'logs'
    os.makedirs(logs_dir, exist_ok=True)
    
    # Temp directories in work_dir
    shared_out_dir = work_dir / 'seeds'
    shared_elf_dir = work_dir / 'elf_file'
    shared_mutate_dir = work_dir / 'mutate'
    shared_mutate_elf_dir = work_dir / 'mutate' / 'elf_file'
    os.makedirs(shared_out_dir, exist_ok=True)
    os.makedirs(shared_elf_dir, exist_ok=True)
    os.makedirs(shared_mutate_dir, exist_ok=True)
    os.makedirs(shared_mutate_elf_dir, exist_ok=True)
    
    xor_cache_file = work_dir / 'xor_cache.json'
    if not xor_cache_file.exists():
        with open(xor_cache_file, 'w') as f:
            json.dump({}, f)
    
    num_warps = args.num_genwarps_per_block if args.arch == 'ventus' else arch_config.get('vortex_nr_warps_per_core', 4)
    
    while not _stop_event.is_set():
        if check_stop_conditions(args.target_instr_count, args.target_time):
            break
        
        with _stats_lock:
            seed_id = _run_counter
            _run_counter += 1

        sim_start = time.time()
        with _stats_lock:
            if _first_gen_start_time is None:
                _first_gen_start_time = sim_start
                with _phase1_lock:
                    if _phase1_results:
                        _phase1_results.first_gen_start_time = sim_start
        
        # Persistent gen log in results_dir/logs/, temp output_json in work_dir
        gen_log = logs_dir / f'seed_{seed_id}_gen.log'
        output_json = work_dir / f'gen_{seed_id}.json'
        bug_filter_json_str = get_bug_filter_path_if_exists(work_dir)
        
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
            '--num-warps-per-core', str(num_warps),
            '--num-threads-per-warp', str(arch_config.get('nr_threads_per_warp', 4))
        ]
        
        if args.arch == 'vortex':
            wrapper_cmd += ['--vortex-max-num-barrier', str(arch_config.get('num_barriers', 4)),
                            '--vortex-max-barrier-count', str(num_warps),
                            '--vortex-cores-per-clusters', str(arch_config['vortex_nr_cores_per_cluster'])]

        if bug_filter_json_str:
            wrapper_cmd.extend([
                '--disable-default-bug-filtering',
                '--bug-filter-json', bug_filter_json_str,
            ])
        
        if args.disable_eliminate:
            wrapper_cmd.append('--disable-eliminate')
        
        rc_gen, gen_time = run_command(wrapper_cmd, str(gen_log), timeout=EMULATOR_TIMEOUT_SECONDS,
                                 record_logs=args.record_logs)
        
        if rc_gen != 0 or not output_json.exists():
            continue
        
        try:
            with open(output_json, 'r') as f:
                gen_result = json.load(f)
            if not gen_result.get('success'):
                continue
            asm_file = Path(gen_result['asm_file']) if gen_result.get('asm_file') else None
            elf_path = Path(gen_result['elf_file']) if gen_result.get('elf_file') else None
            total_count = gen_result.get('instr_count', 0)
            with _stats_lock:
                _total_instrs += total_count
        except Exception:
            continue
        
        if elf_path and elf_path.exists():
            # Persistent emu log in results_dir/logs/
            emu_log = logs_dir / f'seed_{seed_id}_emu.log'
            # Temp stat.json in work_dir
            stat_json = work_dir / f'seed_{seed_id}.stat.json'
            
            emu_cmd = [
                emu_exe, '--elf', str(elf_path),
                '--elf-num-genblocks', str(args.num_genblocks),
                '--elf-num-genwarps-per-block', str(args.num_genwarps_per_block),
                '--elf-num-threads-per-warp', str(arch_config.get('nr_threads_per_warp', 4)),
                '--stat-jsonfile', str(stat_json)
            ]
            if args.emulate:
                emu_cmd.append('--emulate')
            
            # Run emulator and extract stats
            emu_result = run_emulator_with_stats(
                emu_cmd, emu_log, stat_json,
                record_logs=args.record_logs
            )
            sim_end = time.time()
            
            # emu_log_path is relative to results_dir
            rel_path = str(emu_log.relative_to(_results_dir))
            record = TestRecord(
                seed_id=seed_id,
                emu_log_path=rel_path,
                sim_start_time=sim_start,
                sim_end_time=sim_end,
                num_instrs=total_count,
                emu_num_instrs=emu_result.emu_num_instrs,
                success=emu_result.emu_success,
                stat_json_data=emu_result.stat_json_data,
                gen_time=gen_time,
                emu_time=emu_result.emu_time,
                program_construction_time_ms=gen_time * 1000.0,  # For divefuzz, all gen time is program construction
                file_write_time_ms=0.0,
                memory_check_time_ms=emu_result.memory_check_time_ms,
                rtl_emu_time_ms=emu_result.rtl_emu_time_ms,
            )
            add_test_record(record)
            
            # Quick bug detect (no filter maintenance for divefuzz)
            phase1_quick_bug_detect(emu_log, seed_id, work_dir, sim_end_time=sim_end)

        if asm_file and asm_file.exists() and args.divefuzz_mutation_times > 0:
            for mut_idx in range(args.divefuzz_mutation_times):
                if _stop_event.is_set() or check_stop_conditions(args.target_instr_count, args.target_time):
                    break

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
                    '--num-warps-per-core', str(num_warps),
                    '--num-threads-per-warp', str(arch_config.get('nr_threads_per_warp', 4))
                ]

                if args.arch == 'vortex':
                    mut_wrapper_cmd += ['--vortex-max-num-barrier', str(arch_config.get('num_barriers', 4)),
                                        '--vortex-max-barrier-count', str(num_warps),
                                        '--vortex-cores-per-clusters', str(arch_config['vortex_nr_cores_per_cluster'])]

                mut_bug_filter_json_str = get_bug_filter_path_if_exists(work_dir)
                if mut_bug_filter_json_str:
                    mut_wrapper_cmd.extend([
                        '--disable-default-bug-filtering',
                        '--bug-filter-json', mut_bug_filter_json_str,
                    ])

                if args.disable_eliminate:
                    mut_wrapper_cmd.append('--disable-eliminate')

                rc_mut, mut_time = run_command(mut_wrapper_cmd, str(mut_log), timeout=EMULATOR_TIMEOUT_SECONDS,
                                               record_logs=args.record_logs)

                if rc_mut != 0 or not mut_output_json.exists():
                    continue

                try:
                    with open(mut_output_json, 'r') as f:
                        mut_result = json.load(f)
                    if not mut_result.get('success'):
                        continue
                    mut_elf_path = Path(mut_result['elf_file']) if mut_result.get('elf_file') else None
                    mut_count = mut_result.get('instr_count', 0)
                    with _stats_lock:
                        _total_instrs += mut_count
                except Exception:
                    continue

                if mut_elf_path and mut_elf_path.exists():
                    mut_sim_start = time.time()
                    mut_emu_log = logs_dir / f'seed_{seed_id}_mut_{mut_idx}_emu.log'
                    mut_stat_json = work_dir / f'seed_{seed_id}_mut_{mut_idx}.stat.json'

                    mut_emu_cmd = [
                        emu_exe, '--elf', str(mut_elf_path),
                        '--elf-num-genblocks', str(args.num_genblocks),
                        '--elf-num-genwarps-per-block', str(args.num_genwarps_per_block),
                        '--elf-num-threads-per-warp', str(arch_config.get('nr_threads_per_warp', 4)),
                        '--stat-jsonfile', str(mut_stat_json)
                    ]
                    if args.emulate:
                        mut_emu_cmd.append('--emulate')

                    mut_emu_result = run_emulator_with_stats(
                        mut_emu_cmd, mut_emu_log, mut_stat_json,
                        record_logs=args.record_logs
                    )
                    mut_sim_end = time.time()

                    rel_path = str(mut_emu_log.relative_to(_results_dir))
                    record = TestRecord(
                        seed_id=seed_id,
                        emu_log_path=rel_path,
                        sim_start_time=mut_sim_start,
                        sim_end_time=mut_sim_end,
                        num_instrs=mut_count,
                        emu_num_instrs=mut_emu_result.emu_num_instrs,
                        success=mut_emu_result.emu_success,
                        stat_json_data=mut_emu_result.stat_json_data,
                        gen_time=mut_time,
                        emu_time=mut_emu_result.emu_time,
                        program_construction_time_ms=mut_time * 1000.0,
                        file_write_time_ms=0.0,
                        memory_check_time_ms=mut_emu_result.memory_check_time_ms,
                        rtl_emu_time_ms=mut_emu_result.rtl_emu_time_ms,
                    )
                    add_test_record(record)

                    phase1_quick_bug_detect(mut_emu_log, seed_id, work_dir, sim_end_time=mut_sim_end)
        
        print(f"  {GREEN}Worker {worker_id}: seed {seed_id} done (base + {args.divefuzz_mutation_times} mutations){RESET}")
        if seed_id % 10 == 0:
            save_phase1_results(_results_dir)


# =============================================================================
# Phase 2: Bug Detection & Analysis (Idempotent)
# =============================================================================

def _print_phase2_summary(phase2: Phase2Results):
    """Print Phase2 summary to console."""
    print(f"\n{GREEN}=== Summary ==={RESET}")
    print(f"Known bugs: {len(phase2.known_bugs)}")
    for n, r in sorted(phase2.known_bugs.items(), key=lambda x: x[1].first_detection_time):
        print(f"  - {n}: {r.detection_count}x, first @ ts={r.first_detection_time:.2f}s ({r.core_hours:.4f} ch)")


def _scan_single_log(args_tuple):
    """Worker function for parallel log scanning.
    
    Args:
        args_tuple: (emu_log_path, seed_id, sim_end_time, first_gen_start_time)
    
    Returns:
        Tuple of (seed_id, found_bugs_data, error_msg) where found_bugs_data is a list of dicts
    """
    emu_log_path, seed_id, sim_end_time, first_gen_start_time = args_tuple
    try:
        emu_log = Path(emu_log_path)
        if not emu_log.exists():
            return (seed_id, None, 'missing')
        
        found_bugs, _ = scan_file(emu_log)
        
        # Convert bugs to serializable format with pre-computed detection time
        bugs_data = []
        for bug in found_bugs:
            bug_ts = bug.timestamp if bug.timestamp > 0 else sim_end_time
            detection_time = bug_ts - first_gen_start_time
            bugs_data.append({
                'bug_name': bug.bug_name,
                'detection_time': detection_time,
                'bug_dict': bug.__dict__ if hasattr(bug, '__dict__') else None,
            })
        return (seed_id, bugs_data, None)
    except Exception as e:
        return (seed_id, None, str(e))


def run_phase2_analysis(results_dir: Path, num_workers: int = None) -> Phase2Results:
    """
    Phase 2: Re-analyze all emulator logs using bug_detector.
    
    This function independently scans all emu logs - it does NOT use
    Phase1's bug_filter_list. The bug_filter_list is only for fuzzgpu
    generation feedback during Phase 1.
    
    IDEMPOTENT: same emu logs + same bug_detector = same output.
    
    Args:
        results_dir: Path to results directory containing phase1_results.json and logs/
    
    Returns:
        Phase2Results object
    
    Logs are stored in results_dir/logs/, so --analyze only needs results_dir.
    """
    print(f"{BLUE}=== Phase 2: Bug Detection & Analysis ==={RESET}")
    print(f"  Results directory: {results_dir}")
    
    phase1_file = results_dir / 'phase1_results.json'
    phase2_file = results_dir / 'phase2_results.json'
    
    if not phase1_file.exists():
        raise FileNotFoundError(f"Phase 1 results not found: {phase1_file}")
    
    with open(phase1_file, 'r') as f:
        phase1 = Phase1Results.from_dict(json.load(f))
    
    print(f"  Method: {phase1.method}, Arch: {phase1.arch}/x{phase1.xlen}")
    print(f"  Tests: {len(phase1.tests)}, Instrs: {phase1.total_instrs}")
    
    # Logs are now stored in results_dir (e.g., results_dir/logs/testcase_0_emu.log)
    # emu_log_path in TestRecord is relative to results_dir
    
    # Logs are in results_dir - perform analysis
    phase2 = Phase2Results(
        method=phase1.method,
        arch=phase1.arch,
        xlen=phase1.xlen,
        start_seed=phase1.start_seed,
        max_num_instrs=phase1.max_num_instrs,
        first_gen_start_time=phase1.first_gen_start_time,
        last_sim_end_time=phase1.last_sim_end_time,
        total_tests=len(phase1.tests),
        total_instrs=phase1.total_instrs,
        analysis_timestamp=time.time(),
    )
    
    # Validate first_gen_start_time before parallel processing
    assert phase1.first_gen_start_time > 0, \
        f"Invalid first_gen_start_time ({phase1.first_gen_start_time}): cannot compute relative detection time"
    
    # Prepare tasks for parallel processing
    scan_tasks = []
    for test in phase1.tests:
        emu_log_path = str(results_dir / test.emu_log_path)
        scan_tasks.append((emu_log_path, test.seed_id, test.sim_end_time, phase1.first_gen_start_time))
    
    # Determine number of workers
    if num_workers is None:
        num_workers = min(cpu_count(), len(scan_tasks), 32)  # Cap at 32 workers
    num_workers = max(1, num_workers)
    
    print(f"  Scanning {len(scan_tasks)} logs with {num_workers} workers...")
    
    logs_scanned = 0
    logs_missing = 0
    scan_errors = 0
    
    # Use ProcessPoolExecutor for CPU-bound log scanning
    with ProcessPoolExecutor(max_workers=num_workers) as executor:
        # Submit all tasks
        futures = {executor.submit(_scan_single_log, task): task[1] for task in scan_tasks}
        
        # Process results as they complete
        for future in as_completed(futures):
            seed_id = futures[future]
            try:
                result_seed_id, bugs_data, error_msg = future.result()
                
                if error_msg == 'missing':
                    logs_missing += 1
                    continue
                elif error_msg:
                    scan_errors += 1
                    print(f"  {YELLOW}Error scanning seed {result_seed_id}: {error_msg}{RESET}")
                    continue
                
                logs_scanned += 1
                
                # Process found bugs
                for bug_info in bugs_data:
                    name = bug_info['bug_name']
                    detection_time = bug_info['detection_time']
                    core_hours = (detection_time / 3600.0) * NUM_CORES
                    
                    if name not in phase2.known_bugs:
                        phase2.known_bugs[name] = BugRecord(
                            bug_name=name,
                            first_detection_time=detection_time,
                            first_detection_seed=result_seed_id,
                            detection_count=1,
                            core_hours=core_hours,
                            first_occurrence=bug_info['bug_dict'],
                        )
                        print(f"  {GREEN}Bug: {name} @ seed {result_seed_id}, "
                              f"ts={detection_time:.2f}s ({core_hours:.4f} ch){RESET}")
                    else:
                        phase2.known_bugs[name].detection_count += 1
                        
            except Exception as e:
                scan_errors += 1
                print(f"  {YELLOW}Future error for seed {seed_id}: {e}{RESET}")
    
    if logs_missing > 0:
        print(f"  {YELLOW}Warning: {logs_missing} logs missing{RESET}")
    if scan_errors > 0:
        print(f"  {YELLOW}Warning: {scan_errors} scan errors{RESET}")
    print(f"  Scanned {logs_scanned} logs")
    
    # Save phase2_results.json to results directory
    with open(phase2_file, 'w') as f:
        json.dump(phase2.to_dict(), f, indent=2)
    print(f"{GREEN}Phase 2 saved: {phase2_file}{RESET}")
    
    _print_phase2_summary(phase2)
    return phase2


# =============================================================================
# Main: Run Phase 1 and/or Phase 2
# =============================================================================

def run_phase1(args, arch: str, xlen: int, fuzzgpu_dir: Path) -> Tuple[Path, Path]:
    """Run Phase 1: generation & simulation.
    
    Returns:
        Tuple of (results_dir, work_dir) paths, or (None, None) on error.
    """
    global _phase1_results, _bug_filter_set, _seed_counter, _run_counter
    global _first_gen_start_time, _total_instrs, _results_dir, _work_dir
    global _expected_bugs
    
    _seed_counter = args.start_seed
    _run_counter = args.start_seed
    _first_gen_start_time = None
    _total_instrs = 0
    _bug_filter_set.clear()
    _stop_event.clear()
    
    # Load expected bugs from --bug-complete-list if provided
    _expected_bugs = None
    if args.bug_complete_list:
        try:
            with open(args.bug_complete_list, 'r') as f:
                bug_list_data = json.load(f)
            xlen_str = str(xlen)
            if arch in bug_list_data and xlen_str in bug_list_data[arch]:
                _expected_bugs = set(bug_list_data[arch][xlen_str])
                print(f"{BLUE}Loaded {len(_expected_bugs)} expected bugs for {arch}/x{xlen}: {sorted(_expected_bugs)}{RESET}")
            else:
                print(f"{YELLOW}No expected bugs defined for {arch}/x{xlen} in {args.bug_complete_list}{RESET}")
        except Exception as e:
            print(f"{RED}Failed to load bug complete list: {e}{RESET}")
    
    build_dir = fuzzgpu_dir / f"build{xlen}-{arch}-covnone-dft1"
    fuzz_exe = find_executable(str(build_dir / 'fuzz_gpu'))
    emu_exe = find_executable(str(build_dir / 'emulator'))
    
    if not emu_exe:
        print(f"{RED}Emulator not found: {build_dir / 'emulator'}{RESET}")
        return None, None
    
    timestamp = datetime.datetime.now().strftime('%Y%m%d_%H%M%S')
    suffix = f"_maxinstr{args.force_max_num_instrs}" if args.force_max_num_instrs else ""
    run_name = f"{args.method}{suffix}_seed{args.start_seed}_{timestamp}"
    
    # Results directory: scripts/../results/{results_subdir}/{method}_{arch}_{xlen}_seed{start_seed}/
    # When --bug-complete-list is used, include force_max_num_instrs in folder name
    results_base_dir = script_dir.parent / 'results' / args.results_subdir
    if args.force_max_num_instrs:
        results_subfolder = f"{args.method}_{arch}_{xlen}_maxinstr{args.force_max_num_instrs}_seed{args.start_seed}"
    else:
        results_subfolder = f"{args.method}_{arch}_{xlen}_seed{args.start_seed}"
    results_dir = results_base_dir / results_subfolder
    os.makedirs(results_dir, exist_ok=True)
    
    # Work directory: --work-dir/{arch}_x{xlen}/{run_name}/
    work_dir = Path(args.work_dir) / f"{arch}_x{xlen}" / run_name
    os.makedirs(work_dir, exist_ok=True)
    
    _results_dir = results_dir
    _work_dir = work_dir
    
    loop_start_time = time.time()
    
    _phase1_results = Phase1Results(
        method=args.method,
        arch=arch,
        xlen=xlen,
        start_seed=args.start_seed,
        max_num_instrs=args.force_max_num_instrs,
        first_gen_start_time=0,
        work_dir=str(work_dir),
        loop_start_time=loop_start_time,
    )
    
    arch_config = get_arch_config(build_dir)
    
    # Save config to both results and work directories
    config_data = {
        'method': args.method, 'arch': arch, 'xlen': xlen,
        'start_seed': args.start_seed,
        'force_max_num_instrs': args.force_max_num_instrs,
        'target_instr_count': args.target_instr_count,
        'target_time': args.target_time,
        'jobs': args.jobs, 'arch_config': arch_config,
        'results_dir': str(results_dir),
        'work_dir': str(work_dir),
    }
    with open(results_dir / 'config.json', 'w') as f:
        json.dump(config_data, f, indent=2)
    with open(work_dir / 'config.json', 'w') as f:
        json.dump(config_data, f, indent=2)
    
    print(f"{BLUE}=== Phase 1: Generation & Simulation ==={RESET}")
    print(f"  Method: {args.method}, Arch: {arch}/x{xlen}")
    print(f"  Results: {results_dir}")
    print(f"  Work: {work_dir}")
    
    args.arch = arch
    args.xlen = xlen
    
    # Reset Random selection (will be initialized per-run in worker with current bug filter)
    global _random_selection
    _random_selection = None
    if args.method == 'fuzzgpu':
        print(f"{BLUE}Random selection enabled (per-run initialization){RESET}")
        if not fuzz_exe:
            print(f"{RED}fuzz_gpu not found{RESET}")
            return None, None
    
    try:
        if args.method == 'fuzzgpu':
            if not fuzz_exe:
                print(f"{RED}fuzz_gpu not found{RESET}")
                return None, None
            with ThreadPoolExecutor(max_workers=args.jobs) as ex:
                futs = [ex.submit(run_fuzzgpu_worker, args, i, fuzz_exe, emu_exe, work_dir, fuzzgpu_dir)
                        for i in range(args.jobs)]
                for f in as_completed(futs):
                    try: f.result()
                    except Exception as e: print(f"{RED}Error: {e}{RESET}")
        
        elif args.method == 'cascade':
            cascade_dir = Path(args.cascade_dir) if args.cascade_dir else fuzzgpu_dir / 'baseline' / 'cascade-meta'
            gen_script = cascade_dir / 'fuzzer' / 'do_genmanyelfs.py'
            if not gen_script.exists():
                print(f"{RED}Cascade gen not found: {gen_script}{RESET}")
                return None, None
            with ThreadPoolExecutor(max_workers=args.jobs) as ex:
                futs = [ex.submit(run_cascade_worker, args, i, emu_exe, work_dir, gen_script, arch_config, fuzzgpu_dir)
                        for i in range(args.jobs)]
                for f in as_completed(futs):
                    try: f.result()
                    except Exception as e: print(f"{RED}Error: {e}{RESET}")
        
        elif args.method == 'divefuzz':
            divefuzz_dir = Path(args.divefuzz_dir) if args.divefuzz_dir else fuzzgpu_dir / 'baseline' / 'DiveFuzz'
            wrapper_script = script_dir / 'divefuzz_wrapper.py'
            if not divefuzz_dir.exists() or not wrapper_script.exists():
                print(f"{RED}DiveFuzz setup missing{RESET}")
                return None, None
            with ThreadPoolExecutor(max_workers=args.jobs) as ex:
                futs = [ex.submit(run_divefuzz_worker, args, i, emu_exe, work_dir, divefuzz_dir, wrapper_script, arch_config, fuzzgpu_dir)
                        for i in range(args.jobs)]
                for f in as_completed(futs):
                    try: f.result()
                    except Exception as e: print(f"{RED}Error: {e}{RESET}")
    
    except KeyboardInterrupt:
        print(f"{YELLOW}Interrupted{RESET}")
        _stop_event.set()
    finally:
        # Record loop end time before saving
        with _phase1_lock:
            if _phase1_results:
                _phase1_results.loop_end_time = time.time()
        save_phase1_results(results_dir)
    
    # Calculate and display loop duration
    loop_duration = _phase1_results.loop_end_time - _phase1_results.loop_start_time if _phase1_results else 0
    print(f"{GREEN}Phase 1 complete (duration: {loop_duration:.2f}s){RESET}")
    print(f"  Results: {results_dir}")
    print(f"  Work: {work_dir}")
    return results_dir, work_dir


def scan_and_analyze_all(base_dir: Path, num_workers: int = None) -> List[Phase2Results]:
    """Scan all subdirectories for phase1_results.json and analyze each.
    
    Args:
        base_dir: Base directory to scan for results subdirectories
        num_workers: Number of workers for parallel log scanning
    
    Returns:
        List of Phase2Results for all analyzed directories
    """
    print(f"{BLUE}=== Scanning for results directories ==={RESET}")
    print(f"  Base directory: {base_dir}")
    
    # Find all directories containing phase1_results.json
    results_dirs = []
    for phase1_file in base_dir.rglob('phase1_results.json'):
        results_dirs.append(phase1_file.parent)
    
    if not results_dirs:
        print(f"{YELLOW}No phase1_results.json found in {base_dir} or its subdirectories{RESET}")
        return []
    
    # Sort by path for consistent ordering
    results_dirs.sort()
    
    print(f"  Found {len(results_dirs)} results directories:")
    for d in results_dirs:
        print(f"    - {d.relative_to(base_dir) if d.is_relative_to(base_dir) else d}")
    
    # Analyze each directory
    all_results = []
    for idx, results_dir in enumerate(results_dirs, 1):
        print(f"\n{BLUE}[{idx}/{len(results_dirs)}] Analyzing: {results_dir.name}{RESET}")
        try:
            phase2 = run_phase2_analysis(results_dir, num_workers=num_workers)
            all_results.append(phase2)
        except Exception as e:
            print(f"{RED}Error analyzing {results_dir}: {e}{RESET}")
    
    # Print summary of all results
    if all_results:
        print(f"\n{GREEN}=== Overall Summary ({len(all_results)} directories analyzed) ==={RESET}")
        total_bugs = set()
        for phase2 in all_results:
            for bug_name in phase2.known_bugs:
                total_bugs.add(bug_name)
            print(f"  {phase2.method}_{phase2.arch}_x{phase2.xlen}: {len(phase2.known_bugs)} bugs, {phase2.total_tests} tests")
        print(f"  Total unique bugs across all: {len(total_bugs)}")
    
    return all_results


def main():
    parser = argparse.ArgumentParser(description='Fuzzing loop with two-phase bug detection')
    
    # Mode selection
    parser.add_argument('--analyze', type=str, metavar='DIR',
                        help='Run Phase 2 only: analyze bugs in existing results dir (logs in DIR/logs/). '
                             'Use --analyze-recursive to scan all subdirectories.')
    parser.add_argument('--analyze-recursive', action='store_true',
                        help='When used with --analyze, scan all subdirectories for phase1_results.json')
    
    # Phase 1 options
    parser.add_argument('--method', type=str, choices=['fuzzgpu', 'cascade', 'divefuzz'])
    parser.add_argument('--arch', type=str, choices=['ventus', 'vortex'], default='ventus')
    parser.add_argument('--xlen', type=int, choices=[32, 64], default=32)
    parser.add_argument('--all', action='store_true',
                        help='Run all arch/xlen: vortex/32, vortex/64, ventus/32')
    
    parser.add_argument('--start-seed', type=int, default=0)
    parser.add_argument('--force-max-num-instrs', type=int, default=None)
    
    parser.add_argument('--target-instr-count', type=int, default=None)
    parser.add_argument('--target-time', type=int, default=None)
    
    parser.add_argument('--jobs', type=int, default=1)
    parser.add_argument('--work-dir', type=str, default='/tmp/fuzzing_work',
                        help='Directory for temp files (instr.bin/data.bin/aux.bin, default: /tmp/fuzzing_work)')
    parser.add_argument('--emulate', action='store_true')
    parser.add_argument('--no-log', action='store_false', dest='record_logs')
    
    parser.add_argument('--num-genblocks', type=int, default=2)
    parser.add_argument('--num-genwarps-per-block', type=int, default=2)
    parser.add_argument('--max-instrs-per-bb', type=int, default=500)
    parser.add_argument('--min-instrs-per-bb', type=int, default=1)
    
    parser.add_argument('--cascade-dir', type=str, default=None)
    parser.add_argument('--cascade-num-seeds', type=int, default=10)
    parser.add_argument('--divefuzz-dir', type=str, default=None)
    parser.add_argument('--divefuzz-seed-size', type=int, default=200)
    parser.add_argument('--divefuzz-mutation-times', type=int, default=3,
                        help='Number of mutations to run for each DiveFuzz generated seed')
    parser.add_argument('--disable-eliminate', action='store_true', default=False,
                        help='Disable elimination optimization for divefuzz (enabled by default)')
    parser.add_argument('--bug-complete-list', type=str, default=None,
                        help='JSON file with expected bugs. Stop when all expected bugs are found. Format: {"arch": {"xlen": ["bug1", "bug2", ...]}}')
    parser.add_argument('--results-subdir', type=str, default='fuzzing_results',
                        help='Subdirectory under results/ for storing outputs (default: fuzzing_results)')
    
    args = parser.parse_args()
    fuzzgpu_dir = script_dir.parent
    
    # Phase 2 only mode
    if args.analyze:
        analyze_path = Path(args.analyze)
        if args.analyze_recursive:
            scan_and_analyze_all(analyze_path)
        else:
            run_phase2_analysis(analyze_path)
        return
    
    # Phase 1 + Phase 2
    if not args.method:
        parser.error('--method required for Phase 1')
    if not args.target_instr_count and not args.target_time:
        parser.error('--target-instr-count or --target-time required')
    
    signal.signal(signal.SIGINT, _handle_signal)
    signal.signal(signal.SIGTERM, _handle_signal)
    
    configs = ALL_ARCH_CONFIGS if args.all else [{'arch': args.arch, 'xlen': args.xlen}]
    
    for cfg in configs:
        if _stop_event.is_set():
            break
        results_dir, work_dir = run_phase1(args, cfg['arch'], cfg['xlen'], fuzzgpu_dir)
        if results_dir:
            run_phase2_analysis(results_dir)
    
    print(f"\n{GREEN}All done{RESET}")


if __name__ == '__main__':
    from baseline import _cleanup_gen_emu_processes
    _cleanup_gen_emu_processes()
    main()
