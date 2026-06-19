#!/usr/bin/env python3
"""Plot baseline comparison results from multiple fuzzing runs.

Usage:
  python3 scripts/plot_baseline.py --results-dir /tmp/baseline_results
  python3 scripts/plot_baseline.py --results-dir /tmp/baseline_results --output-dir /tmp/plots
"""

import argparse
import json
import sys
from pathlib import Path
from collections import defaultdict
import matplotlib.pyplot as plt
from common_plot import *

GREEN = "\033[32m"
RED = "\033[31m"
YELLOW = "\033[33m"
BLUE = "\033[34m"
RESET = "\033[0m"

# Method name mapping for display
METHOD_DISPLAY_NAMES = {
    'fuzzgpu_random': 'FuzzGPU',
    'cascade': 'Ported-Cascade',
    'divefuzz': 'Ported-DiveFuzz',
}

# Fixed order and colors for methods to ensure consistency across subplots
METHOD_ORDER = ['fuzzgpu_random', 'cascade', 'divefuzz']
METHOD_COLORS = {
    'fuzzgpu_random': '#d62728',  # red
    'cascade': '#1f77b4',         # blue
    'divefuzz': '#2ca02c',        # green
}


def _get_display_name(method_name):
    """Get display name for a method, using mapping if available."""
    return METHOD_DISPLAY_NAMES.get(method_name, method_name)


def _get_method_color(method_name):
    """Get color for a method, using mapping if available."""
    return METHOD_COLORS.get(method_name, None)


def _normalize_coverage_data(raw):
    """Normalize coverage data to a list of dicts with timestamp and percent."""
    normalized = []
    for entry in raw:
        try:
            if isinstance(entry, dict):
                ts = entry.get('timestamp')
                percent = entry.get('percent', entry.get('coverage'))
                covered = entry.get('covered')
                total = entry.get('total')
                if ts is not None and percent is not None:
                    normalized.append({
                        'timestamp': float(ts),
                        'percent': float(percent),
                        'covered': covered,
                        'total': total
                    })
            elif isinstance(entry, (list, tuple)) and len(entry) >= 2:
                ts, cov = entry[0], entry[1]
                normalized.append({
                    'timestamp': float(ts),
                    'percent': float(cov),
                    'covered': None,
                    'total': None
                })
        except Exception:
            continue
    return normalized


def _get_config_key(data):
    """Extract (arch, xlen, cov_type) tuple from result data."""
    # Try to get from top-level fields first (new format)
    arch = data.get('arch')
    xlen = data.get('xlen')
    cov_type = data.get('cov_type')
    
    # Fall back to args if not found at top level (backward compatibility)
    if arch is None or xlen is None or cov_type is None:
        args = data.get('args', {})
        arch = arch or args.get('arch', 'unknown')
        xlen = xlen or args.get('xlen', 32)
        cov_type = cov_type or args.get('cov_type', 'unknown')
    
    return (arch, xlen, cov_type)


def collect_all_results(results_dir):
    """Recursively collect all results.json files and group by (arch, xlen, cov_type)."""
    results_dir = Path(results_dir)
    
    # Structure: {(arch, xlen, cov_type): {method_name: (timestamp, subdir, coverage_data, jobs)}}
    grouped_results = defaultdict(dict)
    
    # Search for results.json files recursively
    for result_file in results_dir.rglob('results.json'):
        try:
            with open(result_file, 'r') as f:
                data = json.load(f)
                
            method_name = data.get('method', 'unknown')
            run_timestamp = data.get('timestamp', '')
            coverage_data = data.get('coverage_data', [])
            config_key = _get_config_key(data)
            
            # Get jobs count from args
            args = data.get('args', {})
            jobs = args.get('jobs', 1)
            
            method_latest = grouped_results[config_key]
            
            # Keep only the latest run for each method within each config group
            if method_name not in method_latest or run_timestamp > method_latest[method_name][0]:
                method_latest[method_name] = (run_timestamp, result_file.parent, coverage_data, jobs)
                
        except Exception as e:
            print(f"{YELLOW}Failed to read {result_file}: {e}{RESET}")
            continue
    
    return grouped_results


