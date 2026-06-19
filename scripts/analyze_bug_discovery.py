#!/usr/bin/env python3
"""Analyze bug discovery times from fuzzing results.

This script extracts bug discovery times from phase2_results.json and generates
summary CSV files for the bug discovery time experiments.
"""

import argparse
import csv
import json
import os
from collections import defaultdict
from pathlib import Path


def extract_bug_times(phase2_json: str, expected_bugs_json: str, arch: str, xlen: str,
                      force_max_num_instrs: int, seed_idx: int, start_seed: int,
                      output_csv: str):
    """Extract bug discovery times from phase2_results.json and append to CSV.
    
    Args:
        phase2_json: Path to phase2_results.json
        expected_bugs_json: Path to expected_bugs.json
        arch: Architecture (vortex, ventus)
        xlen: XLEN (32, 64)
        force_max_num_instrs: Force max num instrs value
        seed_idx: Seed index
        start_seed: Start seed value
        output_csv: Path to output CSV file
    """
    if not os.path.exists(phase2_json):
        print(f"Warning: {phase2_json} not found, skipping")
        return
    
    with open(phase2_json, 'r') as f:
        phase2 = json.load(f)
    
    first_gen_start = phase2.get('first_gen_start_time', 0)
    known_bugs = phase2.get('known_bugs', {})
    total_instrs = phase2.get('total_instrs', 0)
    
    # Check if all expected bugs were found
    with open(expected_bugs_json, 'r') as f:
        expected = json.load(f)
    
    expected_bugs = expected.get(arch, {}).get(str(xlen), [])
    found_bugs = set(known_bugs.keys())
    all_found = all(bug in found_bugs for bug in expected_bugs)
    
    # Append to CSV
    with open(output_csv, 'a', newline='') as f:
        writer = csv.writer(f)
        for bug_name, bug_info in known_bugs.items():
            first_time = bug_info.get('first_detection_time', 0)
            # first_detection_time is already relative to first_gen_start (computed in fuzzing_loop.py)
            # core_hours is already computed in phase2_results.json
            core_hours = bug_info.get('core_hours', 0)
            writer.writerow([
                arch, xlen, force_max_num_instrs, seed_idx, start_seed,
                bug_name, f"{first_time:.2f}", f"{core_hours:.6f}", total_instrs, all_found
            ])
    
    print(f"Extracted {len(known_bugs)} bugs from {phase2_json}")


def generate_pivot_summary(summary_csv: str, output_file: str):
    """Generate a pivot summary from the bug discovery CSV.
    
    Args:
        summary_csv: Path to the summary CSV file
        output_file: Path to output pivot summary CSV
    """
    # Read data: store both time and core_hours
    data = defaultdict(lambda: defaultdict(lambda: {'times': [], 'core_hours': []}))
    
    with open(summary_csv, 'r') as f:
        reader = csv.DictReader(f)
        for row in reader:
            key = (row['arch'], row['xlen'], row['force_max_num_instrs'])
            bug_name = row['bug_name']
            time_sec = float(row['first_detection_time_sec'])
            core_hours = float(row.get('core_hours', 0))
            data[key][bug_name]['times'].append(time_sec)
            data[key][bug_name]['core_hours'].append(core_hours)
    
    # Collect all bug names
    all_bugs = set()
    for key in data:
        all_bugs.update(data[key].keys())
    all_bugs = sorted(all_bugs)
    
    # Write pivot table
    with open(output_file, 'w', newline='') as f:
        writer = csv.writer(f)
        header = ['arch', 'xlen', 'force_max_num_instrs'] + \
                 [f'{bug}_avg_sec' for bug in all_bugs] + \
                 [f'{bug}_avg_core_hours' for bug in all_bugs] + \
                 [f'{bug}_count' for bug in all_bugs]
        writer.writerow(header)
        
        for key in sorted(data.keys()):
            arch, xlen, force_max = key
            row = [arch, xlen, force_max]
            # Average times
            for bug in all_bugs:
                bug_data = data[key].get(bug, {'times': [], 'core_hours': []})
                times = bug_data['times']
                avg = sum(times) / len(times) if times else ''
                row.append(f'{avg:.2f}' if avg != '' else '')
            # Average core_hours
            for bug in all_bugs:
                bug_data = data[key].get(bug, {'times': [], 'core_hours': []})
                ch_list = bug_data['core_hours']
                avg_ch = sum(ch_list) / len(ch_list) if ch_list else ''
                row.append(f'{avg_ch:.6f}' if avg_ch != '' else '')
            # Counts
            for bug in all_bugs:
                bug_data = data[key].get(bug, {'times': [], 'core_hours': []})
                count = len(bug_data['times'])
                row.append(count if count > 0 else '')
            writer.writerow(row)
    
    print(f'Pivot summary saved to: {output_file}')


def main():
    parser = argparse.ArgumentParser(description='Analyze bug discovery times from fuzzing results')
    subparsers = parser.add_subparsers(dest='command', help='Command to run')
    
    # extract command
    extract_parser = subparsers.add_parser('extract', help='Extract bug times from phase2_results.json')
    extract_parser.add_argument('--phase2-json', required=True, help='Path to phase2_results.json')
    extract_parser.add_argument('--expected-bugs-json', required=True, help='Path to expected_bugs.json')
    extract_parser.add_argument('--arch', required=True, help='Architecture (vortex, ventus)')
    extract_parser.add_argument('--xlen', required=True, type=int, help='XLEN (32, 64)')
    extract_parser.add_argument('--force-max-num-instrs', required=True, type=int, help='Force max num instrs value')
    extract_parser.add_argument('--seed-idx', required=True, type=int, help='Seed index')
    extract_parser.add_argument('--start-seed', required=True, type=int, help='Start seed value')
    extract_parser.add_argument('--output-csv', required=True, help='Path to output CSV file')
    
    # pivot command
    pivot_parser = subparsers.add_parser('pivot', help='Generate pivot summary from CSV')
    pivot_parser.add_argument('--summary-csv', required=True, help='Path to summary CSV file')
    pivot_parser.add_argument('--output-file', required=True, help='Path to output pivot summary CSV')
    
    args = parser.parse_args()
    
    if args.command == 'extract':
        extract_bug_times(
            args.phase2_json, args.expected_bugs_json, args.arch, args.xlen,
            args.force_max_num_instrs, args.seed_idx, args.start_seed, args.output_csv
        )
    elif args.command == 'pivot':
        generate_pivot_summary(args.summary_csv, args.output_file)
    else:
        parser.print_help()


if __name__ == '__main__':
    main()
