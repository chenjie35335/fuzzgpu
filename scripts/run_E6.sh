#!/bin/bash
set -e

if [[ -t 0 ]]; then
    RUN_TTY_STATE=$(stty -g)
    trap 'stty "$RUN_TTY_STATE" 2>/dev/null || true' EXIT
fi
source scripts/envs.sh

scripts/run_fuzzing.sh

echo ========= Table 2 =========
python3 scripts/tab_mem_rela.py

echo ========= Figure 14 =========
python3 scripts/plot_breakdown_time.py

echo ========= Figure 16 =========
python3 scripts/plot_bug_discover_time.py
