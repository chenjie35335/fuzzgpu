#!/usr/bin/env python3
"""Plot boxplots of bug first detection time across FuzzGPU runs.

Reads Phase 2 results from fuzzing_loop.py and creates boxplots showing
the distribution of first_detection_time for each known bug.

Bugs are grouped by (bug_name, arch, xlen).
Repeated FuzzGPU runs show the distribution of discovery times.
"""

import json
import sys
from pathlib import Path
from collections import defaultdict
from typing import Dict, List, Tuple, Any

import matplotlib.patches as mpatches
import matplotlib.colors as mcolors
import numpy as np

# Import common plot settings
from common_plot import *

# Number of parallel processes used in fuzzing experiments.
# 
# NOTE: fuzzing_loop.py hardcodes NUM_CORES=1 when computing core_hours in phase2_results.json,
# which means the stored first_detection_time is in wall-clock seconds (not core-seconds).
# To get the correct core-seconds, we multiply by the actual number of parallel workers (24)
# used during the fuzzing experiments:
#   core_seconds = wall_clock_seconds * NUM_CORES
NUM_CORES = 24


def darken_color(color, factor=0.6):
    """Darken a color by multiplying RGB values by factor."""
    rgb = mcolors.to_rgb(color)
    return tuple(c * factor for c in rgb)

# Directories
SCRIPT_DIR = Path(__file__).resolve().parent
FUZZGPU_DIR = SCRIPT_DIR.parent
RESULTS_DIR = FUZZGPU_DIR / 'results' / 'fuzzing_results'
OUTPUT_DIR = FUZZGPU_DIR / 'results' / 'plots'

# Method colors and hatches
METHOD_STYLES = {
    'fuzzgpu': {'color': '#ff7f0e', 'hatch': '///'},     # Orange, diagonal lines
}

# Method display name mapping
METHOD_DISPLAY_NAMES = {
    'fuzzgpu': 'FuzzGPU',
}

# Method order for legend (fuzzgpu first)
METHOD_ORDER = ['fuzzgpu']

# Fallback styles for unknown methods
DEFAULT_COLORS = ['#d62728', '#9467bd', '#8c564b', '#e377c2', '#7f7f7f']
DEFAULT_HATCHES = ['..', '++', 'oo', '**', 'OO']

# Bug name mapping table
BUG_NAME_MAPPING = {
    'amo_dec': 'V2',
    'csr_misa': 'V6',
    'var_thread_num': 'V8',
    'vec_nfield': 'V3',
    'ventus_hw_mulhsu': 'V10',
    'ventus_hw_vfmax': 'V9',
    'ventus_hw_vmulhsu_vv': 'V10',
    'ventus_hw_vmulhsu_vx': 'V10',
    'vftta_vv_wb': 'V11',
    'vmask': 'V14',
    'vmsgt_vx_vmsgtu_vx': 'V5',
    'vmv_x_s': 'V13',
    'czero': 'X2',
    'sra': 'S1',
    'vx_wmma': 'X3',
    'wb_fp_rd_zero': 'X1',
}


def load_phase2_results(results_dir: Path) -> List[Dict[str, Any]]:
    """Load all phase2_results.json files from the results directory.
    
    Returns:
        List of dicts, each containing the phase2 results plus the source path.
    """
    all_results = []
    
    if not results_dir.exists():
        print(f"Warning: Results directory not found: {results_dir}")
        return all_results
    
    for subdir in results_dir.iterdir():
        if not subdir.is_dir():
            continue
        
        phase2_file = subdir / 'phase2_results.json'
        if not phase2_file.exists():
            continue
        
        try:
            with open(phase2_file, 'r') as f:
                data = json.load(f)
                if data.get('method') != 'fuzzgpu':
                    continue
                data['_source_path'] = str(phase2_file)
                all_results.append(data)
        except Exception as e:
            print(f"Warning: Failed to load {phase2_file}: {e}")
    
    return all_results


