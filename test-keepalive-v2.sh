#!/bin/bash

# Better connection monitoring for macOS
echo "Monitoring HTTP connections to Queen servers..."
echo "Press Ctrl+C to stop"
echo ""

while true; do
  # Use lsof instead of netstat for more reliable results on macOS
  CONN_6632=$(lsof -nP -iTCP:6632 -sTCP:ESTABLISHED 2>/dev/null | grep -v COMMAND | wc -l | tr -d ' ')
  CONN_6633=$(lsof -nP -iTCP:6633 -sTCP:ESTABLISHED 2>/dev/null | grep -v COMMAND | wc -l | tr -d ' ')
  
  # Also check all states to see if connections exist at all
  ALL_6632=$(lsof -nP -iTCP:6632 2>/dev/null | grep -v COMMAND | wc -l | tr -d ' ')
  ALL_6633=$(lsof -nP -iTCP:6633 2>/dev/null | grep -v COMMAND | wc -l | tr -d ' ')
  
  # Clear line and print
  echo -ne "\r[6632] EST: $CONN_6632 (Total: $ALL_6632) | [6633] EST: $CONN_6633 (Total: $ALL_6633)    "
  
  sleep 0.5  # Check twice per second
done

