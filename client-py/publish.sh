#!/bin/bash
set -e

echo "🚀 Publishing Queen MQ Python Client to PyPI"
echo "============================================"
echo ""

# Check we're in the right directory
if [ ! -f "pyproject.toml" ]; then
    echo "❌ Error: Must run from client-py directory"
    exit 1
fi

# Check tools are installed
if ! command -v python &> /dev/null; then
    echo "❌ Python not found"
    exit 1
fi

if ! python -c "import build" 2>/dev/null; then
    echo "❌ 'build' not installed. Run: pip install build"
    exit 1
fi

if ! python -c "import twine" 2>/dev/null; then
    echo "❌ 'twine' not installed. Run: pip install twine"
    exit 1
fi

# Check tests pass
echo "📋 Running tests..."
if pytest tests/ -q; then
    echo "✅ Tests passed!"
else
    echo "❌ Tests failed! Fix tests before publishing."
    exit 1
fi
echo ""

# Get version
VERSION=$(python -c "from queen import __version__; print(__version__)")
echo "📦 Package version: $VERSION"
echo ""

# Confirm
read -p "Publish version $VERSION to PyPI? (y/N) " -n 1 -r
echo
if [[ ! $REPLY =~ ^[Yy]$ ]]; then
    echo "❌ Cancelled"
    exit 1
fi

# Clean
echo "🧹 Cleaning old builds..."
rm -rf dist/ build/ *.egg-info queen.egg-info

# Build
echo "🔨 Building package..."
if python -m build; then
    echo "✅ Build successful!"
else
    echo "❌ Build failed!"
    exit 1
fi
echo ""

# Show contents
echo "📦 Package contents:"
ls -lh dist/
echo ""

# Test PyPI first
read -p "Upload to Test PyPI first? (Y/n) " -n 1 -r
echo
if [[ ! $REPLY =~ ^[Nn]$ ]]; then
    echo "📤 Uploading to Test PyPI..."
    if twine upload --repository testpypi dist/*; then
        echo "✅ Test PyPI upload successful!"
        echo "🔗 https://test.pypi.org/project/queen-mq/$VERSION/"
        echo ""
        
        read -p "Test installation worked? Continue to production? (y/N) " -n 1 -r
        echo
        if [[ ! $REPLY =~ ^[Yy]$ ]]; then
            echo "❌ Cancelled"
            exit 1
        fi
    else
        echo "❌ Test PyPI upload failed!"
        exit 1
    fi
fi

# Production PyPI
echo "📤 Uploading to Production PyPI..."
if twine upload dist/*; then
    echo ""
    echo "✅ Successfully published to PyPI!"
    echo "🔗 https://pypi.org/project/queen-mq/$VERSION/"
    echo ""
    echo "📝 Next steps:"
    echo "  1. Wait 1-2 minutes for PyPI to index"
    echo "  2. Test: pip install queen-mq"
    echo "  3. Create GitHub release (git tag v$VERSION-py)"
    echo "  4. Update documentation"
    echo "  5. Announce on social media"
    echo ""
    echo "🎉 Done!"
else
    echo "❌ PyPI upload failed!"
    exit 1
fi

