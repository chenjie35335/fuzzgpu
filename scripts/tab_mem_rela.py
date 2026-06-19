#!/usr/bin/env python3
"""Memory relation statistics analysis.

Reads fuzzing_loop.py output (phase1_results.json) and calculates statistics
(median, mean, variance) for memory relations (co, coe, rf, rfe, rfi, fr, ppo).

Scans:
- method: fuzzgpu (fixed)
- arch/xlen: ventus/32, vortex/64

Usage:
    python tab_mem_rela.py

Output:
    Prints a table with statistics for each memory relation per arch/xlen.
    Saves results to: scripts/../results/mem_rela_stats/
"""

from __future__ import annotations

import argparse
import json
import os
import statistics
import sys
from dataclasses import dataclass, asdict
from pathlib import Path
from typing import Dict, List, Any, Optional

# Colors for terminal output
GREEN = "\033[32m"
RED = "\033[31m"
YELLOW = "\033[33m"
BLUE = "\033[34m"
RESET = "\033[0m"

# Script and project directories
script_dir = Path(__file__).resolve().parent
fuzzgpu_dir = script_dir.parent

# Fixed configuration
METHOD = "fuzzgpu"
ARCH_XLEN_CONFIGS = [
    {"arch": "ventus", "xlen": 32},
    {"arch": "vortex", "xlen": 64},
]

# Memory relation keys to analyze (edges only)
MEM_RELATION_KEYS = [
    "co_edges",
    "coe_edges",
    "rf_edges",
    "rfe_edges",
    "rfi_edges",
    "fr_edges",
    "ppo_edges",
]

# Display names for relations (without _edges suffix) - LaTeX formatted
RELATION_DISPLAY_NAMES = {
    "co_edges": r"$\textcolor{deepblue}{\mathit{co}}$",
    "coe_edges": r"$\textcolor{deepblue}{\mathit{coe}}$",
    "rf_edges": r"$\textcolor{deepblue}{\mathit{rf}}$",
    "rfe_edges": r"$\textcolor{deepblue}{\mathit{rfe}}$",
    "rfi_edges": r"$\textcolor{deepblue}{\mathit{rfi}}$",
    "fr_edges": r"$\textcolor{deepblue}{\mathit{fr}}$",
    "ppo_edges": r"$\textcolor{deepblue}{\mathit{ppo}}$",
}

@dataclass
class MemRelationStats:
    """Statistics for a single memory relation metric."""
    key: str
    count: int
    min_val: float # minimum value
    q1: float      # 25th percentile
    median: float  # 50th percentile
    q3: float      # 75th percentile
    max_val: float # maximum value


@dataclass
class ArchStats:
    """Statistics for an architecture configuration."""
    arch: str
    xlen: int
    total_tests: int
    exercised_tests: int
    mem_relation_stats: Dict[str, MemRelationStats]


def find_result_dirs(arch: str, xlen: int) -> List[Path]:
    """Find all fuzzgpu result directories for given arch/xlen."""
    results_base = fuzzgpu_dir / "results" / "fuzzing_results"
    if not results_base.exists():
        return []
    
    prefix = f"{METHOD}_{arch}_{xlen}_seed"
    dirs = []
    for entry in results_base.iterdir():
        if entry.is_dir() and entry.name.startswith(prefix):
            dirs.append(entry)
    return sorted(dirs)


def load_phase1_results(result_dir: Path) -> Optional[Dict]:
    """Load phase1_results.json from a result directory."""
    phase1_file = result_dir / "phase1_results.json"
    if not phase1_file.exists():
        return None
    try:
        with open(phase1_file, "r") as f:
            return json.load(f)
    except Exception as e:
        print(f"{YELLOW}Warning: Failed to load {phase1_file}: {e}{RESET}")
        return None


def extract_mem_relation_data(
    phase1_data: Dict,
) -> List[Dict[str, Any]]:
    """Extract memory relation data from phase1 results.
    
    Returns:
        List of mem_checker_stats dicts that pass exercise criteria.
    """
    exercised_data = []
    tests = phase1_data.get("tests", [])
    
    for test in tests:
        stat_json_data = test.get("stat_json_data")
        if not stat_json_data:
            continue
        
        mem_checker_stats = stat_json_data.get("mem_checker_stats", {})
        
        if mem_checker_stats.get("checked", 0) != 1:
            continue
        
        if test.get("num_instrs", 0) <= 0:
            continue

        exercised_data.append(mem_checker_stats)
    
    return exercised_data


