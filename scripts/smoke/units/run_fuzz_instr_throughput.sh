#!/usr/bin/env bash
set -u

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
SMOKE_DIR=$(cd "$SCRIPT_DIR/.." && pwd)
PRJ_HOME=$(cd "$SMOKE_DIR/../.." && pwd)

# shellcheck source=../lib.sh
source "$SMOKE_DIR/lib.sh"

UNIT_NAME="run_fuzz_instr_throughput"
RUN_SCRIPT="$PRJ_HOME/scripts/run_fuzz_instr_throughput.sh"
THROUGHPUT_SCRIPT="$PRJ_HOME/scripts/fuzz_instr_throughput.py"
PLOT_SCRIPT="$PRJ_HOME/scripts/plot_fuzz_instr_throughput_results.py"
DIVEFUZZ_WRAPPER="$PRJ_HOME/scripts/divefuzz_wrapper.py"
CASCADE_GEN="$PRJ_HOME/baseline/cascade-meta/fuzzer/do_genmanyelfs.py"
CASCADE_ENV="$PRJ_HOME/baseline/cascade-meta/env.sh"
DIVEFUZZ_DIR="$PRJ_HOME/baseline/DiveFuzz"
DIVEFUZZ_OPCODE_DICT="$DIVEFUZZ_DIR/fuzzer/generator/reg_analyzer/riscv-opcodes/instr_dict.json"

# scripts/run_fuzz_instr_throughput.sh defaults to ARCH=vortex XLEN=64.
SMOKE_FUZZ_THROUGHPUT_ARCH_XLEN_CONFIGS=${SMOKE_FUZZ_THROUGHPUT_ARCH_XLEN_CONFIGS:-"vortex:64"}
SMOKE_FUZZ_THROUGHPUT_METHODS=${SMOKE_FUZZ_THROUGHPUT_METHODS:-"fuzzgpu divefuzz cascade"}
SMOKE_FUZZ_THROUGHPUT_COV_TYPE=${SMOKE_FUZZ_THROUGHPUT_COV_TYPE:-"none"}

target_name() {
  local arch=$1
  local xlen=$2
  local method=$3
  printf '%s/%s/%s/%s' "$UNIT_NAME" "$method" "$arch" "$xlen"
}

