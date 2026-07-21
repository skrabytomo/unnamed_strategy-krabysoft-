#!/usr/bin/env bash
# Preflight only — there is NOTHING to install. gemini_gen.py uses the Python
# standard library exclusively (raw WebSocket + Chrome DevTools Protocol), so it
# runs on the MSYS2 Python you already have. No pip, no venv, no Playwright.
PY="${PYTHON:-python3}"
BRAVE="/c/Program Files/BraveSoftware/Brave-Browser/Application/brave.exe"

echo "== python =="
"$PY" --version || { echo "python3 not found"; exit 1; }

echo "== brave =="
if [ -f "$BRAVE" ]; then echo "found: $BRAVE"; else echo "NOT found at $BRAVE — edit launch_brave.sh"; fi

echo
echo "No install needed. Next:"
echo "  1) Fully quit Brave — check Task Manager, no brave.exe left."
echo "  2) ./launch_brave.sh          (Brave with --remote-debugging-port=9222)"
echo "  3) $PY gemini_gen.py --dry-run   then   $PY gemini_gen.py"
