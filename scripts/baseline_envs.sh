
if [ -n "${BASH_SOURCE:-}" ]; then
    BASELINE_ENVS_SCRIPT="${BASH_SOURCE[0]}"
else
    BASELINE_ENVS_SCRIPT="$0"
fi

PRJ_HOME=$(realpath "$(dirname "$BASELINE_ENVS_SCRIPT")/..")
FUZZGPU_VENV="$HOME/envs"

if [ ! -f "$FUZZGPU_VENV/bin/activate" ]; then
    echo "error: missing fuzzgpu virtualenv: $FUZZGPU_VENV" >&2
    echo "create or move it to \$HOME/envs before sourcing this script" >&2
    return 1 2>/dev/null || exit 1
fi

OLDPWD_BASELINE_ENVS="$PWD"

cd "$PRJ_HOME/baseline/cascade-meta" || return 1 2>/dev/null || exit 1
source "$FUZZGPU_VENV/bin/activate" || return 1 2>/dev/null || exit 1

# cascade-meta/env.sh appends variables such as LD_LIBRARY_PATH that may be
# unset, so callers using set -u should source this script before enabling it.
source "$PRJ_HOME/baseline/cascade-meta/env.sh" || return 1 2>/dev/null || exit 1

export RISCV_TOOLCHAIN_ROOT="${RISCV_TOOLCHAIN_ROOT:-$HOME/tools/riscv64-gnu-toolchain}"
export PATH=$RISCV_TOOLCHAIN_ROOT/bin:$PATH

cd "$OLDPWD_BASELINE_ENVS" || return 1 2>/dev/null || exit 1

export VERILATOR_ROOT="${VERILATOR_ROOT:-$HOME/tools/verilator}"
export PATH=$VERILATOR_ROOT/bin:$PATH
