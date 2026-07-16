#!/bin/bash
set -e

if [[ -t 0 ]]; then
    RUN_TTY_STATE=$(stty -g)
    trap 'stty "$RUN_TTY_STATE" 2>/dev/null || true' EXIT
fi
source scripts/baseline_envs.sh

scripts/run_fuzz_instr_throughput.sh

python3 scripts/plot_fuzz_instr_throughput_results.py
