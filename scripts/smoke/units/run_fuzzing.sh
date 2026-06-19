#!/usr/bin/env bash
set -u

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
SMOKE_DIR=$(cd "$SCRIPT_DIR/.." && pwd)
PRJ_HOME=$(cd "$SMOKE_DIR/../.." && pwd)

# shellcheck source=../lib.sh
source "$SMOKE_DIR/lib.sh"

UNIT_NAME="run_fuzzing"
RUN_SCRIPT="$PRJ_HOME/scripts/run_fuzzing.sh"
FUZZING_SCRIPT="$PRJ_HOME/scripts/fuzzing_loop.py"
DIVEFUZZ_WRAPPER="$PRJ_HOME/scripts/divefuzz_wrapper.py"
CASCADE_GEN="$PRJ_HOME/baseline/cascade-meta/fuzzer/do_genmanyelfs.py"
CASCADE_ENV="$PRJ_HOME/baseline/cascade-meta/env.sh"
DIVEFUZZ_DIR="$PRJ_HOME/baseline/DiveFuzz"

# Cover the requested targets. run_fuzzing.sh currently comments divefuzz out,
# but this smoke unit checks all supported methods.
SMOKE_FUZZING_ARCH_XLEN_CONFIGS=${SMOKE_FUZZING_ARCH_XLEN_CONFIGS:-"ventus:32 vortex:64"}
SMOKE_FUZZING_METHODS=${SMOKE_FUZZING_METHODS:-"fuzzgpu cascade divefuzz"}

