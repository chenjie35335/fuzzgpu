#!/usr/bin/env python3
"""
Plot throughput results from fuzz_instr_throughput.py output JSON.
Creates box plots for gen_throughput, emu_throughput, and effective_throughput.
"""

import argparse
import json
import math
from pathlib import Path
from collections import defaultdict

import matplotlib.pyplot as plt
import matplotlib.ticker as ticker
import numpy as np
from common_plot import *

# Color palette for different methods
METHOD_COLORS = {
    'fuzzgpu': '#1f77b4',    # blue
    'divefuzz': '#ff7f0e',   # orange
    'cascade': '#2ca02c',    # green
}

# Color palette for different architectures (for specific fields)
ARCH_COLORS = {
    'ventus': '#ff7f0e',     # orange (for emu_time)
    'vortex': '#1f77b4',     # blue (for emu_time)
}

# Hatching patterns for different architectures
ARCH_HATCHES = {
    'ventus': '//',
    'vortex': '\\\\',
}

# Display names for plot legend
METHOD_DISPLAY_NAMES = {
    'fuzzgpu': 'FuzzGPU',
    'divefuzz': 'Ported DiveFuzz',
    'cascade': 'Ported Cascade',
}

# Hatching patterns for different methods
METHOD_HATCHES = {
    'fuzzgpu': '',
    'divefuzz': '//',
    'cascade': 'xx',
}

# Colors for DFT comparison (DFT vs non-DFT)
DFT_COLORS = {
    'nodft': '#1f77b4',     # blue for non-DFT (faster timing)
    'dft': '#ff7f0e',       # orange for DFT (with difftest)
}

# Display names for DFT comparison
DFT_DISPLAY_NAMES = {
    'nodft': 'Non-DFT (Timing)',
    'dft': 'DFT (Difftest)',
}

# Hatching patterns for DFT comparison
DFT_HATCHES = {
    'nodft': '',
    'dft': '//',
}

# Custom bins for instruction count ranges
# Each tuple is (lower_bound, upper_bound, label)
CUSTOM_BINS = [
    (0, 500, '0-500'),
    (500, 5000, '500-5K'),
    (5000, 50000, '5K-50K'),
    (50000, 500000, '50K-500K'),
    (500000, float('inf'), '500K+'),
]

# Human-readable labels for x_field names
X_FIELD_LABELS = {
    'gen_num_instrs': 'Gen Instr.',
    'emu_num_instrs': 'Emu Instr.',
}

# Use textwidth_in as base, adjust height based on content
fig_width = textwidth_in
fig_height = fig_width * 0.28  # Slightly shorter for paper-style plots
FIGSIZE = (fig_width, fig_height)

# TLE label styling
TLE_FONTSIZE = FONTSIZE


def load_results(json_path):
    """Load results from JSON file."""
    with open(json_path, 'r') as f:
        return json.load(f)


def get_custom_bin(value):
    """Get custom bin index for a value based on CUSTOM_BINS."""
    for i, (lower, upper, label) in enumerate(CUSTOM_BINS):
        if lower <= value < upper:
            return i
    return len(CUSTOM_BINS) - 1  # Last bin if value exceeds all ranges


def get_custom_bin_label(bin_idx):
    """Get label for a custom bin."""
    if 0 <= bin_idx < len(CUSTOM_BINS):
        return CUSTOM_BINS[bin_idx][2]
    return f"bin{bin_idx}"


def get_custom_bin_endpoint_label(bin_idx):
    """Get endpoint label for a custom bin (right interval end)."""
    if 0 <= bin_idx < len(CUSTOM_BINS):
        upper = CUSTOM_BINS[bin_idx][1]
        if upper == float('inf'):
            return '∞'
        # Convert to human-readable format with K suffix for thousands
        if upper >= 1000000:
            return f"{upper // 1000000}M" if upper % 1000000 == 0 else f"{upper / 1000000:.1f}M"
        elif upper >= 1000:
            val = upper // 1000 if upper % 1000 == 0 else upper / 1000
            return f"{int(val)}K" if isinstance(val, int) or val == int(val) else f"{val:.1f}K"
        else:
            return str(upper)
    return f"bin{bin_idx}"


def get_log_bin(value):
    """Get logarithmic bin for a value (order of magnitude)."""
    if value <= 0:
        return 0
    return int(math.floor(math.log10(value)))


def bin_label(bin_idx):
    """Create label for a log bin (order of magnitude)."""
    return f"$10^{bin_idx}$"


def extract_data_by_arch(results):
    """
    Extract data organized by architecture.
    Returns: {arch: {method: {config_key: [details]}}}
    """
    data_by_arch = defaultdict(lambda: defaultdict(lambda: defaultdict(list)))
    
    for result_key, result_data in results.items():
        if 'arch' not in result_data:
            continue
        
        arch = result_data['arch']
        xlen = result_data.get('xlen', 32)
        method = result_data['method']
        arch_key = f"{arch}_xlen{xlen}"
        
        for config_key, config_data in result_data.get('configs', {}).items():
            if 'details' not in config_data:
                continue
            
            details = config_data['details']
            data_by_arch[arch_key][method][config_key].extend(details)
    
    return data_by_arch


def extract_data_by_arch_dft(results):
    """
    Extract data organized by architecture and DFT status.
    Returns: {arch: {dft_mode: {config_key: [details]}}}
    where dft_mode is 'nodft' or 'dft'
    """
    data_by_arch = defaultdict(lambda: defaultdict(lambda: defaultdict(list)))
    
    for result_key, result_data in results.items():
        if 'arch' not in result_data:
            continue
        
        arch = result_data['arch']
        xlen = result_data.get('xlen', 32)
        dft = result_data.get('dft', False)
        dft_mode = 'dft' if dft else 'nodft'
        arch_key = f"{arch}_xlen{xlen}"
        
        for config_key, config_data in result_data.get('configs', {}).items():
            if 'details' not in config_data:
                continue
            
            details = config_data['details']
            data_by_arch[arch_key][dft_mode][config_key].extend(details)
    
    return data_by_arch
    
    return data_by_arch


