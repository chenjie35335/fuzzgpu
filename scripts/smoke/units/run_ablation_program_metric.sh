#!/usr/bin/env bash
set -u

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
SMOKE_DIR=$(cd "$SCRIPT_DIR/.." && pwd)
PRJ_HOME=$(cd "$SMOKE_DIR/../.." && pwd)

# shellcheck source=../lib.sh
source "$SMOKE_DIR/lib.sh"

UNIT_NAME="run_ablation_program_metric"
RUN_SCRIPT="$PRJ_HOME/scripts/run_ablation_program_metric.sh"
ABLATION_SCRIPT="$PRJ_HOME/scripts/ablation_study.py"

# run_ablation_program_metric.sh currently runs ventus/32 through ablation_study.py.
SMOKE_ABLATION_PROGRAM_ARCH_XLEN_CONFIGS=${SMOKE_ABLATION_PROGRAM_ARCH_XLEN_CONFIGS:-"ventus:32"}
SMOKE_ABLATION_PROGRAM_CONFIGS=${SMOKE_ABLATION_PROGRAM_CONFIGS:-"baseline no_emu_guide no_barrier_plan no_taint"}

target_name() {
  local arch=$1
  local xlen=$2
  local config=$3
  printf '%s/%s/%s/%s' "$UNIT_NAME" "$config" "$arch" "$xlen"
}

build_dir_for() {
  local arch=$1
  local xlen=$2
  printf '%s/build%s-%s-covnone-dft1' "$PRJ_HOME" "$xlen" "$arch"
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
  local build_dir=$4

  smoke_require_file "$target" "$RUN_SCRIPT" "run script"
  smoke_require_file "$target" "$ABLATION_SCRIPT" "ablation program metric driver"
  smoke_require_text "$target" "$RUN_SCRIPT" "python3 ablation_study.py" "ablation driver invocation"
  smoke_require_text "$target" "$RUN_SCRIPT" "--num-seeds" "num-seeds forwarding"
  smoke_require_text "$target" "$RUN_SCRIPT" "--emulate" "emulate forwarding"
  smoke_require_text "$target" "$RUN_SCRIPT" "--force-max-num-instrs" "force-max-num-instrs forwarding"

  smoke_require_dir "$target" "$build_dir" "program metric build dir"
  smoke_require_executable "$target" "$build_dir/fuzz_gpu" "fuzzgpu generator"
  smoke_require_executable "$target" "$build_dir/emulator" "emulator"
  smoke_require_executable "$target" "$build_dir/get_arch_config" "arch config reader"
  check_arch_config "$target" "$arch" "$build_dir/get_arch_config"

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

  smoke_require_text "$target" "$ABLATION_SCRIPT" "ABLATION_CONFIGS = {" "ablation config table"
  smoke_require_text "$target" "$ABLATION_SCRIPT" "'baseline': []" "baseline config"
  smoke_require_text "$target" "$ABLATION_SCRIPT" "'no_emu_guide': ['--disable-emu-guide']" "no_emu_guide config"
  smoke_require_text "$target" "$ABLATION_SCRIPT" "'no_barrier_plan': ['--disable-barrier-plan']" "no_barrier_plan config"
  smoke_require_text "$target" "$ABLATION_SCRIPT" "'no_taint': ['--disable-taint']" "no_taint config"
  smoke_require_text "$target" "$ABLATION_SCRIPT" "'--num-genblocks'" "num-genblocks generator arg"
  smoke_require_text "$target" "$ABLATION_SCRIPT" "'--num-genwarps-per-block'" "num-genwarps generator arg"
  smoke_require_text "$target" "$ABLATION_SCRIPT" "'--max-num-instrs'" "max-num-instrs generator arg"
  smoke_require_text "$target" "$ABLATION_SCRIPT" "'--bug-filter-json'" "bug-filter generator arg"
  smoke_require_text "$target" "$ABLATION_SCRIPT" "isa_emulator_stats" "program metric extraction"

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
  local config=$3
  local target
  local build_dir

  target=$(target_name "$arch" "$xlen" "$config")
  build_dir=$(build_dir_for "$arch" "$xlen")

  smoke_info "$target" "checking ablation program metric dependencies"
  check_common_dependencies "$target" "$arch" "$xlen" "$build_dir"
  check_config_dependencies "$target" "$config"
}

run_unit() {
  smoke_banner "Smoking unit: $UNIT_NAME"
  smoke_info "$UNIT_NAME" "arch/xlen configs: $SMOKE_ABLATION_PROGRAM_ARCH_XLEN_CONFIGS"
  smoke_info "$UNIT_NAME" "ablation configs: $SMOKE_ABLATION_PROGRAM_CONFIGS"

  for arch_xlen in $SMOKE_ABLATION_PROGRAM_ARCH_XLEN_CONFIGS; do
    local arch=${arch_xlen%%:*}
    local xlen=${arch_xlen##*:}

    if [ "$arch" = "vortex" ] && [ "$xlen" = "32" ]; then
      smoke_info "$UNIT_NAME" "skipping vortex:32 by request"
      continue
    fi

    for config in $SMOKE_ABLATION_PROGRAM_CONFIGS; do
      smoke_one_target "$arch" "$xlen" "$config"
    done
  done
}

run_unit
smoke_summary
