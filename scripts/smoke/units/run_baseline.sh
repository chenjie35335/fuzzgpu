#!/usr/bin/env bash
set -u

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
SMOKE_DIR=$(cd "$SCRIPT_DIR/.." && pwd)
PRJ_HOME=$(cd "$SMOKE_DIR/../.." && pwd)

# shellcheck source=../lib.sh
source "$SMOKE_DIR/lib.sh"

UNIT_NAME="run_baseline"
RUN_SCRIPT="$PRJ_HOME/scripts/run_baseline.sh"
BASELINE_SCRIPT="$PRJ_HOME/scripts/baseline.py"
DIVEFUZZ_WRAPPER="$PRJ_HOME/scripts/divefuzz_wrapper.py"
CASCADE_GEN="$PRJ_HOME/baseline/cascade-meta/fuzzer/do_genmanyelfs.py"
CASCADE_ENV="$PRJ_HOME/baseline/cascade-meta/env.sh"
DIVEFUZZ_DIR="$PRJ_HOME/baseline/DiveFuzz"
DIVEFUZZ_OPCODE_DICT="$DIVEFUZZ_DIR/fuzzer/generator/reg_analyzer/riscv-opcodes/instr_dict.json"

# run_baseline.sh currently comments vortex:64 out, but this smoke unit covers
# the intended baseline targets requested here.
SMOKE_BASELINE_ARCH_XLEN_CONFIGS=${SMOKE_BASELINE_ARCH_XLEN_CONFIGS:-"ventus:32 vortex:64"}
SMOKE_BASELINE_METHODS=${SMOKE_BASELINE_METHODS:-"fuzzgpu cascade divefuzz"}
SMOKE_BASELINE_COV_TYPES=${SMOKE_BASELINE_COV_TYPES:-"line toggle expr"}