def plot_coverage_for_config(config_key, method_latest, output_path):
    """Generate coverage plot for a specific (arch, xlen, cov_type) configuration."""
    arch, xlen, cov_type = config_key
    
    if not method_latest:
        print(f"{YELLOW}No data for {arch}/x{xlen}/cov{cov_type}{RESET}")
        return False
    
    print(f"\n{BLUE}Generating plot for: arch={arch}, xlen={xlen}, cov_type={cov_type}{RESET}")
    
    # Extract coverage data from latest runs (with jobs count)
    methods_data = {}
    for method_name, (_, subdir, coverage_data, jobs) in method_latest.items():
        methods_data[method_name] = (coverage_data, jobs)
        print(f"  Using latest run for {method_name}: {subdir.name} (jobs={jobs})")
    
    # Plot
    fig_width = textwidth_in
    fig_height = fig_width * 0.45
    plt.figure(figsize=(fig_width, fig_height))
    
    for method_name, (coverage_data, jobs) in methods_data.items():
        normalized = _normalize_coverage_data(coverage_data)
        if not normalized:
            print(f"  {YELLOW}No coverage data for {method_name}{RESET}")
            continue

        normalized.sort(key=lambda x: x['timestamp'])

        # Aggregate: take max percent for same timestamp
        time_cov_map = {}
        for item in normalized:
            ts = item['timestamp']
            pct = item['percent']
            if ts not in time_cov_map:
                time_cov_map[ts] = pct
            else:
                time_cov_map[ts] = max(time_cov_map[ts], pct)

        times = sorted(time_cov_map.keys())
        if times:
            start_time = times[0]
            # Convert wall-clock seconds to core hours: (elapsed_seconds * jobs) / 3600
            times_core_hours = [(t - start_time) * jobs / 3600.0 for t in times]
            percents = [time_cov_map[t] for t in times]

            # Cumulative max keeps curve non-decreasing
            max_cov = []
            current_max = 0.0
            for c in percents:
                current_max = max(current_max, c)
                max_cov.append(current_max)

            display_name = _get_display_name(method_name)
            plt.plot(times_core_hours, max_cov, label=display_name, linewidth=1)
    
    plt.xlabel('Core Hours')
    # Set ylabel based on coverage type
    if cov_type == 'expr':
        ylabel = 'Expr Coverage (%)'
    elif cov_type == 'line':
        ylabel = 'Line Coverage (%)'
    elif cov_type == 'toggle':
        ylabel = 'Toggle Coverage (%)'
    else:
        ylabel = 'Line/Toggle Coverage (%)'
    plt.ylabel(ylabel)
    #plt.title(f'Coverage Comparison: {arch} x{xlen} cov={cov_type}')
    if cov_type == 'line':
        plt.legend(fontsize=FONTSIZE-1, ncol=3, handlelength=1.5, columnspacing=1)
        #plt.ylim(top=100)
    plt.grid(True, alpha=0.3)
    plt.tight_layout()
    
    # Save both PNG and PDF
    output_path_png = output_path.with_suffix('.png')
    output_path_pdf = output_path.with_suffix('.pdf')
    plt.savefig(output_path_png, dpi=300, bbox_inches='tight')
    plt.savefig(output_path_pdf, format='pdf', dpi=300, bbox_inches='tight')
    plt.close()
    print(f"  {GREEN}Plots saved to: {output_path_png} and {output_path_pdf}{RESET}")
    
    return True


