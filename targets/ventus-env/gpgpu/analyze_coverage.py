#!/usr/bin/env python3
"""
Coverage Analysis Script for Verilator Coverage Data
Analyzes .sv files with coverage annotations and generates a detailed report
"""

import os
import re
import sys
from pathlib import Path
from collections import defaultdict
from dataclasses import dataclass
from typing import List, Dict, Tuple
import argparse

@dataclass
class CoverageLine:
    """Represents a line with coverage information"""
    line_num: int
    hits: int
    code: str
    source_file: str = ""

@dataclass
class FileCoverage:
    """Coverage statistics for a file"""
    filename: str
    total_lines: int
    covered_lines: int
    uncovered_lines: int
    low_coverage_lines: int  # < 100 hits
    coverage_percentage: float
    uncovered_details: List[CoverageLine]
    low_coverage_details: List[CoverageLine]

class CoverageAnalyzer:
    def __init__(self, coverage_dir: str):
        self.coverage_dir = Path(coverage_dir)
        self.file_stats: Dict[str, FileCoverage] = {}
        
    def parse_coverage_line(self, line: str) -> Tuple[int, str]:
        """
        Parse a line with coverage annotation
        Returns: (hit_count, code)
        Coverage formats:
        - %000000  : 0 hits (uncovered)
        - ~123456  : ~123456 hits (low coverage marker)
        - 1234567  : exact hit count
        """
        # Match coverage annotation at start of line
        match = re.match(r'^([%~]?\d+)\s+(.*)$', line)
        if match:
            hit_str = match.group(1)
            code = match.group(2)
            
            # Parse hit count
            if hit_str.startswith('%'):
                hits = 0
            elif hit_str.startswith('~'):
                hits = int(hit_str[1:])
            else:
                hits = int(hit_str)
            
            return hits, code
        return -1, line  # -1 means no coverage data
    
    def analyze_file(self, filepath: Path) -> FileCoverage:
        """Analyze coverage for a single file"""
        uncovered = []
        low_coverage = []
        total_lines = 0
        covered_lines = 0
        
        try:
            with open(filepath, 'r', encoding='utf-8', errors='ignore') as f:
                for line_num, line in enumerate(f, 1):
                    line = line.rstrip()
                    hits, code = self.parse_coverage_line(line)
                    
                    if hits >= 0:  # Has coverage data
                        total_lines += 1
                        
                        # Extract source file info from comments
                        source_match = re.search(r'//\s*(.+?\.scala:\d+)', code)
                        source_file = source_match.group(1) if source_match else ""
                        
                        if hits == 0:
                            uncovered.append(CoverageLine(line_num, hits, code, source_file))
                        else:
                            covered_lines += 1
                            if hits < 100:
                                low_coverage.append(CoverageLine(line_num, hits, code, source_file))
        
        except Exception as e:
            print(f"Error reading {filepath}: {e}", file=sys.stderr)
            return None
        
        coverage_pct = (covered_lines / total_lines * 100) if total_lines > 0 else 0
        
        return FileCoverage(
            filename=filepath.name,
            total_lines=total_lines,
            covered_lines=covered_lines,
            uncovered_lines=len(uncovered),
            low_coverage_lines=len(low_coverage),
            coverage_percentage=coverage_pct,
            uncovered_details=uncovered,
            low_coverage_details=low_coverage
        )
    
    def analyze_directory(self):
        """Analyze all .sv files in the directory"""
        sv_files = list(self.coverage_dir.glob('*.sv'))
        
        print(f"Found {len(sv_files)} .sv files in {self.coverage_dir}")
        print("Analyzing coverage...\n")
        
        for sv_file in sorted(sv_files):
            coverage = self.analyze_file(sv_file)
            if coverage and coverage.total_lines > 0:
                self.file_stats[str(sv_file.name)] = coverage
    
    def generate_summary_report(self, output_file: str = None):
        """Generate summary report of coverage statistics"""
        # Sort by coverage percentage (ascending)
        sorted_files = sorted(
            self.file_stats.values(),
            key=lambda x: x.coverage_percentage
        )
        
        lines = []
        lines.append("=" * 100)
        lines.append("COVERAGE ANALYSIS SUMMARY REPORT")
        lines.append("=" * 100)
        lines.append("")
        
        # Overall statistics
        total_lines_all = sum(f.total_lines for f in self.file_stats.values())
        covered_lines_all = sum(f.covered_lines for f in self.file_stats.values())
        uncovered_lines_all = sum(f.uncovered_lines for f in self.file_stats.values())
        
        lines.append(f"Total files analyzed: {len(self.file_stats)}")
        lines.append(f"Total lines with coverage data: {total_lines_all:,}")
        lines.append(f"Total covered lines: {covered_lines_all:,} ({covered_lines_all/total_lines_all*100:.2f}%)")
        lines.append(f"Total uncovered lines: {uncovered_lines_all:,} ({uncovered_lines_all/total_lines_all*100:.2f}%)")
        lines.append("")
        
        # Top 20 files with lowest coverage
        lines.append("=" * 100)
        lines.append("TOP 20 FILES WITH LOWEST COVERAGE")
        lines.append("=" * 100)
        lines.append(f"{'File':<40} {'Total':>8} {'Covered':>8} {'Uncovered':>10} {'Coverage':>10}")
        lines.append("-" * 100)
        
        for fc in sorted_files[:20]:
            lines.append(
                f"{fc.filename:<40} {fc.total_lines:>8} {fc.covered_lines:>8} "
                f"{fc.uncovered_lines:>10} {fc.coverage_percentage:>9.2f}%"
            )
        
        lines.append("")
        
        # Files with most uncovered lines
        lines.append("=" * 100)
        lines.append("TOP 20 FILES WITH MOST UNCOVERED LINES")
        lines.append("=" * 100)
        lines.append(f"{'File':<40} {'Uncovered':>10} {'Total':>8} {'Coverage':>10}")
        lines.append("-" * 100)
        
        by_uncovered = sorted(
            self.file_stats.values(),
            key=lambda x: x.uncovered_lines,
            reverse=True
        )
        
        for fc in by_uncovered[:20]:
            lines.append(
                f"{fc.filename:<40} {fc.uncovered_lines:>10} {fc.total_lines:>8} "
                f"{fc.coverage_percentage:>9.2f}%"
            )
        
        report = "\n".join(lines)
        print(report)
        
        if output_file:
            with open(output_file, 'w') as f:
                f.write(report)
            print(f"\nSummary report written to: {output_file}")
    
    def generate_detailed_report(self, output_file: str, top_n: int = 10):
        """Generate detailed report of uncovered lines"""
        lines = []
        lines.append("=" * 120)
        lines.append("DETAILED UNCOVERED LINES REPORT")
        lines.append("=" * 120)
        lines.append("")
        
        # Sort by number of uncovered lines
        by_uncovered = sorted(
            self.file_stats.values(),
            key=lambda x: x.uncovered_lines,
            reverse=True
        )
        
        for fc in by_uncovered[:top_n]:
            lines.append("=" * 120)
            lines.append(f"File: {fc.filename}")
            lines.append(f"Coverage: {fc.coverage_percentage:.2f}% ({fc.covered_lines}/{fc.total_lines} lines)")
            lines.append(f"Uncovered lines: {fc.uncovered_lines}")
            lines.append("=" * 120)
            lines.append("")
            
            if fc.uncovered_details:
                lines.append(f"{'Line':>6} {'Hits':>8} {'Source':<50} {'Code'}")
                lines.append("-" * 120)
                
                for detail in fc.uncovered_details[:50]:  # Limit to 50 per file
                    source_info = detail.source_file[:48] if detail.source_file else ""
                    code_preview = detail.code[:50] if len(detail.code) <= 50 else detail.code[:47] + "..."
                    lines.append(
                        f"{detail.line_num:>6} {detail.hits:>8} {source_info:<50} {code_preview}"
                    )
                
                if len(fc.uncovered_details) > 50:
                    lines.append(f"... and {len(fc.uncovered_details) - 50} more uncovered lines")
            
            lines.append("")
        
        report = "\n".join(lines)
        
        with open(output_file, 'w') as f:
            f.write(report)
        print(f"Detailed report written to: {output_file}")
    
    def generate_module_analysis(self, output_file: str):
        """Analyze coverage by module/feature"""
        lines = []
        lines.append("=" * 100)
        lines.append("MODULE-SPECIFIC COVERAGE ANALYSIS")
        lines.append("=" * 100)
        lines.append("")
        
        # Group files by module
        modules = defaultdict(list)
        for fc in self.file_stats.values():
            # Extract module name from filename
            if fc.filename.startswith('Queue'):
                modules['Queue/FIFO'].append(fc)
            elif 'FPU' in fc.filename or 'Float' in fc.filename or 'FMA' in fc.filename or 'FMUL' in fc.filename:
                modules['FPU'].append(fc)
            elif 'IntDiv' in fc.filename or 'Multiplier' in fc.filename:
                modules['Integer Arithmetic'].append(fc)
            elif 'LSU' in fc.filename or 'DCache' in fc.filename or 'ICache' in fc.filename:
                modules['Memory/Cache'].append(fc)
            elif 'branch' in fc.filename.lower() or 'simt' in fc.filename.lower():
                modules['Control Flow'].append(fc)
            elif 'CSR' in fc.filename:
                modules['CSR'].append(fc)
            elif 'Arbiter' in fc.filename:
                modules['Arbiters'].append(fc)
            elif 'ALU' in fc.filename:
                modules['ALU'].append(fc)
            else:
                modules['Other'].append(fc)
        
        # Analyze each module
        for module_name in sorted(modules.keys()):
            files = modules[module_name]
            total = sum(f.total_lines for f in files)
            covered = sum(f.covered_lines for f in files)
            uncovered = sum(f.uncovered_lines for f in files)
            
            lines.append(f"\n{module_name}")
            lines.append("-" * 100)
            lines.append(f"  Files: {len(files)}")
            lines.append(f"  Total lines: {total:,}")
            lines.append(f"  Covered: {covered:,} ({covered/total*100:.2f}%)")
            lines.append(f"  Uncovered: {uncovered:,} ({uncovered/total*100:.2f}%)")
            
            # List worst files in this module
            worst = sorted(files, key=lambda x: x.coverage_percentage)[:3]
            if worst:
                lines.append(f"  Worst coverage files:")
                for f in worst:
                    lines.append(f"    - {f.filename}: {f.coverage_percentage:.2f}% ({f.uncovered_lines} uncovered)")
        
        report = "\n".join(lines)
        print(report)
        
        with open(output_file, 'w') as f:
            f.write(report)
        print(f"\nModule analysis written to: {output_file}")