def extract_bug_discovery_times(results: List[Dict]) -> Dict[str, Dict[str, List[float]]]:
    """Extract first detection times for each bug, grouped by method.
    
    Returns:
        Dict mapping bug_name -> {method: [detection_times]}
    """
    # bug_key -> {method -> [times]}
    bug_times: Dict[str, Dict[str, List[float]]] = defaultdict(lambda: defaultdict(list))
    
    # Track unmapped bug names to avoid duplicate warnings
    unmapped_bugs = set()
    
    for result in results:
        method = result.get('method', 'unknown')
        arch = result.get('arch', 'unknown')
        xlen = result.get('xlen', 32)
        first_gen_start_time = result.get('first_gen_start_time', 0)
        
        known_bugs = result.get('known_bugs', {})
        
        for bug_name, bug_record in known_bugs.items():
            # Map bug_name using the mapping table
            if bug_name not in BUG_NAME_MAPPING:
                if bug_name not in unmapped_bugs:
                    print(f"\033[93mWarning: Bug name '{bug_name}' not found in mapping table, ignoring.\033[0m")
                    unmapped_bugs.add(bug_name)
                continue
            
            mapped_name = BUG_NAME_MAPPING[bug_name]
            
            # Calculate relative detection time from start and convert to core-seconds
            first_detection_time = bug_record.get('first_detection_time', 0)
            relative_time = max(0, first_detection_time)
            # Convert wall-clock seconds to core-seconds: time_seconds * NUM_CORES
            core_seconds = relative_time * NUM_CORES
            # Clamp to minimum value of 1 to avoid log(0) issues in log-scale plots
            core_seconds = max(1.0, core_seconds)
            
            bug_key = mapped_name
            bug_times[bug_key][method].append(core_seconds)
    
    return bug_times


