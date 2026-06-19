#!/usr/bin/env python3
"""
Plot Ablation Coverage Results

Reads JSON output from ablation_coverage.py and plots coverage over time
for different configurations.

Usage:
    python plot_ablation_coverage.py <result_json_file> [options]
    python plot_ablation_coverage.py --result-dir <dir> [options]
"""

from __future__ import annotations

import argparse
import json
import os
import sys
from pathlib import Path
from typing import Dict, List, Optional, Tuple

# Add script directory to path for common_plot import
script_dir = Path(__file__).resolve().parent
sys.path.insert(0, str(script_dir))

from common_plot import *  # Import unified plot settings (FONTSIZE, textwidth_in, etc.)
import numpy as np

# Colors for different configurations
CONFIG_COLORS = {
    'baseline': '#2ecc71',      # Green
    'no_emu_guide': '#e74c3c',  # Red
    'no_barrier_plan': '#3498db',  # Blue
    'no_taint': '#9b59b6',      # Purple
}

# Line styles for different configurations
CONFIG_LINESTYLES = {
    'baseline': '-',
    'no_emu_guide': '--',
    'no_barrier_plan': '-.',
    'no_taint': ':',
}

# Markers for different configurations
CONFIG_MARKERS = {
    'baseline': 'o',
    'no_emu_guide': 's',
    'no_barrier_plan': '^',
    'no_taint': 'd',
}


def load_ablation_results(json_path: Path) -> Dict:
    """Load ablation coverage results from JSON file."""
    with open(json_path, 'r') as f:
        return json.load(f)


def find_result_files(result_dir: Path, cov_type: Optional[str] = None, 
                      arch: Optional[str] = None) -> List[Path]:
    """Find all ablation coverage result JSON files in directory."""
    pattern = "ablation_coverage_*.json"
    files = list(result_dir.glob(pattern))
    
    # Filter by coverage type if specified
    if cov_type:
        files = [f for f in files if f"_{cov_type}_" in f.name]
    
    # Filter by architecture if specified
    if arch:
        files = [f for f in files if f"_{arch}_" in f.name]
    
    return sorted(files, key=lambda x: x.stat().st_mtime, reverse=True)


def extract_coverage_data(data: Dict) -> Dict[str, Tuple[List[float], List[int]]]:
    """Extract coverage over time data for each configuration.
    
    Returns:
        Dict mapping config_name -> (time_points, coverage_points)
        where coverage_points is the absolute number of covered points
    """
    coverage_data = {}
    
    configs = data.get('configs', {})
    for config_name, config_data in configs.items():
        # Try to get absolute covered counts from coverage_timeline in results
        results = config_data.get('results', {})
        coverage_timeline = results.get('coverage_timeline', [])
        
        if coverage_timeline:
            time_points = [c['timestamp'] for c in coverage_timeline]
            coverage_points = [c['covered'] for c in coverage_timeline]
            # Get total from last entry
            total_points = coverage_timeline[-1].get('total', 0) if coverage_timeline else 0
            coverage_data[config_name] = (time_points, coverage_points, total_points)
        else:
            # Fallback to coverage_over_time (percent-based, for backward compatibility)
            cov_over_time = config_data.get('coverage_over_time', {})
            time_points = cov_over_time.get('time_points', [])
            coverage_percents = cov_over_time.get('coverage_percents', [])
            
            if time_points and coverage_percents:
                # Convert percent to approximate points if total is available
                stats = config_data.get('stats', {})
                final_cov = stats.get('final_coverage', {})
                total = final_cov.get('total', 0)
                if total > 0:
                    coverage_points = [int(p * total / 100) for p in coverage_percents]
                else:
                    coverage_points = coverage_percents  # Fallback to percent
                coverage_data[config_name] = (time_points, coverage_points, total)
    
    # Print coverage data summary
    print("\n" + "=" * 70)
    print("Coverage Data Summary (Absolute Coverage Points)")
    print("=" * 70)
    for config_name, (time_points, cov_points, total_points) in coverage_data.items():
        if cov_points:
            final_cov = cov_points[-1]
            max_time_hours = time_points[-1] / 3600 if time_points else 0
            print(f"  {config_name}:")
            print(f"    Data points: {len(cov_points)}")
            print(f"    Covered: {final_cov} / {total_points} points ({100*final_cov/total_points:.2f}%)" if total_points > 0 else f"    Covered: {final_cov} points")
            print(f"    Max time: {max_time_hours:.2f} core hours")
    print("=" * 70 + "\n")
    
    return coverage_data


