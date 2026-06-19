#!/bin/bash
set -e
source scripts/baseline_envs.sh

scripts/run_instr_complete_rate.sh

python3 scripts/plot_instr_completion_rate.py
