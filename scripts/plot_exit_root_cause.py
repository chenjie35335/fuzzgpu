#!/usr/bin/env python3
"""
Plot exit root cause distribution for fuzzing methods.
Reads crash triage results and visualizes the proportion of successful vs failed tests,
with failed tests broken down by crash reason.
"""
import json
import os
import argparse
import matplotlib.pyplot as plt
import numpy as np
from pathlib import Path
from collections import defaultdict
from common_plot import *

def load_triage_results(triage_path, metrics_path):
    """
    Load triage results and metrics data.
    Returns dict with success count and failure reasons breakdown.
    """
    result = {
        'success': 0,
        'failures': defaultdict(int),
        'total': 0
    }
    
    # First, get total and success count from metrics.json (output of instr_completion_rate.py)
    if os.path.exists(metrics_path):
        with open(metrics_path, 'r') as f:
            metrics_data = json.load(f)
            result['total'] = len(metrics_data)
            result['success'] = sum(1 for item in metrics_data if item.get('rc', 0) == 0)
    
    # Then, get failure reasons from triage.log
    if os.path.exists(triage_path):
        with open(triage_path, 'r') as f:
            for line in f:
                line = line.strip()
                if not line:
                    continue
                # Format: "Seed {id}: {reason}"
                if ':' in line:
                    parts = line.split(':', 1)
                    if len(parts) == 2:
                        reason = parts[1].strip()
                        norm = reason.lower()

                        # Normalize specific reason labels
                        if 'mis-steered branch' in norm or 'thread-divergent branch' in norm:
                            reason = 'Invalid Ctrl. Flow'
                            norm = reason.lower()
                        if 'barrier dead lock' in norm:
                            reason = 'Barrier Deadlock'
                            norm = reason.lower()
                        if 'simt stack underflow' in norm:
                            reason = 'SIMT Stack Err.'
                            norm = reason.lower()
                        if 'wmma invalid tmask' in norm:
                            reason = 'WMMA Thread Mask Err.'
                            norm = reason.lower()
                        if 'others' in norm:
                            reason = 'Others'
                            norm = reason.lower()
                        # Group specific reasons under 'others'
                        if ('log_not_found' in norm) or ('deadloop' in norm) or ('wspawn dead lock' in norm):
                            grouped_reason = 'Others'
                        else:
                            grouped_reason = reason
                        result['failures'][grouped_reason] += 1
    
    return result

