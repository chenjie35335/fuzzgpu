#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PRJ_HOME=$(realpath "$SCRIPT_DIR/..")
cd "$SCRIPT_DIR"
TARGET_INSTR_COUNT=100000000000 # inf
TARGET_TIME=1800 # 30 min
JOBS=3

# Configuration combinations: cross product of (vortex/32, ventus/32, vortex/64) and (line, toggle, expr)
ARCH_XLEN_CONFIGS=(
    #"vortex:32"
    "ventus:32"
    #"vortex:64"
)

COV_TYPES=(
    "line"
    "toggle"
    "expr"
)

# All available methods
ALL_METHODS=(
    "fuzzgpu"
    "cascade"
    "divefuzz"
)

# Parse command line arguments
SPECIFIED_METHOD=""
while [[ $# -gt 0 ]]; do
    case $1 in
        --method|-m)
            SPECIFIED_METHOD="$2"
            shift 2
            ;;
        --help|-h)
            echo "Usage: $0 [--method|-m <method>]"
            echo "  --method, -m  Specify a single method to run (fuzzgpu, cascade, divefuzz)"
            echo "                If not specified, all methods will be run."
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            echo "Use --help for usage information."
            exit 1
            ;;
    esac
done

# Determine which methods to run
if [ -n "$SPECIFIED_METHOD" ]; then
    # Validate the specified method
    valid_method=false
    for m in "${ALL_METHODS[@]}"; do
        if [ "$m" = "$SPECIFIED_METHOD" ]; then
            valid_method=true
            break
        fi
    done
    if [ "$valid_method" = false ]; then
        echo "Error: Invalid method '$SPECIFIED_METHOD'. Valid methods are: ${ALL_METHODS[*]}"
        exit 1
    fi
    METHODS=("$SPECIFIED_METHOD")
    echo "Running only method: $SPECIFIED_METHOD"
else
    METHODS=("${ALL_METHODS[@]}")
    echo "Running all methods: ${METHODS[*]}"
fi

# Iterate through all configuration combinations
for arch_xlen in "${ARCH_XLEN_CONFIGS[@]}"; do
    ARCH=$(echo $arch_xlen | cut -d: -f1)
    XLEN=$(echo $arch_xlen | cut -d: -f2)
    
    # Set parameters based on architecture
    if [ "$ARCH" = "ventus" ]; then
        NUM_GENBLOCKS=2
        NUM_GENWARPS_PER_BLOCK=4 # 8 allowed warps, but having writeback bugs in some cases
    elif [ "$ARCH" = "vortex" ]; then
        NUM_GENBLOCKS=4 # nr_cores
        NUM_GENWARPS_PER_BLOCK=4 # nr_warps_per_core
    else
        echo "Unsupported ARCH: $ARCH"
        continue
    fi
    
    # Iterate through all coverage types
    for COV_TYPE in "${COV_TYPES[@]}"; do
        echo "=========================================="
        echo "Running: ARCH=$ARCH, XLEN=$XLEN, COV_TYPE=$COV_TYPE"
        echo "=========================================="
        
        RESULTS_DIR="${PRJ_HOME}/results/baseline_results"
        WORK_DIR="/tmp/baseline_work"
        
        # Run selected methods
        for METHOD in "${METHODS[@]}"; do
            echo "----------------------------------------"
            echo "Method: $METHOD"
            echo "----------------------------------------"
            
            python baseline.py --arch ${ARCH} --xlen ${XLEN} --jobs $JOBS --target-instr-count $TARGET_INSTR_COUNT --target-time $TARGET_TIME --results-dir $RESULTS_DIR --work-dir $WORK_DIR --method $METHOD --num-genblocks $NUM_GENBLOCKS --num-genwarps-per-block $NUM_GENWARPS_PER_BLOCK --cov-type $COV_TYPE #--disable-emu-guide
        done
    done
done