def extract_data_by_arch_emu_dft_comparison(results):
    """
    Extract data organized by architecture with DFT (dft_time) vs non-DFT (nodft_time) comparison.
    Returns: {arch: {'dft': {config_key: [details]}, 'nodft': {config_key: [details]}}}
    This creates two versions of each detail:
    - 'dft': uses dft_time field as emu_time
    - 'nodft': uses nodft_time field as emu_time
    """
    data_by_arch = defaultdict(lambda: defaultdict(lambda: defaultdict(list)))
    
    for result_key, result_data in results.items():
        if 'arch' not in result_data:
            continue
        
        arch = result_data['arch']
        xlen = result_data.get('xlen', 32)
        method = result_data['method']
        arch_key = f"{arch}_xlen{xlen}"
        
        for config_key, config_data in result_data.get('configs', {}).items():
            if 'details' not in config_data:
                continue
            
            details = config_data['details']
            
            # Create two versions of details for dft vs nodft comparison
            for detail in details:
                if 'dft_time' in detail and 'nodft_time' in detail:
                    # DFT version: use dft_time as emu_time
                    dft_detail = detail.copy()
                    dft_detail['emu_time'] = detail['dft_time']
                    data_by_arch[arch_key]['dft'][config_key].append(dft_detail)
                    
                    # Non-DFT version: use nodft_time as emu_time
                    nodft_detail = detail.copy()
                    nodft_detail['emu_time'] = detail['nodft_time']
                    data_by_arch[arch_key]['nodft'][config_key].append(nodft_detail)
    
    return data_by_arch


def prepare_boxplot_data(details, x_field, y_field):
    """
    Prepare data for box plots by binning x values logarithmically.
    Returns: {bin_idx: [y_values]}
    """
    binned_data = defaultdict(list)
    
    for d in details:
        x_val = d.get(x_field)
        y_val = d.get(y_field)
        
        if x_val is None or y_val is None:
            continue
        if x_val <= 0 or y_val <= 0:
            continue
        
        bin_idx = get_log_bin(x_val)
        binned_data[bin_idx].append(y_val)
    
    return binned_data


def prepare_scatter_data(details, x_field, y_field):
    """
    Prepare data for scatter plots without binning.
    Returns: (x_values, y_values) as lists
    """
    x_vals = []
    y_vals = []
    
    for d in details:
        x_val = d.get(x_field)
        y_val = d.get(y_field)
        
        if x_val is None or y_val is None:
            continue
        if x_val <= 0 or y_val <= 0:
            continue
        
        x_vals.append(x_val)
        y_vals.append(y_val)
    
    return x_vals, y_vals


def compute_percentiles(data, percentiles=[25, 50, 75, 90, 95, 99]):
    """Compute summary statistics for a list of positive values."""
    if not data:
        return {}
    arr = np.array(data, dtype=float)
    positive_arr = arr[arr > 0]
    result = {
        'count': len(arr),
        'min': float(np.min(arr)),
        'max': float(np.max(arr)),
        'mean': float(np.mean(arr)),
        'geomean': float(np.exp(np.mean(np.log(positive_arr)))) if len(positive_arr) else float('nan'),
        'std': float(np.std(arr)),
    }
    for p in percentiles:
        result[f'p{p}'] = float(np.percentile(arr, p))
    return result


def print_percentile_table(arch_key, method, y_field, binned_data, sorted_bins):
    """Print percentile information as a table."""
    print(f"\n{'='*80}")
    print(f"Architecture: {arch_key} | Method: {method} | Metric: {y_field}")
    print(f"{'='*80}")
    header = f"{'Bin':<12} {'Count':<8} {'Min':<12} {'P25':<12} {'P50(Med)':<12} {'P75':<12} {'P95':<12} {'Max':<12} {'Mean':<12} {'GeoMean':<12}"
    print(header)
    print('-' * len(header))
    
    for bin_val in sorted_bins:
        if bin_val in binned_data and len(binned_data[bin_val]) > 0:
            stats = compute_percentiles(binned_data[bin_val])
            bin_str = f"10^{bin_val}" if bin_val > 0 else "1"
            print(f"{bin_str:<12} {stats['count']:<8} {stats['min']:<12.2f} {stats['p25']:<12.2f} "
                  f"{stats['p50']:<12.2f} {stats['p75']:<12.2f} {stats['p95']:<12.2f} "
                f"{stats['max']:<12.2f} {stats['mean']:<12.2f} {stats['geomean']:<12.2f}")


# Line styles for different methods
METHOD_MARKERS = {
    'fuzzgpu': 'o',      # circle
    'divefuzz': 's',     # square
    'cascade': '^',      # triangle
}

METHOD_LINESTYLES = {
    'fuzzgpu': '-',      # solid
    'divefuzz': '--',    # dashed
    'cascade': '-.',     # dash-dot
}


