#!/usr/bin/env python3
"""
Plot time breakdown for fuzzgpu from fuzzing_loop.py output JSON.
Creates horizontal stacked bar charts showing time breakdown by architecture.
"""

import argparse
import json
from pathlib import Path
from collections import defaultdict

import matplotlib.pyplot as plt
import matplotlib.patches as mpatches
import numpy as np

from common_plot import *

# Time breakdown categories and their display labels
# Note: fuzzing_loop.py stores these in milliseconds with _ms suffix
TIME_BREAKDOWN_FIELDS = {
    'program_construction_time_ms': 'Prog. Construction',
    'file_write_time_ms': 'File Write',
    'memory_check_time_ms': 'Memory Check',
    'rtl_emu_time_ms': 'RTL Emulation',
}

# Color scheme for time breakdown categories
TIME_BREAKDOWN_COLORS = {
    'program_construction_time_ms':"#56b356", # "#2f85c2",  # blue
    'file_write_time_ms': "#f14f46",            # light blue
    'memory_check_time_ms':"#ff983e", # "#d49c00",          # yellow
    'rtl_emu_time_ms':  "#4c92c3", #  "#468b04",               # red
}

# Hatch patterns for B&W visibility
TIME_BREAKDOWN_HATCHES = {
    'program_construction_time_ms': '///',
    'file_write_time_ms': '---',
    'memory_check_time_ms': 'xxx',
    'rtl_emu_time_ms': '\\\\\\',
}


def darken_color(hex_color, factor=0.6):
    """Darken a hex color by a factor (0-1, lower = darker)."""
    hex_color = hex_color.lstrip('#')
    r, g, b = int(hex_color[0:2], 16), int(hex_color[2:4], 16), int(hex_color[4:6], 16)
    r, g, b = int(r * factor), int(g * factor), int(b * factor)
    return f'#{r:02x}{g:02x}{b:02x}'

# Unified figure size configuration
FIG_WIDTH_IN = textwidth_in
FIG_HEIGHT_IN = FIG_WIDTH_IN * 0.45

# Label display thresholds (set to 0 to show all labels)
PCT_LABEL_MIN = 0.0
TIME_LABEL_MIN_S = 0.0


def load_results(json_path):
    """Load results from JSON file."""
    with open(json_path, 'r') as f:
        return json.load(f)


def find_fuzzgpu_result_dirs(results_dir):
    """Find all fuzzgpu result directories in the fuzzing_results directory."""
    result_dirs = []
    if not results_dir.exists():
        return result_dirs
    for d in results_dir.iterdir():
        if d.is_dir() and d.name.startswith('fuzzgpu_'):
            phase1_file = d / 'phase1_results.json'
            if phase1_file.exists():
                result_dirs.append(d)
    return result_dirs


def extract_fuzzgpu_time_breakdown(results_dir):
    """
    Extract time breakdown data for fuzzgpu by architecture from fuzzing_loop.py output.
    Returns: {arch_key: {time_field: [values]}}
    
    Scans all fuzzgpu_* directories in results_dir for phase1_results.json files.
    """
    data_by_arch = defaultdict(lambda: defaultdict(list))
    # Also collect per-test percentages for relative time plot
    pct_by_arch = defaultdict(lambda: defaultdict(list))
    
    result_dirs = find_fuzzgpu_result_dirs(results_dir)
    
    for result_dir in result_dirs:
        phase1_file = result_dir / 'phase1_results.json'
        try:
            with open(phase1_file, 'r') as f:
                phase1_data = json.load(f)
        except Exception as e:
            print(f"Warning: Failed to load {phase1_file}: {e}")
            continue
        
        # Only process fuzzgpu results
        if phase1_data.get('method', '') != 'fuzzgpu':
            continue
        
        arch = phase1_data.get('arch', '')
        xlen = phase1_data.get('xlen', 32)
        if not arch:
            continue
        
        arch_key = f"{arch}_xlen{xlen}"
        
        # Process each test record
        for test in phase1_data.get('tests', []):
            # Only include successful runs
            if not test.get('success', False):
                continue
            
            # Calculate total time for this test (for per-test percentage)
            test_total = 0
            test_times = {}
            for field in TIME_BREAKDOWN_FIELDS.keys():
                if field in test and test[field] is not None:
                    value_seconds = test[field] / 1000.0
                    test_times[field] = value_seconds
                    test_total += value_seconds
                else:
                    test_times[field] = 0
            
            # Extract timing fields and compute per-test percentages
            for field in TIME_BREAKDOWN_FIELDS.keys():
                value_seconds = test_times.get(field, 0)
                data_by_arch[arch_key][field].append(value_seconds)
                
                # Compute percentage for this test
                if test_total > 0:
                    pct = (value_seconds / test_total) * 100
                else:
                    pct = 0
                pct_by_arch[arch_key][field].append(pct)
    
    return data_by_arch, pct_by_arch