def create_boxplot(bug_times: Dict[str, Dict[str, List[float]]], 
                   output_prefix: Path = None,
                   show: bool = True,
                   methods_filter: List[str] = None):
    """Create boxplot visualization of bug discovery times.
    
    Args:
        bug_times: Dict mapping (bug_name, arch, xlen) -> {method: [detection_times]}
        output_prefix: Path prefix for output files (will save .pdf and .png)
        show: Whether to display the plot
        methods_filter: List of methods to include (None = all methods)
    """
    if not bug_times:
        print("No bug discovery data to plot.")
        return
    
    # Collect all methods
    all_methods = set()
    for method_times in bug_times.values():
        all_methods.update(method_times.keys())
    
    # Filter methods if specified
    if methods_filter:
        all_methods = all_methods & set(methods_filter)
        if not all_methods:
            print(f"Warning: None of the specified methods {methods_filter} found in data.")
            return
    
    # Sort methods: known methods first in order, then unknown methods alphabetically
    def method_sort_key(m):
        if m in METHOD_ORDER:
            return (0, METHOD_ORDER.index(m))
        return (1, m)
    all_methods = sorted(all_methods, key=method_sort_key)
    
    # Assign styles to methods
    method_styles = {}
    unknown_idx = 0
    for method in all_methods:
        if method in METHOD_STYLES:
            method_styles[method] = METHOD_STYLES[method]
        else:
            method_styles[method] = {
                'color': DEFAULT_COLORS[unknown_idx % len(DEFAULT_COLORS)],
                'hatch': DEFAULT_HATCHES[unknown_idx % len(DEFAULT_HATCHES)]
            }
            unknown_idx += 1
    
    # Sort bugs by prefix letter then by numeric value (e.g., V2, V3, V10, X1, X2)
    def bug_sort_key(bug_name):
        import re
        match = re.match(r'([A-Za-z]+)(\d+)', bug_name)
        if match:
            prefix, num = match.groups()
            return (prefix, int(num))
        return (bug_name, 0)
    
    sorted_bugs = sorted(bug_times.keys(), key=bug_sort_key)
    
    # Create figure with standard sizing
    num_bugs = len(sorted_bugs)
    num_methods = len(all_methods)
    
    # Use textwidth_in as base, adjust height based on content
    fig_width = textwidth_in * 2
    fig_height = fig_width * 0.18  # Reasonable aspect ratio for boxplots
    fig, ax = plt.subplots(figsize=(fig_width, fig_height))
    
    # Calculate positions
    group_width = 0.8
    box_width = group_width / num_methods
    positions = []
    labels = []
    
    for bug_idx, bug_key in enumerate(sorted_bugs):
        bug_name = bug_key
        label = bug_name
        labels.append(label)
        
        method_times = bug_times[bug_key]
        
        for method_idx, method in enumerate(all_methods):
            times = method_times.get(method, [])
            if not times:
                continue
            
            pos = bug_idx + (method_idx - num_methods / 2 + 0.5) * box_width
            positions.append(pos)
            
            # Create boxplot
            bp = ax.boxplot(
                [times],
                positions=[pos],
                widths=box_width * 0.8,
                vert=True, showfliers=False,
                patch_artist=True,
                manage_ticks=False
            )
            
            # Apply style (only fill when multiple methods)
            style = method_styles[method]
            edge_color = darken_color(style['color'], 0.6)
            for patch in bp['boxes']:
                if num_methods > 1:
                    patch.set_facecolor(style['color'])
                    patch.set_hatch(style['hatch'])
                    patch.set_alpha(0.7)
                else:
                    patch.set_facecolor('none')
                patch.set_edgecolor(edge_color)
                patch.set_linewidth(0.8)
            for element in ['whiskers', 'caps', 'medians']:
                for line in bp[element]:
                    line.set_color(edge_color)
                    line.set_linewidth(0.8)
            for flier in bp['fliers']:
                flier.set(marker='o', markerfacecolor=style['color'], 
                        markeredgecolor=edge_color, markersize=4, alpha=0.5,
                        markeredgewidth=0.5)
    
    # Add vertical dashed lines to separate bugs
    for i in range(num_bugs - 1):
        ax.axvline(x=i + 0.5, color='gray', linestyle='--', linewidth=0.5, alpha=0.5)
        
    # Set x-axis
    ax.set_xticks(range(num_bugs))
    ax.set_xticklabels(labels, rotation=0, ha='right')
    
    # Labels and title
    #ax.set_xlabel('Bug')
    ax.set_ylabel('Time (seconds)')
    
    # Create legend
    legend_patches = []
    for method in all_methods:
        style = method_styles[method]
        display_name = METHOD_DISPLAY_NAMES.get(method, method)
        patch = mpatches.Patch(
            facecolor=style['color'],
            hatch=style['hatch'],
            alpha=0.7,
            edgecolor='black',
            label=display_name
        )
        legend_patches.append(patch)
    
    ax.legend(handles=legend_patches, loc='upper right', fontsize='small',
               handleheight=0.6, labelspacing=0.12, borderpad=0.1)

    # Set log scale for y-axis
    ax.set_yscale('log')
    
    # Grid
    ax.yaxis.grid(True, linestyle='--', alpha=0.7)
    ax.set_axisbelow(True)
    
    # Adjust layout
    plt.tight_layout()
    
    # Save to both PDF and PNG
    if output_prefix:
        output_prefix.parent.mkdir(parents=True, exist_ok=True)
        
        pdf_path = output_prefix.with_suffix('.pdf')
        png_path = output_prefix.with_suffix('.png')
        
        plt.savefig(pdf_path, format='pdf', dpi=300, bbox_inches='tight')
        plt.savefig(png_path, format='png', dpi=300, bbox_inches='tight')
        
        print(f"Figure saved to: {pdf_path}")
        print(f"Figure saved to: {png_path}")
    
    if show:
        plt.show()
    else:
        plt.close()


def print_summary(bug_times: Dict[str, Dict[str, List[float]]]):
    """Print summary statistics of bug discovery times."""
    print("\n" + "=" * 80)
    print("Bug Discovery Time Summary")
    print("=" * 80)
    
    sorted_bugs = sorted(bug_times.keys())
    
    for bug_key in sorted_bugs:
        bug_name = bug_key
        print(f"\n{bug_name}:")
        
        method_times = bug_times[bug_key]
        for method in sorted(method_times.keys()):
            times = method_times[method]  # times are now in core-seconds
            if times:
                mean_time = np.mean(times)
                std_time = np.std(times) if len(times) > 1 else 0
                min_time = min(times)
                max_time = max(times)
                print(f"  {method:12s}: n={len(times):3d}, "
                      f"mean={mean_time:8.2f}s, std={std_time:7.2f}s, "
                      f"min={min_time:7.2f}s, max={max_time:7.2f}s")


