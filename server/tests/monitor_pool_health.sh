#!/bin/bash

# Pool Health Monitor Script
# Monitors Queen server pool health by analyzing logs and metrics
#
# Usage: ./monitor_pool_health.sh [log_file] [duration_seconds]
#        If log_file not provided, reads from stdin
#        If duration provided, monitors for that many seconds

set -e

LOG_FILE="${1:-/dev/stdin}"
DURATION="${2:-0}"
ALERT_THRESHOLD_WARNINGS=5
ALERT_THRESHOLD_POOL_SHRINK=20  # Percent

RED='\033[0;31m'
YELLOW='\033[1;33m'
GREEN='\033[0;32m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo "╔══════════════════════════════════════════════════════════╗"
echo "║         Queen Pool Health Monitor                        ║"
echo "╚══════════════════════════════════════════════════════════╝"
echo ""

# Counters
invalid_conn_count=0
pool_timeout_count=0
health_check_fail_count=0
max_pool_size=0
min_pool_size=999
current_pool_size=0
pool_size_changes=0

# Function to analyze a line
analyze_line() {
    local line="$1"
    
    # Check for invalid connection warnings
    if echo "$line" | grep -q "Returned invalid connection to pool"; then
        ((invalid_conn_count++))
        echo -e "${YELLOW}⚠️  Invalid connection returned to pool (count: $invalid_conn_count)${NC}"
    fi
    
    # Check for pool timeout errors
    if echo "$line" | grep -q "Database connection pool timeout"; then
        ((pool_timeout_count++))
        echo -e "${RED}❌ Pool timeout! (count: $pool_timeout_count)${NC}"
    fi
    
    # Check for health check failures
    if echo "$line" | grep -q "Health check failed"; then
        ((health_check_fail_count++))
        echo -e "${RED}💔 Health check failed! (count: $health_check_fail_count)${NC}"
    fi
    
    # Extract pool statistics
    if echo "$line" | grep -q "Pool:"; then
        # Format: "Pool: 4/5 conn (1 in use)"
        if [[ "$line" =~ Pool:\ ([0-9]+)/([0-9]+)\ conn\ \(([0-9]+)\ in\ use\) ]]; then
            local available="${BASH_REMATCH[1]}"
            local total="${BASH_REMATCH[2]}"
            local in_use="${BASH_REMATCH[3]}"
            
            if [ "$total" -gt "$max_pool_size" ]; then
                max_pool_size=$total
            fi
            
            if [ "$total" -lt "$min_pool_size" ]; then
                min_pool_size=$total
                if [ $pool_size_changes -gt 0 ] && [ "$total" -lt "$current_pool_size" ]; then
                    echo -e "${RED}📉 Pool size decreased: $current_pool_size → $total${NC}"
                fi
            fi
            
            current_pool_size=$total
            ((pool_size_changes++))
            
            # Alert if pool is exhausted
            if [ "$available" -eq 0 ]; then
                echo -e "${RED}🚨 POOL EXHAUSTED! 0/$total available, $in_use in use${NC}"
            elif [ "$available" -le 2 ]; then
                echo -e "${YELLOW}⚠️  Pool running low: $available/$total available${NC}"
            fi
        fi
    fi
    
    # Check for successful connection replacements
    if echo "$line" | grep -q "Successfully replaced invalid connection"; then
        echo -e "${GREEN}✅ Connection replacement successful${NC}"
    fi
    
    # Check for failed connection replacements
    if echo "$line" | grep -q "Failed to create replacement connection"; then
        echo -e "${RED}❌ Failed to replace connection - pool shrinking!${NC}"
    fi
}

# Monitor function
monitor_logs() {
    if [ "$DURATION" -gt 0 ]; then
        echo -e "${BLUE}📊 Monitoring for $DURATION seconds...${NC}"
        timeout "$DURATION" tail -f "$LOG_FILE" | while IFS= read -r line; do
            analyze_line "$line"
        done
    else
        while IFS= read -r line; do
            analyze_line "$line"
        done < "$LOG_FILE"
    fi
}

# Run monitoring
monitor_logs

# Print summary
echo ""
echo "╔══════════════════════════════════════════════════════════╗"
echo "║                    SUMMARY                               ║"
echo "╚══════════════════════════════════════════════════════════╝"
echo ""
echo "Invalid connections returned:    $invalid_conn_count"
echo "Pool timeout errors:              $pool_timeout_count"
echo "Health check failures:            $health_check_fail_count"
echo "Pool size range:                  $min_pool_size - $max_pool_size"
echo ""

# Health assessment
overall_health="HEALTHY"
if [ "$pool_timeout_count" -gt 0 ] || [ "$health_check_fail_count" -gt 0 ]; then
    overall_health="CRITICAL"
    echo -e "${RED}🚨 OVERALL HEALTH: $overall_health${NC}"
    exit 1
elif [ "$invalid_conn_count" -ge "$ALERT_THRESHOLD_WARNINGS" ]; then
    overall_health="WARNING"
    echo -e "${YELLOW}⚠️  OVERALL HEALTH: $overall_health${NC}"
    exit 2
else
    echo -e "${GREEN}✅ OVERALL HEALTH: $overall_health${NC}"
    exit 0
fi

