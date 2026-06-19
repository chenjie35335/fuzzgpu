#!/bin/bash
set -e
source scripts/baseline_envs.sh

scripts/run_fuzz_instr_throughput.sh

python3 scripts/plot_fuzz_instr_throughput_results.py