build_dir_for() {
  local arch=$1
  local xlen=$2
  local dft=$3
  printf '%s/build%s-%s-cov%s-dft%s' "$PRJ_HOME" "$xlen" "$arch" "$SMOKE_FUZZ_THROUGHPUT_COV_TYPE" "$dft"
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
  local build_dir_dft0=$4
  local build_dir_dft1=$5

  smoke_require_file "$target" "$RUN_SCRIPT" "run script"
  smoke_require_file "$target" "$THROUGHPUT_SCRIPT" "throughput driver"
  smoke_require_file "$target" "$PLOT_SCRIPT" "plot driver"
  smoke_require_text "$target" "$RUN_SCRIPT" "python3 fuzz_instr_throughput.py" "throughput driver invocation"
  smoke_require_text "$target" "$RUN_SCRIPT" "python3 plot_fuzz_instr_throughput_results.py" "plot driver invocation"
  smoke_require_text "$target" "$RUN_SCRIPT" "--summary-json" "summary-json config"

  smoke_require_dir "$target" "$build_dir_dft0" "dft0 build dir"
  smoke_require_executable "$target" "$build_dir_dft0/emulator" "dft0 emulator"
  smoke_require_dir "$target" "$build_dir_dft1" "dft1 build dir"
  smoke_require_executable "$target" "$build_dir_dft1/emulator" "dft1 emulator"
  smoke_require_executable "$target" "$build_dir_dft1/get_arch_config" "dft1 arch config reader"
  check_arch_config "$target" "$arch" "$build_dir_dft1/get_arch_config"

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
  local build_dir_dft1=$4

  case "$method" in
    fuzzgpu)
      smoke_require_executable "$target" "$build_dir_dft1/fuzz_gpu" "dft1 fuzzgpu generator"
      smoke_require_text "$target" "$THROUGHPUT_SCRIPT" "configs = num_instrs_configs" "fuzzgpu max-instr config scan"
      smoke_require_text "$target" "$THROUGHPUT_SCRIPT" "'--max-num-instrs'" "fuzzgpu max-num-instrs arg"
      smoke_require_text "$target" "$THROUGHPUT_SCRIPT" "'--num-genblocks'" "fuzzgpu num-genblocks arg"
      smoke_require_text "$target" "$THROUGHPUT_SCRIPT" "'--num-genwarps-per-block'" "fuzzgpu num-genwarps arg"
      ;;
    cascade)
      smoke_require_file "$target" "$CASCADE_GEN" "cascade generator"
      smoke_require_file "$target" "$CASCADE_ENV" "cascade environment script"
      if [ -n "${CASCADE_ENV_SOURCED:-}" ]; then
        smoke_pass "$target" "CASCADE_ENV_SOURCED is set"
      else
        smoke_fail "$target" "CASCADE_ENV_SOURCED is not set; source scripts/baseline_envs.sh before running cascade"
      fi
      smoke_require_text "$target" "$THROUGHPUT_SCRIPT" "cascade_script = cascade_dir / 'fuzzer' / 'do_genmanyelfs.py'" "cascade script resolution"
      smoke_require_text "$target" "$THROUGHPUT_SCRIPT" "'--num-instrs'" "cascade instr-number arg"
      smoke_require_text "$target" "$THROUGHPUT_SCRIPT" "design_name = f\"{arch}{xlen}\"" "cascade vortex design-name mapping"
      ;;
    divefuzz)
      smoke_require_dir "$target" "$DIVEFUZZ_DIR" "DiveFuzz checkout"
      smoke_require_file "$target" "$DIVEFUZZ_OPCODE_DICT" "DiveFuzz riscv-opcodes instruction dictionary"
      smoke_require_file "$target" "$DIVEFUZZ_WRAPPER" "DiveFuzz wrapper"
      smoke_require_text "$target" "$THROUGHPUT_SCRIPT" "wrapper_script = script_dir / 'divefuzz_wrapper.py'" "DiveFuzz wrapper resolution"
      smoke_require_text "$target" "$THROUGHPUT_SCRIPT" "'--instr-number'" "DiveFuzz instr-number arg"
      smoke_require_text "$target" "$DIVEFUZZ_WRAPPER" "allowed_ext_name = 'vortex32' if is_rv32 else 'vortex64'" "DiveFuzz vortex XLEN extension config"
      smoke_require_text "$target" "$DIVEFUZZ_WRAPPER" "allowed_ext_name = 'ventus'" "DiveFuzz ventus extension config"
      ;;
    *)
      smoke_fail "$target" "unknown method: $method"
      ;;
  esac

  if [ "$arch" = "vortex" ]; then
    smoke_require_text "$target" "$THROUGHPUT_SCRIPT" "'--vortex-max-num-barrier'" "vortex barrier-count config arg"
    smoke_require_text "$target" "$THROUGHPUT_SCRIPT" "'--vortex-max-barrier-count'" "vortex max-barrier-count config arg"
    smoke_require_text "$target" "$THROUGHPUT_SCRIPT" "'--vortex-cores-per-clusters'" "vortex cores-per-cluster config arg"
  fi
}

smoke_one_target() {
  local arch=$1
  local xlen=$2
  local method=$3
  local target
  local build_dir_dft0
  local build_dir_dft1

  target=$(target_name "$arch" "$xlen" "$method")
  build_dir_dft0=$(build_dir_for "$arch" "$xlen" 0)
  build_dir_dft1=$(build_dir_for "$arch" "$xlen" 1)

  smoke_info "$target" "checking throughput build/config dependencies"
  check_common_dependencies "$target" "$arch" "$xlen" "$build_dir_dft0" "$build_dir_dft1"
  check_method_dependencies "$target" "$arch" "$method" "$build_dir_dft1"
}

run_unit() {
  smoke_banner "Smoking unit: $UNIT_NAME"
  smoke_info "$UNIT_NAME" "arch/xlen configs: $SMOKE_FUZZ_THROUGHPUT_ARCH_XLEN_CONFIGS"
  smoke_info "$UNIT_NAME" "methods: $SMOKE_FUZZ_THROUGHPUT_METHODS"
  smoke_info "$UNIT_NAME" "coverage type: $SMOKE_FUZZ_THROUGHPUT_COV_TYPE"

  for arch_xlen in $SMOKE_FUZZ_THROUGHPUT_ARCH_XLEN_CONFIGS; do
    local arch=${arch_xlen%%:*}
    local xlen=${arch_xlen##*:}

    if [ "$arch" = "vortex" ] && [ "$xlen" = "32" ]; then
      smoke_info "$UNIT_NAME" "skipping vortex:32 by request"
      continue
    fi

    for method in $SMOKE_FUZZ_THROUGHPUT_METHODS; do
      smoke_one_target "$arch" "$xlen" "$method"
    done
  done
}

run_unit
smoke_summary
