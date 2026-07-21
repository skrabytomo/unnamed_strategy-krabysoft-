#!/usr/bin/env bash
# Launch Brave with remote debugging on your NORMAL profile so the generator
# reuses your logged-in Gemini. Fully quit Brave first, or this flag is ignored
# and it just opens a tab in the already-running instance (no debug port).
BRAVE="/c/Program Files/BraveSoftware/Brave-Browser/Application/brave.exe"
PORT="${1:-9222}"
if [ ! -f "$BRAVE" ]; then
  echo "Brave not found at: $BRAVE"
  echo "Edit this script's BRAVE path to match your install."
  exit 1
fi
echo "Launching Brave with --remote-debugging-port=$PORT (your normal profile)…"
"$BRAVE" --remote-debugging-port="$PORT" >/dev/null 2>&1 &
echo "Open https://gemini.google.com and make sure you're signed in, then run gemini_gen.py"
