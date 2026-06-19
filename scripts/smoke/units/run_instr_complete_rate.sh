#!/usr/bin/env bash
set -u

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
SMOKE_DIR=$(cd "$SCRIPT_DIR/.." && pwd)
PRJ_HOME=$(cd "$SMOKE_DIR/../.." && pwd)

# shellcheck source=../lib.sh
source "$SMOKE_DIR/lib.sh"

UNIT_NAME="run_instr_complete_rate"
RUN_SCRIPT="$PRJ_HOME/scripts/run_instr_complete_rate.sh"
INSTR_SCRIPT="$PRJ_HOME/scripts/instr_completion_rate.py"
DIVEFUZZ_WRAPPER="$PRJ_HOME/scripts/divefuzz_wrapper.py"
CASCADE_GEN="$PRJ_HOME/baseline/cascade-meta/fuzzer/do_genmanyelfs.py"
CASCADE_ENV="$PRJ_HOME/baseline/cascade-meta/env.sh"
DIVEFUZZ_DIR="$PRJ_HOME/baseline/DiveFuzz"

# Original instruction-completion coverage, except vortex:32 is intentionally
# excluded per current request.
SMOKE_ARCH_XLEN_CONFIGS=${SMOKE_ARCH_XLEN_CONFIGS:-"ventus:32 vortex:64"}
SMOKE_METHODS=${SMOKE_METHODS:-"fuzzgpu cascade divefuzz"}

target_name() {
  local arch=$1
  local xlen=$2
  local method=$3
  printf '%s/%s/%s/%s' "$UNIT_NAME" "$method" "$arch" "$xlen"
}

build_dir_for() {
  local arch=$1
  local xlen=$2
  local build_dir="$PRJ_HOME/build${xlen}-${arch}-covnone-dft0"

  # scripts/instr_completion_rate.py appends -debug for vortex when --emulate
  # is used by scripts/run_instr_complete_rate.sh.
  if [ "$arch" = "vortex" ]; then
    build_dir="${build_dir}-debug"
  fi

  printf '%s' "$build_dir"
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

check_method_dependencies() {
  local target=$1
  local arch=$2
  local xlen=$3
  local method=$4
  local build_dir=$5

  case "$method" in
    fuzzgpu)
      smoke_require_executable "$target" "$build_dir/fuzz_gpu" "fuzzgpu generator"
      smoke_require_text "$target" "$INSTR_SCRIPT" "'--max-num-fuzzing-instrs-per-bb'" "fuzzgpu max-instrs config arg"
      smoke_require_text "$target" "$INSTR_SCRIPT" "'--num-genwarps-per-block'" "fuzzgpu generated-warp config arg"
      ;;
    cascade)
      smoke_require_file "$target" "$CASCADE_GEN" "cascade generator"
      smoke_require_file "$target" "$CASCADE_ENV" "cascade environment script"
      if [ -n "${CASCADE_ENV_SOURCED:-}" ]; then
        smoke_pass "$target" "CASCADE_ENV_SOURCED is set"
      else
        smoke_fail "$target" "CASCADE_ENV_SOURCED is not set; source scripts/baseline_envs.sh before running cascade"
      fi

      if [ "$arch" = "vortex" ]; then
        smoke_require_text "$target" "$CASCADE_GEN" "choices=['vortex32', 'vortex64', 'ventus']" "cascade vortex64 arch choice"
        smoke_require_text "$target" "$INSTR_SCRIPT" "design_name = f\"{args.arch}{args.xlen}\"" "cascade vortex design-name mapping"
      else
        smoke_require_text "$target" "$CASCADE_GEN" "choices=['vortex32', 'vortex64', 'ventus']" "cascade ventus arch choice"
      fi
      ;;
    divefuzz)
      smoke_require_dir "$target" "$DIVEFUZZ_DIR" "DiveFuzz checkout"
      smoke_require_file "$target" "$DIVEFUZZ_WRAPPER" "DiveFuzz wrapper"
      smoke_require_text "$target" "$DIVEFUZZ_WRAPPER" "allowed_ext_name = 'ventus'" "DiveFuzz ventus extension config"
      smoke_require_text "$target" "$DIVEFUZZ_WRAPPER" "allowed_ext_name = 'vortex32' if is_rv32 else 'vortex64'" "DiveFuzz vortex XLEN extension config"
      smoke_require_text "$target" "$DIVEFUZZ_WRAPPER" "bug_filter.load_generator_filter_json" "DiveFuzz bug-filter config"
      ;;
    *)
      smoke_fail "$target" "unknown method: $method"
      ;;
  esac

  if [ "$arch" = "vortex" ]; then
    smoke_require_text "$target" "$INSTR_SCRIPT" "'--vortex-max-num-barrier'" "vortex barrier-count config arg"
    smoke_require_text "$target" "$INSTR_SCRIPT" "'--vortex-max-barrier-count'" "vortex max-barrier-count config arg"
    smoke_require_text "$target" "$INSTR_SCRIPT" "'--vortex-cores-per-clusters'" "vortex cores-per-cluster config arg"
  fi

  case "$xlen" in
    32|64)
      smoke_pass "$target" "XLEN is supported: $xlen"
      ;;
    *)
      smoke_fail "$target" "unsupported XLEN: $xlen"
      ;;
  esac
}

smoke_one_target() {
  local arch=$1
  local xlen=$2
  local method=$3
  local target
  local build_dir

  target=$(target_name "$arch" "$xlen" "$method")
  build_dir=$(build_dir_for "$arch" "$xlen")

  smoke_info "$target" "checking build/config dependencies"
  smoke_require_file "$target" "$RUN_SCRIPT" "run script"
  smoke_require_file "$target" "$INSTR_SCRIPT" "instruction completion driver"
  smoke_require_dir "$target" "$build_dir" "runtime build dir"
  smoke_require_executable "$target" "$build_dir/emulator" "emulator"
  smoke_require_executable "$target" "$build_dir/get_arch_config" "arch config reader"
  check_arch_config "$target" "$arch" "$build_dir/get_arch_config"
  check_method_dependencies "$target" "$arch" "$xlen" "$method" "$build_dir"
}

run_unit() {
  smoke_banner "Smoking unit: $UNIT_NAME"
  smoke_info "$UNIT_NAME" "arch/xlen configs: $SMOKE_ARCH_XLEN_CONFIGS"
  smoke_info "$UNIT_NAME" "methods: $SMOKE_METHODS"

  for arch_xlen in $SMOKE_ARCH_XLEN_CONFIGS; do
    local arch=${arch_xlen%%:*}
    local xlen=${arch_xlen##*:}

    if [ "$arch" = "vortex" ] && [ "$xlen" = "32" ]; then
      smoke_info "$UNIT_NAME" "skipping vortex:32 by request"
      continue
    fi

    for method in $SMOKE_METHODS; do
      smoke_one_target "$arch" "$xlen" "$method"
    done
  done
}

run_unit
smoke_summary
