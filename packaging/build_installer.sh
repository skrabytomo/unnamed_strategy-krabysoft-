#!/usr/bin/env bash
# One-shot: build the game, bundle a self-contained dist/ (exe + DLLs + assets),
# then produce a one-click Setup.exe installer.
#
# For the Setup.exe you need ONE of these on the build machine:
#   - NSIS   (recommended, from msys2):  pacman -S mingw-w64-ucrt-x86_64-nsis
#   - Inno Setup (iscc on PATH):         https://jrsoftware.org/isdl.php
# If neither is present you still get a portable ZIP that runs anywhere.
#
# Output:
#   dist/                                          self-contained portable folder
#   build/UnnamedStrategy-*.exe  (NSIS)     OR     one-click installer
#   packaging/Output/UnnamedStrategy-Setup.exe (Inno, if used)
set -e
cd "$(dirname "$0")/.."

echo "== 1/3  Building release =="
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4

echo "== 2/3  Assembling self-contained dist/ (exe + DLLs + assets) =="
rm -rf dist
cmake --install build --prefix dist

echo "== 3/3  Building installer =="
if command -v makensis >/dev/null 2>&1; then
    # NSIS via CPack — produces build/UnnamedStrategy-<ver>-win64.exe (Setup)
    ( cd build && cpack )
    echo ">> Setup.exe created under build/  (NSIS one-click installer)"
elif command -v iscc >/dev/null 2>&1 || command -v ISCC.exe >/dev/null 2>&1; then
    ISCC="$(command -v iscc || command -v ISCC.exe)"
    "$ISCC" packaging/installer.iss
    echo ">> packaging/Output/UnnamedStrategy-Setup.exe created  (Inno Setup)"
else
    echo "!! No installer tool found — making a portable ZIP instead."
    echo "   For a real Setup.exe: pacman -S mingw-w64-ucrt-x86_64-nsis"
    ( cd build && cpack -G ZIP )
    echo ">> Portable ZIP under build/  (and dist/ folder is runnable as-is)"
fi

echo ""
echo "Done."
echo "  Portable:  double-click dist/unnamed_strategy.exe (no install needed)"
echo "  Installer: see the >> line above"