def _plot_coverage_on_ax(ax, methods_data, cov_type, show_legend=False):
    """Plot coverage data on a given axis.
    
    Args:
        methods_data: dict mapping method_name -> (coverage_data, jobs)
    """
    # Sort methods by predefined order to ensure consistent colors
    sorted_methods = sorted(methods_data.keys(), 
                            key=lambda m: METHOD_ORDER.index(m) if m in METHOD_ORDER else len(METHOD_ORDER))
    
    # Track max covered and total for title
    max_covered = 0
    total_points = None
    
    for method_name in sorted_methods:
        coverage_data, jobs = methods_data[method_name]
        normalized = _normalize_coverage_data(coverage_data)
        if not normalized:
            continue

        normalized.sort(key=lambda x: x['timestamp'])

        # Aggregate: take max percent for same timestamp, track covered/total
        time_cov_map = {}
        time_covered_map = {}
        for item in normalized:
            ts = item['timestamp']
            pct = item['percent']
            covered = item.get('covered')
            total = item.get('total')
            
            if ts not in time_cov_map:
                time_cov_map[ts] = pct
                time_covered_map[ts] = covered
            else:
                if pct > time_cov_map[ts]:
                    time_cov_map[ts] = pct
                    time_covered_map[ts] = covered
            
            # Update total (should be consistent across entries)
            if total is not None:
                total_points = total

        times = sorted(time_cov_map.keys())
        if times:
            start_time = times[0]
            # Convert wall-clock seconds to core hours: (elapsed_seconds * jobs) / 3600
            times_core_hours = [(t - start_time) * jobs / 3600.0 for t in times]
            percents = [time_cov_map[t] for t in times]

            # Cumulative max keeps curve non-decreasing
            max_cov = []
            current_max = 0.0
            current_max_covered = 0
            for i, c in enumerate(percents):
                if c > current_max:
                    current_max = c
                    ts = times[i]
                    if time_covered_map.get(ts) is not None:
                        current_max_covered = time_covered_map[ts]
                max_cov.append(current_max)
            
            # Track the overall max covered across all methods
            if current_max_covered > max_covered:
                max_covered = current_max_covered

            display_name = _get_display_name(method_name)
            color = _get_method_color(method_name)
            ax.plot(times_core_hours, max_cov, label=display_name, linewidth=1.2, color=color)
    
    # Set title based on coverage type with absolute coverage points
    if cov_type == 'expr':
        title_base = 'Expression Coverage'
    elif cov_type == 'line':
        title_base = 'Line Coverage'
    elif cov_type == 'toggle':
        title_base = 'Toggle Coverage'
    else:
        title_base = f'{cov_type} Coverage'
    
    # Add total coverage points to title if available
    if total_points is not None:
        title = f'{title_base} ({total_points} points)'
    else:
        title = title_base
    
    ax.set_ylabel('Coverage (%)')
    ax.set_title(title, fontsize=FONTSIZE, pad=1)
    ax.grid(True, alpha=0.3)
    
    if show_legend:
        ax.legend(fontsize=FONTSIZE-1, ncol=3, handlelength=1.5, columnspacing=1)


