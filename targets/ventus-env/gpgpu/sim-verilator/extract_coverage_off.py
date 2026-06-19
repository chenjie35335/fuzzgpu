#!/usr/bin/env python3
"""
Extract coverage_off directives from Verilog files.
Analyzes `ifndef SYNTHESIS ... `endif blocks and generates Verilator coverage_off options.
"""

import os
import sys
import re
from pathlib import Path
from typing import List, Tuple, Dict

def analyze_file(filepath: str, verilog_dir: str) -> List[Tuple[str, int, int]]:
    """
    Analyze a Verilog/SystemVerilog file and extract `ifndef SYNTHESIS ... `endif blocks.
    
    Returns:
        List of tuples: (relative_filepath, start_line, end_line) for each SYNTHESIS block found
    """
    results = []
    
    try:
        with open(filepath, 'r', encoding='utf-8', errors='ignore') as f:
            lines = f.readlines()
    except Exception as e:
        print(f"Warning: Could not read file {filepath}: {e}", file=sys.stderr)
        return results
    
    # Use relative path from verilog_dir to match how files are passed to Verilator
    base_dir = os.path.dirname(os.path.abspath(verilog_dir))
    relative_filepath = os.path.relpath(filepath, base_dir)
    stack = []  # Stack to handle nested `ifdef/`ifndef
    
    for line_num, line in enumerate(lines, 1):
        # Remove comments for analysis
        code_part = line.split('//')[0]
        
        # Check for `ifndef SYNTHESIS
        if re.search(r'`ifndef\s+SYNTHESIS\b', code_part):
            stack.append(('ifndef_SYNTHESIS', line_num))
        
        # Check for `ifdef SYNTHESIS
        elif re.search(r'`ifdef\s+SYNTHESIS\b', code_part):
            stack.append(('ifdef_SYNTHESIS', line_num))
        
        # Check for `endif
        elif re.search(r'`endif\b', code_part):
            if stack:
                pragma_type, start_line = stack.pop()
                # We only care about `ifndef SYNTHESIS blocks
                if pragma_type == 'ifndef_SYNTHESIS':
                    results.append((relative_filepath, start_line, line_num))
    
    return results

def find_verilog_files(verilog_dir: str) -> List[str]:
    """Find all .v and .sv files in the directory."""
    files = []
    
    if not os.path.isdir(verilog_dir):
        print(f"Warning: Verilog directory not found: {verilog_dir}", file=sys.stderr)
        return files
    
    for root, dirs, filenames in os.walk(verilog_dir):
        for filename in filenames:
            if filename.endswith(('.v', '.sv')):
                files.append(os.path.join(root, filename))
    
    return sorted(files)

def generate_coverage_off_directives(results: Dict[str, List[Tuple[int, int]]]) -> str:
    """
    Generate a Verilator Control File (.vlt) disabling coverage on SYNTHESIS blocks.
    
    Format inside the vlt:
    `verilator_config
    coverage_off [-file "<filename>" [-lines <line> [ - <line> ]]]
    """
    directives = ["`verilator_config"]

    for filename in sorted(results.keys()):
        blocks = sorted(results[filename])

        for start_line, end_line in blocks:
            if start_line == end_line:
                directive = f'coverage_off -file "{filename}" -lines {start_line}'
            else:
                directive = f'coverage_off -file "{filename}" -lines {start_line} - {end_line}'
            directives.append(directive)

    return '\n'.join(directives) + ('\n' if directives else '')

def main():
    """Main entry point."""
    if len(sys.argv) < 2:
        print("Usage: extract_coverage_off.py <verilog_dir> [output_file]", file=sys.stderr)
        print("  verilog_dir: Directory containing .v and .sv files", file=sys.stderr)
        print("  output_file: Optional file to write coverage_off directives (default: stdout)", file=sys.stderr)
        sys.exit(1)
    
    verilog_dir = sys.argv[1]
    output_file = sys.argv[2] if len(sys.argv) > 2 else None
    
    # Find all Verilog files
    verilog_files = find_verilog_files(verilog_dir)
    
    if not verilog_files:
        print(f"Warning: No .v or .sv files found in {verilog_dir}", file=sys.stderr)
        sys.exit(0)
    
    # Analyze all files
    all_results = {}
    for filepath in verilog_files:
        blocks = analyze_file(filepath, verilog_dir)
        if blocks:
            # Group by filepath (now relative)
            for relative_filepath, start_line, end_line in blocks:
                if relative_filepath not in all_results:
                    all_results[relative_filepath] = []
                all_results[relative_filepath].append((start_line, end_line))
    
    # Generate coverage_off directives
    directives = generate_coverage_off_directives(all_results)

    # Output results
    if output_file:
        with open(output_file, 'w') as f:
            f.write(directives)
        if directives.strip():
            print(f"Generated {len(all_results)} files with coverage_off directives")
            print(f"Output written to: {output_file}", file=sys.stderr)
    else:
        print(directives)

if __name__ == '__main__':
    main()
