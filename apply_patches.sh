#!/usr/bin/env bash
# ============================================================
# Unnamed Strategy — MSYS2/Linux Patch Application Script
# Run this from your repo root (where src/ folder lives)
# ============================================================

set -e

echo "=========================================="
echo "  Unnamed Strategy Auto-Patcher"
echo "=========================================="
echo ""

# Check if Python is available
if ! command -v python3 &> /dev/null && ! command -v python &> /dev/null; then
    echo "ERROR: Python not found. Install with: pacman -S python"
    exit 1
fi

PYTHON=$(command -v python3 || command -v python)

# Check if we're in a repo
if [ ! -d ".git" ]; then
    echo "WARNING: No .git folder found. Make sure you run this from repo root."
    echo "Current dir: $(pwd)"
    read -p "Continue anyway? (y/n): " CONTINUE
    if [ "$CONTINUE" != "y" ]; then exit 1; fi
fi

# Run dry-run first
echo ""
echo "[1/3] Running DRY-RUN to preview changes..."
$PYTHON apply_patches.py --dry-run --repo .

echo ""
echo "[2/3] Dry-run complete. Review the output above."
read -p "Apply patches for real? (y/n): " CONFIRM
if [ "$CONFIRM" != "y" ]; then
    echo "Cancelled."
    exit 0
fi

# Apply for real
echo ""
echo "[3/3] Applying patches..."
$PYTHON apply_patches.py --repo .

echo ""
echo "=========================================="
echo "  Patches applied successfully!"
echo "=========================================="
echo ""
echo "Next steps:"
echo "  1. Review:  git diff"
echo "  2. Build:   cmake --build build -j$(nproc)"
echo "  3. Test:    ./build/bin/unnamed_strategy"
echo "  4. Commit:  git add -A && git commit -m 'AI fixes: early game, boats, elimination'"
echo "  5. Push:    git push origin main"
echo ""
