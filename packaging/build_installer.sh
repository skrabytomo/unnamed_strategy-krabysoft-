#!/usr/bin/env bash
# One-shot: build the game, assemble a self-contained dist/ folder (exe + DLLs +
# assets), then produce the one-click installer.
#
# Requirements on the build machine:
#   - the normal msys2/ucrt64 toolchain used by run.sh
#   - Inno Setup (for the .exe installer)  OR  just use the ZIP fallback
#
# Output:
#   dist/                         self-contained portable folder (runnable as-is)
#   packaging/Output/UnnamedStrategy-Setup.exe   one-click installer (if Inno present)
set -e
cd "$(dirname "$0")/.."

echo "== 1/3  Building release =="
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4

echo "== 2/3  Assembling self-contained dist/ (exe + DLLs + assets) =="
rm -rf dist
cmake --install build --prefix dist

echo "== 3/3  Packaging =="
if command -v iscc >/dev/null 2>&1; then
    iscc packaging/installer.iss
    echo "Installer: packaging/Output/UnnamedStrategy-Setup.exe"
elif command -v ISCC.exe >/dev/null 2>&1; then
    ISCC.exe packaging/installer.iss
    echo "Installer: packaging/Output/UnnamedStrategy-Setup.exe"
else
    echo "Inno Setup (iscc) not found — making a portable ZIP instead."
    ( cd build && cpack -G ZIP )
    echo "Portable ZIP written under build/. Unzip and run ${PWD}/dist too."
fi

echo "Done. Portable folder: dist/ (double-click dist/unnamed_strategy.exe)"
