#!/usr/bin/env bash
set -e
git pull
# Configure the build dir if it doesn't exist yet (e.g. after rm -rf build)
if [ ! -d build ]; then
    cmake -B build -G Ninja
fi
cmake --build build -j4
exec build/bin/unnamed_strategy
