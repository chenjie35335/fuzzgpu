#!/bin/bash

# Default values
ARCH=ventus
XLEN=32
CMAKE_BUILD_TYPE=Release
ENABLE_EMU_DEBUG=OFF
ENABLE_ADDRESS_SANITIZER=OFF
ENABLE_COVERAGE=OFF
ENABLE_DOCS=${ENABLE_DOCS:-OFF}
DOCS_ONLY=OFF
DOCS_OUTPUT_DIR=docs

# Coverage type: none, line, toggle, expr
COV_TYPE=${COV_TYPE:-none}
# Difftest: 0 or 1
DFT_ENABLED=${DFT_ENABLED:-0}

# Parse command line arguments
while [[ $# -gt 0 ]]; do
  case $1 in
    --cov-line)
      COV_TYPE=line
      shift
      ;;
    --cov-toggle)
      COV_TYPE=toggle
      shift
      ;;
    --cov-expr)
      COV_TYPE=expr
      shift
      ;;
    --cov-none)
      COV_TYPE=none
      shift
      ;;
    --dft)
      DFT_ENABLED=1
      shift
      ;;
    --no-dft)
      DFT_ENABLED=0
      shift
      ;;
    --debug)
      CMAKE_BUILD_TYPE=Debug
      ENABLE_EMU_DEBUG=ON
      shift
      ;;
    --asan)
      ENABLE_ADDRESS_SANITIZER=ON
      shift
      ;;
    --docs)
      ENABLE_DOCS=ON
      DOCS_ONLY=ON
      shift
      ;;
    --no-docs)
      ENABLE_DOCS=OFF
      DOCS_ONLY=OFF
      shift
      ;;
    --xlen)
      XLEN=$2
      shift 2
      ;;
    --arch)
      ARCH=$2
      shift 2
      ;;
    --all)
      # Build all configurations
      echo "Building all configurations..."

      build_config() {
        local arch=$1
        local xlen=$2
        local cov=$3
        local dft=$4
        local debug=${5:-0}
        local debug_args=()
        local debug_label=""

        if [ "$debug" = "1" ]; then
          debug_args=(--debug)
          debug_label=", debug"
        fi

        echo ""
        echo "=========================================="
        echo "Building: arch=$arch xlen=$xlen cov=$cov, dft=$dft$debug_label"
        echo "=========================================="
        COV_TYPE=$cov DFT_ENABLED=$dft "$0" --arch "$arch" --xlen "$xlen" "${debug_args[@]}"
        if [ $? -ne 0 ]; then
          echo "ERROR: Build failed for arch=$arch xlen=$xlen cov=$cov, dft=$dft$debug_label"
          exit 1
        fi
      }
      
      # Define all arch-xlen combinations
      ARCH_XLEN_COMBINATIONS=(
        #"vortex 32"
        "vortex 64"
        "ventus 32"
      )
      
      # Iterate over all arch-xlen combinations
      for combo in "${ARCH_XLEN_COMBINATIONS[@]}"; do
        read -r arch xlen <<< "$combo"
        
        echo ""
        echo "########################################## "
        echo "# Processing: arch=$arch xlen=$xlen"
        echo "########################################## "
        
        # COV_TYPE=none with DFT_ENABLED=0 and 1
        for dft in 0 1; do
          build_config "$arch" "$xlen" none "$dft"
        done
        
        # COV_TYPE=line, toggle, and expr with DFT_ENABLED=1
        for cov in line toggle expr; do
          build_config "$arch" "$xlen" "$cov" 1
        done

        # Throughput scripts can compare covered DUT runs with and without DFT.
        for cov in line toggle; do
          build_config "$arch" "$xlen" "$cov" 0
        done

        if [ "$arch" = "vortex" ]; then
          for dft in 0 1; do
            build_config "$arch" "$xlen" none "$dft" 1
          done
        fi
      done
      
      exit 0
      ;;
    --help|-h)
      echo "Usage: $0 [options]"
      echo "Options:"
      echo "  --cov-none         No coverage (default)"
      echo "  --cov-line         Enable line coverage"
      echo "  --cov-toggle       Enable toggle coverage"
      echo "  --cov-expr         Enable expression coverage"
      echo "  --dft              Enable difftest"
      echo "  --no-dft           Disable difftest (default)"
      echo "  --debug            Debug build in a separate *-debug directory"
      echo "  --asan             Enable address sanitizer"
      echo "  --docs             Build only Doxygen docs into docs/html"
      echo "  --no-docs          Disable Doxygen documentation (default)"
      echo "  --xlen <32|64>     Set XLEN (default: 32)"
      echo "  --arch <ventus|vortex>  Set architecture (default: ventus)"
      echo "  --all              Build all configurations"
      echo "  --help, -h         Show this help"
      echo ""
      echo "Environment variables:"
      echo "  COV_TYPE=<none|line|toggle|expr>"
      echo "  DFT_ENABLED=<0|1>"
      echo "  ENABLE_DOCS=<ON|OFF>  Add docs to the normal build; use --docs for docs/html only"
      exit 0
      ;;
    *)
      echo "Unknown option: $1"
      echo "Use --help for usage information"
      exit 1
      ;;
  esac
