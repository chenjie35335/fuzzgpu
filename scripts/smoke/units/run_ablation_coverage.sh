#!/usr/bin/env bash
set -u

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
SMOKE_DIR=$(cd "$SCRIPT_DIR/.." && pwd)
PRJ_HOME=$(cd "$SMOKE_DIR/../.." && pwd)

# shellcheck source=../lib.sh
source "$SMOKE_DIR/lib.sh"

UNIT_NAME="run_ablation_coverage"
RUN_SCRIPT="$PRJ_HOME/scripts/run_ablation_coverage.sh"
ABLATION_COV_SCRIPT="$PRJ_HOME/scripts/ablation_coverage.py"

SMOKE_ABLATION_COVERAGE_ARCH_XLEN_CONFIGS=${SMOKE_ABLATION_COVERAGE_ARCH_XLEN_CONFIGS:-"ventus:32"}
SMOKE_ABLATION_COVERAGE_COV_TYPES=${SMOKE_ABLATION_COVERAGE_COV_TYPES:-"line toggle expr"}
SMOKE_ABLATION_COVERAGE_CONFIGS=${SMOKE_ABLATION_COVERAGE_CONFIGS:-"baseline no_emu_guide no_barrier_plan no_taint"}

target_name() {
  local arch=$1
  local xlen=$2
  local cov_type=$3
  local config=$4
  printf '%s/%s/%s/%s/cov%s' "$UNIT_NAME" "$config" "$arch" "$xlen" "$cov_type"
}

build_dir_for() {
  local arch=$1
  local xlen=$2
  local cov_type=$3
  printf '%s/build%s-%s-cov%s-dft1' "$PRJ_HOME" "$xlen" "$arch" "$cov_type"
}

arch_config_has_key() {
  local config=$1
  local key=$2
  printf '%s\n' "$config" | grep -E "^${key}=[0-9]+$" >/dev/null 2>&1
}

check_arch_config() {
  local target=$1
  local arch=$2
  local get_arch_config=$3
  local config

  if [ ! -x "$get_arch_config" ]; then
    smoke_fail "$target" "cannot read arch config because get_arch_config is missing"
    return
  fi

  if ! config=$("$get_arch_config" 2>&1); then
    smoke_fail "$target" "get_arch_config failed: $get_arch_config"
    return
  fi

  if arch_config_has_key "$config" "nr_threads_per_warp"; then
    smoke_pass "$target" "arch config has nr_threads_per_warp"
  else
    smoke_fail "$target" "arch config missing nr_threads_per_warp"
  fi

  if [ "$arch" = "vortex" ]; then
    for key in vortex_nr_warps_per_core vortex_nr_cores_per_cluster vortex_nr_total_warps num_barriers; do
      if arch_config_has_key "$config" "$key"; then
        smoke_pass "$target" "arch config has $key"
      else
        smoke_fail "$target" "arch config missing $key"
      fi
    done
  fi
}

check_common_dependencies() {
  local target=$1
  local arch=$2
  local xlen=$3
  local cov_type=$4
  local build_dir=$5

  smoke_require_file "$target" "$RUN_SCRIPT" "run script"
  smoke_require_file "$target" "$ABLATION_COV_SCRIPT" "ablation coverage driver"
  smoke_require_text "$target" "$RUN_SCRIPT" "python3 ablation_coverage.py" "ablation coverage driver invocation"
  smoke_require_text "$target" "$RUN_SCRIPT" "COV_TYPES=" "coverage matrix"
  smoke_require_text "$target" "$RUN_SCRIPT" "--force-max-num-instrs" "force-max-num-instrs forwarding"
  smoke_require_text "$target" "$RUN_SCRIPT" "--output-dir" "output-dir forwarding"
  smoke_require_text "$target" "$RUN_SCRIPT" "--work-dir" "work-dir forwarding"

  smoke_require_dir "$target" "$build_dir" "coverage build dir"
  smoke_require_executable "$target" "$build_dir/fuzz_gpu" "fuzzgpu generator"
  smoke_require_executable "$target" "$build_dir/emulator" "coverage emulator"
  smoke_require_executable "$target" "$build_dir/get_arch_config" "arch config reader"
  check_arch_config "$target" "$arch" "$build_dir/get_arch_config"

  case "$cov_type" in
    line|toggle|expr)
      smoke_pass "$target" "coverage type is supported: $cov_type"
      ;;
    *)
      smoke_fail "$target" "unsupported coverage type: $cov_type"
      ;;
  esac

  case "$xlen" in
    32|64)
      smoke_pass "$target" "XLEN is supported: $xlen"
      ;;
    *)
      smoke_fail "$target" "unsupported XLEN: $xlen"
      ;;
  esac
}

