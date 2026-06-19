#!/usr/bin/env python3
from html import parser
import json
import os
import argparse
import matplotlib.pyplot as plt
import numpy as np

import matplotlib.patches as mpatches
from pathlib import Path
from common_plot import *

def load_coverage(json_path):
    if not os.path.exists(json_path):
        print(f"Warning: {json_path} not found.")
        return []
    with open(json_path, 'r') as f:
        data = json.load(f)
    # Extract instr_completion_rate (from instr_completion_rate.py output)
    # Fallback to coverage_percent for backward compatibility
    result = []
    for item in data:
        if 'instr_completion_rate' in item:
            result.append(item['instr_completion_rate'])
        elif 'coverage_percent' in item:
            result.append(item['coverage_percent'])
    return result

def main():
    script_dir = Path(__file__).resolve().parent
    fuzzgpu_dir = script_dir.parent
    
    parser = argparse.ArgumentParser(description='Plot instruction completion rate distribution for multiple methods.')
    parser.add_argument('--log-dir-base', type=str, default=None, help='Base directory containing per-method logs (expects cascade/, divefuzz/, fuzzgpu/ subdirectories)')
    parser.add_argument('--output', type=str, default=None, help='Output filename prefix (default: results/plots/instr_complete_rate)')
    parser.add_argument('--arch', type=str, choices=['ventus', 'vortex'], default='vortex', help='Target architecture')
    parser.add_argument('--xlen', type=int, choices=[32, 64], default=64, help='Target XLEN')
    args = parser.parse_args()

    methods = [
        ("cascade", "Cascade"),
        ("divefuzz", "DiveFuzz"),
        ("fuzzgpu", "FuzzGPU")
    ]

    def resolve_log_dir(method):
        if args.log_dir_base:
            candidate = os.path.join(args.log_dir_base, method)
            if os.path.exists(candidate):
                return candidate
        
        # Read directly from results directory
        results_dir = fuzzgpu_dir / 'results' / f'program_metric/{method}_{args.arch}_{args.xlen}'
        return str(results_dir)

    configs = [
        ("single", "Single Thread"),
        ("multi-warp-multiple-thread", "Multi-Warp Multi-Thread")
    ]

    data = {label: {} for _, label in configs}

    # Collect data per method
    for method_key, method_label in methods:
        log_dir = resolve_log_dir(method_key)
        print(f"Looking for logs in ({method_label}): {log_dir}")
        for conf_id, conf_label in configs:
            # Try metrics.json first (from instr_completion_rate.py), fallback to coverage.json
            json_path = os.path.join(log_dir, conf_id, 'metrics.json')
            if not os.path.exists(json_path):
                json_path = os.path.join(log_dir, conf_id, 'coverage.json')
            cov_values = load_coverage(json_path)
            data[conf_label][method_label] = cov_values
            if cov_values:
                print(f"{method_label} {conf_label}: {len(cov_values)} samples, Mean: {np.mean(cov_values):.2f}%, Median: {np.median(cov_values):.2f}%, Max: {np.max(cov_values):.2f}%")
            else:
                print(f"{method_label} {conf_label}: No data found.")

    # Plotting
    # Academic style settings
    try:
        plt.style.use('seaborn-v0_8-whitegrid')
    except:
        # Fallback if style not available
        plt.style.use('classic')
        plt.grid(True, linestyle='--', alpha=0.3)

    fig_width = textwidth_in
    fig_height = fig_width * 0.35
    plt.figure(figsize=(fig_width, fig_height))
    
    colors = ['#1f77b4', '#ff7f0e', '#2ca02c']
    hatches = ['///', '\\\\\\', 'xxx']  # Different hatching patterns for each method
    linestyles = ['-', '--', '-.']
    
    # Determine bins based on all data to ensure alignment
    all_values = []
    for values in data["Multi-Warp Multi-Thread"].values():
        all_values.extend(values)
    
    if not all_values:
        print("No data to plot.")
        return

    # Use 5% bins as requested (0-5%, 5-10%, etc.)
    bins = np.linspace(0, 100, 21) 
    bin_width = 5
    
    # Prepare data for plotting
    hist_data = []
    for method_key, method_label in methods:
        values = data["Multi-Warp Multi-Thread"].get(method_label, [])
        if not values:
            counts = np.zeros(len(bins)-1)
        else:
            counts, _ = np.histogram(values, bins=bins)
            counts = counts / len(values) * 100
        hist_data.append(counts)
    
    # Plotting bars
    bar_width = bin_width * 0.8
    
    # Center of each bin
    bin_centers = (bins[:-1] + bins[1:]) / 2
    
    for i, (method_key, method_label) in enumerate(methods):
        plt.bar(bin_centers, hist_data[i], width=bar_width, label=method_label, 
                color=colors[i], alpha=0.8, edgecolor='black', linewidth=0.5,
                hatch=hatches[i])
    
    # Add median lines for each method (optional, disabled by default)
    if 0:
        ls = ['--', '-.', ':']
        for i, (method_key, method_label) in enumerate(methods):
            values = data["Multi-Warp Multi-Thread"].get(method_label, [])
            if values:
                median_val = np.median(values)
                plt.axvline(x=median_val, color=colors[i], linestyle=ls[i], linewidth=2, alpha=0.7, label=f'{method_label} Median')
            
    plt.xlabel('Instruction Completion Rate (%)')
    plt.ylabel('Test Cases (%)')
    #plt.yscale('log')
    # plt.title('Coverage Distribution by Configuration') # Academic papers often put title in caption, but user asked for plot.
    plt.legend(loc='upper center', ncol=3, bbox_to_anchor=(0.5, 1.05), frameon=False)
    plt.grid(True, linestyle='--', alpha=0.3, axis='y')
    plt.xlim(0, 100)
    plt.xticks(bins[::2])  # Show every 10% (skip every other tick)
    plt.tight_layout()
    
    # Resolve output path
    if args.output is None:
        output_dir = fuzzgpu_dir / 'results' / 'plots'
        output_dir.mkdir(parents=True, exist_ok=True)
        output_prefix = output_dir / 'instr_complete_rate'
    else:
        output_prefix = Path(args.output)
        output_prefix.parent.mkdir(parents=True, exist_ok=True)
    
    # Save both PDF and PNG
    out_pdf = str(output_prefix) + '.pdf'
    out_png = str(output_prefix) + '.png'
    plt.savefig(out_pdf, format='pdf', dpi=300, bbox_inches='tight')
    plt.savefig(out_png, format='png', dpi=300, bbox_inches='tight')
    
    print(f"Plots saved to {out_pdf} and {out_png}")

if __name__ == '__main__':
    main()