def plot_throughput_boxplots(data_by_arch, x_field, y_field, title_prefix, ylabel, output_dir,
                             methods_filter=None, show_legend=True, figsize=None, label_format='range', ylabel_coords=None, show_ylabel=True, ylim=None):
    """
    Create grouped bar charts for each architecture.
    Data is binned by custom instruction count ranges and aggregated by geometric mean.
    
    Args:
        methods_filter: List of methods to include. If None, include all methods.
        show_legend: Whether to show the legend. Default True.
        figsize: Figure size tuple (width, height). If None, uses default FIGSIZE.
        label_format: 'range' for full range labels, 'endpoint' for right endpoint only.
        ylabel_coords: Tuple (x, y) for ylabel position. If None, uses default (-0.1, 0.37).
        show_ylabel: Whether to show ylabel. Default True.
        ylim: Tuple (ymin, ymax) for y-axis limits. If None, auto-scale.
    """
    for arch_key, method_data in data_by_arch.items():
        plot_figsize = figsize if figsize is not None else FIGSIZE
        fig, ax = plt.subplots(figsize=plot_figsize)

        # For log scale, multiplicative padding is the right choice.
        # Keep enough space for an in-axes legend.
        legend_ymax_factor = 1.55
        
        methods_to_plot = sorted(method_data.keys())
        if methods_filter:
            methods_to_plot = [m for m in methods_to_plot if m in methods_filter]
        
        # Collect all data by custom bins: {method: {bin_idx: [y_values]}}
        method_binned_data = {}
        all_bins_used = set()
        
        for method in methods_to_plot:
            config_data = method_data[method]
            method_binned_data[method] = defaultdict(list)
            
            # Merge all configs for this method and bin by x value
            for config_key in config_data.keys():
                details = config_data[config_key]
                for d in details:
                    x_val = d.get(x_field)
                    y_val = d.get(y_field)
                    
                    if x_val is None or y_val is None:
                        continue
                    if x_val <= 0 or y_val <= 0:
                        continue
                    
                    # Convert emu_time from seconds to milliseconds
                    if y_field == 'emu_time':
                        y_val = y_val * 1000
                    
                    bin_idx = get_custom_bin(x_val)
                    method_binned_data[method][bin_idx].append(y_val)
                    all_bins_used.add(bin_idx)
        
        if not all_bins_used:
            plt.close(fig)
            continue
        
        # Sort bins
        sorted_bins = sorted(all_bins_used)
        n_bins = len(sorted_bins)
        n_methods = len(methods_to_plot)
        
        # Calculate bar positions
        x_positions = np.arange(n_bins)
        bar_width = 0.8 / max(n_methods, 1)
        
        # Plot bars for each method
        for method_idx, method in enumerate(methods_to_plot):
            # Use architecture color and hatch for emu_time, otherwise use method color/hatch
            if y_field == 'emu_time':
                # Determine which arch key to use
                arch_lower = arch_key.lower()
                if 'ventus' in arch_lower:
                    color = ARCH_COLORS.get('ventus', METHOD_COLORS.get(method, '#333333'))
                    hatch = ARCH_HATCHES.get('ventus', '')
                elif 'vortex' in arch_lower:
                    color = ARCH_COLORS.get('vortex', METHOD_COLORS.get(method, '#333333'))
                    hatch = ARCH_HATCHES.get('vortex', '')
                else:
                    color = METHOD_COLORS.get(method, '#333333')
                    hatch = METHOD_HATCHES.get(method, '')
            else:
                color = METHOD_COLORS.get(method, '#333333')
                hatch = METHOD_HATCHES.get(method, '')
            method_label = METHOD_DISPLAY_NAMES.get(method, method)
            
            y_geomeans = []
            y_err_low = []
            y_err_high = []
            valid_x = []
            
            print(f"\n{'='*80}")
            print(f"Architecture: {arch_key} | Method: {method_label} | Metric: {y_field}")
            print(f"{'='*80}")
            
            for bin_pos, bin_idx in enumerate(sorted_bins):
                offset = (method_idx - (n_methods - 1) / 2) * bar_width
                x_pos = x_positions[bin_pos] + offset
                
                if bin_idx in method_binned_data[method] and len(method_binned_data[method][bin_idx]) > 0:
                    y_vals = method_binned_data[method][bin_idx]
                    stats = compute_percentiles(y_vals)
                    y_geomean = stats['geomean']
                    y_med = stats['p50']
                    y_p25 = stats['p25']
                    y_p75 = stats['p75']
                    
                    y_geomeans.append(y_geomean)
                    y_err_low.append(y_geomean - y_p25)
                    y_err_high.append(y_p75 - y_geomean)
                    valid_x.append(x_pos)
                    
                    bin_label_str = get_custom_bin_label(bin_idx)
                    print(f"  {bin_label_str}: count={len(y_vals)}, y_geomean={y_geomean:.2f}, y_med={y_med:.2f}, y_p25={y_p25:.2f}, y_p75={y_p75:.2f}")
                else:
                    # Missing data for this bin
                    bin_label_str = get_custom_bin_label(bin_idx)
                    print(f"  {bin_label_str}: TLE (no data)")
            
            if y_geomeans:
                ax.bar(
                    valid_x,
                    y_geomeans,
                    width=bar_width * 0.9,
                    color=color,
                    alpha=0.8,
                    hatch=hatch,
                    edgecolor='black',
                    linewidth=0.5,
                    label=method_label,
                    zorder=2
                )
                # Error bars disabled by default for cleaner visualization
                # Uncomment the following lines to enable error bars:
                # ax.errorbar(
                #     valid_x,
                #     y_geomeans,
                #     yerr=[y_err_low, y_err_high],
                #     fmt='none',
                #     ecolor='black',
                #     elinewidth=1.5,
                #     capsize=3,
                #     capthick=1,
                #     zorder=3
                # )
            
        # Set x-axis with bin labels
        ax.set_xticks(x_positions)
        if label_format == 'endpoint':
            ax.set_xticklabels([get_custom_bin_endpoint_label(b) for b in sorted_bins])
        else:
            ax.set_xticklabels([get_custom_bin_label(b) for b in sorted_bins])
        
        # Labels
        x_label = X_FIELD_LABELS.get(x_field, x_field)
        ax.set_xlabel(f'{x_label}')
        # Only show ylabel if requested and not (emu_time field with ventus architecture)
        if show_ylabel and not (y_field == 'emu_time' and 'ventus' in arch_key.lower()):
            ax.set_ylabel(ylabel)
        # Micro-adjust label position for a tighter layout.
        if ylabel_coords is None:
            ylabel_coords = (-0.1, 0.37)
        ax.yaxis.set_label_coords(ylabel_coords[0], ylabel_coords[1])
        
        # Legend: keep it inside the axes and slightly below the top border so
        # it doesn't touch the saved figure's top edge (especially with tight bboxes).
        if show_legend:
            ncol = max(1, len(methods_to_plot))
            ax.legend(
                loc='upper center',
                bbox_to_anchor=(0.5, 1.1),
                ncol=ncol,
                fontsize=FONTSIZE - 1,
                frameon=False,
                columnspacing=0.9,
                handletextpad=0.4,
                borderaxespad=0.6,
            )
        
        # Grid
        ax.grid(True, axis='y', alpha=0.3, linestyle='--')
        ax.set_axisbelow(True)
        
        # Y-axis uses log scale
        ax.set_yscale('log')
        ax.yaxis.set_major_formatter(ticker.LogFormatterMathtext())

        # Apply unified ylim if specified
        if ylim is not None:
            ax.set_ylim(ylim[0], ylim[1])
        # Expand ymax slightly so there's room near the top.
        elif show_legend:
            y0, y1 = ax.get_ylim()
            if y1 > 0:
                ax.set_ylim(y0, y1 * legend_ymax_factor)
        
        # Add TLE text for missing data positions (after setting y scale)
        xaxis_transform = ax.get_xaxis_transform()  # x in data, y in axes-fraction
        for method_idx, method in enumerate(methods_to_plot):
            # Use architecture color for emu_time, otherwise use method color
            if y_field == 'emu_time':
                arch_lower = arch_key.lower()
                if 'ventus' in arch_lower:
                    color = ARCH_COLORS.get('ventus', METHOD_COLORS.get(method, '#333333'))
                elif 'vortex' in arch_lower:
                    color = ARCH_COLORS.get('vortex', METHOD_COLORS.get(method, '#333333'))
                else:
                    color = METHOD_COLORS.get(method, '#333333')
            else:
                color = METHOD_COLORS.get(method, '#333333')
            for bin_pos, bin_idx in enumerate(sorted_bins):
                offset = (method_idx - (n_methods - 1) / 2) * bar_width
                x_pos = x_positions[bin_pos] + offset
                if bin_idx not in method_binned_data[method] or len(method_binned_data[method][bin_idx]) == 0:
                    ax.text(
                        x_pos, 
                        0.02,  # close to x-axis (axes fraction)
                        'TLE',
                        transform=xaxis_transform,
                        ha='center',
                        va='bottom',
                        fontsize=TLE_FONTSIZE-1,
                        fontweight='bold',
                        color=color,
                        rotation=0,
                        zorder=4,
                        clip_on=False,
                    )
        
        # Layout: keep the plot compact but readable.
        fig.tight_layout(pad=0.10)
        
        # Save figure (both PDF and PNG)
        filename_base = f"{title_prefix.lower().replace(' ', '_')}_{arch_key}"
        output_path_png = output_dir / f"{filename_base}.png"
        output_path_pdf = output_dir / f"{filename_base}.pdf"
        plt.savefig(
            output_path_png,
            dpi=150,
            bbox_inches='tight',
            pad_inches=0.05,
        )
        plt.savefig(
            output_path_pdf,
            format='pdf',
            dpi=150,
            bbox_inches='tight',
            pad_inches=0.05,
        )
        print(f"Saved: {output_path_png} and {output_path_pdf}")
        plt.close(fig)


