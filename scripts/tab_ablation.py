#!/usr/bin/env python3
"""Generate LaTeX tables from ablation study results.

Reads ablation program-metric JSON results and the OpenGPGPU expression
coverage ablation result, then outputs LaTeX formatted tables suitable for
insertion into academic papers.
"""

import argparse
import json
import sys
from pathlib import Path
from typing import Dict, List, Any, Optional

# Script directory
SCRIPT_DIR = Path(__file__).resolve().parent
FUZZGPU_DIR = SCRIPT_DIR.parent
RESULTS_DIR = FUZZGPU_DIR / 'results' / 'ablation_study'
COVERAGE_RESULTS_DIR = FUZZGPU_DIR / 'results' / 'ablation_coverage'

# Config name mapping for display
CONFIG_DISPLAY_NAMES = {
    'baseline': 'Vanilla',
    'no_emu_guide': 'w/o Emu Guide',
    'no_barrier_plan': 'w/o Barrier Plan',
    'no_taint': 'w/o Taint',
}

# Config order for table
CONFIG_ORDER = ['baseline', 'no_emu_guide', 'no_barrier_plan', 'no_taint']
OPENGPGPU_ARCH = 'ventus'
OPENGPGPU_XLEN = 32
OPENGPGPU_COV_TYPE = 'expr'
EXPR_COVERAGE_CONFIG_ORDER = ['baseline', 'no_emu_guide', 'no_taint', 'no_barrier_plan']
EXPR_COVERAGE_DISPLAY_NAMES = {
    'baseline': 'Vanilla',
    'no_emu_guide': 'w/o Generation Stack',
    'no_taint': 'w/o Taint Propagation',
    'no_barrier_plan': 'w/o Barrier Planner',
}


def _parse_arch_xlen_from_stem(stem: str, prefix: str) -> Optional[Dict[str, Any]]:
    """Parse result filenames with optional method prefixes.

    Supported forms:
      ablation_{arch}_x{xlen}_{timestamp}
      ablation_{method}_{arch}_x{xlen}_{timestamp}
      ablation_coverage_{arch}_x{xlen}_{cov_type}_{timestamp}
      ablation_coverage_{method}_{arch}_x{xlen}_{cov_type}_{timestamp}
    """
    if not stem.startswith(prefix):
        return None

    parts = stem[len(prefix):].split('_')
    for idx, part in enumerate(parts):
        if not part.startswith('x') or idx == 0:
            continue
        try:
            xlen = int(part[1:])
        except ValueError:
            continue

        parsed = {
            'method': '_'.join(parts[:idx - 1]) or None,
            'arch': parts[idx - 1],
            'xlen': xlen,
        }
        if prefix == 'ablation_coverage_' and idx + 1 < len(parts):
            parsed['cov_type'] = parts[idx + 1]
        return parsed

    return None


def load_latest_results(results_dir: Path, arch: str = None, xlen: int = None) -> Dict[str, Any]:
    """Load the latest ablation results JSON file.
    
    Args:
        results_dir: Directory containing result JSON files
        arch: Filter by architecture (optional)
        xlen: Filter by XLEN (optional)
    
    Returns:
        Parsed JSON data from the latest matching file
    """
    if not results_dir.exists():
        print(f"Error: Results directory not found: {results_dir}")
        sys.exit(1)
    
    # Find all ablation result files
    pattern = "ablation_*.json"
    files = list(results_dir.glob(pattern))
    
    if not files:
        print(f"Error: No ablation result files found in {results_dir}")
        sys.exit(1)
    
    # Filter by arch/xlen if specified
    if arch or xlen:
        filtered = []
        for f in files:
            parsed = _parse_arch_xlen_from_stem(f.stem, 'ablation_')
            if not parsed:
                continue
            if arch and parsed['arch'] != arch:
                continue
            if xlen and parsed['xlen'] != xlen:
                continue
            filtered.append(f)
        files = filtered
    
    if not files:
        print(f"Error: No matching result files found")
        sys.exit(1)
    
    # Sort by modification time, get latest
    files.sort(key=lambda f: f.stat().st_mtime, reverse=True)
    latest_file = files[0]
    
    print(f"Loading results from: {latest_file}")
    
    with open(latest_file, 'r') as f:
        return json.load(f)


