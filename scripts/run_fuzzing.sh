#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PRJ_HOME=$(realpath "$SCRIPT_DIR/..")
cd "$SCRIPT_DIR"
TARGET_INSTR_COUNT=100000000000 # basically unlimited
TARGET_TIME=300
JOBS=3
SEED_MULTIPLIER=1000000 # Large multiplier to avoid seed overlap between runs
DIVEFUZZ_MUTATION_TIMES=3

# Start seed indices to scan (1-10)
START_SEED_INDICES=(1 2 3 4 5 6 7 8 9 10)

# Configuration combinations: vortex/32, ventus/32, vortex/64
ARCH_XLEN_CONFIGS=(
    #"vortex:32"
    "ventus:32"
    "vortex:64"
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
    
    echo "=========================================="
    echo "Running: ARCH=$ARCH, XLEN=$XLEN"
    echo "=========================================="
    
    # Iterate through start seed indices
    for SEED_IDX in "${START_SEED_INDICES[@]}"; do
        START_SEED=$((SEED_IDX * SEED_MULTIPLIER))
        
        echo "  ========================================"
        echo "  Start Seed Index: $SEED_IDX (actual seed: $START_SEED)"
        echo "  ========================================"
        
        # Run three methods
        for METHOD in "${METHODS[@]}"; do
            echo "----------------------------------------"
            echo "Method: $METHOD, Start Seed: $START_SEED"
            echo "----------------------------------------"

            python fuzzing_loop.py \
                --method $METHOD \
                --arch $ARCH \
                --xlen $XLEN \
                --jobs $JOBS \
                --target-instr-count $TARGET_INSTR_COUNT \
                --target-time $TARGET_TIME \
                --start-seed $START_SEED \
                --num-genblocks $NUM_GENBLOCKS \
                --num-genwarps-per-block $NUM_GENWARPS_PER_BLOCK
        done
    done
done

echo "=========================================="
echo "All fuzzing runs completed!"
echo "=========================================="