def plot_arch_comparison(data_by_arch, x_field, y_field, title_prefix, ylabel, output_dir,
                         methods_filter=None, show_legend=True, figsize=None, label_format='range', 
                         ylabel_coords=None, ylim=None):
    """
    Create grouped bar charts comparing different architectures in the same plot.
    Data is binned by custom instruction count ranges and aggregated by geometric mean.
    
    Args:
        data_by_arch: Output from extract_data_by_arch()
        methods_filter: List of methods to include. If None, include all methods.
        show_legend: Whether to show the legend. Default True.
        figsize: Figure size tuple (width, height). If None, uses default FIGSIZE.
        label_format: 'range' for full range labels, 'endpoint' for right endpoint only.
        ylabel_coords: Tuple (x, y) for ylabel position. If None, uses default (-0.1, 0.37).
        ylim: Tuple (ymin, ymax) for y-axis limits. If None, auto-scale.
    """
    plot_figsize = figsize if figsize is not None else FIGSIZE
    fig, ax = plt.subplots(figsize=plot_figsize)

    # For log scale, multiplicative padding is the right choice.
    legend_ymax_factor = 1.55
    
    # Collect architectures to plot
    archs_to_plot = sorted(data_by_arch.keys())
    
    # Collect all data by custom bins: {arch: {bin_idx: [y_values]}}
    arch_binned_data = {}
    all_bins_used = set()
    
    for arch_key in archs_to_plot:
        method_data = data_by_arch[arch_key]
        arch_binned_data[arch_key] = defaultdict(list)
        
        # Determine which methods to use
        methods_to_use = sorted(method_data.keys())
        if methods_filter:
            methods_to_use = [m for m in methods_to_use if m in methods_filter]
        
        # Merge all methods for this arch and bin by x value
        for method in methods_to_use:
            config_data = method_data[method]
            for config_key in config_data.keys():
                details = config_data[config_key]
                for d in details:
                    x_val = d.get(x_field)
                    y_val = d.get(y_field)
                    
                    if x_val is None or y_val is None:
                        continue
                    if x_val <= 0 or y_val <= 0:
                        continue
                    
                    # Convert emu_time from seconds to milliseconds
                    if y_field == 'emu_time':
                        y_val = y_val * 1000
                    
                    bin_idx = get_custom_bin(x_val)
                    arch_binned_data[arch_key][bin_idx].append(y_val)
                    all_bins_used.add(bin_idx)
    
    if not all_bins_used:
        plt.close(fig)
        return
    
    # Sort bins
    sorted_bins = sorted(all_bins_used)
    n_bins = len(sorted_bins)
    n_archs = len(archs_to_plot)
    
    # Calculate bar positions
    x_positions = np.arange(n_bins)
    bar_width = 0.8 / max(n_archs, 1)
    
    # Plot bars for each architecture
    for arch_idx, arch_key in enumerate(archs_to_plot):
        # Determine arch name for color/hatch lookup
        arch_lower = arch_key.lower()
        if 'ventus' in arch_lower:
            arch_name = 'ventus'
        elif 'vortex' in arch_lower:
            arch_name = 'vortex'
        else:
            arch_name = arch_key
        
        color = ARCH_COLORS.get(arch_name, '#333333')
        hatch = ARCH_HATCHES.get(arch_name, '')
        # Create display name
        if arch_name == 'ventus':
            arch_label = 'OpenGPGPU'
        else:
            arch_label = arch_name.capitalize()
        
        y_geomeans = []
        valid_x = []
        
        print(f"\n{'='*80}")
        print(f"Architecture: {arch_key} | Metric: {y_field}")
        print(f"{'='*80}")
        
        for bin_pos, bin_idx in enumerate(sorted_bins):
            offset = (arch_idx - (n_archs - 1) / 2) * bar_width
            x_pos = x_positions[bin_pos] + offset
            
            if bin_idx in arch_binned_data[arch_key] and len(arch_binned_data[arch_key][bin_idx]) > 0:
                y_vals = arch_binned_data[arch_key][bin_idx]
                stats = compute_percentiles(y_vals)
                y_geomean = stats['geomean']
                y_med = stats['p50']
                
                y_geomeans.append(y_geomean)
                valid_x.append(x_pos)
                
                bin_label_str = get_custom_bin_label(bin_idx)
                print(f"  {bin_label_str}: count={len(y_vals)}, y_geomean={y_geomean:.2f}, y_med={y_med:.2f}")
            else:
                bin_label_str = get_custom_bin_label(bin_idx)
                print(f"  {bin_label_str}: TLE (no data)")
        
        if y_geomeans:
            ax.bar(
                valid_x,
            y_geomeans,
                width=bar_width * 0.9,
                color=color,
                alpha=0.8,
                hatch=hatch,
                edgecolor='black',
                linewidth=0.5,
                label=arch_label,
                zorder=2
            )
        
    # Set x-axis with bin labels
    ax.set_xticks(x_positions)
    if label_format == 'endpoint':
        ax.set_xticklabels([get_custom_bin_endpoint_label(b) for b in sorted_bins])
    else:
        ax.set_xticklabels([get_custom_bin_label(b) for b in sorted_bins])
    
    # Labels
    x_label = X_FIELD_LABELS.get(x_field, x_field)
    ax.set_xlabel(f'{x_label}')
    ax.set_ylabel(ylabel)
    if ylabel_coords is None:
        ylabel_coords = (-0.1, 0.37)
    ax.yaxis.set_label_coords(ylabel_coords[0], ylabel_coords[1])
    
    # Legend
    if show_legend:
        ncol = max(1, len(archs_to_plot))
        ax.legend(
            loc='upper center',
            bbox_to_anchor=(0.5, 1.1),
            ncol=ncol,
            fontsize=FONTSIZE - 1,
            frameon=False,
            columnspacing=0.9,
            handletextpad=0.4,
            borderaxespad=0.6,
        )
    
    # Grid
    ax.grid(True, axis='y', alpha=0.3, linestyle='--')
    ax.set_axisbelow(True)
    
    # Y-axis uses log scale
    ax.set_yscale('log')
    ax.yaxis.set_major_formatter(ticker.LogFormatterMathtext())

    # Apply unified ylim if specified
    if ylim is not None:
        ax.set_ylim(ylim[0], ylim[1])
    elif show_legend:
        y0, y1 = ax.get_ylim()
        if y1 > 0:
            ax.set_ylim(y0, y1 * legend_ymax_factor)
    
    # Add TLE text for missing data positions
    xaxis_transform = ax.get_xaxis_transform()
    for arch_idx, arch_key in enumerate(archs_to_plot):
        arch_lower = arch_key.lower()
        if 'ventus' in arch_lower:
            arch_name = 'ventus'
        elif 'vortex' in arch_lower:
            arch_name = 'vortex'
        else:
            arch_name = arch_key
        color = ARCH_COLORS.get(arch_name, '#333333')
        
        for bin_pos, bin_idx in enumerate(sorted_bins):
            offset = (arch_idx - (n_archs - 1) / 2) * bar_width
            x_pos = x_positions[bin_pos] + offset
            if bin_idx not in arch_binned_data[arch_key] or len(arch_binned_data[arch_key][bin_idx]) == 0:
                ax.text(
                    x_pos, 
                    0.02,
                    'TLE',
                    transform=xaxis_transform,
                    ha='center',
                    va='bottom',
                    fontsize=TLE_FONTSIZE-1,
                    fontweight='bold',
                    color=color,
                    rotation=0,
                    zorder=4,
                    clip_on=False,
                )
    
    # Layout
    fig.tight_layout(pad=0.10)
    
    # Save figure (both PDF and PNG)
    filename_base = f"{title_prefix.lower().replace(' ', '_')}_arch_comparison"
    output_path_png = output_dir / f"{filename_base}.png"
    output_path_pdf = output_dir / f"{filename_base}.pdf"
    plt.savefig(
        output_path_png,
        dpi=150,
        bbox_inches='tight',
        pad_inches=0.05,
    )
    plt.savefig(
        output_path_pdf,
        format='pdf',
        dpi=150,
        bbox_inches='tight',
        pad_inches=0.05,
    )
    print(f"Saved: {output_path_png} and {output_path_pdf}")
    plt.close(fig)


