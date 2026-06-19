#!/bin/bash
# Run ablation study for coverage metrics (line, toggle, expr)
# This script measures RTL coverage for each ablation configuration.
# Separate from run_ablation_program_metric.sh and run_ablation_bug_discover.sh
#
# Usage:
#   ./run_ablation_coverage.sh                          # Run with defaults
#   ARCH=vortex XLEN=64 ./run_ablation_coverage.sh      # Custom arch/xlen
#   COV_TYPES="line toggle" ./run_ablation_coverage.sh  # Specific coverage types
#   CONFIGS="baseline no_emu_guide" ./run_ablation_coverage.sh  # Specific configs

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# Default values
ARCH="${ARCH:-ventus}"
XLEN="${XLEN:-32}"
NUM_SEEDS="${NUM_SEEDS:-100}"
MAX_TIME="${MAX_TIME:-}"  # Optional: time limit per config (seconds)
JOBS="${JOBS:-4}"
FORCE_MAX_NUM_INSTRS="${FORCE_MAX_NUM_INSTRS:-10000}"
OUTPUT_DIR="${OUTPUT_DIR:-../results/ablation_coverage}"
WORK_DIR="${WORK_DIR:-/tmp/ablation_coverage}"

# Coverage types to measure (space-separated)
COV_TYPES="${COV_TYPES:-line toggle expr}"

# Ablation configs to run (space-separated, empty = all)
# Available: baseline, no_emu_guide, no_barrier_plan, no_taint
CONFIGS="${CONFIGS:-}"

echo "=============================================="
echo "    Ablation Study - Coverage Metrics"
echo "=============================================="
echo "Arch: $ARCH, XLEN: $XLEN"
echo "Coverage types: $COV_TYPES"
echo "Num seeds: $NUM_SEEDS"
echo "Jobs: $JOBS"
echo "Output dir: $OUTPUT_DIR"
echo "Work dir: $WORK_DIR"
if [ -n "$MAX_TIME" ]; then
    echo "Max time per config: ${MAX_TIME}s"
fi
if [ -n "$CONFIGS" ]; then
    echo "Configs: $CONFIGS"
else
    echo "Configs: all (baseline, no_emu_guide, no_barrier_plan, no_taint)"
fi
echo ""

# Create output directory
mkdir -p "$OUTPUT_DIR"

# Run ablation_coverage.py for each coverage type
for COV_TYPE in $COV_TYPES; do
    echo ""
    echo "=============================================="
    echo "  Coverage Type: $COV_TYPE"
    echo "=============================================="
    
    # Check if build exists
    BUILD_DIR="../build${XLEN}-${ARCH}-cov${COV_TYPE}-dft1"
    if [ ! -d "$BUILD_DIR" ]; then
        echo "WARNING: Build directory not found: $BUILD_DIR"
        echo "Please build with: COV_TYPE=${COV_TYPE} DFT_ENABLED=1 ./build.sh --arch ${ARCH} --xlen ${XLEN}"
        echo "Skipping $COV_TYPE coverage..."
        continue
    fi
    
    if [ ! -x "$BUILD_DIR/emulator" ]; then
        echo "WARNING: Emulator not found in $BUILD_DIR"
        echo "Skipping $COV_TYPE coverage..."
        continue
    fi
    
    # Build command
    CMD="python3 ablation_coverage.py \
        --arch $ARCH \
        --xlen $XLEN \
        --cov-type $COV_TYPE \
        --num-seeds $NUM_SEEDS \
        --jobs $JOBS \
        --force-max-num-instrs $FORCE_MAX_NUM_INSTRS \
        --output-dir $OUTPUT_DIR \
        --work-dir $WORK_DIR"
    
    if [ -n "$MAX_TIME" ]; then
        CMD="$CMD --max-time $MAX_TIME"
    fi
    
    if [ -n "$CONFIGS" ]; then
        CMD="$CMD --configs $CONFIGS"
    fi
    
    echo "Running: $CMD"
    echo ""
    
    eval $CMD
    
    if [ $? -ne 0 ]; then
        echo "ERROR: ablation_coverage.py failed for $COV_TYPE coverage"
        # Continue with other coverage types instead of exiting
    fi
done

echo ""
echo "=============================================="
echo "    Coverage Ablation Study Complete"
echo "=============================================="
echo "Results saved to: $OUTPUT_DIR"
