#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

python3 ablation_study.py --num-seeds 5000 --emulate --arch ventus --force-max-num-instrs 10000 
