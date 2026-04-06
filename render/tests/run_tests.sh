#!/bin/bash
# Build and run all render library tests.
# Usage: ./run_tests.sh [test_name]
#   No args: run all tests
#   With arg: run only matching test (e.g. ./run_tests.sh meg)

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"

# Load local environment overrides (not committed)
if [ -f "$SCRIPT_DIR/.env" ]; then
    # shellcheck disable=SC1091
    set -a; source "$SCRIPT_DIR/.env"; set +a
fi

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"
cmake "$SCRIPT_DIR" -DCMAKE_BUILD_TYPE=Debug 2>&1 | tail -2
make -j8 2>&1

echo ""
echo "═══════════════════════════════════════════"
echo " Running tests"
echo "═══════════════════════════════════════════"
echo ""

FILTER="${1:-}"
TOTAL=0
PASSED=0
FAILED=0

for test_bin in test_*; do
    [ -x "$test_bin" ] || continue

    if [ -n "$FILTER" ] && [[ "$test_bin" != *"$FILTER"* ]]; then
        continue
    fi

    echo "─── $test_bin ───"
    if "./$test_bin"; then
        PASSED=$((PASSED + 1))
    else
        FAILED=$((FAILED + 1))
    fi
    TOTAL=$((TOTAL + 1))
    echo ""
done

echo "═══════════════════════════════════════════"
echo " $PASSED/$TOTAL test suites passed"
if [ $FAILED -gt 0 ]; then
    echo " $FAILED FAILED"
    exit 1
fi
echo "══════════════════════════════��════════════"
