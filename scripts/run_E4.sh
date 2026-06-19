#!/bin/bash
set -e
source scripts/baseline_envs.sh

scripts/run_baseline.sh

python3 scripts/plot_baseline.py --results-dir results/baseline_results