def compute_average_breakdown(data_by_arch):
    """
    Compute arithmetic mean for each breakdown category per architecture.
    Returns: {arch_key: {time_field: mean}}
    """
    avg_by_arch = {}
    
    for arch_key, field_data in data_by_arch.items():
        avg_by_arch[arch_key] = {}
        for field, values in field_data.items():
            if values:
                # Filter out abnormal values (due to crashes)
                positive_values = [v for v in values if v > 0]
                if positive_values:
                    avg_by_arch[arch_key][field] = np.mean(values)
                else:
                    avg_by_arch[arch_key][field] = 0.0
            else:
                avg_by_arch[arch_key][field] = 0.0
    
    return avg_by_arch


def plot_time_breakdown(avg_pct_by_arch, output_dir, show=False, show_emu=False):
    """
    Create horizontal stacked bar chart showing time breakdown by architecture.
    Uses pre-computed average percentages (computed per-test, then averaged).
    
    Args:
        avg_pct_by_arch: Average percentages by architecture
        output_dir: Directory to save plots
        show: Whether to display plots interactively
        show_emu: Whether to include RTL emulation time (default: False)
    """
    if not avg_pct_by_arch:
        print("No data to plot.")
        return
    
    # Academic style settings
    try:
        plt.style.use('seaborn-v0_8-whitegrid')
    except:
        plt.style.use('classic')
        plt.grid(True, linestyle='--', alpha=0.3)
    
    # Set hatch color to gray
    plt.rcParams['hatch.color'] = 'gray'
    
    # Sort architectures for consistent ordering
    arch_keys = sorted(avg_pct_by_arch.keys())
    n_archs = len(arch_keys)
    
    # Create figure
    fig, ax = plt.subplots(figsize=(FIG_WIDTH_IN, FIG_HEIGHT_IN))
    
    # Prepare data
    y = np.arange(n_archs)
    height = 0.5
    
    # Create labels for y-axis
    y_labels = []
    for arch_key in arch_keys:
        parts = arch_key.split('_xlen')
        arch_name = parts[0].capitalize()
        if arch_name.lower() == 'ventus':
            arch_name = 'Open\nGPGPU'
        y_labels.append(arch_name)
    
    # Create stacked horizontal bars
    lefts = np.zeros(n_archs)
    legend_handles = []
    
    # Collect all bar info for annotation later
    all_bars_info = []  # [(arch_idx, field, pct, bar_left, bar_right, bar_center_y, color)]
    
    # Fields to exclude from display (configurable via show_emu parameter)
    exclude_from_display = set() if show_emu else {'rtl_emu_time_ms'}
    
    for field in TIME_BREAKDOWN_FIELDS.keys():
        # Skip excluded fields in display
        if field in exclude_from_display:
            continue
        
        percentages = []
        for arch_key in arch_keys:
            pct = avg_pct_by_arch[arch_key].get(field, 0)
            percentages.append(pct)
        
        color = TIME_BREAKDOWN_COLORS.get(field, '#333333')
        hatch = TIME_BREAKDOWN_HATCHES.get(field, '')
        label = TIME_BREAKDOWN_FIELDS.get(field, field)
        edge_color = darken_color(color, 0.6)
        
        # Draw bars with darkened hatch color
        bars = ax.barh(y, percentages, height, left=lefts, label=label,
                   color=color, hatch=hatch, edgecolor=edge_color, linewidth=0.5)
        
        # Draw black border on top (without fill and hatch)
        ax.barh(y, percentages, height, left=lefts,
                   color='none', edgecolor='black', linewidth=0.5)
        
        # Collect bar info for annotation
        for i, (bar, pct) in enumerate(zip(bars, percentages)):
            if pct > PCT_LABEL_MIN:
                bar_width = bar.get_width()
                bar_left = lefts[i]
                bar_center_x = bar_left + bar_width / 2.0
                bar_center_y = bar.get_y() + bar.get_height() / 2.0
                bar_right = bar_left + bar_width
                
                all_bars_info.append({
                    'arch_idx': i,
                    'field': field,
                    'pct': pct,
                    'bar_left': bar_left,
                    'bar_right': bar_right,
                    'bar_center_x': bar_center_x,
                    'bar_center_y': bar_center_y,
                    'color': color
                })

        lefts += percentages
        legend_handles.append(mpatches.Patch(color=color, hatch=hatch, 
                                             edgecolor=edge_color, label=label))
    
    # Now add labels, handling small bars specially to avoid overlap
    # Group small labels by architecture to stagger them
    small_labels_by_arch = defaultdict(list)
    for info in all_bars_info:
        if info['pct'] < 5 and info['pct'] >= 0.05:
            small_labels_by_arch[info['arch_idx']].append(info)
    
    for info in all_bars_info:
        pct = info['pct']
        bar_center_x = info['bar_center_x']
        bar_center_y = info['bar_center_y']
        bar_right = info['bar_right']
        arch_idx = info['arch_idx']
        bar_top = bar_center_y + height / 2.0
        
        # Threshold: if bar width < 5%, place label above
        if pct < 5:
            # Skip labels that are essentially zero
            if pct < 0.05:
                continue
            
            # Find index in small labels for this arch to stagger horizontally
            small_list = small_labels_by_arch[arch_idx]
            idx_in_small = next((i for i, x in enumerate(small_list) if x is info), 0)
            n_small = len(small_list)
            
            # Stagger labels: offset x position based on index
            # Spread labels evenly, alternating left/right from center
            x_offset = (idx_in_small - (n_small - 1) / 2.0) * 12.0
            label_x = bar_center_x + x_offset
            label_y = bar_top + 0.08
            
            # Use 1 decimal for very small values (< 1%), integer otherwise
            if pct < 1:
                label_text = f'{pct:.1f}%'
            else:
                label_text = f'{pct:.0f}%'
            
            ax.annotate(label_text,
                        xy=(bar_center_x, bar_top),
                        xytext=(label_x, label_y),
                        fontsize=FONTSIZE-1, fontweight='bold', color='black',
                        ha='center', va='bottom',
                        arrowprops=dict(arrowstyle='-', color='gray', lw=1.1, shrinkA=0, shrinkB=0))
        else:
            # Place label inside the bar
            ax.text(bar_center_x, bar_center_y,
                    f'{pct:.0f}%',
                    ha='center', va='center', fontsize=FONTSIZE-1,
                    fontweight='bold', color='white')
    
    # Customize plot
    ax.set_xlabel('Percentage of Total Time (%)')
    ax.set_yticks(y)
    ax.set_yticklabels(y_labels, fontsize=FONTSIZE-1, ha='right')
    ax.set_xlim(0, 100)
    # Extend y-axis top to make room for labels above bars
    ax.set_ylim(-0.5, n_archs - 0.3)
    
    # Place legend at top with padding from plot edge
    ax.legend(handles=legend_handles, loc='lower center', bbox_to_anchor=(0.5, 1.05),
              frameon=True, ncol=3, labelspacing=0.4, handlelength=1.2, 
              handleheight=0.8, columnspacing=0.8, borderaxespad=0.0)
    
    ax.grid(True, axis='x', linestyle='--', alpha=0.3)
    
    plt.tight_layout()
    
    # Save figure
    output_dir.mkdir(parents=True, exist_ok=True)
    filename_base = 'time_breakdown_fuzzgpu'
    output_path_png = output_dir / f"{filename_base}.png"
    output_path_pdf = output_dir / f"{filename_base}.pdf"
    plt.savefig(output_path_png, dpi=300, bbox_inches='tight')
    plt.savefig(output_path_pdf, format='pdf', dpi=300, bbox_inches='tight')
    print(f"Saved: {output_path_png} and {output_path_pdf}")
    
    if show:
        plt.show()
    else:
        plt.close(fig)


