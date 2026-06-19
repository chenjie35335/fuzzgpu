#!/bin/bash
set -e
source scripts/baseline_envs.sh

scripts/run_ablation_program_metric.sh

scripts/run_ablation_coverage.sh

python3 scripts/tab_ablation.py
