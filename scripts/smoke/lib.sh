#!/usr/bin/env bash

if [ -n "${SMOKE_LIB_SH_INCLUDED:-}" ]; then
  return 0
fi
SMOKE_LIB_SH_INCLUDED=1

SMOKE_PASS_COUNT=${SMOKE_PASS_COUNT:-0}
SMOKE_FAIL_COUNT=${SMOKE_FAIL_COUNT:-0}
SMOKE_INFO_COUNT=${SMOKE_INFO_COUNT:-0}

if [ -t 1 ] && [ -z "${NO_COLOR:-}" ]; then
  SMOKE_GREEN=$'\033[32m'
  SMOKE_RED=$'\033[31m'
  SMOKE_YELLOW=$'\033[33m'
  SMOKE_BLUE=$'\033[34m'
  SMOKE_BOLD=$'\033[1m'
  SMOKE_RESET=$'\033[0m'
else
  SMOKE_GREEN=''
  SMOKE_RED=''
  SMOKE_YELLOW=''
  SMOKE_BLUE=''
  SMOKE_BOLD=''
  SMOKE_RESET=''
fi

smoke_banner() {
  local title=$1
  printf '%s\n' '============================================================'
  printf '%s%s%s\n' "$SMOKE_BOLD" "$title" "$SMOKE_RESET"
  printf '%s\n' '============================================================'
}

smoke_info() {
  local target=$1
  local message=$2
  SMOKE_INFO_COUNT=$((SMOKE_INFO_COUNT + 1))
  printf '%s>>> [INFO] %s :: %s%s\n' "$SMOKE_BLUE" "$target" "$message" "$SMOKE_RESET"
}

smoke_pass() {
  local target=$1
  local message=$2
  SMOKE_PASS_COUNT=$((SMOKE_PASS_COUNT + 1))
  printf '%s>>> [PASS] %s :: %s%s\n' "$SMOKE_GREEN" "$target" "$message" "$SMOKE_RESET"
}

smoke_fail() {
  local target=$1
  local message=$2
  SMOKE_FAIL_COUNT=$((SMOKE_FAIL_COUNT + 1))
  printf '%s>>> [FAIL] %s :: %s%s\n' "$SMOKE_RED" "$target" "$message" "$SMOKE_RESET"
}

smoke_require_file() {
  local target=$1
  local path=$2
  local label=$3

  if [ -f "$path" ]; then
    smoke_pass "$target" "$label exists: $path"
  else
    smoke_fail "$target" "$label missing: $path"
  fi
}

smoke_require_dir() {
  local target=$1
  local path=$2
  local label=$3

  if [ -d "$path" ]; then
    smoke_pass "$target" "$label exists: $path"
  else
    smoke_fail "$target" "$label missing: $path"
  fi
}

smoke_require_executable() {
  local target=$1
  local path=$2
  local label=$3

  if [ -x "$path" ]; then
    smoke_pass "$target" "$label executable: $path"
  else
    smoke_fail "$target" "$label not executable or missing: $path"
  fi
}

smoke_require_text() {
  local target=$1
  local path=$2
  local pattern=$3
  local label=$4

  if [ -f "$path" ] && grep -F -- "$pattern" "$path" >/dev/null 2>&1; then
    smoke_pass "$target" "$label present"
  else
    smoke_fail "$target" "$label missing: $pattern"
  fi
}

smoke_summary() {
  printf '%s\n' '------------------------------------------------------------'
  if [ "$SMOKE_FAIL_COUNT" -eq 0 ]; then
    printf '%s%sSMOKE RESULT: PASS%s  pass=%d fail=%d info=%d\n' \
      "$SMOKE_BOLD" "$SMOKE_GREEN" "$SMOKE_RESET" \
      "$SMOKE_PASS_COUNT" "$SMOKE_FAIL_COUNT" "$SMOKE_INFO_COUNT"
    return 0
  fi

  printf '%s%sSMOKE RESULT: FAIL%s  pass=%d fail=%d info=%d\n' \
    "$SMOKE_BOLD" "$SMOKE_RED" "$SMOKE_RESET" \
    "$SMOKE_PASS_COUNT" "$SMOKE_FAIL_COUNT" "$SMOKE_INFO_COUNT"
  return 1
}