check_config_dependencies() {
  local target=$1
  local config=$2

  smoke_require_text "$target" "$ABLATION_COV_SCRIPT" "ABLATION_CONFIGS = {" "ablation config table"
  smoke_require_text "$target" "$ABLATION_COV_SCRIPT" "'baseline': []" "baseline config"
  smoke_require_text "$target" "$ABLATION_COV_SCRIPT" "'no_emu_guide': ['--disable-emu-guide']" "no_emu_guide config"
  smoke_require_text "$target" "$ABLATION_COV_SCRIPT" "'no_barrier_plan': ['--disable-barrier-plan']" "no_barrier_plan config"
  smoke_require_text "$target" "$ABLATION_COV_SCRIPT" "'no_taint': ['--disable-taint']" "no_taint config"
  smoke_require_text "$target" "$ABLATION_COV_SCRIPT" "'--num-genblocks'" "num-genblocks generator arg"
  smoke_require_text "$target" "$ABLATION_COV_SCRIPT" "'--num-genwarps-per-block'" "num-genwarps generator arg"
  smoke_require_text "$target" "$ABLATION_COV_SCRIPT" "'--max-num-instrs'" "max-num-instrs generator arg"
  smoke_require_text "$target" "$ABLATION_COV_SCRIPT" "coverage.dat" "coverage dat output"
  smoke_require_text "$target" "$ABLATION_COV_SCRIPT" "verilator_coverage" "coverage merge tool"

  case "$config" in
    baseline|no_emu_guide|no_barrier_plan|no_taint)
      smoke_pass "$target" "ablation config is supported: $config"
      ;;
    *)
      smoke_fail "$target" "unknown ablation config: $config"
      ;;
  esac
}

smoke_one_target() {
  local arch=$1
  local xlen=$2
  local cov_type=$3
  local config=$4
  local target
  local build_dir

  target=$(target_name "$arch" "$xlen" "$cov_type" "$config")
  build_dir=$(build_dir_for "$arch" "$xlen" "$cov_type")

  smoke_info "$target" "checking ablation coverage dependencies"
  check_common_dependencies "$target" "$arch" "$xlen" "$cov_type" "$build_dir"
  check_config_dependencies "$target" "$config"
}

run_unit() {
  smoke_banner "Smoking unit: $UNIT_NAME"
  smoke_info "$UNIT_NAME" "arch/xlen configs: $SMOKE_ABLATION_COVERAGE_ARCH_XLEN_CONFIGS"
  smoke_info "$UNIT_NAME" "coverage types: $SMOKE_ABLATION_COVERAGE_COV_TYPES"
  smoke_info "$UNIT_NAME" "ablation configs: $SMOKE_ABLATION_COVERAGE_CONFIGS"

  for arch_xlen in $SMOKE_ABLATION_COVERAGE_ARCH_XLEN_CONFIGS; do
    local arch=${arch_xlen%%:*}
    local xlen=${arch_xlen##*:}

    if [ "$arch" = "vortex" ] && [ "$xlen" = "32" ]; then
      smoke_info "$UNIT_NAME" "skipping vortex:32 by request"
      continue
    fi

    for cov_type in $SMOKE_ABLATION_COVERAGE_COV_TYPES; do
      for config in $SMOKE_ABLATION_COVERAGE_CONFIGS; do
        smoke_one_target "$arch" "$xlen" "$cov_type" "$config"
      done
    done
  done
}

run_unit
smoke_summary
