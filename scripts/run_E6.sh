#!/bin/bash
set -e
source scripts/baseline_envs.sh

scripts/run_fuzzing.sh

echo ========= Table 2 =========
python3 scripts/tab_mem_rela.py

echo ========= Figure 14 =========
python3 scripts/plot_breakdown_time.py

echo ========= Figure 16 =========
python3 scripts/plot_bug_discover_time.py