def plot_coverage_over_time(
    coverage_data: Dict[str, Tuple[List[float], List[int], int]],
    title: str = "Coverage Over Time",
    output_path: Optional[Path] = None,
    figsize: Optional[Tuple[float, float]] = None,
    show_markers: bool = True,
    marker_interval: int = 10,
    grid: bool = True,
    legend_loc: str = 'lower right',
    xlim: Optional[Tuple[float, float]] = None,
    ylim: Optional[Tuple[float, float]] = None,
):
    """Plot coverage points over time for multiple configurations.
    
    Args:
        coverage_data: Dict mapping config_name -> (time_points, coverage_points, total_points)
        title: Plot title
        output_path: Path to save the plot (if None, displays interactively)
        figsize: Figure size (width, height). If None, uses common_plot defaults.
        show_markers: Whether to show markers on data points
        marker_interval: Interval between markers (to avoid overcrowding)
        grid: Whether to show grid
        legend_loc: Legend location
        xlim: X-axis limits (min, max)
        ylim: Y-axis limits (min, max)
    """
    # Use common_plot sizing if not specified
    if figsize is None:
        fig_width = textwidth_in
        fig_height = fig_width * 0.6  # Good ratio for line plots
        figsize = (fig_width, fig_height)
    
    fig, ax = plt.subplots(figsize=figsize)
    
    # Time conversion factor: convert seconds to core hours
    # Core hours = wall time (seconds) / 3600
    time_factor = 1.0 / 3600.0
    time_label = "Core Hours"
    
    for config_name, (time_points, coverage_points, total_points) in coverage_data.items():
        # Convert time
        times = [t * time_factor for t in time_points]
        
        # Get style for this config
        color = CONFIG_COLORS.get(config_name, '#333333')
        linestyle = CONFIG_LINESTYLES.get(config_name, '-')
        marker = CONFIG_MARKERS.get(config_name, 'o') if show_markers else None
        
        # Subsample markers to avoid overcrowding
        markevery = marker_interval if show_markers and len(times) > marker_interval else None
        
        # Plot the line
        ax.plot(
            times, coverage_points,
            label=config_name,
            color=color,
            linestyle=linestyle,
            marker=marker,
            markevery=markevery,
            markersize=6,
            linewidth=2,
            alpha=0.9,
        )
    
    # Formatting (use FONTSIZE from common_plot)
    ax.set_xlabel(time_label)
    ax.set_ylabel("Coverage Points")
    ax.set_title(title)
    
    if grid:
        ax.grid(True, linestyle='--', alpha=0.7)
    
    ax.legend(loc=legend_loc)
    
    if xlim:
        ax.set_xlim(xlim)
    if ylim:
        ax.set_ylim(ylim)
    else:
        # Default: 0 to slightly above max coverage points
        all_covs = [c for _, (_, covs, _) in coverage_data.items() for c in covs]
        if all_covs:
            max_cov = max(all_covs)
            ax.set_ylim(0, max_cov * 1.1)
    
    plt.tight_layout()
    
    if output_path:
        # Save both PDF and PNG as per PLOT_CONVENTIONS.md
        output_path = Path(output_path)
        plt.savefig(output_path.with_suffix('.png'), dpi=300, bbox_inches='tight')
        plt.savefig(output_path.with_suffix('.pdf'), format='pdf', dpi=300, bbox_inches='tight')
        print(f"Plot saved to: {output_path.with_suffix('.png')}")
        print(f"Plot saved to: {output_path.with_suffix('.pdf')}")
    else:
        plt.show()
    
    plt.close(fig)