def load_latest_coverage_results(
    results_dir: Path,
    arch: str = OPENGPGPU_ARCH,
    xlen: int = OPENGPGPU_XLEN,
    cov_type: str = OPENGPGPU_COV_TYPE,
) -> Optional[Dict[str, Any]]:
    """Load the latest OpenGPGPU expression coverage result JSON file."""
    if not results_dir.exists():
        print(f"Warning: Coverage results directory not found: {results_dir}", file=sys.stderr)
        return None

    files = []
    for f in results_dir.glob("ablation_coverage_*.json"):
        parsed = _parse_arch_xlen_from_stem(f.stem, 'ablation_coverage_')
        if not parsed:
            continue
        if parsed.get('cov_type') != cov_type:
            continue
        if arch and parsed['arch'] != arch:
            continue
        if xlen and parsed['xlen'] != xlen:
            continue
        files.append(f)

    if not files:
        print(
            f"Warning: No OpenGPGPU expression coverage result files found in {results_dir}",
            file=sys.stderr,
        )
        return None

    def coverage_completeness(path: Path) -> int:
        try:
            with open(path, 'r') as f:
                data = json.load(f)
        except (OSError, json.JSONDecodeError):
            return 0

        configs = data.get('configs', {})
        score = 0
        for config_name in CONFIG_ORDER:
            final_cov = (
                configs.get(config_name, {})
                .get('stats', {})
                .get('final_coverage', {})
            )
            if final_cov.get('percent') is not None:
                score += 1
        return score

    files.sort(key=lambda f: (coverage_completeness(f), f.stat().st_mtime), reverse=True)
    latest_file = files[0]
    print(f"Loading OpenGPGPU expression coverage results from: {latest_file}")

    with open(latest_file, 'r') as f:
        return json.load(f)


def generate_latex_table(data: Dict[str, Any], use_median: bool = True) -> str:
    """Generate LaTeX table from ablation results.
    
    Args:
        data: Parsed JSON data from ablation results
        use_median: Use median (True) or mean (False) for statistics
    
    Returns:
        LaTeX table string
    """
    arch = data.get('arch', 'unknown')
    xlen = data.get('xlen', 32)
    configs = data.get('configs', {})
    
    stat_key = 'median' if use_median else 'mean'
    
    # Print tests and success info separately
    print("\n" + "=" * 60)
    print("TEST SUMMARY (not in table)")
    print("=" * 60)
    for config_name in CONFIG_ORDER:
        if config_name not in configs:
            continue
        config_data = configs[config_name]
        stats = config_data.get('stats', {})
        num_tests = stats.get('num_tests', 0)
        num_success = stats.get('num_successful', 0)
        display_name = CONFIG_DISPLAY_NAMES.get(config_name, config_name)
        print(f"  {display_name:<20}: Tests={num_tests}, Success={num_success}")
    print("=" * 60 + "\n")
    
    # Build LaTeX table
    lines = []
    lines.append(r"\begin{table}[htbp]")
    lines.append(r"  \centering")
    lines.append(f"  \\caption{{Ablation Study Results ({arch}/x{xlen})}}")
    lines.append(f"  \\label{{tab:ablation_{arch}_x{xlen}}}")
    lines.append(r"  \small")
    lines.append(r"  \begin{tabular}{lrrrr}")
    lines.append(r"    \toprule")
    lines.append(r"    Config & Compl. & Depth & Diverg. & Barr. \\")
    lines.append(r"    \midrule")
    
    for config_name in CONFIG_ORDER:
        if config_name not in configs:
            continue
        
        config_data = configs[config_name]
        stats = config_data.get('stats', {})
        
        # Get statistics
        compl_rate = stats.get('instr_completion_rate', {}).get(stat_key, 0) * 100
        simt_depth = stats.get('max_simt_stack_depth', {}).get(stat_key, 0)
        diverg_rate = stats.get('thread_divergence_rate', {}).get(stat_key, 0) * 100
        barrier_count = stats.get('barrier_success_count', {}).get(stat_key, 0)
        
        display_name = CONFIG_DISPLAY_NAMES.get(config_name, config_name)
        
        # Format row (rates as percentages with 1 decimal)
        row = f"    {display_name} & {compl_rate:.1f}\\% & {simt_depth:.1f} & {diverg_rate:.1f}\\% & {barrier_count:.1f} \\\\"
        lines.append(row)
    
    lines.append(r"    \bottomrule")
    lines.append(r"  \end{tabular}")
    lines.append(r"\end{table}")
    
    return "\n".join(lines)


