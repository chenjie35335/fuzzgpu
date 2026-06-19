#!/bin/bash
set -e

SOURCE_DIR=$(dirname $(readlink -f "$0"))
echo "Source Dir: $SOURCE_DIR"
echo "Project Root: $PROJECT_ROOT"

# Default values
ENABLE_EMU_DEBUG=${ENABLE_EMU_DEBUG:-OFF}
ENABLE_ADDRESS_SANITIZER=${ENABLE_ADDRESS_SANITIZER:-OFF}
ENABLE_COVERAGE=OFF

ENABLE_EMU_COVERAGE_LINE=OFF
ENABLE_EMU_COVERAGE_TOGGLE=OFF
ENABLE_EMU_COVERAGE_EXPR=OFF


ENABLE_DIFFTEST=OFF

build_variant() {
    ARCH=$1
    XLEN=$2
    BUILD_DIR="build${XLEN}-${ARCH}"

    echo "--------------------------------------------------"
    echo "Building ${ARCH} (XLEN=${XLEN})"
    echo "Output Directory: ${BUILD_DIR}"
    echo "--------------------------------------------------"

    cmake -S "${SOURCE_DIR}" -B "${BUILD_DIR}" \
        -DARCH="${ARCH}" \
        -DXLEN="${XLEN}" \
        -DCMAKE_BUILD_TYPE=Release \
        -DENABLE_COVERAGE=$ENABLE_COVERAGE \
        -DENABLE_EMU_DEBUG=$ENABLE_EMU_DEBUG \
        -DENABLE_EMU_COVERAGE_LINE=$ENABLE_EMU_COVERAGE_LINE \
        -DENABLE_EMU_COVERAGE_TOGGLE=$ENABLE_EMU_COVERAGE_TOGGLE \
        -DENABLE_EMU_COVERAGE_EXPR=$ENABLE_EMU_COVERAGE_EXPR \
        -DENABLE_DIFFTEST=$ENABLE_DIFFTEST \
        -DENABLE_ADDRESS_SANITIZER=$ENABLE_ADDRESS_SANITIZER

    cmake --build "${BUILD_DIR}" -j$(nproc)
    
    echo "Build for ${ARCH}-${XLEN} completed."
    echo ""
}

# 1. Vortex 32-bit
# Vortex32 is outside the current artifact coverage and would require the
# riscv32 toolchain under $HOME/tools.
# build_variant "vortex" 32

# 2. Vortex 64-bit
build_variant "vortex" 64

# 3. Ventus 32-bit
build_variant "ventus" 32

echo "All configurations built successfully."