def compute_stats(values: List[float], key: str) -> MemRelationStats:
    """Compute statistics for a list of values."""
    if not values:
        return MemRelationStats(
            key=key,
            count=0,
            min_val=0.0,
            q1=0.0,
            median=0.0,
            q3=0.0,
            max_val=0.0,
        )
    
    sorted_values = sorted(values)
    median_val = statistics.median(sorted_values)
    q1_val = statistics.quantiles(sorted_values, n=4)[0] if len(sorted_values) >= 2 else sorted_values[0]
    q3_val = statistics.quantiles(sorted_values, n=4)[2] if len(sorted_values) >= 2 else sorted_values[-1]
    
    return MemRelationStats(
        key=key,
        count=len(values),
        min_val=min(sorted_values),
        q1=q1_val,
        median=median_val,
        q3=q3_val,
        max_val=max(sorted_values),
    )


def analyze_arch(arch: str, xlen: int) -> ArchStats:
    """Analyze all fuzzgpu results for a given arch/xlen."""
    print(f"\n{BLUE}Analyzing {arch}/x{xlen}...{RESET}")
    
    result_dirs = find_result_dirs(arch, xlen)
    if not result_dirs:
        print(f"  {YELLOW}No result directories found{RESET}")
        return ArchStats(
            arch=arch,
            xlen=xlen,
            total_tests=0,
            exercised_tests=0,
            mem_relation_stats={},
        )
    
    print(f"  Found {len(result_dirs)} result directories")
    
    # Collect all exercised mem_checker_stats
    all_exercised_data: List[Dict[str, Any]] = []
    total_tests = 0
    
    for result_dir in result_dirs:
        phase1_data = load_phase1_results(result_dir)
        if not phase1_data:
            continue
        
        tests = phase1_data.get("tests", [])
        total_tests += len(tests)
        
        exercised = extract_mem_relation_data(phase1_data)
        all_exercised_data.extend(exercised)
        print(f"    {result_dir.name}: {len(tests)} tests, {len(exercised)} exercised")
    
    print(f"  Total: {total_tests} tests, {len(all_exercised_data)} exercised")
    
    # Compute stats for each memory relation key
    mem_relation_stats: Dict[str, MemRelationStats] = {}
    for key in MEM_RELATION_KEYS:
        values = [d.get(key, 0) for d in all_exercised_data]
        stats = compute_stats(values, key)
        mem_relation_stats[key] = stats
    
    return ArchStats(
        arch=arch,
        xlen=xlen,
        total_tests=total_tests,
        exercised_tests=len(all_exercised_data),
        mem_relation_stats=mem_relation_stats,
    )


def print_stats_table(arch_stats_list: List[ArchStats], single_arch: bool = False):
    """Print statistics in LaTeX table format."""
    print(f"\n{GREEN}{'='*80}{RESET}")
    print(f"{GREEN}Memory Relation Statistics (method={METHOD}, checked=1){RESET}")
    print(f"{GREEN}{'='*80}{RESET}")
    
    # Print sample counts separately
    print(f"\n{BLUE}Sample Counts:{RESET}")
    for arch_stats in arch_stats_list:
        print(f"  {arch_stats.arch}/x{arch_stats.xlen}: "
              f"Total={arch_stats.total_tests}, Exercised={arch_stats.exercised_tests}")
    
    # Print LaTeX table
    print(f"\n{BLUE}LaTeX Table:{RESET}")
    print("")
    print(r"\begin{table}[htbp]")
    print(r"\centering")
    print(r"\caption{Memory Relation Edge Statistics}")
    print(r"\label{tab:mem_rela_stats}")
    
    if single_arch:
        # Single arch: no Arch column
        print(r"\begin{tabular}{lrrrrr}")
        print(r"\toprule")
        print(r"Relation & Min & Q1 & Median & Q3 & Max \\")
        print(r"\midrule")
        
        arch_stats = arch_stats_list[0]
        for key in MEM_RELATION_KEYS:
            rela_name = RELATION_DISPLAY_NAMES.get(key, key)
            stats = arch_stats.mem_relation_stats.get(key)
            if not stats:
                continue
            print(f"{rela_name} & {stats.min_val:.0f} & {stats.q1:.0f} & {stats.median:.0f} & {stats.q3:.0f} & {stats.max_val:.0f} \\\\")
    else:
        # Multiple archs: include Arch column
        print(r"\begin{tabular}{llrrrrr}")
        print(r"\toprule")
        print(r"Arch & Relation & Min & Q1 & Median & Q3 & Max \\")
        print(r"\midrule")
        
        num_relations = len(MEM_RELATION_KEYS)
        for i, arch_stats in enumerate(arch_stats_list):
            arch_label = f"{arch_stats.arch}/x{arch_stats.xlen}"
            for j, key in enumerate(MEM_RELATION_KEYS):
                rela_name = RELATION_DISPLAY_NAMES.get(key, key)
                stats = arch_stats.mem_relation_stats.get(key)
                if not stats:
                    continue
                if j == 0:
                    # First relation: use multirow for arch column
                    print(f"\\multirow{{{num_relations}}}{{*}}{{{arch_label}}} & {rela_name} & {stats.min_val:.0f} & {stats.q1:.0f} & {stats.median:.0f} & {stats.q3:.0f} & {stats.max_val:.0f} \\\\")
                else:
                    # Subsequent relations: empty arch column
                    print(f" & {rela_name} & {stats.min_val:.0f} & {stats.q1:.0f} & {stats.median:.0f} & {stats.q3:.0f} & {stats.max_val:.0f} \\\\")
            # Add midrule between different architectures (but not after the last one)
            if i < len(arch_stats_list) - 1:
                print(r"\midrule")
    
    print(r"\bottomrule")
    print(r"\end{tabular}")
    print(r"\end{table}")