def main():
    script_dir = Path(__file__).resolve().parent
    fuzzgpu_dir = script_dir.parent
    
    parser = argparse.ArgumentParser(description='Plot exit root cause distribution.')
    parser.add_argument('--log-dir-base', type=str, default=None, 
                        help='Base directory containing per-method logs (expects cascade/ and divefuzz/ subdirectories)')
    parser.add_argument('--output', type=str, default=None, 
                        help='Output filename prefix (default: results/plots/exit_root_cause)')
    parser.add_argument('--arch', type=str, choices=['ventus', 'vortex'], default='vortex', 
                        help='Target architecture')
    parser.add_argument('--xlen', type=int, choices=[32, 64], default=64, 
                        help='Target XLEN')
    parser.add_argument('--config', type=str, default='multi-warp-multiple-thread',
                        help='Configuration name (e.g., single, multi-warp-multiple-thread)')
    args = parser.parse_args()

    methods = [
        ("cascade", "Cascade"),
        ("divefuzz", "DiveFuzz")
    ]

    def resolve_results_dir(method):
        if args.log_dir_base:
            candidate = os.path.join(args.log_dir_base, method)
            if os.path.exists(candidate):
                return candidate
        
        # Read directly from results directory (output of instr_completion_rate.py)
        # Output path: results/program_metric/{method}_{arch}_{xlen}/{config}/
        results_dir = fuzzgpu_dir / 'results' / 'program_metric' / f'{method}_{args.arch}_{args.xlen}'
        return str(results_dir)

    # Collect data for both methods
    data = {}
    for method_key, method_label in methods:
        results_dir = resolve_results_dir(method_key)
        triage_path = os.path.join(results_dir, args.config, 'triage.log')
        metrics_path = os.path.join(results_dir, args.config, 'metrics.json')
        
        print(f"Loading {method_label} results from {results_dir}/{args.config}")
        result = load_triage_results(triage_path, metrics_path)
        data[method_label] = result
        
        print(f"  {method_label}: Total={result['total']}, Success={result['success']}, Failures={sum(result['failures'].values())}")
        if result['failures']:
            for reason, count in sorted(result['failures'].items(), key=lambda x: -x[1]):
                print(f"    {reason}: {count}")

    # Collect all unique failure reasons across both methods
    all_reasons = set()
    for method_data in data.values():
        all_reasons.update(method_data['failures'].keys())
    
    # Sort reasons by total count across methods
    reason_counts = defaultdict(int)
    for method_data in data.values():
        for reason, count in method_data['failures'].items():
            reason_counts[reason] += count
    sorted_reasons = sorted(all_reasons, key=lambda r: -reason_counts[r])
    
    # Prepare data for stacked bar chart
    # Categories: Success, Reason1, Reason2, ...
    categories = ['Success'] + sorted_reasons
    
    # Academic style settings
    try:
        plt.style.use('seaborn-v0_8-whitegrid')
    except:
        plt.style.use('classic')
        plt.grid(True, linestyle='--', alpha=0.3)
    
    # Create figure
    fig_width = textwidth_in
    fig_height = fig_width * 0.43
    fig, ax = plt.subplots(figsize=(fig_width, fig_height))
    
    # Color scheme with hatching patterns for better B&W visibility
    colors = ["#c2c2c2",
              '#a02b93',  "#2ab4f0",
              "#ffbf00ff",
         '#e74c3c',  "#34a362", '#95a5a6', '#ffffff']
    
    # Hatch patterns for different categories - ensures B&W distinction
    hatch_patterns = ['', '///', '\\\\\\', '', '---', 'xxx', 'ooo', '+++']
    
    # Prepare data arrays
    method_labels = ['D.F.' if label == 'DiveFuzz' else 'Cas.' if label == 'Cascade' else label for _, label in methods]
    y = np.arange(len(method_labels))
    height = 0.5
    
    # Create stacked bars
    lefts = np.zeros(len(method_labels))
    
    for idx, category in enumerate(categories):
        values = []
        for method_key, method_label in methods:
            method_data = data[method_label]
            total = method_data['total']
            if total == 0:
                values.append(0)
                continue
                
            if category == 'Success':
                count = method_data['success']
            else:
                count = method_data['failures'].get(category, 0)
            
            percentage = (count / total) * 100 if total > 0 else 0
            values.append(percentage)
        
        color = colors[idx % len(colors)]
        hatch = hatch_patterns[idx % len(hatch_patterns)]
        bars = ax.barh(y, values, height, left=lefts, label=category, color=color, 
                       hatch=hatch, edgecolor='black', linewidth=0.5)
        
        # Add percentage labels on bars if significant enough
        for i, (bar, val) in enumerate(zip(bars, values)):
            if val > 5:  # Only show label if > 5%
                width = bar.get_width()
                ax.text(lefts[i] + width/2., bar.get_y() + bar.get_height()/2.,
                       f'{val:.0f}',
                       ha='center', va='center', fontsize=FONTSIZE, fontweight='bold', color='white')
        
        lefts += values
    
    # Customize plot
    ax.set_xlabel('Percentage of Test Cases (%)')
    ax.set_ylabel('Method')
    ax.set_yticks(y)
    ax.set_yticklabels(method_labels, rotation=90, va='center', fontsize=FONTSIZE-1)
    ax.set_xlim(0, 100)
    # Place legend at top with horizontal layout to minimize height
    legend = ax.legend(loc='lower center', bbox_to_anchor=(0.5, 1.1), frameon=True, 
                      ncol=2, labelspacing=0.4, handlelength=1.2, handleheight=0.8,
                      columnspacing=0.8, borderaxespad=0.0)
    ax.grid(True, axis='x', linestyle='--', alpha=0.3)
    
    plt.tight_layout()
    
    if False:
        # Align axes width with legend width
        fig.canvas.draw()
        legend_bbox = legend.get_window_extent().transformed(fig.transFigure.inverted())
        ax_bbox = ax.get_position()
        
        # Adjust axes to match legend width
        new_left = legend_bbox.x0
        new_right = legend_bbox.x1
        ax.set_position([new_left, ax_bbox.y0, new_right - new_left, ax_bbox.height])
        
    # Resolve output path
    if args.output is None:
        output_dir = fuzzgpu_dir / 'results' / 'plots'
        output_dir.mkdir(parents=True, exist_ok=True)
        output_prefix = output_dir / 'exit_root_cause'
    else:
        output_prefix = Path(args.output)
        output_prefix.parent.mkdir(parents=True, exist_ok=True)
    
    # Save both PDF and PNG
    out_pdf = str(output_prefix) + '.pdf'
    out_png = str(output_prefix) + '.png'
    plt.savefig(out_pdf, format='pdf', dpi=300, bbox_inches='tight')
    plt.savefig(out_png, format='png', dpi=300, bbox_inches='tight')
    
    print(f"\nPlots saved to {out_pdf} and {out_png}")
    
    # Print summary statistics
    print("\nSummary:")
    for method_key, method_label in methods:
        method_data = data[method_label]
        total = method_data['total']
        success = method_data['success']
        success_rate = (success / total * 100) if total > 0 else 0
        print(f"{method_label}: {success}/{total} tests passed ({success_rate:.1f}%)")

if __name__ == '__main__':
    main()