def plot_combined_coverage(arch_xlen_key, cov_type_data, output_path):
    """Generate combined coverage plot with line, toggle, expr as subplots."""
    arch, xlen = arch_xlen_key
    
    # Order of coverage types to plot
    cov_order = ['line', 'toggle', 'expr']
    available_cov_types = [ct for ct in cov_order if ct in cov_type_data]
    
    if not available_cov_types:
        print(f"{YELLOW}No coverage data for {arch}/x{xlen}{RESET}")
        return False
    
    print(f"\n{BLUE}Generating combined plot for: arch={arch}, xlen={xlen}{RESET}")
    
    n_plots = len(available_cov_types)
    fig_width = textwidth_in
    fig_height = fig_width * 0.35 * n_plots
    
    fig, axes = plt.subplots(n_plots, 1, figsize=(fig_width, fig_height), sharex=True)
    
    # Handle case of single subplot
    if n_plots == 1:
        axes = [axes]
    
    for idx, cov_type in enumerate(available_cov_types):
        method_latest = cov_type_data[cov_type]
        
        # Extract coverage data from latest runs (with jobs count)
        methods_data = {}
        for method_name, (_, subdir, coverage_data, jobs) in method_latest.items():
            methods_data[method_name] = (coverage_data, jobs)
            print(f"  Using latest run for {method_name} ({cov_type}): {subdir.name} (jobs={jobs})")
        
        # Show legend only on the first subplot
        show_legend = (idx == 0)
        _plot_coverage_on_ax(axes[idx], methods_data, cov_type, show_legend=show_legend)
    
    # Set xlabel only on the bottom subplot
    axes[-1].set_xlabel('Core Hours')
    
    plt.tight_layout()
    
    # Save both PNG and PDF
    output_path_png = output_path.with_suffix('.png')
    output_path_pdf = output_path.with_suffix('.pdf')
    plt.savefig(output_path_png, dpi=300, bbox_inches='tight')
    plt.savefig(output_path_pdf, format='pdf', dpi=300, bbox_inches='tight')
    plt.close()
    print(f"  {GREEN}Plots saved to: {output_path_png} and {output_path_pdf}{RESET}")
    
    return True


def plot_coverage_comparison(results_dir, output_dir=None):
    """Generate coverage comparison plots for each (arch, xlen) combination with all cov_types combined."""
    results_dir = Path(results_dir)
    
    # Set output directory
    if output_dir is None:
        # Default: save plots to results/plots directory
        script_dir = Path(__file__).resolve().parent
        fuzzgpu_dir = script_dir.parent
        output_dir = fuzzgpu_dir / 'results' / 'plots'
    else:
        output_dir = Path(output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)
    
    # Collect all results grouped by configuration
    grouped_results = collect_all_results(results_dir)
    
    if not grouped_results:
        print(f"{RED}No valid results found in {results_dir}{RESET}")
        return False
    
    print(f"{BLUE}Found {len(grouped_results)} configuration(s):{RESET}")
    for config_key in sorted(grouped_results.keys()):
        arch, xlen, cov_type = config_key
        methods = list(grouped_results[config_key].keys())
        # Print jobs for each method
        jobs_info = []
        for method_name, (_, _, _, jobs) in grouped_results[config_key].items():
            jobs_info.append(f"{method_name}(jobs={jobs})")
        print(f"  {arch}_x{xlen}_cov{cov_type}: {len(methods)} method(s) - {', '.join(jobs_info)}")
    
    # Regroup by (arch, xlen) to combine cov_types
    arch_xlen_grouped = defaultdict(dict)
    for config_key, method_data in grouped_results.items():
        arch, xlen, cov_type = config_key
        arch_xlen_grouped[(arch, xlen)][cov_type] = method_data
    
    # Generate a combined plot for each (arch, xlen)
    success_count = 0
    for arch_xlen_key in sorted(arch_xlen_grouped.keys()):
        arch, xlen = arch_xlen_key
        output_filename = f"coverage_comparison_{arch}_x{xlen}_combined.png"
        output_path = output_dir / output_filename
        
        if plot_combined_coverage(arch_xlen_key, arch_xlen_grouped[arch_xlen_key], output_path):
            success_count += 1
    
    print(f"\n{GREEN}Generated {success_count}/{len(arch_xlen_grouped)} combined plot(s) in {output_dir}{RESET}")
    return success_count > 0


