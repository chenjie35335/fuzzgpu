#!/usr/bin/env bash
# Environment shared by the FuzzGPU experiment wrappers.
FUZZGPU_VENV="$HOME/envs"

if [ ! -f "$FUZZGPU_VENV/bin/activate" ]; then
    echo "error: missing fuzzgpu virtualenv: $FUZZGPU_VENV" >&2
    echo 'create or move it to $HOME/envs before sourcing this script' >&2
    return 1 2>/dev/null || exit 1
fi

source "$FUZZGPU_VENV/bin/activate" || return 1 2>/dev/null || exit 1

export RISCV_TOOLCHAIN_ROOT="${RISCV_TOOLCHAIN_ROOT:-$HOME/tools/riscv64-gnu-toolchain}"
export VERILATOR_ROOT="${VERILATOR_ROOT:-$HOME/tools/verilator}"
export PATH="$VERILATOR_ROOT/bin:$RISCV_TOOLCHAIN_ROOT/bin:$PATH"
