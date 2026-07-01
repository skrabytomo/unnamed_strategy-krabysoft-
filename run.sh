#!/usr/bin/env bash
set -e
git pull
cmake --build build -j4
exec build/bin/unnamed_strategy
