#!/usr/bin/env python3
"""
Script to find gen.log/emu.log pairs where Global scope barrier PCs 
appear in terminated warps messages.
"""
import os
import re
from pathlib import Path

def find_global_barrier_pcs(gen_log_path):
    """Find all PCs with scope Global from gen.log"""
    pcs = []
    try:
        with open(gen_log_path, 'r') as f:
            for line in f:
                # Look for lines like: [info]     - Barrier id 0, pc 0x1de6ece0c, scope Global, count 1
                match = re.search(r'pc\s+(0x[0-9a-fA-F]+).*scope\s+Global', line)
                if match:
                    pcs.append(match.group(1))
    except Exception as e:
        print(f"Error reading {gen_log_path}: {e}", file=sys.stderr)
    return pcs

def check_emu_log(emu_log_path, pcs):
    """Check if any of the PCs appear in terminated warps message"""
    found_pcs = []
    try:
        with open(emu_log_path, 'r') as f:
            for line in f:
                if "terminated but some warps still have uncommitted instructions:" in line:
                    # Extract PC from this line
                    match = re.search(r'pc=(0x[0-9a-fA-F]+)', line)
                    if match:
                        pc = match.group(1)
                        if pc in pcs:
                            found_pcs.append(pc)
    except Exception as e:
        print(f"Error reading {emu_log_path}: {e}", file=sys.stderr)
    return found_pcs

def main():
    # Start from current directory or workspace root
    root_dir = Path('/tmp/fuzzing_results_vortex_64')
    
    print("Searching for gen.log/emu.log pairs...")
    found_count = 0
    
    # Find all gen.log files
    for gen_log in root_dir.rglob('gen.log'):
        # Check if corresponding emu.log exists
        emu_log = gen_log.parent / 'emu.log'
        if emu_log.exists():
            # Find global barrier PCs in gen.log
            global_pcs = find_global_barrier_pcs(gen_log)
            
            if global_pcs:
                # Check if any of these PCs appear in emu.log with the specific message
                matched_pcs = check_emu_log(emu_log, global_pcs)
                if matched_pcs:
                    print(f"{emu_log}")
                    for pc in matched_pcs:
                        print(f"  PC: {pc}")
                    found_count += 1
    
    print(f"\nTotal matches found: {found_count}", file=sys.stderr)

if __name__ == '__main__':
    import sys
    main()
