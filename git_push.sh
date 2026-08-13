#!/usr/bin/env bash
# ============================================================
# Git Push Helper — Uses your Personal Access Token
# Save your PAT in an environment variable first:
#   export GITHUB_TOKEN=ghp_xxxxxxxxxxxx
# Then run: ./git_push.sh
# ============================================================

set -e

echo "=========================================="
echo "  Git Push Helper"
echo "=========================================="

# Check for token
if [ -z "$GITHUB_TOKEN" ]; then
    echo ""
    echo "ERROR: GITHUB_TOKEN environment variable not set."
    echo ""
    echo "To create a Personal Access Token:"
    echo "  1. Go to https://github.com/settings/tokens"
    echo "  2. Click 'Generate new token (classic)'"
    echo "  3. Select 'repo' scope"
    echo "  4. Copy the token"
    echo "  5. Run: export GITHUB_TOKEN=ghp_xxxxxxxxxxxx"
    echo ""
    exit 1
fi

# Check git status
if [ -z "$(git status --porcelain)" ]; then
    echo "No changes to commit."
    exit 0
fi

echo ""
echo "Current branch: $(git branch --show-current)"
echo "Changes:"
git status --short
echo ""

read -p "Enter commit message [AI fixes: early game, boats, elimination]: " MSG
MSG=${MSG:-"AI fixes: early game, boats, elimination"}

echo ""
echo "Committing with message: $MSG"
git add -A
git commit -m "$MSG"

# Push using token via HTTPS
echo ""
echo "Pushing to origin..."
REMOTE_URL=$(git remote get-url origin)

# Convert SSH URL to HTTPS if needed
if [[ "$REMOTE_URL" == git@github.com:* ]]; then
    REPO_PATH=${REMOTE_URL#git@github.com:}
    REMOTE_URL="https://${GITHUB_TOKEN}@github.com/${REPO_PATH}"
fi

# If already HTTPS, inject token
if [[ "$REMOTE_URL" == https://github.com/* ]]; then
    REMOTE_URL="https://${GITHUB_TOKEN}@${REMOTE_URL#https://}"
fi

git push "$REMOTE_URL" $(git branch --show-current)

echo ""
echo "=========================================="
echo "  Pushed successfully!"
echo "=========================================="
