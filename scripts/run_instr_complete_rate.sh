#!/bin/bash
set -e

PRJ_HOME=$(realpath $(dirname "$0")/..)
SCRIPT_DIR=$(dirname "$0")

# Configurable parameters
NUM_SEEDS=${NUM_SEEDS:-5000}
JOBS=${JOBS:-30}

# Configuration combinations: vortex/32, ventus/32, vortex/64
ARCH_XLEN_CONFIGS=(
    #"vortex:32"
    #"ventus:32"
    "vortex:64"
)

# Methods to run
METHODS=(
    "fuzzgpu"
    "cascade"
    "divefuzz"
)

# Iterate through all configuration combinations
for arch_xlen in "${ARCH_XLEN_CONFIGS[@]}"; do
    ARCH=$(echo $arch_xlen | cut -d: -f1)
    XLEN=$(echo $arch_xlen | cut -d: -f2)
    
    echo "=========================================="
    echo "Running: ARCH=$ARCH, XLEN=$XLEN"
    echo "=========================================="
    
    # Run three methods
    for METHOD in "${METHODS[@]}"; do
        echo "----------------------------------------"
        echo "Method: $METHOD, ARCH=$ARCH, XLEN=$XLEN"
        echo "----------------------------------------"
        
        python "$SCRIPT_DIR/instr_completion_rate.py" \
            --method $METHOD \
            --arch $ARCH \
            --xlen $XLEN \
            --jobs $JOBS \
            --num-seeds $NUM_SEEDS \
            --emulate
    done
done

echo "=========================================="
echo "All instruction completion rate runs completed!"
echo "=========================================="