def plot_time_breakdown_absolute(avg_by_arch, output_dir, show=False):
    """
    Create horizontal stacked bar chart showing absolute time breakdown by architecture.
    """
    if not avg_by_arch:
        print("No data to plot.")
        return
    
    # Academic style settings
    try:
        plt.style.use('seaborn-v0_8-whitegrid')
    except:
        plt.style.use('classic')
        plt.grid(True, linestyle='--', alpha=0.3)
    
    # Set hatch color to gray
    plt.rcParams['hatch.color'] = 'gray'
    
    # Sort architectures for consistent ordering
    arch_keys = sorted(avg_by_arch.keys())
    n_archs = len(arch_keys)
    
    # Create figure
    fig, ax = plt.subplots(figsize=(FIG_WIDTH_IN, FIG_HEIGHT_IN))
    
    # Prepare data
    y = np.arange(n_archs)
    height = 0.5
    
    # Create labels for y-axis
    y_labels = []
    for arch_key in arch_keys:
        parts = arch_key.split('_xlen')
        arch_name = parts[0].capitalize()
        if arch_name.lower() == 'ventus':
            arch_name = 'OpenGPGPU'
        y_labels.append(arch_name)
    
    # Create stacked horizontal bars
    lefts = np.zeros(n_archs)
    legend_handles = []
    
    for field in TIME_BREAKDOWN_FIELDS.keys():
        values = []
        for arch_key in arch_keys:
            val = avg_by_arch[arch_key].get(field, 0)
            values.append(val)
        
        color = TIME_BREAKDOWN_COLORS.get(field, '#333333')
        hatch = TIME_BREAKDOWN_HATCHES.get(field, '')
        label = TIME_BREAKDOWN_FIELDS.get(field, field)
        edge_color = darken_color(color, 0.6)
        
        # Draw bars with darkened hatch color
        bars = ax.barh(y, values, height, left=lefts, label=label,
                   color=color, hatch=hatch, edgecolor=edge_color, linewidth=0.5)
        
        # Draw black border on top (without fill and hatch)
        ax.barh(y, values, height, left=lefts,
                   color='none', edgecolor='black', linewidth=0.5)
        
        # Add time labels on bars if significant enough
        for i, (bar, val) in enumerate(zip(bars, values)):
            if val > TIME_LABEL_MIN_S:
                bar_width = bar.get_width()
                ax.text(lefts[i] + bar_width / 2.0, bar.get_y() + bar.get_height() / 2.0,
                        f'{val:.1f}s',
                        ha='center', va='center', fontsize=FONTSIZE-1,
                        fontweight='bold', color='white')

        lefts += values
        legend_handles.append(mpatches.Patch(color=color, hatch=hatch, 
                                             edgecolor=edge_color, label=label))
    
    # Customize plot
    ax.set_xlabel('Arithmetic Mean Time (seconds)')
    ax.set_yticks(y)
    ax.set_yticklabels(y_labels, fontsize=FONTSIZE, rotation=45, ha='right')
    
    # Place legend at top with horizontal layout
    ax.legend(handles=legend_handles, loc='lower center', bbox_to_anchor=(0.5, 1.02),
              frameon=True, ncol=4, labelspacing=0.4, handlelength=1.2, 
              handleheight=0.8, columnspacing=0.8, borderaxespad=0.0)
    
    ax.grid(True, axis='x', linestyle='--', alpha=0.3)
    
    plt.tight_layout()
    
    # Save figure
    output_dir.mkdir(parents=True, exist_ok=True)
    filename_base = 'time_breakdown_fuzzgpu_absolute'
    output_path_png = output_dir / f"{filename_base}.png"
    output_path_pdf = output_dir / f"{filename_base}.pdf"
    plt.savefig(output_path_png, dpi=300, bbox_inches='tight')
    plt.savefig(output_path_pdf, format='pdf', dpi=300, bbox_inches='tight')
    print(f"Saved: {output_path_png} and {output_path_pdf}")
    
    if show:
        plt.show()
    else:
        plt.close(fig)


