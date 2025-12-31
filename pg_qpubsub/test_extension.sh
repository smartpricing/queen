#!/bin/bash
# test_extension.sh - Comprehensive test suite for pg_qpubsub
#
# Usage:
#   ./test_extension.sh              # Run all tests
#   ./test_extension.sh --keep-db    # Keep test database after tests
#   ./test_extension.sh 02 03        # Run only tests 02 and 03

set -e

DB="pg_qpubsub_test"
KEEP_DB=false
SPECIFIC_TESTS=()

# Parse arguments
for arg in "$@"; do
    if [ "$arg" = "--keep-db" ]; then
        KEEP_DB=true
    else
        SPECIFIC_TESTS+=("$arg")
    fi
done

echo "============================================================"
echo "        pg_qpubsub Extension Test Suite"
echo "============================================================"
echo ""

# Build extension
echo "Building extension..."
./build.sh

echo ""
echo "Creating test database..."
PGPASSWORD="${PGPASSWORD:-postgres}" psql -U "${PGUSER:-postgres}" -h "${PGHOST:-localhost}" -d postgres -c "DROP DATABASE IF EXISTS $DB" 2>/dev/null || true
PGPASSWORD="${PGPASSWORD:-postgres}" psql -U "${PGUSER:-postgres}" -h "${PGHOST:-localhost}" -d postgres -c "CREATE DATABASE $DB"

echo "Loading pgcrypto extension..."
PGPASSWORD="${PGPASSWORD:-postgres}" psql -U "${PGUSER:-postgres}" -h "${PGHOST:-localhost}" -d $DB -c "CREATE EXTENSION IF NOT EXISTS pgcrypto"

echo "Loading pg_qpubsub extension..."
PGPASSWORD="${PGPASSWORD:-postgres}" psql -U "${PGUSER:-postgres}" -h "${PGHOST:-localhost}" -d $DB -f pg_qpubsub--1.0.sql -v ON_ERROR_STOP=1 -q 2>/dev/null

echo ""
echo "============================================================"
echo "                    Running Tests"
echo "============================================================"
echo ""

# Get list of test files
if [ ${#SPECIFIC_TESTS[@]} -gt 0 ]; then
    # Run specific tests
    TEST_FILES=()
    for num in "${SPECIFIC_TESTS[@]}"; do
        # Pad with leading zero if needed
        padded=$(printf "%02d" "$num" 2>/dev/null || echo "$num")
        for f in test/sql/${padded}*.sql test/sql/${num}*.sql; do
            if [ -f "$f" ]; then
                TEST_FILES+=("$f")
            fi
        done
    done
else
    # Run all tests in order
    TEST_FILES=(test/sql/*.sql)
fi

# Track results
PASSED=0
FAILED=0
FAILED_TESTS=()

for test_file in "${TEST_FILES[@]}"; do
    test_name=$(basename "$test_file")
    echo ">>> Running: $test_name"
    
    if PGPASSWORD="${PGPASSWORD:-postgres}" psql -U "${PGUSER:-postgres}" -h "${PGHOST:-localhost}" -d $DB -f "$test_file" -v ON_ERROR_STOP=1 2>&1; then
        echo "✓ $test_name PASSED"
        ((PASSED++))
    else
        echo "✗ $test_name FAILED"
        ((FAILED++))
        FAILED_TESTS+=("$test_name")
    fi
    echo ""
done

echo "============================================================"
echo "                    Test Summary"
echo "============================================================"
echo ""
echo "Total:  $((PASSED + FAILED))"
echo "Passed: $PASSED"
echo "Failed: $FAILED"

if [ $FAILED -gt 0 ]; then
    echo ""
    echo "Failed tests:"
    for test in "${FAILED_TESTS[@]}"; do
        echo "  - $test"
    done
fi

echo ""

# Cleanup
if [ "$KEEP_DB" = false ]; then
    echo "Cleaning up test database..."
    PGPASSWORD="${PGPASSWORD:-postgres}" psql -U "${PGUSER:-postgres}" -h "${PGHOST:-localhost}" -d postgres -c "DROP DATABASE $DB" 2>/dev/null || true
else
    echo "Test database '$DB' preserved (--keep-db)"
fi

echo ""
if [ $FAILED -eq 0 ]; then
    echo "============================================================"
    echo "              ✓ ALL TESTS PASSED! ✓"
    echo "============================================================"
    exit 0
else
    echo "============================================================"
    echo "              ✗ SOME TESTS FAILED ✗"
    echo "============================================================"
    exit 1
fi