target_name() {
  local arch=$1
  local xlen=$2
  local cov_type=$3
  local method=$4
  printf '%s/%s/%s/%s/cov%s' "$UNIT_NAME" "$method" "$arch" "$xlen" "$cov_type"
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
  smoke_require_file "$target" "$BASELINE_SCRIPT" "baseline driver"
  smoke_require_text "$target" "$RUN_SCRIPT" "ALL_METHODS=(" "baseline method matrix"
  smoke_require_text "$target" "$RUN_SCRIPT" "COV_TYPES=(" "baseline coverage matrix"
  smoke_require_text "$target" "$RUN_SCRIPT" "python baseline.py" "baseline driver invocation"
  smoke_require_text "$target" "$RUN_SCRIPT" "--num-genblocks" "num-genblocks forwarding"
  smoke_require_text "$target" "$RUN_SCRIPT" "--num-genwarps-per-block" "num-genwarps forwarding"
  smoke_require_text "$target" "$RUN_SCRIPT" "--cov-type" "coverage forwarding"

  smoke_require_dir "$target" "$build_dir" "coverage build dir"
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

check_method_dependencies() {
  local target=$1
  local arch=$2
  local method=$3
  local build_dir=$4

  case "$method" in
    fuzzgpu)
      smoke_require_executable "$target" "$build_dir/fuzz_gpu" "fuzzgpu generator"
      smoke_require_text "$target" "$BASELINE_SCRIPT" "'--max-num-instrs'" "fuzzgpu max-num-instrs arg"
      smoke_require_text "$target" "$BASELINE_SCRIPT" "'--max-num-fuzzing-instrs-per-bb'" "fuzzgpu max-instrs-per-bb arg"
      smoke_require_text "$target" "$BASELINE_SCRIPT" "'--min-num-fuzzing-instrs-per-bb'" "fuzzgpu min-instrs-per-bb arg"
      smoke_require_text "$target" "$BASELINE_SCRIPT" "'--disable-emu-guide'" "fuzzgpu disable-emu-guide arg"
      ;;
    cascade)
      smoke_require_file "$target" "$CASCADE_GEN" "cascade generator"
      smoke_require_file "$target" "$CASCADE_ENV" "cascade environment script"
      if [ -n "${CASCADE_ENV_SOURCED:-}" ]; then
        smoke_pass "$target" "CASCADE_ENV_SOURCED is set"
      else
        smoke_fail "$target" "CASCADE_ENV_SOURCED is not set; source scripts/baseline_envs.sh before running cascade"
      fi
      smoke_require_text "$target" "$BASELINE_SCRIPT" "gen_script = cascade_dir / 'fuzzer' / 'do_genmanyelfs.py'" "cascade generator resolution"
      smoke_require_text "$target" "$BASELINE_SCRIPT" "design_name = f\"{args.arch}{args.xlen}\"" "cascade vortex design-name mapping"
      smoke_require_text "$target" "$BASELINE_SCRIPT" "'--num-warps-per-core'" "cascade num-warps arg"
      smoke_require_text "$target" "$BASELINE_SCRIPT" "'--num-threads-per-warp'" "cascade num-threads arg"
      ;;
    divefuzz)
      smoke_require_dir "$target" "$DIVEFUZZ_DIR" "DiveFuzz checkout"
      smoke_require_file "$target" "$DIVEFUZZ_OPCODE_DICT" "DiveFuzz riscv-opcodes instruction dictionary"
      smoke_require_file "$target" "$DIVEFUZZ_WRAPPER" "DiveFuzz wrapper"
      smoke_require_text "$target" "$BASELINE_SCRIPT" "wrapper_script = script_dir / 'divefuzz_wrapper.py'" "DiveFuzz wrapper resolution"
      smoke_require_text "$target" "$BASELINE_SCRIPT" "'--instr-number'" "DiveFuzz instr-number arg"
      smoke_require_text "$target" "$BASELINE_SCRIPT" "'--xor-cache-file'" "DiveFuzz xor-cache arg"
      smoke_require_text "$target" "$DIVEFUZZ_WRAPPER" "allowed_ext_name = 'ventus'" "DiveFuzz ventus extension config"
      smoke_require_text "$target" "$DIVEFUZZ_WRAPPER" "allowed_ext_name = 'vortex32' if is_rv32 else 'vortex64'" "DiveFuzz vortex XLEN extension config"
      ;;
    *)
      smoke_fail "$target" "unknown method: $method"
      ;;
  esac

  if [ "$arch" = "vortex" ]; then
    smoke_require_text "$target" "$BASELINE_SCRIPT" "'--vortex-max-num-barrier'" "vortex barrier-count config arg"
    smoke_require_text "$target" "$BASELINE_SCRIPT" "'--vortex-max-barrier-count'" "vortex max-barrier-count config arg"
    smoke_require_text "$target" "$BASELINE_SCRIPT" "'--vortex-cores-per-clusters'" "vortex cores-per-cluster config arg"
  fi
}

smoke_one_target() {
  local arch=$1
  local xlen=$2
  local cov_type=$3
  local method=$4
  local target
  local build_dir

  target=$(target_name "$arch" "$xlen" "$cov_type" "$method")
  build_dir=$(build_dir_for "$arch" "$xlen" "$cov_type")

  smoke_info "$target" "checking baseline build/config dependencies"
  check_common_dependencies "$target" "$arch" "$xlen" "$cov_type" "$build_dir"
  check_method_dependencies "$target" "$arch" "$method" "$build_dir"
}

run_unit() {
  smoke_banner "Smoking unit: $UNIT_NAME"
  smoke_info "$UNIT_NAME" "arch/xlen configs: $SMOKE_BASELINE_ARCH_XLEN_CONFIGS"
  smoke_info "$UNIT_NAME" "methods: $SMOKE_BASELINE_METHODS"
  smoke_info "$UNIT_NAME" "coverage types: $SMOKE_BASELINE_COV_TYPES"

  for arch_xlen in $SMOKE_BASELINE_ARCH_XLEN_CONFIGS; do
    local arch=${arch_xlen%%:*}
    local xlen=${arch_xlen##*:}

    if [ "$arch" = "vortex" ] && [ "$xlen" = "32" ]; then
      smoke_info "$UNIT_NAME" "skipping vortex:32 by request"
      continue
    fi

    for cov_type in $SMOKE_BASELINE_COV_TYPES; do
      for method in $SMOKE_BASELINE_METHODS; do
        smoke_one_target "$arch" "$xlen" "$cov_type" "$method"
      done
    done
  done
}

run_unit
smoke_summary