def plot_dft_comparison(data_by_arch, x_field, y_field, title_prefix, ylabel, output_dir,
                            show_legend=True, figsize=None, label_format='range', ylabel_coords=None, show_ylabel=True, ylim=None):
    """
    Create grouped bar charts comparing DFT vs non-DFT mode for each architecture.
    Data is binned by custom instruction count ranges and aggregated by geometric mean.
    
    Args:
        data_by_arch: Output from extract_data_by_arch_dft()
        show_legend: Whether to show the legend. Default True.
        figsize: Figure size tuple (width, height). If None, uses default FIGSIZE.
        label_format: 'range' for full range labels, 'endpoint' for right endpoint only.
        ylabel_coords: Tuple (x, y) for ylabel position. If None, uses default (-0.1, 0.37).
        show_ylabel: Whether to show ylabel. Default True.
        ylim: Tuple (ymin, ymax) for y-axis limits. If None, auto-scale.
    """
    for arch_key, dft_mode_data in data_by_arch.items():
        plot_figsize = figsize if figsize is not None else FIGSIZE
        fig, ax = plt.subplots(figsize=plot_figsize)

        # For log scale, multiplicative padding is the right choice.
        legend_ymax_factor = 1.55
        
        # Order: nodft first, then dft
        modes_to_plot = []
        if 'nodft' in dft_mode_data:
            modes_to_plot.append('nodft')
        if 'dft' in dft_mode_data:
            modes_to_plot.append('dft')
        
        if not modes_to_plot:
            plt.close(fig)
            continue
        
        # Collect all data by custom bins: {mode: {bin_idx: [y_values]}}
        mode_binned_data = {}
        all_bins_used = set()
        
        for mode in modes_to_plot:
            config_data = dft_mode_data[mode]
            mode_binned_data[mode] = defaultdict(list)
            
            # Merge all configs for this mode and bin by x value
            for config_key in config_data.keys():
                details = config_data[config_key]
                for d in details:
                    x_val = d.get(x_field)
                    y_val = d.get(y_field)
                    
                    if x_val is None or y_val is None:
                        continue
                    if x_val <= 0 or y_val <= 0:
                        continue
                    
                    # Convert emu_time from seconds to milliseconds
                    if y_field == 'emu_time':
                        y_val = y_val * 1000
                    
                    bin_idx = get_custom_bin(x_val)
                    mode_binned_data[mode][bin_idx].append(y_val)
                    all_bins_used.add(bin_idx)
        
        if not all_bins_used:
            plt.close(fig)
            continue
        
        # Sort bins
        sorted_bins = sorted(all_bins_used)
        n_bins = len(sorted_bins)
        n_modes = len(modes_to_plot)
        
        # Calculate bar positions
        x_positions = np.arange(n_bins)
        bar_width = 0.8 / max(n_modes, 1)
        
        # Plot bars for each mode
        for mode_idx, mode in enumerate(modes_to_plot):
            color = DFT_COLORS.get(mode, '#333333')
            hatch = DFT_HATCHES.get(mode, '')
            mode_label = DFT_DISPLAY_NAMES.get(mode, mode)
            
            y_geomeans = []
            y_err_low = []
            y_err_high = []
            valid_x = []
            
            print(f"\n{'='*80}")
            print(f"Architecture: {arch_key} | Mode: {mode_label} | Metric: {y_field}")
            print(f"{'='*80}")
            
            for bin_pos, bin_idx in enumerate(sorted_bins):
                offset = (mode_idx - (n_modes - 1) / 2) * bar_width
                x_pos = x_positions[bin_pos] + offset
                
                if bin_idx in mode_binned_data[mode] and len(mode_binned_data[mode][bin_idx]) > 0:
                    y_vals = mode_binned_data[mode][bin_idx]
                    stats = compute_percentiles(y_vals)
                    y_geomean = stats['geomean']
                    y_med = stats['p50']
                    y_p25 = stats['p25']
                    y_p75 = stats['p75']
                    
                    y_geomeans.append(y_geomean)
                    y_err_low.append(y_geomean - y_p25)
                    y_err_high.append(y_p75 - y_geomean)
                    valid_x.append(x_pos)
                    
                    bin_label_str = get_custom_bin_label(bin_idx)
                    print(f"  {bin_label_str}: count={len(y_vals)}, y_geomean={y_geomean:.2f}, y_med={y_med:.2f}, y_p25={y_p25:.2f}, y_p75={y_p75:.2f}")
                else:
                    # Missing data for this bin
                    bin_label_str = get_custom_bin_label(bin_idx)
                    print(f"  {bin_label_str}: TLE (no data)")
            
            if y_geomeans:
                ax.bar(
                    valid_x,
                    y_geomeans,
                    width=bar_width * 0.9,
                    color=color,
                    alpha=0.8,
                    hatch=hatch,
                    edgecolor='black',
                    linewidth=0.5,
                    label=mode_label,
                    zorder=2
                )
            
        # Set x-axis with bin labels
        ax.set_xticks(x_positions)
        if label_format == 'endpoint':
            ax.set_xticklabels([get_custom_bin_endpoint_label(b) for b in sorted_bins])
        else:
            ax.set_xticklabels([get_custom_bin_label(b) for b in sorted_bins])
        
        # Labels
        x_label = X_FIELD_LABELS.get(x_field, x_field)
        ax.set_xlabel(f'{x_label}')
        if show_ylabel:
            ax.set_ylabel(ylabel)
        if ylabel_coords is None:
            ylabel_coords = (-0.1, 0.37)
        ax.yaxis.set_label_coords(ylabel_coords[0], ylabel_coords[1])
        
        # Legend
        if show_legend:
            ncol = max(1, len(modes_to_plot))
            ax.legend(
                loc='upper center',
                bbox_to_anchor=(0.5, 1.1),
                ncol=ncol,
                fontsize=FONTSIZE - 1,
                frameon=False,
                columnspacing=0.9,
                handletextpad=0.4,
                borderaxespad=0.6,
            )
        
        # Grid
        ax.grid(True, axis='y', alpha=0.3, linestyle='--')
        ax.set_axisbelow(True)
        
        # Y-axis uses log scale
        ax.set_yscale('log')
        ax.yaxis.set_major_formatter(ticker.LogFormatterMathtext())

        # Apply unified ylim if specified
        if ylim is not None:
            ax.set_ylim(ylim[0], ylim[1])
        elif show_legend:
            y0, y1 = ax.get_ylim()
            if y1 > 0:
                ax.set_ylim(y0, y1 * legend_ymax_factor)
        
        # Add TLE text for missing data positions
        xaxis_transform = ax.get_xaxis_transform()
        for mode_idx, mode in enumerate(modes_to_plot):
            color = DFT_COLORS.get(mode, '#333333')
            for bin_pos, bin_idx in enumerate(sorted_bins):
                offset = (mode_idx - (n_modes - 1) / 2) * bar_width
                x_pos = x_positions[bin_pos] + offset
                if bin_idx not in mode_binned_data[mode] or len(mode_binned_data[mode][bin_idx]) == 0:
                    ax.text(
                        x_pos, 
                        0.02,
                        'TLE',
                        transform=xaxis_transform,
                        ha='center',
                        va='bottom',
                        fontsize=TLE_FONTSIZE-1,
                        fontweight='bold',
                        color=color,
                        rotation=0,
                        zorder=4,
                        clip_on=False,
                    )
        
        # Layout
        fig.tight_layout(pad=0.10)
        
        # Save figure (both PDF and PNG)
        filename_base = f"{title_prefix.lower().replace(' ', '_')}_dft_cmp_{arch_key}"
        output_path_png = output_dir / f"{filename_base}.png"
        output_path_pdf = output_dir / f"{filename_base}.pdf"
        plt.savefig(
            output_path_png,
            dpi=150,
            bbox_inches='tight',
            pad_inches=0.05,
        )
        plt.savefig(
            output_path_pdf,
            format='pdf',
            dpi=150,
            bbox_inches='tight',
            pad_inches=0.05,
        )
        print(f"Saved: {output_path_png} and {output_path_pdf}")
        plt.close(fig)