def save_results(arch_stats_list: List[ArchStats], output_dir: Path):
    """Save results to JSON files."""
    os.makedirs(output_dir, exist_ok=True)
    
    # Save summary JSON
    summary = {
        "method": METHOD,
        "archs": [],
    }
    
    for arch_stats in arch_stats_list:
        arch_data = {
            "arch": arch_stats.arch,
            "xlen": arch_stats.xlen,
            "total_tests": arch_stats.total_tests,
            "exercised_tests": arch_stats.exercised_tests,
            "mem_relation_stats": {
                key: asdict(stats) for key, stats in arch_stats.mem_relation_stats.items()
            },
        }
        summary["archs"].append(arch_data)
    
    summary_file = output_dir / "mem_rela_stats.json"
    with open(summary_file, "w") as f:
        json.dump(summary, f, indent=2)
    print(f"\n{GREEN}Results saved to: {summary_file}{RESET}")


def main():
    parser = argparse.ArgumentParser(
        description="Analyze memory relation statistics from fuzzing_loop.py output",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument(
        "--output-dir",
        type=str,
        default=None,
        help="Output directory for results (default: results/mem_rela_stats/)",
    )
    parser.add_argument(
        "--arch",
        type=str,
        default=None,
        choices=["ventus", "vortex"],
        help="Only analyze specified architecture (ventus or vortex). If not set, analyze all.",
    )
    parser.add_argument(
        "--xlen",
        type=int,
        default=None,
        choices=[32, 64],
        help="Only analyze specified xlen (32 or 64). If not set, use default for arch.",
    )
    args = parser.parse_args()
    
    # Set output directory
    if args.output_dir:
        output_dir = Path(args.output_dir)
    else:
        output_dir = fuzzgpu_dir / "results" / "mem_rela_stats"
    
    # Determine which arch/xlen to analyze
    if args.arch:
        # Use specified arch, determine xlen
        if args.xlen:
            xlen = args.xlen
        else:
            # Default xlen for each arch
            xlen = 32 if args.arch == "ventus" else 64
        configs = [{"arch": args.arch, "xlen": xlen}]
    else:
        configs = ARCH_XLEN_CONFIGS
    
    print(f"{BLUE}=== Memory Relation Statistics Analysis ==={RESET}")
    print(f"  Method: {METHOD}")
    print(f"  Succeeded: ")
    arch_list = ', '.join(f"{c['arch']}/x{c['xlen']}" for c in configs)
    print(f"  Arch/XLEN: {arch_list}")
    
    # Analyze each architecture
    arch_stats_list: List[ArchStats] = []
    for config in configs:
        stats = analyze_arch(config["arch"], config["xlen"])
        arch_stats_list.append(stats)
    
    # Print results (single_arch=True if only one arch specified)
    single_arch = args.arch is not None
    print_stats_table(arch_stats_list, single_arch=single_arch)
    
    # Save results
    save_results(arch_stats_list, output_dir)


if __name__ == "__main__":
    main()
