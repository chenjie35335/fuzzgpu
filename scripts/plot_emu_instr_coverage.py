#!/usr/bin/env python3
"""
Parse instruction coverage from emu.log files and plot histogram.
Usage: python plot_emu_instr_coverage.py <results_dir> <method> [output_file]
Method: fuzzgpu, cascade, or divefuzz
"""

import re
import sys
import os
import json
from pathlib import Path
import matplotlib.pyplot as plt
import numpy as np


def parse_coverage_from_log(log_file):
    """
    Parse instruction coverage percentage from emu.log file.
    Looks for pattern: [info] Instruction coverage: XX.XX%
    Returns: float percentage value or None if not found
    """
    if not os.path.exists(log_file):
        return None
    
    try:
        with open(log_file, 'r') as f:
            content = f.read()
            # Look for pattern: [info] Instruction coverage: XX.XX%
            match = re.search(r'\[info\]\s+Instruction coverage:\s+([0-9.]+)%', content)
            if match:
                return float(match.group(1))
    except Exception as e:
        print(f"Error reading {log_file}: {e}", file=sys.stderr)
    
    return None


def collect_coverage_data(results_dir, method='fuzzgpu'):
    """
    Traverse all testcase directories and collect instruction coverage percentages.
    
    Args:
        results_dir: Path to results directory
        method: 'fuzzgpu', 'cascade', or 'divefuzz'
    
    Returns: list of coverage percentages
    """
    coverage_data = []
    results_path = Path(results_dir)
    
    if not results_path.exists():
        print(f"Error: Results directory does not exist: {results_dir}", file=sys.stderr)
        return coverage_data
    
    if method == 'fuzzgpu':
        # Original fuzzgpu logic: testcase_*/emu.log
        testcase_dirs = sorted(results_path.glob("testcase_*"))
        
        if not testcase_dirs:
            print(f"Warning: No testcase directories found in {results_dir}", file=sys.stderr)
            return coverage_data
        
        print(f"Found {len(testcase_dirs)} testcase directories")
        
        for testcase_dir in testcase_dirs:
            log_file = testcase_dir / "emu.log"
            coverage = parse_coverage_from_log(str(log_file))
            
            if coverage is not None:
                coverage_data.append(coverage)
                print(f"{testcase_dir.name}: {coverage}%")
            else:
                print(f"{testcase_dir.name}: No coverage data found", file=sys.stderr)
    
    elif method == 'cascade':
        # Cascade logic: run_*/elf/ventus_X_numinstrs.txt and run_*/ventus_X.stat.json
        run_dirs = sorted(results_path.glob("run_*"))
        if not run_dirs:
            print(f"Warning: No run directories found in {results_dir}", file=sys.stderr)
            return coverage_data
        
        print(f"Found {len(run_dirs)} run directories")
        
        for run_dir in run_dirs:
            elf_dir = run_dir / "elf"
            if not elf_dir.exists():
                continue
            
            # Find all ventus_X_numinstrs.txt files
            numinstrs_files = sorted(elf_dir.glob("ventus_*_numinstrs.txt"))
            
            for numinstrs_file in numinstrs_files:
                # Extract test name (e.g., ventus_0)
                test_name = numinstrs_file.stem.replace('_numinstrs', '')
                
                try:
                    with open(numinstrs_file, 'r') as f:
                        total_count = int(f.read().strip(), 16)
                except Exception as e:
                    print(f"{run_dir.name}/{test_name}: Error reading instruction count: {e}", file=sys.stderr)
                    continue
                
                # Find corresponding stat file in parent run directory
                stat_file = run_dir / f"{test_name}.stat.json"
                if not stat_file.exists():
                    print(f"{run_dir.name}/{test_name}: No stat file found", file=sys.stderr)
                    continue
                
                try:
                    with open(stat_file, 'r') as f:
                        stat_data = json.load(f)
                        num_executed_instr = stat_data.get('num_executed_instr', 0)
                        coverage = (num_executed_instr / total_count) * 100 if total_count > 0 else 0
                        coverage_data.append(coverage)
                        print(f"{run_dir.name}/{test_name}: {coverage:.2f}%")
                except Exception as e:
                    print(f"{run_dir.name}/{test_name}: Error reading stat file: {e}", file=sys.stderr)
    
    elif method == 'divefuzz':
        # DiveFuzz logic: supports both old and new formats
        # Old format: gen_X.json and seed_X.stat.json
        # New format: *_mut_*.json and corresponding *.emu.log
        
        # Try old format first
        gen_files = sorted(results_path.glob("gen_*.json"))
        
        if gen_files:
            print(f"Found {len(gen_files)} generation files (old format)")
            
            for gen_file in gen_files:
                # Extract seed id from gen_X.json
                match = re.match(r'gen_(\d+)\.json', gen_file.name)
                if not match:
                    continue
                seed_id = match.group(1)
                
                try:
                    with open(gen_file, 'r') as f:
                        gen_data = json.load(f)
                    
                    if not gen_data.get('success', False):
                        print(f"seed_{seed_id}: Generation failed", file=sys.stderr)
                        continue
                    
                    total_count = gen_data.get('instr_count', 0)
                    if total_count == 0:
                        print(f"seed_{seed_id}: No instruction count", file=sys.stderr)
                        continue
                    
                except Exception as e:
                    print(f"seed_{seed_id}: Error reading generation file: {e}", file=sys.stderr)
                    continue
                
                # Find corresponding stat file
                stat_file = results_path / f"seed_{seed_id}.stat.json"
                if not stat_file.exists():
                    print(f"seed_{seed_id}: No stat file found", file=sys.stderr)
                    continue
                
                try:
                    with open(stat_file, 'r') as f:
                        stat_data = json.load(f)
                        num_executed_instr = stat_data.get('num_executed_instr', 0)
                        coverage = (num_executed_instr / total_count) * 100 if total_count > 0 else 0
                        coverage_data.append(coverage)
                        print(f"seed_{seed_id}: {coverage:.2f}%")
                except Exception as e:
                    print(f"seed_{seed_id}: Error reading stat file: {e}", file=sys.stderr)
        
        # Try new format: *_mut_*.json with *.emu.log
        mut_files = sorted(results_path.glob("*_mut_*.json"))
        
        if mut_files:
            print(f"Found {len(mut_files)} mutation files (new format)")
            
            for json_file in mut_files:
                # Extract base name without .json extension
                base_name = json_file.stem  # e.g., "mutate_0_mut_0" or "seed_4_mut_1"
                
                try:
                    with open(json_file, 'r') as f:
                        gen_data = json.load(f)
                    
                    # Check if generation was successful
                    if not gen_data.get('success', False):
                        print(f"{base_name}: Generation failed", file=sys.stderr)
                        continue
                    
                except Exception as e:
                    print(f"{base_name}: Error reading JSON file: {e}", file=sys.stderr)
                    continue
                
                # Find corresponding emu.log file
                emu_log_file = results_path / f"{base_name}.emu.log"
                if not emu_log_file.exists():
                    print(f"{base_name}: No emu.log file found", file=sys.stderr)
                    continue
                
                # Parse coverage from emu.log
                coverage = parse_coverage_from_log(str(emu_log_file))
                if coverage is not None:
                    coverage_data.append(coverage)
                    print(f"{base_name}: {coverage:.2f}%")
                else:
                    print(f"{base_name}: No coverage data in emu.log", file=sys.stderr)
        
        if not gen_files and not mut_files:
            print(f"Warning: No gen_*.json or *_mut_*.json files found in {results_dir}", file=sys.stderr)
            return coverage_data
    
    else:
        print(f"Error: Unknown method '{method}'. Use 'fuzzgpu', 'cascade', or 'divefuzz'", file=sys.stderr)
    
    return coverage_data