def plot_coverage_comparison_bar(
    data: Dict,
    output_path: Optional[Path] = None,
    figsize: Optional[Tuple[float, float]] = None,
):
    """Plot bar chart comparing final coverage across configurations."""
    # Use common_plot sizing if not specified
    if figsize is None:
        fig_width = textwidth_in
        fig_height = fig_width * 0.5  # Good ratio for bar charts
        figsize = (fig_width, fig_height)
    
    fig, ax = plt.subplots(figsize=figsize)
    
    configs = data.get('configs', {})
    config_names = []
    final_coverages = []
    colors = []
    
    for config_name, config_data in configs.items():
        stats = config_data.get('stats', {})
        final_cov = stats.get('final_coverage', {})
        percent = final_cov.get('percent', 0.0)
        
        config_names.append(config_name)
        final_coverages.append(percent)
        colors.append(CONFIG_COLORS.get(config_name, '#333333'))
    
    x_pos = np.arange(len(config_names))
    bars = ax.bar(x_pos, final_coverages, color=colors, alpha=0.8, edgecolor='black')
    
    # Add value labels on bars
    for bar, val in zip(bars, final_coverages):
        height = bar.get_height()
        ax.annotate(f'{val:.2f}%',
                    xy=(bar.get_x() + bar.get_width() / 2, height),
                    xytext=(0, 3),
                    textcoords="offset points",
                    ha='center', va='bottom')
    
    ax.set_xlabel("Configuration")
    ax.set_ylabel("Final Coverage (%)")
    ax.set_title(f"Final Coverage Comparison ({data.get('cov_type', 'unknown')} coverage)")
    ax.set_xticks(x_pos)
    ax.set_xticklabels(config_names, rotation=15, ha='right')
    ax.grid(True, axis='y', linestyle='--', alpha=0.7)
    
    plt.tight_layout()
    
    if output_path:
        # Save both PDF and PNG as per PLOT_CONVENTIONS.md
        output_path = Path(output_path)
        plt.savefig(output_path.with_suffix('.png'), dpi=300, bbox_inches='tight')
        plt.savefig(output_path.with_suffix('.pdf'), format='pdf', dpi=300, bbox_inches='tight')
        print(f"Bar chart saved to: {output_path.with_suffix('.png')}")
        print(f"Bar chart saved to: {output_path.with_suffix('.pdf')}")
    else:
        plt.show()
    
    plt.close(fig)


def print_summary(data: Dict):
    """Print summary of results."""
    print("\n" + "=" * 60)
    print(f"Ablation Coverage Results Summary")
    print("=" * 60)
    print(f"  Timestamp: {data.get('timestamp', 'unknown')}")
    print(f"  Architecture: {data.get('arch', 'unknown')}")
    print(f"  XLEN: {data.get('xlen', 'unknown')}")
    print(f"  Coverage Type: {data.get('cov_type', 'unknown')}")
    print("-" * 60)
    
    header = f"{'Config':<20} {'Tests':>8} {'Success':>8} {'Covered':>10} {'Total':>10} {'Percent':>10}"
    print(header)
    print("-" * len(header))
    
    configs = data.get('configs', {})
    for config_name, config_data in configs.items():
        stats = config_data.get('stats', {})
        num_tests = stats.get('num_tests', 0)
        num_success = stats.get('num_successful', 0)
        final_cov = stats.get('final_coverage', {})
        covered = final_cov.get('covered', 0)
        total = final_cov.get('total', 0)
        percent = final_cov.get('percent', 0.0)
        
        print(f"{config_name:<20} {num_tests:>8} {num_success:>8} {covered:>10} {total:>10} {percent:>10.2f}%")
    
    print("=" * 60 + "\n")