def print_coverage_summary(results_dir):
    """Print human-readable coverage summary for all configurations and methods."""
    results_dir = Path(results_dir)
    
    # Collect all results grouped by configuration
    grouped_results = collect_all_results(results_dir)
    
    if not grouped_results:
        print(f"{RED}No valid results found in {results_dir}{RESET}")
        return False
    
    # Group by (arch, xlen) for organized output
    arch_xlen_grouped = defaultdict(dict)
    for config_key, method_data in grouped_results.items():
        arch, xlen, cov_type = config_key
        arch_xlen_grouped[(arch, xlen)][cov_type] = method_data
    
    print(f"\n{'='*80}")
    print(f"{'COVERAGE SUMMARY':^80}")
    print(f"{'='*80}\n")
    
    for (arch, xlen) in sorted(arch_xlen_grouped.keys()):
        print(f"{BLUE}┌{'─'*78}┐{RESET}")
        print(f"{BLUE}│ {'Architecture: ' + arch.upper() + ' x' + str(xlen):^76} │{RESET}")
        print(f"{BLUE}└{'─'*78}┘{RESET}")
        
        cov_type_data = arch_xlen_grouped[(arch, xlen)]
        
        for cov_type in ['line', 'toggle', 'expr']:
            if cov_type not in cov_type_data:
                continue
            
            method_latest = cov_type_data[cov_type]
            
            print(f"\n  {GREEN}▶ {cov_type.upper()} Coverage:{RESET}")
            print(f"  {'─'*74}")
            print(f"  {'Method':<20} {'Final %':>10} {'Covered':>12} {'Total':>12} {'Jobs':>6} {'Run':>16}")
            print(f"  {'─'*74}")
            
            # Sort methods by predefined order
            sorted_methods = sorted(method_latest.keys(), 
                                    key=lambda m: METHOD_ORDER.index(m) if m in METHOD_ORDER else len(METHOD_ORDER))
            
            for method_name in sorted_methods:
                timestamp, subdir, coverage_data, jobs = method_latest[method_name]
                
                # Get final coverage values
                final_percent = None
                final_covered = None
                final_total = None
                
                normalized = _normalize_coverage_data(coverage_data)
                if normalized:
                    # Find max coverage (cumulative)
                    max_percent = 0.0
                    for entry in normalized:
                        pct = entry.get('percent', 0)
                        if pct > max_percent:
                            max_percent = pct
                            final_covered = entry.get('covered')
                            final_total = entry.get('total')
                    final_percent = max_percent
                
                display_name = _get_display_name(method_name)
                run_name = subdir.name if hasattr(subdir, 'name') else str(subdir)
                # Truncate run name for display
                if len(run_name) > 16:
                    run_name = run_name[-16:]
                
                if final_percent is not None:
                    covered_str = str(final_covered) if final_covered is not None else 'N/A'
                    total_str = str(final_total) if final_total is not None else 'N/A'
                    print(f"  {display_name:<20} {final_percent:>9.2f}% {covered_str:>12} {total_str:>12} {jobs:>6} {run_name:>16}")
                else:
                    print(f"  {display_name:<20} {'N/A':>10} {'N/A':>12} {'N/A':>12} {jobs:>6} {run_name:>16}")
            
            print(f"  {'─'*74}")
        
        print()
    
    print(f"{'='*80}\n")
    return True


def main():
    parser = argparse.ArgumentParser(description='Plot baseline comparison results.')
    parser.add_argument('--results-dir', type=str, required=True,
                        help='Directory containing baseline results')
    parser.add_argument('--output-dir', type=str, default=None,
                        help='Output directory for plots (default: same as results-dir)')
    parser.add_argument('--print-summary', action='store_true',
                        help='Print human-readable coverage summary without generating plots')
    parser.add_argument('--no-plot', action='store_true',
                        help='Skip generating plots (use with --print-summary)')
    
    args = parser.parse_args()
    
    results_dir = Path(args.results_dir)
    if not results_dir.exists():
        print(f"{RED}Results directory not found: {results_dir}{RESET}")
        sys.exit(1)
    
    # Always print summary
    print_coverage_summary(results_dir)
    
    # Generate plots unless --no-plot is specified
    if not args.no_plot:
        success = plot_coverage_comparison(results_dir, args.output_dir)
        sys.exit(0 if success else 1)
    
    sys.exit(0)


if __name__ == '__main__':
    main()