target_name() {
  local arch=$1
  local xlen=$2
  local method=$3
  printf '%s/%s/%s/%s' "$UNIT_NAME" "$method" "$arch" "$xlen"
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
  smoke_require_file "$target" "$FUZZING_SCRIPT" "fuzzing loop driver"
  smoke_require_text "$target" "$RUN_SCRIPT" "ARCH_XLEN_CONFIGS=(" "arch/xlen matrix"
  smoke_require_text "$target" "$RUN_SCRIPT" "ALL_METHODS=(" "method matrix"
  smoke_require_text "$target" "$RUN_SCRIPT" "python fuzzing_loop.py" "fuzzing loop invocation"
  smoke_require_text "$target" "$RUN_SCRIPT" "--target-instr-count" "target-instr-count forwarding"
  smoke_require_text "$target" "$RUN_SCRIPT" "--target-time" "target-time forwarding"
  smoke_require_text "$target" "$RUN_SCRIPT" "--start-seed" "start-seed forwarding"
  smoke_require_text "$target" "$RUN_SCRIPT" "--num-genblocks" "num-genblocks forwarding"
  smoke_require_text "$target" "$RUN_SCRIPT" "--num-genwarps-per-block" "num-genwarps forwarding"

  smoke_require_dir "$target" "$build_dir" "fuzzing build dir"
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

check_method_dependencies() {
  local target=$1
  local arch=$2
  local method=$3
  local build_dir=$4

  case "$method" in
    fuzzgpu)
      smoke_require_executable "$target" "$build_dir/fuzz_gpu" "fuzzgpu generator"
      smoke_require_text "$target" "$FUZZING_SCRIPT" "run_fuzzgpu_worker" "fuzzgpu worker"
      smoke_require_text "$target" "$FUZZING_SCRIPT" "'--max-num-instrs'" "fuzzgpu max-num-instrs arg"
      smoke_require_text "$target" "$FUZZING_SCRIPT" "'--instr-prob-json'" "random selection instr-prob arg"
      smoke_require_text "$target" "$FUZZING_SCRIPT" "'--disable-default-bug-filtering'" "fuzzgpu bug-filter mode"
      smoke_require_text "$target" "$FUZZING_SCRIPT" "'--bug-filter-json'" "fuzzgpu bug-filter arg"
      ;;
    cascade)
      smoke_require_file "$target" "$CASCADE_GEN" "cascade generator"
      smoke_require_file "$target" "$CASCADE_ENV" "cascade environment script"
      if [ -n "${CASCADE_ENV_SOURCED:-}" ]; then
        smoke_pass "$target" "CASCADE_ENV_SOURCED is set"
      else
        smoke_fail "$target" "CASCADE_ENV_SOURCED is not set; source scripts/baseline_envs.sh before running cascade"
      fi
      smoke_require_text "$target" "$FUZZING_SCRIPT" "gen_script = cascade_dir / 'fuzzer' / 'do_genmanyelfs.py'" "cascade generator resolution"
      smoke_require_text "$target" "$FUZZING_SCRIPT" "design_name = f\"{args.arch}{args.xlen}\"" "cascade vortex design-name mapping"
      smoke_require_text "$target" "$FUZZING_SCRIPT" "'--num-warps-per-core'" "cascade num-warps arg"
      smoke_require_text "$target" "$FUZZING_SCRIPT" "'--num-threads-per-warp'" "cascade num-threads arg"
      ;;
    divefuzz)
      smoke_require_dir "$target" "$DIVEFUZZ_DIR" "DiveFuzz checkout"
      smoke_require_file "$target" "$DIVEFUZZ_WRAPPER" "DiveFuzz wrapper"
      smoke_require_text "$target" "$FUZZING_SCRIPT" "wrapper_script = script_dir / 'divefuzz_wrapper.py'" "DiveFuzz wrapper resolution"
      smoke_require_text "$target" "$FUZZING_SCRIPT" "'--instr-number'" "DiveFuzz instr-number arg"
      smoke_require_text "$target" "$FUZZING_SCRIPT" "'--xor-cache-file'" "DiveFuzz xor-cache arg"
      smoke_require_text "$target" "$FUZZING_SCRIPT" "divefuzz_mutation_times" "DiveFuzz mutation config"
      smoke_require_text "$target" "$DIVEFUZZ_WRAPPER" "allowed_ext_name = 'ventus'" "DiveFuzz ventus extension config"
      smoke_require_text "$target" "$DIVEFUZZ_WRAPPER" "allowed_ext_name = 'vortex32' if is_rv32 else 'vortex64'" "DiveFuzz vortex XLEN extension config"
      ;;
    *)
      smoke_fail "$target" "unknown method: $method"
      ;;
  esac

  if [ "$arch" = "vortex" ]; then
    smoke_require_text "$target" "$FUZZING_SCRIPT" "'--vortex-max-num-barrier'" "vortex barrier-count config arg"
    smoke_require_text "$target" "$FUZZING_SCRIPT" "'--vortex-max-barrier-count'" "vortex max-barrier-count config arg"
    if [ "$method" = "divefuzz" ]; then
      smoke_require_text "$target" "$FUZZING_SCRIPT" "'--vortex-cores-per-clusters'" "vortex cores-per-cluster config arg"
    fi
  fi
}

smoke_one_target() {
  local arch=$1
  local xlen=$2
  local method=$3
  local target
  local build_dir

  target=$(target_name "$arch" "$xlen" "$method")
  build_dir=$(build_dir_for "$arch" "$xlen")

  smoke_info "$target" "checking fuzzing loop dependencies"
  check_common_dependencies "$target" "$arch" "$xlen" "$build_dir"
  check_method_dependencies "$target" "$arch" "$method" "$build_dir"
}

run_unit() {
  smoke_banner "Smoking unit: $UNIT_NAME"
  smoke_info "$UNIT_NAME" "arch/xlen configs: $SMOKE_FUZZING_ARCH_XLEN_CONFIGS"
  smoke_info "$UNIT_NAME" "methods: $SMOKE_FUZZING_METHODS"

  for arch_xlen in $SMOKE_FUZZING_ARCH_XLEN_CONFIGS; do
    local arch=${arch_xlen%%:*}
    local xlen=${arch_xlen##*:}

    if [ "$arch" = "vortex" ] && [ "$xlen" = "32" ]; then
      smoke_info "$UNIT_NAME" "skipping vortex:32 by request"
      continue
    fi

    for method in $SMOKE_FUZZING_METHODS; do
      smoke_one_target "$arch" "$xlen" "$method"
    done
  done
}

run_unit
smoke_summary