def main():
    parser = argparse.ArgumentParser(
        description='Analyze Verilator coverage data from .sv files'
    )
    parser.add_argument(
        'coverage_dir',
        nargs='?',
        default='ventus/merged_seed_46_coverage',
        help='Directory containing .sv files with coverage annotations'
    )
    parser.add_argument(
        '--output-dir',
        '-o',
        default='coverage_reports',
        help='Output directory for reports'
    )
    parser.add_argument(
        '--top-n',
        '-n',
        type=int,
        default=20,
        help='Number of files to include in detailed report'
    )
    
    args = parser.parse_args()
    
    # Create output directory
    output_dir = Path(args.output_dir)
    output_dir.mkdir(exist_ok=True)
    
    # Run analysis
    analyzer = CoverageAnalyzer(args.coverage_dir)
    analyzer.analyze_directory()
    
    if not analyzer.file_stats:
        print("No coverage data found!")
        return 1
    
    # Generate reports
    print("\n" + "=" * 100)
    analyzer.generate_summary_report(
        output_file=str(output_dir / 'coverage_summary.txt')
    )
    
    print("\n" + "=" * 100)
    analyzer.generate_detailed_report(
        output_file=str(output_dir / 'coverage_detailed.txt'),
        top_n=args.top_n
    )
    
    print("\n" + "=" * 100)
    analyzer.generate_module_analysis(
        output_file=str(output_dir / 'coverage_by_module.txt')
    )
    
    print("\n" + "=" * 100)
    print(f"Analysis complete! Reports saved to: {output_dir}")
    print("=" * 100)
    
    return 0

if __name__ == '__main__':
    sys.exit(main())
