# Publishing to PyPI - TL;DR

## Quick Start (First Time)

### 1. Setup (One-Time)

```bash
# Install tools
pip install build twine

# Create PyPI account
# Go to: https://pypi.org/account/register/

# Create API token
# Go to: https://pypi.org/manage/account/token/
# Copy token (starts with pypi-)

# Save token
echo "[pypi]
username = __token__
password = pypi-YOUR-TOKEN-HERE" > ~/.pypirc
chmod 600 ~/.pypirc
```

### 2. Publish (Every Release)

```bash
cd /Users/alice/Work/queen/client-py

# Run automated script
./publish.sh
```

That's it! The script will:
- ✅ Run tests
- ✅ Build package
- ✅ Upload to Test PyPI (optional)
- ✅ Upload to Production PyPI
- ✅ Verify everything

---

## Manual Process (If Needed)

```bash
# 1. Test
pytest tests/

# 2. Build
rm -rf dist/ && python -m build

# 3. Upload
twine upload dist/*

# 4. Verify
pip install queen-mq
```

---

## Update Version

Before publishing a new version:

```bash
# Edit version in 2 places:
# 1. pyproject.toml: version = "0.7.5"
# 2. queen/__init__.py: __version__ = "0.7.5"

# Then run:
./publish.sh
```

---

## Troubleshooting

**"File already exists"**
→ Increment version number and rebuild

**"Authentication failed"**  
→ Check `~/.pypirc` has correct token

**"Package not found"**
→ Wait 1-2 minutes after upload

---

## Resources

- **Your Package:** https://pypi.org/project/queen-mq/
- **Full Guide:** See PUBLISHING_GUIDE.md
- **PyPI Help:** https://pypi.org/help/

---

## After Publishing

```bash
# Anyone can now install with:
pip install queen-mq

# And use with:
from queen import Queen
```

🎉 **That's it!**