def main():
    script_dir = Path(__file__).resolve().parent
    fuzzgpu_dir = script_dir.parent
    default_results_dir = fuzzgpu_dir / 'results' / 'fuzzing_results'
    
    parser = argparse.ArgumentParser(
        description='Plot time breakdown for fuzzgpu from fuzzing_loop.py output.'
    )
    parser.add_argument(
        'results_dir',
        type=str,
        nargs='?',
        default=None,
        help='Path to the fuzzing_results directory (default: results/fuzzing_results/)'
    )
    parser.add_argument(
        '--output-dir',
        type=str,
        default=None,
        help='Directory to save plots (default: results/plots/)'
    )
    parser.add_argument(
        '--show',
        action='store_true',
        help='Show plots interactively instead of just saving'
    )
    parser.add_argument(
        '--show-emu',
        action='store_true',
        help='Include RTL emulation time in the breakdown (default: hidden)'
    )
    
    args = parser.parse_args()
    
    # Determine input path
    if args.results_dir:
        results_dir = Path(args.results_dir)
    else:
        results_dir = default_results_dir
    
    if not results_dir.exists():
        print(f"Error: Results directory not found: {results_dir}")
        print("Run fuzzing_loop.py first to generate results.")
        return 1
    
    if args.output_dir:
        output_dir = Path(args.output_dir)
    else:
        output_dir = fuzzgpu_dir / 'results' / 'plots'
    
    print(f"Loading results from: {results_dir}")
    
    print(f"Extracting fuzzgpu time breakdown data...")
    data_by_arch, pct_by_arch = extract_fuzzgpu_time_breakdown(results_dir)
    
    if not data_by_arch:
        print("Error: No fuzzgpu data found in results directory.")
        return 1
    
    print(f"Found architectures: {list(data_by_arch.keys())}")
    
    # Compute average times (for absolute time plot)
    avg_by_arch = compute_average_breakdown(data_by_arch)
    
    # Compute average percentages (for relative time plot)
    avg_pct_by_arch = compute_average_breakdown(pct_by_arch)
    
    # Print summary
    print("\nTime Breakdown Summary (arithmetic mean per test):")
    for arch_key in sorted(avg_by_arch.keys()):
        print(f"\n  {arch_key}:")
        total = 0
        for field, label in TIME_BREAKDOWN_FIELDS.items():
            val = avg_by_arch[arch_key].get(field, 0)
            pct = avg_pct_by_arch[arch_key].get(field, 0)
            total += val
            print(f"    {label}: {val:.4f}s ({pct:.1f}%)")
        print(f"    Total: {total:.4f}s")
    
    print(f"\nGenerating plots...")
    print(f"Output directory: {output_dir}")
    
    # Generate percentage-based plot (using per-test averaged percentages)
    plot_time_breakdown(avg_pct_by_arch, output_dir, show=args.show, show_emu=args.show_emu)
    
    # Generate absolute time plot
    plot_time_breakdown_absolute(avg_by_arch, output_dir, show=args.show)
    
    print(f"\nDone! Plots saved to: {output_dir}")
    return 0


if __name__ == '__main__':
    exit(main())
