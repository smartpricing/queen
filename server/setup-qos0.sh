#!/bin/bash

# Setup script for QoS 0 file buffer
# NOTE: This script is OPTIONAL - the server automatically creates directories on startup

echo "🚀 Queen QoS 0 File Buffer Setup (Optional)"
echo "============================================"
echo ""
echo "⚠️  This script is optional - the server auto-creates directories on startup"
echo ""

# Detect platform and set default directory
if [[ "$OSTYPE" == "darwin"* ]]; then
    # macOS
    DEFAULT_DIR="/tmp/queen"
    echo "Platform: macOS"
else
    # Linux
    DEFAULT_DIR="/var/lib/queen/buffers"
    echo "Platform: Linux"
fi

# Allow override via environment variable
BUFFER_DIR="${FILE_BUFFER_DIR:-$DEFAULT_DIR}"

echo "Buffer directory: $BUFFER_DIR"
echo ""

# Create buffer directory
echo "Creating buffer directory..."
if [[ "$BUFFER_DIR" == /tmp/* ]]; then
    # /tmp directory - no sudo needed
    mkdir -p "$BUFFER_DIR"
    mkdir -p "$BUFFER_DIR/failed"
else
    # System directory - use sudo
    sudo mkdir -p "$BUFFER_DIR"
    sudo mkdir -p "$BUFFER_DIR/failed"
    
    # Set permissions
    echo "Setting permissions..."
    sudo chown -R $USER:$USER "$BUFFER_DIR"
fi

chmod 755 "$BUFFER_DIR"
chmod 755 "$BUFFER_DIR/failed"

echo ""
echo "✅ Setup complete!"
echo ""
echo "Directory structure:"
ls -lh "$BUFFER_DIR"
echo ""
echo "You can now start the server:"
echo "  ./bin/queen-server"
echo ""
echo "Or set custom directory:"
echo "  FILE_BUFFER_DIR=/custom/path ./bin/queen-server"
echo ""
echo "Monitor buffer files with:"
echo "  watch -n 1 'ls -lh $BUFFER_DIR/'"
echo ""
echo "Check buffer stats:"
echo "  curl http://localhost:6632/api/v1/status/buffers"

