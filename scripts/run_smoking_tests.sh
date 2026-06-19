#!/usr/bin/env bash

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)

# 
source "$SCRIPT_DIR/baseline_envs.sh"

set -u

SMOKE_DIR="$SCRIPT_DIR/smoke"
UNITS_DIR="$SMOKE_DIR/units"

usage() {
  cat <<'EOF'
Usage: scripts/run_smoking_tests.sh [options]

Options:
  --all           Run all smoking units (default)
  --unit <name>   Run one unit, e.g. run_instr_complete_rate
  --list          List available units
  --help, -h      Show this help

Environment overrides:
  SMOKE_ARCH_XLEN_CONFIGS="ventus:32 vortex:64"
  SMOKE_METHODS="fuzzgpu cascade divefuzz"
  NO_COLOR=1
EOF
}

list_units() {
  local unit
  for unit in "$UNITS_DIR"/*.sh; do
    [ -f "$unit" ] || continue
    basename "$unit" .sh
  done
}

print_suite_summary() {
  local total=$1
  local passed=$2
  local failed=$3
  local failed_units=$4

  echo "============================================================"
  if [ "$failed" -eq 0 ]; then
    echo ">>> SMOKING SUITE RESULT: PASS"
  else
    echo ">>> SMOKING SUITE RESULT: FAIL"
  fi
  echo ">>> units_total=$total units_pass=$passed units_fail=$failed"
  if [ -n "$failed_units" ]; then
    echo ">>> failed_units:$failed_units"
  fi
  echo "============================================================"
}

run_unit_path() {
  local unit_path=$1
  local unit_name
  unit_name=$(basename "$unit_path" .sh)

  if bash "$unit_path"; then
    SUITE_PASS_COUNT=$((SUITE_PASS_COUNT + 1))
  else
    SUITE_FAIL_COUNT=$((SUITE_FAIL_COUNT + 1))
    SUITE_FAILED_UNITS="${SUITE_FAILED_UNITS} ${unit_name}"
  fi
  SUITE_TOTAL_COUNT=$((SUITE_TOTAL_COUNT + 1))
}

SPECIFIED_UNIT=""
RUN_ALL=1
while [ "$#" -gt 0 ]; do
  case "$1" in
    --all)
      RUN_ALL=1
      SPECIFIED_UNIT=""
      shift
      ;;
    --unit)
      SPECIFIED_UNIT=$2
      RUN_ALL=0
      shift 2
      ;;
    --list)
      list_units
      exit 0
      ;;
    --help|-h)
      usage
      exit 0
      ;;
    *)
      echo "Unknown option: $1"
      usage
      exit 1
      ;;
  esac
done

SUITE_TOTAL_COUNT=0
SUITE_PASS_COUNT=0
SUITE_FAIL_COUNT=0
SUITE_FAILED_UNITS=""

if [ -n "$SPECIFIED_UNIT" ]; then
  unit_path="$UNITS_DIR/$SPECIFIED_UNIT.sh"
  if [ ! -f "$unit_path" ]; then
    echo "Unknown smoking unit: $SPECIFIED_UNIT"
    echo "Available units:"
    list_units
    exit 1
  fi
  run_unit_path "$unit_path"
  print_suite_summary "$SUITE_TOTAL_COUNT" "$SUITE_PASS_COUNT" "$SUITE_FAIL_COUNT" "$SUITE_FAILED_UNITS"
  [ "$SUITE_FAIL_COUNT" -eq 0 ]
  exit $?
fi

found_unit=0
for unit_path in "$UNITS_DIR"/*.sh; do
  [ -f "$unit_path" ] || continue
  found_unit=1
  run_unit_path "$unit_path"
done

if [ "$found_unit" -eq 0 ]; then
  echo "No smoking units found under $UNITS_DIR"
  exit 1
fi

print_suite_summary "$SUITE_TOTAL_COUNT" "$SUITE_PASS_COUNT" "$SUITE_FAIL_COUNT" "$SUITE_FAILED_UNITS"
[ "$SUITE_FAIL_COUNT" -eq 0 ]
exit $?