def plot_histogram(coverage_data, output_file=None):
    """
    Plot histogram of instruction coverage percentages.
    Saves figure to output_file if provided.
    """
    if not coverage_data:
        print("Error: No coverage data to plot", file=sys.stderr)
        return
    
    fig, ax = plt.subplots(figsize=(12, 6))
    
    # Create histogram
    n, bins, patches = ax.hist(coverage_data, bins=30, edgecolor='black', alpha=0.7)
    
    ax.set_xlabel('Instruction Coverage (%)', fontsize=12)
    ax.set_ylabel('Frequency', fontsize=12)
    ax.set_title('Distribution of Instruction Coverage Across Testcases', fontsize=14, fontweight='bold')
    ax.grid(axis='y', alpha=0.3)
    
    # Add statistics
    mean_coverage = np.mean(coverage_data)
    median_coverage = np.median(coverage_data)
    min_coverage = np.min(coverage_data)
    max_coverage = np.max(coverage_data)
    std_coverage = np.std(coverage_data)
    
    stats_text = (
        f'Count: {len(coverage_data)}\n'
        f'Mean: {mean_coverage:.2f}%\n'
        f'Median: {median_coverage:.2f}%\n'
        f'Std Dev: {std_coverage:.2f}%\n'
        f'Min: {min_coverage:.2f}%\n'
        f'Max: {max_coverage:.2f}%'
    )
    
    ax.text(0.98, 0.97, stats_text, transform=ax.transAxes,
            fontsize=10, verticalalignment='top', horizontalalignment='right',
            bbox=dict(boxstyle='round', facecolor='wheat', alpha=0.5))
    
    plt.tight_layout()
    
    if output_file:
        # Ensure output directory exists and save both PNG and PDF
        output_path = Path(output_file)
        output_path.parent.mkdir(parents=True, exist_ok=True)
        output_path_png = output_path.with_suffix('.png')
        output_path_pdf = output_path.with_suffix('.pdf')
        plt.savefig(output_path_png, dpi=150, bbox_inches='tight')
        plt.savefig(output_path_pdf, format='pdf', dpi=150, bbox_inches='tight')
        print(f"Histogram saved to {output_path_png} and {output_path_pdf}")
    else:
        plt.show()
    
    return fig, ax, {
        'count': len(coverage_data),
        'mean': mean_coverage,
        'median': median_coverage,
        'min': min_coverage,
        'max': max_coverage,
        'std': std_coverage
    }


