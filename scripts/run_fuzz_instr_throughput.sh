#!/bin/bash
# Run fuzz_instr_throughput.py and generate plots

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# Configuration
ARCH="${ARCH:-vortex}"
XLEN="${XLEN:-64}"
TARGET_TIME="${TARGET_TIME:-1440}"

# Output directory for results
RESULTS_DIR="$SCRIPT_DIR/../results/fuzz_instr_throughput"
mkdir -p "$RESULTS_DIR"

# Timestamp for this run
TIMESTAMP=$(date +%Y%m%dT%H%M%S)

echo "========================================"
echo "Running Fuzz Instruction Throughput Test"
echo "========================================"
echo "Architecture: $ARCH"
echo "XLEN: $XLEN"
echo "Target time per config: ${TARGET_TIME}s"
echo "Results directory: $RESULTS_DIR"
echo ""

# Run throughput test
# By default, the script runs both dft (for instr count) and non-dft (for timing) internally
echo "========================================"
echo "Running Throughput Test"
echo "========================================"
RESULT_JSON="$RESULTS_DIR/throughput_results_${METHOD}_${ARCH}_xlen${XLEN}_${TIMESTAMP}.json"
python3 fuzz_instr_throughput.py \
    --arch "$ARCH" \
    --xlen "$XLEN" \
    --target-config-time "$TARGET_TIME" \
    --summary-json "$RESULT_JSON"

echo ""
echo "Results saved to: $RESULT_JSON"
echo ""

# Plot results
echo "========================================"
echo "Plotting Results"
echo "========================================"
PLOTS_DIR="$SCRIPT_DIR/../results/plots"
mkdir -p "$PLOTS_DIR"

python3 plot_fuzz_instr_throughput_results.py \
    "$RESULT_JSON" \
    --output-dir "$PLOTS_DIR"

echo ""
echo "========================================"
echo "Done!"
echo "========================================"
echo "Results:"
echo "  JSON: $RESULT_JSON"
echo "  Plots: $PLOTS_DIR"
