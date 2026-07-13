#!/usr/bin/env bash
set -e
git pull
# Configure the build dir if it doesn't exist yet (e.g. after rm -rf build)
if [ ! -d build ]; then
    cmake -B build -G Ninja
fi
cmake --build build -j4

# ── Run the game, capturing all output to session.log ─────────────────────────
# `exec` was replaced so we regain control after the game exits and can push
# the log. tee shows output live in the terminal AND writes it to the file.
# `|| true` keeps the script going even if the game crashes/returns non-zero,
# so a crash log still gets committed.
set +e
build/bin/unnamed_strategy 2>&1 | tee session.log
set -e

# ── Commit & push the session log after the game closes ───────────────────────
if [ -s session.log ]; then
    git add session.log
    git commit -m "session log $(date '+%Y-%m-%d %H:%M')" || true
    git push || echo "(log commit made locally; push failed — check auth)"
fi
