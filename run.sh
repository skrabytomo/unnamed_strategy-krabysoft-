#!/usr/bin/env bash
# Pull (non-fatal), build, run. Log capture/push is best-effort and never
# blocks building or running the game.

# Pull latest — don't abort the build if pull fails (offline / auth / diverged).
git pull --no-edit || echo "(git pull skipped/failed — building current tree)"

# Configure the build dir if missing (e.g. after rm -rf build).
if [ ! -d build ]; then
    cmake -B build -G Ninja || { echo "CMake configure failed"; exit 1; }
fi

# Build — this is the only step that SHOULD stop the script on failure.
cmake --build build -j4 || { echo "Build failed"; exit 1; }

# Run, capturing output to session.log (shown live via tee).
build/bin/unnamed_strategy 2>&1 | tee session.log

# Best-effort log push — never fails the script.
if [ -s session.log ]; then
    git add session.log 2>/dev/null
    git commit -m "session log $(date '+%Y-%m-%d %H:%M')" 2>/dev/null || true
    git push 2>/dev/null || echo "(log not pushed — configure git credentials to enable)"
fi
