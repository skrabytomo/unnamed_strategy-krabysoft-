#!/usr/bin/env bash
# Pull (non-fatal), build, run. Writes session.log locally but does NOT commit
# it — that auto-commit kept diverging the branch and dropping you into a merge
# editor. To share a log with Claude, push it manually:
#     git add -f session.log && git commit -m log && git push

# Pull latest — never abort the build if pull fails (offline / auth / diverged).
git pull --no-edit --ff-only 2>/dev/null || echo "(git pull skipped — building current tree)"

# Configure the build dir if missing (e.g. after rm -rf build).
if [ ! -d build ]; then
    cmake -B build -G Ninja || { echo "CMake configure failed"; exit 1; }
fi

# Build — the only step that stops the script on failure.
cmake --build build -j4 || { echo "Build failed"; exit 1; }

# Run, capturing output to session.log (shown live via tee, kept local only).
build/bin/unnamed_strategy 2>&1 | tee session.log
