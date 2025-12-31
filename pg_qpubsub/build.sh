#!/bin/bash
# build.sh - Assemble pg_qpubsub extension from Queen schema and procedures

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SCHEMA_DIR="$SCRIPT_DIR/../lib/schema"
OUTPUT="$SCRIPT_DIR/pg_qpubsub--1.0.sql"
WRAPPERS="$SCRIPT_DIR/wrappers.sql"

echo "Building pg_qpubsub extension..."

# Verify source files exist
if [ ! -f "$SCHEMA_DIR/schema.sql" ]; then
    echo "ERROR: Cannot find $SCHEMA_DIR/schema.sql"
    exit 1
fi

# Start with header (no \quit guard for direct loading)
cat > "$OUTPUT" << 'HEADER'
-- pg_qpubsub--1.0.sql
-- High-performance message queue for PostgreSQL
-- https://github.com/nicois/queen

-- ============================================================================
-- SCHEMA AND TABLES
-- ============================================================================

HEADER

# Add schema (skip any \echo commands)
echo "-- Source: lib/schema/schema.sql" >> "$OUTPUT"
grep -v '\\echo' "$SCHEMA_DIR/schema.sql" >> "$OUTPUT" || cat "$SCHEMA_DIR/schema.sql" >> "$OUTPUT"
echo "" >> "$OUTPUT"

# Add procedures in order
echo "" >> "$OUTPUT"
echo "-- ============================================================================" >> "$OUTPUT"
echo "-- STORED PROCEDURES" >> "$OUTPUT"
echo "-- ============================================================================" >> "$OUTPUT"

for f in "$SCHEMA_DIR/procedures"/*.sql; do
    echo "" >> "$OUTPUT"
    echo "-- Source: $(basename "$f")" >> "$OUTPUT"
    grep -v '\\echo' "$f" >> "$OUTPUT" || cat "$f" >> "$OUTPUT"
done

# Add wrapper functions
if [ -f "$WRAPPERS" ]; then
    echo "" >> "$OUTPUT"
    echo "-- ============================================================================" >> "$OUTPUT"
    echo "-- SIMPLIFIED WRAPPER FUNCTIONS" >> "$OUTPUT"
    echo "-- ============================================================================" >> "$OUTPUT"
    echo "" >> "$OUTPUT"
    cat "$WRAPPERS" >> "$OUTPUT"
fi

# Add permissions
cat >> "$OUTPUT" << 'PERMISSIONS'

-- ============================================================================
-- PERMISSIONS
-- ============================================================================

GRANT USAGE ON SCHEMA queen TO PUBLIC;
GRANT SELECT, INSERT, UPDATE, DELETE ON ALL TABLES IN SCHEMA queen TO PUBLIC;
GRANT USAGE ON ALL SEQUENCES IN SCHEMA queen TO PUBLIC;
GRANT EXECUTE ON ALL FUNCTIONS IN SCHEMA queen TO PUBLIC;
PERMISSIONS

echo "Built: $OUTPUT"
echo "Lines: $(wc -l < "$OUTPUT")"