def export_table(bug_times: Dict[str, Dict[str, List[float]]], 
                 output_prefix: Path = None,
                 format: str = 'csv'):
    """Export bug discovery times as a table.
    
    Args:
        bug_times: Dict mapping bug_name -> {method: [detection_times]}
        output_prefix: Path prefix for output file
        format: Output format ('csv', 'latex', 'markdown')
    """
    if not bug_times:
        print("No bug discovery data to export.")
        return
    
    # Collect all methods
    all_methods = set()
    for method_times in bug_times.values():
        all_methods.update(method_times.keys())
    
    # Sort methods: known methods first in order, then unknown methods alphabetically
    def method_sort_key(m):
        if m in METHOD_ORDER:
            return (0, METHOD_ORDER.index(m))
        return (1, m)
    all_methods = sorted(all_methods, key=method_sort_key)
    
    # Sort bugs
    sorted_bugs = sorted(bug_times.keys())
    
    # Build table data
    headers = ['Bug'] + [f'{METHOD_DISPLAY_NAMES.get(m, m)}_mean' for m in all_methods] + \
              [f'{METHOD_DISPLAY_NAMES.get(m, m)}_std' for m in all_methods] + \
              [f'{METHOD_DISPLAY_NAMES.get(m, m)}_n' for m in all_methods]
    
    rows = []
    for bug_key in sorted_bugs:
        row = [bug_key]
        method_times = bug_times[bug_key]
        
        # Mean values
        for method in all_methods:
            times = method_times.get(method, [])
            if times:
                row.append(f"{np.mean(times):.2f}")
            else:
                row.append("-")
        
        # Std values
        for method in all_methods:
            times = method_times.get(method, [])
            if times and len(times) > 1:
                row.append(f"{np.std(times):.2f}")
            elif times:
                row.append("0.00")
            else:
                row.append("-")
        
        # Count values
        for method in all_methods:
            times = method_times.get(method, [])
            row.append(str(len(times)) if times else "-")
        
        rows.append(row)
    
    # Output based on format
    if format == 'csv':
        import csv
        output_path = output_prefix.with_suffix('.csv') if output_prefix else None
        
        if output_path:
            with open(output_path, 'w', newline='') as f:
                writer = csv.writer(f)
                writer.writerow(headers)
                writer.writerows(rows)
            print(f"Table saved to: {output_path}")
        else:
            # Print to console
            print(','.join(headers))
            for row in rows:
                print(','.join(row))
    
    elif format == 'latex':
        output_path = output_prefix.with_suffix('.tex') if output_prefix else None
        
        # Create LaTeX table
        num_cols = len(headers)
        col_spec = 'l' + 'r' * (num_cols - 1)
        
        latex_lines = [
            r'\begin{table}[htbp]',
            r'\centering',
            r'\caption{Bug Discovery Time (core-seconds)}',
            r'\label{tab:bug_discover_time}',
            f'\\begin{{tabular}}{{{col_spec}}}',
            r'\toprule',
        ]
        
        # Simplified headers for LaTeX (just mean values)
        simple_headers = ['Bug'] + [METHOD_DISPLAY_NAMES.get(m, m) for m in all_methods]
        latex_lines.append(' & '.join(simple_headers) + r' \\')
        latex_lines.append(r'\midrule')
        
        # Rows with mean ± std format
        for bug_key in sorted_bugs:
            method_times = bug_times[bug_key]
            row_parts = [bug_key]
            
            for method in all_methods:
                times = method_times.get(method, [])
                if times:
                    mean_val = np.mean(times)
                    std_val = np.std(times) if len(times) > 1 else 0
                    row_parts.append(f"${mean_val:.1f} \\pm {std_val:.1f}$")
                else:
                    row_parts.append("-")
            
            latex_lines.append(' & '.join(row_parts) + r' \\')
        
        latex_lines.extend([
            r'\bottomrule',
            r'\end{tabular}',
            r'\end{table}',
        ])
        
        latex_content = '\n'.join(latex_lines)
        
        if output_path:
            with open(output_path, 'w') as f:
                f.write(latex_content)
            print(f"LaTeX table saved to: {output_path}")
        else:
            print(latex_content)
    
    elif format == 'markdown':
        output_path = output_prefix.with_suffix('.md') if output_prefix else None
        
        # Simplified headers (just mean values)
        simple_headers = ['Bug'] + [METHOD_DISPLAY_NAMES.get(m, m) for m in all_methods]
        
        md_lines = [
            '# Bug Discovery Time (core-seconds)',
            '',
            '| ' + ' | '.join(simple_headers) + ' |',
            '| ' + ' | '.join([':---'] + [':---:'] * len(all_methods)) + ' |',
        ]
        
        # Rows with mean ± std format
        for bug_key in sorted_bugs:
            method_times = bug_times[bug_key]
            row_parts = [bug_key]
            
            for method in all_methods:
                times = method_times.get(method, [])
                if times:
                    mean_val = np.mean(times)
                    std_val = np.std(times) if len(times) > 1 else 0
                    row_parts.append(f"{mean_val:.1f} ± {std_val:.1f}")
                else:
                    row_parts.append("-")
            
            md_lines.append('| ' + ' | '.join(row_parts) + ' |')
        
        md_content = '\n'.join(md_lines)
        
        if output_path:
            with open(output_path, 'w') as f:
                f.write(md_content)
            print(f"Markdown table saved to: {output_path}")
        else:
            print(md_content)
    
    else:
        print(f"Unknown format: {format}. Supported formats: csv, latex, markdown")