def main():
    parser = argparse.ArgumentParser(
        description='Plot ablation coverage results from FuzzGPU'
    )
    
    # Input options
    input_group = parser.add_mutually_exclusive_group(required=True)
    input_group.add_argument('result_file', type=str, nargs='?',
                             help='Path to ablation coverage result JSON file')
    input_group.add_argument('--result-dir', type=str,
                             help='Directory containing result JSON files (uses most recent)')
    
    # Filter options
    parser.add_argument('--cov-type', type=str, choices=['line', 'toggle', 'expr'],
                        help='Filter results by coverage type (when using --result-dir)')
    parser.add_argument('--arch', type=str, choices=['ventus', 'vortex'],
                        help='Filter results by architecture (when using --result-dir)')
    
    # Output options
    parser.add_argument('--output', '-o', type=str, default=None,
                        help='Output file path for the plot (PNG/PDF/SVG)')
    parser.add_argument('--output-bar', type=str, default=None,
                        help='Output file path for bar chart comparison')
    parser.add_argument('--no-show', action='store_true',
                        help='Do not display plots interactively')
    
    # Plot options
    parser.add_argument('--title', type=str, default=None,
                        help='Custom plot title')
    parser.add_argument('--figsize', type=str, default=None,
                        help='Figure size as "width,height" (default: uses common_plot textwidth_in)')
    parser.add_argument('--no-markers', action='store_true',
                        help='Do not show markers on data points')
    parser.add_argument('--marker-interval', type=int, default=10,
                        help='Interval between markers (default: 10)')
    parser.add_argument('--no-grid', action='store_true',
                        help='Do not show grid')
    parser.add_argument('--legend-loc', type=str, default='lower right',
                        help='Legend location (default: lower right)')
    parser.add_argument('--xlim', type=str, default=None,
                        help='X-axis limits as "min,max"')
    parser.add_argument('--ylim', type=str, default=None,
                        help='Y-axis limits as "min,max"')
    
    # Misc
    parser.add_argument('--summary', action='store_true',
                        help='Print summary of results')
    parser.add_argument('--configs', type=str, nargs='*', default=None,
                        help='Only plot specific configurations')
    
    args = parser.parse_args()
    
    # Determine input file
    if args.result_file:
        json_path = Path(args.result_file)
    else:
        result_dir = Path(args.result_dir)
        if not result_dir.exists():
            print(f"Error: Result directory not found: {result_dir}", file=sys.stderr)
            sys.exit(1)
        
        files = find_result_files(result_dir, cov_type=args.cov_type, arch=args.arch)
        if not files:
            print(f"Error: No result files found in {result_dir}", file=sys.stderr)
            sys.exit(1)
        
        json_path = files[0]  # Most recent
        print(f"Using result file: {json_path}")
    
    if not json_path.exists():
        print(f"Error: Result file not found: {json_path}", file=sys.stderr)
        sys.exit(1)
    
    # Load data
    data = load_ablation_results(json_path)
    
    # Print summary if requested
    if args.summary:
        print_summary(data)
    
    # Extract coverage data
    coverage_data = extract_coverage_data(data)
    
    if not coverage_data:
        print("Error: No coverage data found in result file", file=sys.stderr)
        sys.exit(1)
    
    # Filter configurations if specified
    if args.configs:
        coverage_data = {k: v for k, v in coverage_data.items() if k in args.configs}
        if not coverage_data:
            print(f"Error: None of the specified configs found: {args.configs}", file=sys.stderr)
            sys.exit(1)
    
    # Parse plot options
    figsize = tuple(map(float, args.figsize.split(','))) if args.figsize else None
    xlim = tuple(map(float, args.xlim.split(','))) if args.xlim else None
    ylim = tuple(map(float, args.ylim.split(','))) if args.ylim else None
    
    # Default title
    if args.title:
        title = args.title
    else:
        cov_type = data.get('cov_type', 'unknown')
        arch = data.get('arch', 'unknown')
        xlen = data.get('xlen', 'unknown')
        title = f"Coverage Over Time ({cov_type.capitalize()} Coverage, {arch}/x{xlen})"
    
    # Default output directory: results/plots/
    fuzzgpu_dir = script_dir.parent
    plots_dir = fuzzgpu_dir / 'results' / 'plots'
    plots_dir.mkdir(parents=True, exist_ok=True)
    
    # Determine output path
    if args.output:
        output_path = Path(args.output)
    else:
        # Default output filename based on input JSON
        cov_type = data.get('cov_type', 'unknown')
        arch = data.get('arch', 'unknown')
        xlen = data.get('xlen', 'unknown')
        output_path = plots_dir / f"ablation_coverage_{arch}_x{xlen}_{cov_type}"
    
    # Plot coverage over time
    plot_coverage_over_time(
        coverage_data,
        title=title,
        output_path=output_path,
        figsize=figsize,
        show_markers=not args.no_markers,
        marker_interval=args.marker_interval,
        grid=not args.no_grid,
        legend_loc=args.legend_loc,
        xlim=xlim,
        ylim=ylim,
    )
    
    # Plot bar chart if requested
    if args.output_bar:
        bar_output_path = Path(args.output_bar)
    else:
        # Default bar chart output
        cov_type = data.get('cov_type', 'unknown')
        arch = data.get('arch', 'unknown')
        xlen = data.get('xlen', 'unknown')
        bar_output_path = plots_dir / f"ablation_coverage_bar_{arch}_x{xlen}_{cov_type}"
    
    plot_coverage_comparison_bar(data, output_path=bar_output_path)
    
    print("Done!")


if __name__ == '__main__':
    main()