def _format_optional_percent(value: Optional[float]) -> str:
    if value is None:
        return "--"
    return f"{value:.1f}\\%"


def generate_latex_expression_coverage_table(data: Dict[str, Any]) -> str:
    """Generate LaTeX table for OpenGPGPU expression coverage ablation."""
    configs = data.get('configs', {})
    print("\n" + "=" * 60)
    print("OPENGPGPU EXPRESSION COVERAGE SUMMARY (not in table)")
    print("=" * 60)
    print(f"  timestamp: {data.get('timestamp', 'unknown')}")
    for config_name in EXPR_COVERAGE_CONFIG_ORDER:
        config_data = configs.get(config_name, {})
        stats = config_data.get('stats', {})
        final_cov = stats.get('final_coverage', {})
        display_name = EXPR_COVERAGE_DISPLAY_NAMES.get(config_name, config_name)
        covered = final_cov.get('covered')
        total = final_cov.get('total')
        percent = final_cov.get('percent')
        if percent is None:
            print(f"  {display_name:<20}: --")
        else:
            print(f"  {display_name:<20}: {covered}/{total} ({percent:.2f}%)")
    print("=" * 60 + "\n")

    lines = []
    lines.append(r"\begin{table}[htbp]")
    lines.append(r"  \centering")
    lines.append(r"  \caption{Impact of Ablations on OpenGPGPU Expression Coverage}")
    lines.append(r"  \label{tab:ablation_opengpgpu_expr_coverage}")
    lines.append(r"  \small")
    lines.append(r"  \begin{tabular}{lr}")
    lines.append(r"    \toprule")
    lines.append(r"    Config & Expr. Cov. \\")
    lines.append(r"    \midrule")

    for config_name in EXPR_COVERAGE_CONFIG_ORDER:
        display_name = EXPR_COVERAGE_DISPLAY_NAMES.get(config_name, config_name)
        config_data = configs.get(config_name, {})
        stats = config_data.get('stats', {})
        final_cov = stats.get('final_coverage', {})
        percent = final_cov.get('percent')
        lines.append(f"    {display_name} & {_format_optional_percent(percent)} \\\\")

    lines.append(r"    \bottomrule")
    lines.append(r"  \end{tabular}")
    lines.append(r"\end{table}")

    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(
        description='Generate LaTeX table from ablation study results'
    )
    
    parser.add_argument(
        '--results-dir', type=str, default=str(RESULTS_DIR),
        help=f'Path to ablation results directory (default: {RESULTS_DIR})'
    )
    parser.add_argument(
        '--coverage-results-dir', type=str, default=str(COVERAGE_RESULTS_DIR),
        help=f'Path to ablation coverage results directory (default: {COVERAGE_RESULTS_DIR})'
    )
    parser.add_argument(
        '--no-coverage', action='store_true',
        help='Only generate the program-metric ablation table'
    )
    parser.add_argument(
        '--arch', type=str, choices=['ventus', 'vortex'], default=None,
        help='Filter by architecture'
    )
    parser.add_argument(
        '--xlen', type=int, choices=[32, 64], default=None,
        help='Filter by XLEN'
    )
    parser.add_argument(
        '--use-mean', action='store_true',
        help='Use mean instead of median for statistics'
    )
    parser.add_argument(
        '--output', '-o', type=str, default=None,
        help='Output file path (default: print to stdout)'
    )
    
    args = parser.parse_args()
    
    results_dir = Path(args.results_dir)
    data = load_latest_results(results_dir, args.arch, args.xlen)
    
    latex_tables = [generate_latex_table(data, use_median=not args.use_mean)]

    if not args.no_coverage:
        coverage_results_dir = Path(args.coverage_results_dir)
        coverage_data = load_latest_coverage_results(coverage_results_dir)
        if coverage_data:
            latex_tables.append(generate_latex_expression_coverage_table(coverage_data))
        else:
            print("\nWarning: OpenGPGPU expression coverage table not generated", file=sys.stderr)

    latex_output = "\n\n".join(latex_tables)
    
    print("\nLaTeX Tables:")
    print("-" * 60)
    print(latex_output)
    print("-" * 60)
    
    if args.output:
        output_path = Path(args.output)
        output_path.parent.mkdir(parents=True, exist_ok=True)
        with open(output_path, 'w') as f:
            f.write(latex_output)
        print(f"\nTable saved to: {output_path}")


if __name__ == '__main__':
    main()
