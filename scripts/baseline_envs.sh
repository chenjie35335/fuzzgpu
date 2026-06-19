
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
source "$PRJ_HOME/baseline/cascade-meta/env.sh" || return 1 2>/dev/null || exit 1

# riscv64-unknown-elf-objcopy required by the cascade
export PATH=$PATH:$PRJ_HOME/baseline/DiveFuzz/env/riscv/bin/
# for vortex
export PATH=$PATH:/home/ain/tools/riscv64-gnu-toolchain/bin

cd "$OLDPWD_BASELINE_ENVS" || return 1 2>/dev/null || exit 1

export PATH=$PATH:/home/ain/fuzz_gpgpu/ventus-tools/verilator/bin
