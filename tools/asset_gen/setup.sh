#!/usr/bin/env bash
# One-time setup: install Playwright into the MSYS2/UCRT64 Python.
# We attach to your existing Brave over CDP, so NO browser download is needed
# (that's why we skip `playwright install`).
set -e
PY="${PYTHON:-python3}"

echo "== bootstrapping pip =="
"$PY" -m ensurepip --upgrade || true
"$PY" -m pip install --upgrade pip

echo "== installing playwright (python package only) =="
"$PY" -m pip install playwright

echo
echo "Done. Next:"
echo "  1) Fully quit Brave (no brave.exe in Task Manager)."
echo "  2) ./launch_brave.sh        (or run launch_brave.ps1 in PowerShell)"
echo "  3) python gemini_gen.py --dry-run   then   python gemini_gen.py"