def find_latest_result_json(results_dir):
    """Find the most recent JSON result file in the results directory."""
    json_files = list(results_dir.glob('throughput_results_*.json'))
    if not json_files:
        return None
    # Sort by modification time, newest first
    json_files.sort(key=lambda p: p.stat().st_mtime, reverse=True)
    return json_files[0]


def main():
    script_dir = Path(__file__).resolve().parent
    fuzzgpu_dir = script_dir.parent
    default_results_dir = fuzzgpu_dir / 'results' / 'fuzz_instr_throughput'
    
    parser = argparse.ArgumentParser(
        description='Plot throughput results from fuzz_instr_throughput.py output.'
    )
    parser.add_argument(
        'input_json',
        type=str,
        nargs='*',
        default=None,
        help='Path(s) to the JSON results file(s) from fuzz_instr_throughput.py. '
             'Multiple files can be provided for DFT comparison (default: latest file in results/fuzz_instr_throughput/)'
    )
    parser.add_argument(
        '--output-dir',
        type=str,
        default=None,
        help='Directory to save plots (default: same as input JSON directory)'
    )
    parser.add_argument(
        '--show',
        action='store_true',
        help='Show plots interactively instead of saving'
    )
    parser.add_argument(
        '--compare-dft',
        action='store_true',
        help='Generate comparison plots for DFT vs non-DFT mode (requires results from both modes)'
    )
    
    args = parser.parse_args()
    
    # Determine input paths
    input_paths = []
    if args.input_json and len(args.input_json) > 0:
        for json_path in args.input_json:
            input_paths.append(Path(json_path))
    else:
        # Default: find the latest JSON in the results directory
        if not default_results_dir.exists():
            print(f"Error: Results directory not found: {default_results_dir}")
            print("Run fuzz_instr_throughput.py first to generate results.")
            return 1
        input_path = find_latest_result_json(default_results_dir)
        if input_path is None:
            print(f"Error: No result JSON files found in {default_results_dir}")
            print("Run fuzz_instr_throughput.py first to generate results.")
            return 1
        print(f"Using latest result file: {input_path}")
        input_paths = [input_path]
    
    # Check all input paths exist
    for input_path in input_paths:
        if not input_path.exists():
            print(f"Error: Input file not found: {input_path}")
            return 1
    
    if args.output_dir:
        output_dir = Path(args.output_dir)
    else:
        # Default: save plots to results/plots directory
        output_dir = fuzzgpu_dir / 'results' / 'plots'
    
    output_dir.mkdir(parents=True, exist_ok=True)
    
    # Load and merge all results
    merged_results = {}
    for input_path in input_paths:
        print(f"Loading results from: {input_path}")
        results = load_results(input_path)
        merged_results.update(results)
    
    print(f"Extracting data by architecture...")
    data_by_arch = extract_data_by_arch(merged_results)
    
    if not data_by_arch:
        print("Error: No valid data found in results file(s).")
        return 1
    
    print(f"Found architectures: {list(data_by_arch.keys())}")
    for arch_key, method_data in data_by_arch.items():
        print(f"  {arch_key}: methods = {list(method_data.keys())}")
    
    print(f"\nGenerating plots...")
    print(f"Output directory: {output_dir}")
    
    # Group 1: gen_throughput vs gen_num_instrs
    print("\n--- Gen Throughput Plots ---")
    plot_throughput_boxplots(
        data_by_arch,
        x_field='gen_num_instrs',
        y_field='gen_throughput',
        title_prefix='Gen Throughput',
        ylabel='Throughput (IPS)',
        output_dir=output_dir
    )
    
    # Group 2: emu_time vs emu_num_instrs (fuzzgpu only, no legend)
    print("\n--- Emu Absolute Time Plots ---")
    emu_figsize = (textwidth_in, textwidth_in * 0.28)
    plot_throughput_boxplots(
        data_by_arch,
        x_field='gen_num_instrs',
        y_field='emu_time',
        title_prefix='Emu Absolute Time',
        ylabel='Time (ms)',
        output_dir=output_dir,
        methods_filter=['fuzzgpu'],
        show_legend=False,
        figsize=emu_figsize,
        label_format='endpoint',
        ylabel_coords=(-0.24, 0.37),
        ylim=(50, 50000)  # Unified ylim for vortex and ventus
    )
    
    # Group 2b: emu_time comparison across architectures (fuzzgpu only)
    print("\n--- Emu Absolute Time Arch Comparison ---")
    plot_arch_comparison(
        data_by_arch,
        x_field='gen_num_instrs',
        y_field='emu_time',
        title_prefix='Emu Absolute Time',
        ylabel='Time (ms)',
        output_dir=output_dir,
        methods_filter=['fuzzgpu'],
        show_legend=True,
        figsize=emu_figsize,
        label_format='endpoint',
        ylabel_coords=(-0.12, 0.37),
        ylim=(50, 50000)
    )
    
    # Group 3: effective_throughput vs emu_num_instrs
    print("\n--- Effective Throughput Plots ---")
    plot_throughput_boxplots(
        data_by_arch,
        x_field='gen_num_instrs',
        y_field='effective_throughput',
        title_prefix='Effective Throughput',
        ylabel='Throughput (IPS)',
        output_dir=output_dir
    )
    
    # Group 4: EMU DFT vs non-DFT timing comparison (comparing dft_time vs nodft_time from same runs)
    print("\n--- Emu Time: DFT (with difftest) vs non-DFT (timing only) Comparison ---")
    data_by_arch_emu_dft_cmp = extract_data_by_arch_emu_dft_comparison(merged_results)
    
    # Check if we have dft_time and nodft_time fields
    has_emu_dft_comparison = False
    for arch_key, dft_data in data_by_arch_emu_dft_cmp.items():
        if 'dft' in dft_data and 'nodft' in dft_data:
            if dft_data['dft'] or dft_data['nodft']:  # Check if there's data
                has_emu_dft_comparison = True
                break
    
    if has_emu_dft_comparison:
        print(f"Found EMU DFT comparison data for architectures: {list(data_by_arch_emu_dft_cmp.keys())}")
        for arch_key, dft_data in data_by_arch_emu_dft_cmp.items():
            print(f"  {arch_key}: modes = {list(dft_data.keys())}")
        
        # EMU time comparison: dft_time vs nodft_time
        print("\n--- Emu Time (DFT with difftest vs non-DFT timing) ---")
        plot_dft_comparison(
            data_by_arch_emu_dft_cmp,
            x_field='gen_num_instrs',
            y_field='emu_time',
            title_prefix='Emu Time DFT Comparison',
            ylabel='Emu. Time (ms)',
            output_dir=output_dir,
            figsize=emu_figsize,
            label_format='endpoint',
            ylabel_coords=(-0.18, 0.37),
        )
    else:
        print("Note: EMU DFT comparison not available. Make sure dft_time and nodft_time are in the results.")
    
    # DFT comparison plots (for separate runs with --dft flag)
    if args.compare_dft:
        print("\n--- DFT Comparison Plots (from separate runs) ---")
        data_by_arch_dft = extract_data_by_arch_dft(merged_results)
        
        # Check if we have both DFT and non-DFT data
        has_comparison_data = False
        for arch_key, dft_data in data_by_arch_dft.items():
            if 'nodft' in dft_data and 'dft' in dft_data:
                has_comparison_data = True
                break
        
        if has_comparison_data:
            print(f"Found DFT comparison data for architectures: {list(data_by_arch_dft.keys())}")
            for arch_key, dft_data in data_by_arch_dft.items():
                print(f"  {arch_key}: modes = {list(dft_data.keys())}")
            
            # DFT comparison: emu_time
            print("\n--- Emu Time (separate DFT vs non-DFT runs) ---")
            plot_dft_comparison(
                data_by_arch_dft,
                x_field='gen_num_instrs',
                y_field='emu_time',
                title_prefix='Emu Time Separate Run Comparison',
                ylabel='Emu. Time (ms)',
                output_dir=output_dir,
                figsize=emu_figsize,
                label_format='endpoint',
                ylabel_coords=(-0.18, 0.37),
            )
            
            # DFT comparison: effective_throughput
            print("\n--- Effective Throughput (separate DFT vs non-DFT runs) ---")
            plot_dft_comparison(
                data_by_arch_dft,
                x_field='gen_num_instrs',
                y_field='effective_throughput',
                title_prefix='Effective Throughput Separate Run Comparison',
                ylabel='Throughput (IPS)',
                output_dir=output_dir
            )
        else:
            print("Warning: No separate DFT comparison data found. Make sure you have results from both --dft and non-dft runs.")
    
    print(f"\nDone! Plots saved to: {output_dir}")
    return 0


if __name__ == '__main__':
    exit(main())