def main():
    import argparse
    
    parser = argparse.ArgumentParser(
        description='Plot boxplots of bug first detection time across FuzzGPU runs'
    )
    parser.add_argument(
        '--results-dir', type=str, default=str(RESULTS_DIR),
        help=f'Path to fuzzing results directory (default: {RESULTS_DIR})'
    )
    parser.add_argument(
        '--output', '-o', type=str, default=None,
        help='Output filename prefix (default: results/plots/bug_discover_time)'
    )
    parser.add_argument(
        '--no-show', action='store_true',
        help='Do not display the plot (useful for headless environments)'
    )
    parser.add_argument(
        '--summary', action='store_true',
        help='Print summary statistics to console'
    )
    parser.add_argument(
        '--methods', '-m', type=str, nargs='+', choices=['fuzzgpu'], default=None,
        help='Only show specified methods (e.g., --methods fuzzgpu)'
    )
    parser.add_argument(
        '--table', '-t', type=str, choices=['csv', 'latex', 'markdown'], default=None,
        help='Export data as table in specified format (csv, latex, markdown)'
    )
    
    args = parser.parse_args()
    
    results_dir = Path(args.results_dir)
    print(f"Loading results from: {results_dir}")
    
    # Load all phase2 results
    results = load_phase2_results(results_dir)
    print(f"Loaded {len(results)} result files")
    
    if not results:
        print("No phase2_results.json files found.")
        sys.exit(1)
    
    # Extract bug discovery times
    bug_times = extract_bug_discovery_times(results)
    print(f"Found {len(bug_times)} unique bugs across all results")
    
    if args.summary:
        print_summary(bug_times)
    
    # Determine output prefix
    # Default: results/plots/bug_discover_time
    if args.output:
        output_prefix = Path(args.output)
        # Remove suffix if provided, we'll add .pdf and .png
        if output_prefix.suffix in ['.pdf', '.png', '.csv', '.tex', '.md']:
            output_prefix = output_prefix.with_suffix('')
    else:
        output_prefix = OUTPUT_DIR / 'bug_discover_time'
    
    # Ensure output directory exists
    output_prefix.parent.mkdir(parents=True, exist_ok=True)
    
    # Export table if requested
    if args.table:
        export_table(bug_times, output_prefix, format=args.table)
    
    # Plot
    show = not args.no_show
    create_boxplot(bug_times, output_prefix, show=show, methods_filter=args.methods)


if __name__ == '__main__':
    main()
