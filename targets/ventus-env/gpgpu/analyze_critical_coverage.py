#!/usr/bin/env python3
"""
Critical Coverage Analysis - Identifies key untested features
Focus on functional coverage rather than just line coverage
"""

import re
from pathlib import Path
from collections import defaultdict
from dataclasses import dataclass
from typing import List, Dict

@dataclass
class CriticalFeature:
    """Represents a critical untested feature"""
    module: str
    feature: str
    severity: str  # 'HIGH', 'MEDIUM', 'LOW'
    reason: str
    files_affected: List[str]
    example_lines: List[str]

class CriticalCoverageAnalyzer:
    def __init__(self, coverage_dir: str):
        self.coverage_dir = Path(coverage_dir)
        self.critical_features = []
        
    def parse_file_for_patterns(self, filepath: Path):
        """Parse file looking for critical uncovered patterns"""
        patterns = []
        
        try:
            with open(filepath, 'r', encoding='utf-8', errors='ignore') as f:
                content = f.read()
                lines = content.split('\n')
                
                # Check for atomic operations
                atomic_lines = []
                for i, line in enumerate(lines):
                    if re.match(r'^%000000\s+', line) or re.match(r'^~0+\s+', line):
                        if 'atomic' in line.lower():
                            atomic_lines.append((i+1, line.strip()))
                
                if atomic_lines:
                    patterns.append({
                        'type': 'atomic',
                        'lines': atomic_lines[:5],
                        'file': filepath.name
                    })
                
                # Check for overflow/exception handling
                overflow_lines = []
                for i, line in enumerate(lines):
                    if re.match(r'^%000000\s+', line):
                        if any(kw in line.lower() for kw in ['overflow', 'exception', 'invalid']):
                            overflow_lines.append((i+1, line.strip()))
                
                if overflow_lines:
                    patterns.append({
                        'type': 'overflow',
                        'lines': overflow_lines[:5],
                        'file': filepath.name
                    })
                
                # Check for rounding modes
                rounding_lines = []
                for i, line in enumerate(lines):
                    if re.match(r'^%000000\s+', line):
                        if any(kw in line.lower() for kw in ['rm', 'rounding', 'frm']):
                            rounding_lines.append((i+1, line.strip()))
                
                if rounding_lines:
                    patterns.append({
                        'type': 'rounding',
                        'lines': rounding_lines[:5],
                        'file': filepath.name
                    })
                
                # Check for shared memory operations
                shmem_lines = []
                for i, line in enumerate(lines):
                    if re.match(r'^%000000\s+', line):
                        if any(kw in line.lower() for kw in ['shared', 'bank', 'lds']):
                            shmem_lines.append((i+1, line.strip()))
                
                if shmem_lines:
                    patterns.append({
                        'type': 'shared_memory',
                        'lines': shmem_lines[:5],
                        'file': filepath.name
                    })
                
                # Check for branch/control flow
                branch_lines = []
                for i, line in enumerate(lines):
                    if re.match(r'^%000000\s+', line):
                        if any(kw in line.lower() for kw in ['branch', 'jump', 'simt', 'stack']):
                            branch_lines.append((i+1, line.strip()))
                
                if branch_lines:
                    patterns.append({
                        'type': 'branch',
                        'lines': branch_lines[:5],
                        'file': filepath.name
                    })
                
                # Check for L2 cache / directory
                l2_lines = []
                for i, line in enumerate(lines):
                    if re.match(r'^%000000\s+', line):
                        if any(kw in line.lower() for kw in ['directory', 'l2', 'coherence', 'dirty']):
                            l2_lines.append((i+1, line.strip()))
                
                if l2_lines and len(l2_lines) > 10:  # Only if significant
                    patterns.append({
                        'type': 'l2_cache',
                        'lines': l2_lines[:5],
                        'file': filepath.name
                    })
                    
        except Exception as e:
            pass
        
        return patterns
    
    def analyze(self):
        """Perform critical coverage analysis"""
        sv_files = list(self.coverage_dir.glob('*.sv'))
        
        feature_map = defaultdict(list)
        
        for sv_file in sv_files:
            patterns = self.parse_file_for_patterns(sv_file)
            for pattern in patterns:
                feature_map[pattern['type']].append({
                    'file': pattern['file'],
                    'lines': pattern['lines']
                })
        
        # Analyze and categorize findings
        
        # 1. Atomic Operations
        if 'atomic' in feature_map:
            files = [p['file'] for p in feature_map['atomic']]
            examples = []
            for p in feature_map['atomic'][:3]:
                for line_num, line in p['lines'][:2]:
                    examples.append(f"{p['file']}:{line_num}: {line[:80]}")
            
            self.critical_features.append(CriticalFeature(
                module='LSU/Memory',
                feature='Atomic Operations (AMO)',
                severity='HIGH',
                reason='No coverage for RISC-V A-extension atomic instructions',
                files_affected=files,
                example_lines=examples
            ))
        
        # 2. Integer Overflow
        if 'overflow' in feature_map:
            files = [p['file'] for p in feature_map['overflow']]
            examples = []
            for p in feature_map['overflow'][:3]:
                for line_num, line in p['lines'][:2]:
                    examples.append(f"{p['file']}:{line_num}: {line[:80]}")
            
            self.critical_features.append(CriticalFeature(
                module='Integer Arithmetic',
                feature='Overflow Handling',
                severity='HIGH',
                reason='Edge cases like INT_MIN/-1 not tested',
                files_affected=files,
                example_lines=examples
            ))
        
        # 3. FP Rounding Modes
        if 'rounding' in feature_map:
            files = [p['file'] for p in feature_map['rounding']]
            examples = []
            for p in feature_map['rounding'][:3]:
                for line_num, line in p['lines'][:2]:
                    examples.append(f"{p['file']}:{line_num}: {line[:80]}")
            
            self.critical_features.append(CriticalFeature(
                module='FPU',
                feature='Floating-Point Rounding Modes',
                severity='HIGH',
                reason='IEEE 754 rounding modes not exercised',
                files_affected=files,
                example_lines=examples
            ))
        
        # 4. Shared Memory
        if 'shared_memory' in feature_map:
            files = [p['file'] for p in feature_map['shared_memory']]
            examples = []
            for p in feature_map['shared_memory'][:3]:
                for line_num, line in p['lines'][:2]:
                    examples.append(f"{p['file']}:{line_num}: {line[:80]}")
            
            self.critical_features.append(CriticalFeature(
                module='Shared Memory',
                feature='Bank Conflict Resolution',
                severity='MEDIUM',
                reason='Shared memory bank conflicts not tested',
                files_affected=files,
                example_lines=examples
            ))
        
        # 5. Branch/Control
        if 'branch' in feature_map:
            files = [p['file'] for p in feature_map['branch']]
            examples = []
            for p in feature_map['branch'][:3]:
                for line_num, line in p['lines'][:2]:
                    examples.append(f"{p['file']}:{line_num}: {line[:80]}")
            
            self.critical_features.append(CriticalFeature(
                module='Control Flow',
                feature='SIMT Stack Operations',
                severity='MEDIUM',
                reason='Complex divergence scenarios not fully tested',
                files_affected=files,
                example_lines=examples
            ))
        
        # 6. L2 Cache
        if 'l2_cache' in feature_map:
            files = [p['file'] for p in feature_map['l2_cache']]
            examples = []
            for p in feature_map['l2_cache'][:3]:
                for line_num, line in p['lines'][:2]:
                    examples.append(f"{p['file']}:{line_num}: {line[:80]}")
            
            self.critical_features.append(CriticalFeature(
                module='L2 Cache',
                feature='Directory & Coherence',
                severity='MEDIUM',
                reason='Cache coherence protocol not fully exercised',
                files_affected=files,
                example_lines=examples
            ))
    
    def generate_report(self):
        """Generate critical coverage report"""
        lines = []
        lines.append("=" * 100)
        lines.append("CRITICAL COVERAGE GAPS - FUNCTIONAL ANALYSIS")
        lines.append("=" * 100)
        lines.append("")
        lines.append("This report identifies untested features that are critical for correctness.")
        lines.append("Focus on these areas to maximize testing effectiveness.")
        lines.append("")
        
        # Sort by severity
        severity_order = {'HIGH': 0, 'MEDIUM': 1, 'LOW': 2}
        sorted_features = sorted(
            self.critical_features,
            key=lambda x: severity_order.get(x.severity, 3)
        )
        
        for i, feature in enumerate(sorted_features, 1):
            lines.append("=" * 100)
            lines.append(f"{i}. [{feature.severity}] {feature.module}: {feature.feature}")
            lines.append("=" * 100)
            lines.append(f"Reason: {feature.reason}")
            lines.append(f"Files Affected: {len(feature.files_affected)}")
            lines.append("")
            
            if feature.files_affected:
                lines.append("Key Files:")
                for f in feature.files_affected[:5]:
                    lines.append(f"  - {f}")
                if len(feature.files_affected) > 5:
                    lines.append(f"  ... and {len(feature.files_affected) - 5} more")
                lines.append("")
            
            if feature.example_lines:
                lines.append("Example Uncovered Lines:")
                for ex in feature.example_lines[:5]:
                    lines.append(f"  {ex}")
                lines.append("")
        
        # Add recommendations
        lines.append("=" * 100)
        lines.append("TESTING RECOMMENDATIONS")
        lines.append("=" * 100)
        lines.append("")
        
        recommendations = [
            ("1. Atomic Operations", [
                "- Test LR/SC (Load-Reserved/Store-Conditional) sequences",
                "- Test AMOSWAP, AMOADD, AMOAND, AMOOR, AMOXOR",
                "- Test AMOMIN, AMOMAX, AMOMINU, AMOMAXU",
                "- Test concurrent atomic operations from multiple threads",
                "- Test atomic operations on shared memory",
            ]),
            ("2. Integer Division Edge Cases", [
                "- Test INT_MIN / -1 (overflow case)",
                "- Test division by zero with both signed and unsigned",
                "- Test REMU with zero divisor",
                "- Test small divisor followed by overflow",
            ]),
            ("3. Floating-Point Rounding", [
                "- Set FCSR/FRM to different rounding modes (RNE, RTZ, RDN, RUP, RMM)",
                "- Test each mode with FP division and square root",
                "- Test per-instruction rounding mode overrides",
                "- Verify correct rounding on edge cases (ties, exact halfway)",
            ]),
            ("4. Shared Memory Access Patterns", [
                "- Test bank conflicts (multiple threads accessing same bank)",
                "- Test broadcast reads (all threads read same address)",
                "- Test strided access patterns",
                "- Test random access patterns",
                "- Test bank conflict resolution correctness",
            ]),
            ("5. Control Flow Divergence", [
                "- Test deep branch nesting (exceed stack depth)",
                "- Test all threads diverge vs all converge",
                "- Test unbalanced branches (1 thread vs 31 threads)",
                "- Test multiple warps with simultaneous divergence",
                "- Test reconvergence at different levels",
            ]),
            ("6. Cache Coherence", [
                "- Test write-back scenarios",
                "- Test cache line invalidation",
                "- Test multi-core cache coherence",
                "- Test atomic operations with cache",
            ]),
        ]
        
        for title, items in recommendations:
            lines.append(title)
            lines.append("-" * 80)
            for item in items:
                lines.append(item)
            lines.append("")
        
        lines.append("=" * 100)
        lines.append("PRIORITY SUMMARY")
        lines.append("=" * 100)
        lines.append("")
        
        high_count = len([f for f in sorted_features if f.severity == 'HIGH'])
        med_count = len([f for f in sorted_features if f.severity == 'MEDIUM'])
        low_count = len([f for f in sorted_features if f.severity == 'LOW'])
        
        lines.append(f"HIGH Priority Issues: {high_count}")
        lines.append(f"MEDIUM Priority Issues: {med_count}")
        lines.append(f"LOW Priority Issues: {low_count}")
        lines.append("")
        lines.append("Recommended Testing Order:")
        lines.append("1. Atomic operations (correctness-critical)")
        lines.append("2. Integer overflow handling (safety-critical)")
        lines.append("3. FP rounding modes (IEEE 754 compliance)")
        lines.append("4. Shared memory bank conflicts (performance-critical)")
        lines.append("5. Control flow edge cases (completeness)")
        lines.append("6. Cache coherence (multi-core scenarios)")
        
        return "\n".join(lines)

def main():
    analyzer = CriticalCoverageAnalyzer('ventus/merged_seed_46_coverage')
    
    print("Analyzing critical coverage gaps...")
    analyzer.analyze()
    
    report = analyzer.generate_report()
    print(report)
    
    # Save report
    output_dir = Path('coverage_reports')
    output_dir.mkdir(exist_ok=True)
    
    with open(output_dir / 'critical_coverage_gaps.txt', 'w') as f:
        f.write(report)
    
    print(f"\nReport saved to: {output_dir / 'critical_coverage_gaps.txt'}")

if __name__ == '__main__':
    main()