def main():
    if len(sys.argv) < 3:
        print("Usage: python plot_emu_instr_coverage.py <results_dir> <method> [output_file]", file=sys.stderr)
        print("Method: fuzzgpu, cascade, or divefuzz", file=sys.stderr)
        print("Example: python plot_emu_instr_coverage.py /tmp/baseline_results/fuzzgpu_vanilla_20251231_09222", file=sys.stderr)
        sys.exit(1)
    
    results_dir = sys.argv[1]
    method = sys.argv[2]
    output_file = sys.argv[3] if len(sys.argv) > 3 else None
    
    # Validate method
    if method not in ['fuzzgpu', 'cascade', 'divefuzz']:
        print(f"Error: Invalid method '{method}'. Use 'fuzzgpu', 'cascade', or 'divefuzz'", file=sys.stderr)
        sys.exit(1)
    
    # Collect coverage data
    coverage_data = collect_coverage_data(results_dir, method)
    
    if not coverage_data:
        print("Error: No coverage data collected", file=sys.stderr)
        sys.exit(1)
    
    print(f"\nCollected {len(coverage_data)} coverage values")
    
    # Plot histogram
    fig, ax, stats = plot_histogram(coverage_data, output_file)
    
    # Print statistics
    print("\n=== Statistics ===")
    for key, value in stats.items():
        if key == 'count':
            print(f"{key}: {value}")
        else:
            print(f"{key}: {value:.2f}%")


if __name__ == "__main__":
    main()