done

# Set coverage CMake flags based on COV_TYPE
case $COV_TYPE in
  line)
    ENABLE_EMU_COVERAGE_LINE=ON
    ENABLE_EMU_COVERAGE_TOGGLE=OFF
    ENABLE_EMU_COVERAGE_EXPR=OFF
    ;;
  toggle)
    ENABLE_EMU_COVERAGE_LINE=OFF
    ENABLE_EMU_COVERAGE_TOGGLE=ON
    ENABLE_EMU_COVERAGE_EXPR=OFF
    ;;
  expr)
    ENABLE_EMU_COVERAGE_LINE=OFF
    ENABLE_EMU_COVERAGE_TOGGLE=OFF
    ENABLE_EMU_COVERAGE_EXPR=ON
    ;;
  none|*)
    ENABLE_EMU_COVERAGE_LINE=OFF
    ENABLE_EMU_COVERAGE_TOGGLE=OFF
    ENABLE_EMU_COVERAGE_EXPR=OFF
    ;;
esac

# Set difftest CMake flag
if [ "$DFT_ENABLED" = "1" ]; then
  ENABLE_DIFFTEST=ON
else
  ENABLE_DIFFTEST=OFF
fi

# Construct build directory name. Keep the release directory name stable because
# performance scripts reference it directly.
if [ "$DOCS_ONLY" = "ON" ]; then
  BUILD_DIR="build-docs"
else
  BUILD_DIR="build${XLEN}-${ARCH}-cov${COV_TYPE}-dft${DFT_ENABLED}"
  if [ "$CMAKE_BUILD_TYPE" = "Debug" ]; then
    BUILD_DIR="${BUILD_DIR}-debug"
  fi
fi

echo "=========================================="
echo "Configuration:"
echo "  Architecture: $ARCH"
echo "  XLEN: $XLEN"
echo "  Build Type: $CMAKE_BUILD_TYPE"
echo "  Coverage: $COV_TYPE"
echo "  Difftest: $DFT_ENABLED"
echo "  Docs: $ENABLE_DOCS"
echo "  Docs Only: $DOCS_ONLY"
if [ "$DOCS_ONLY" = "ON" ]; then
  echo "  Docs Output Dir: $DOCS_OUTPUT_DIR/html"
fi
echo "  Build Dir: $BUILD_DIR"
echo "=========================================="

run_make_with_verilator_retry() {
  local log_file="build.log"
  local attempt=1
  local max_attempts=10
  local rc=0
  local fault_pattern="%Error"

  while [ "$attempt" -le "$max_attempts" ]; do
    if [ "$attempt" -gt 1 ]; then
      echo ""
      echo "Retrying build after %Error in build log (attempt $attempt/$max_attempts)..."
    fi

    set +e
    make -j"$(nproc)" 2>&1 | tee "$log_file"
    rc=${PIPESTATUS[0]}
    set -e

    if [ "$rc" -eq 0 ]; then
      return 0
    fi

    # Verilator has some upstream unknown internal bugs that can pass on retry.
    if grep -Eq "$fault_pattern" "$log_file" && [ "$attempt" -lt "$max_attempts" ]; then
      attempt=$((attempt + 1))
      continue
    fi

    return "$rc"
  done
}

# Create build directory
set +e
mkdir -p "$BUILD_DIR"
set -e

# Configure and build
cd "$BUILD_DIR"
DOXYGEN_OUTPUT_ARG=""
if [ "$DOCS_ONLY" = "ON" ]; then
  DOXYGEN_OUTPUT_ARG="-DDOXYGEN_OUTPUT_DIR=$(pwd)/../$DOCS_OUTPUT_DIR"
fi

cmake .. \
  -DARCH=$ARCH \
  -DXLEN=$XLEN \
  -DCMAKE_BUILD_TYPE=$CMAKE_BUILD_TYPE \
  -DENABLE_COVERAGE=$ENABLE_COVERAGE \
  -DENABLE_EMU_DEBUG=$ENABLE_EMU_DEBUG \
  -DENABLE_EMU_COVERAGE_LINE=$ENABLE_EMU_COVERAGE_LINE \
  -DENABLE_EMU_COVERAGE_TOGGLE=$ENABLE_EMU_COVERAGE_TOGGLE \
  -DENABLE_EMU_COVERAGE_EXPR=$ENABLE_EMU_COVERAGE_EXPR \
  -DENABLE_DIFFTEST=$ENABLE_DIFFTEST \
  -DENABLE_ADDRESS_SANITIZER=$ENABLE_ADDRESS_SANITIZER \
  $DOXYGEN_OUTPUT_ARG \
  -DENABLE_DOCS=$ENABLE_DOCS

if [ "$DOCS_ONLY" = "ON" ]; then
  make docs
else
  run_make_with_verilator_retry
fi

echo ""
echo "=========================================="
echo "Build complete: $BUILD_DIR"
echo "=========================================="
