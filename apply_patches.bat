@echo off
REM ============================================================
REM Unnamed Strategy — Windows Patch Application Script
REM Run this from your repo root (where src/ folder lives)
REM ============================================================

echo ==========================================
echo  Unnamed Strategy Auto-Patcher
echo ==========================================
echo.

REM Check if Python is available
python --version >nul 2>&1
if errorlevel 1 (
    echo ERROR: Python not found in PATH.
    echo Please install Python 3.8+ or run from MSYS2 with python3.
    pause
    exit /b 1
)

REM Check if we're in a repo
if not exist ".git" (
    echo WARNING: No .git folder found. Make sure you run this from repo root.
    echo Current dir: %CD%
    set /p CONTINUE="Continue anyway? (y/n): "
    if /I not "%CONTINUE%"=="y" exit /b 1
)

REM Run dry-run first
echo.
echo [1/3] Running DRY-RUN to preview changes...
python apply_patches.py --dry-run --repo .
if errorlevel 1 (
    echo Dry-run failed. Check output above.
    pause
    exit /b 1
)

echo.
echo [2/3] Dry-run complete. Review the output above.
set /p CONFIRM="Apply patches for real? (y/n): "
if /I not "%CONFIRM%"=="y" (
    echo Cancelled.
    exit /b 0
)

REM Apply for real
echo.
echo [3/3] Applying patches...
python apply_patches.py --repo .
if errorlevel 1 (
    echo Some patches failed. Check output above.
    echo Backups saved as *.orig files.
    pause
    exit /b 1
)

echo.
echo ==========================================
echo  Patches applied successfully!
echo ==========================================
echo.
echo Next steps:
echo   1. Review:  git diff
echo   2. Build:   cmake --build build -j4
echo   3. Test:    .\build\bin\unnamed_strategy.exe
echo   4. Commit:  git add -A ^&^& git commit -m "AI fixes: early game, boats, elimination"
echo   5. Push:    git push origin main
echo.
pause
