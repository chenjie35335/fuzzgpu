#!/bin/bash
set -e

if [[ -t 0 ]]; then
    RUN_TTY_STATE=$(stty -g)
    trap 'stty "$RUN_TTY_STATE" 2>/dev/null || true' EXIT
fi
source scripts/envs.sh

scripts/run_ablation_program_metric.sh

COV_TYPES=expr scripts/run_ablation_coverage.sh

python3 scripts/tab_ablation.py
