@echo off
cd /d "%~dp0"
python apply_patches_v4.py --dry-run
set /p CONFIRM="Apply for real? (y/n): "
if /I "%CONFIRM%"=="y" python apply_patches_v4.py
pause
