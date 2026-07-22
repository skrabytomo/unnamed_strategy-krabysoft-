#!/usr/bin/env bash
# Relaunch Brave with remote debugging on your NORMAL profile so the generator
# reuses your logged-in Gemini. Handles the #1 gotcha for you: any Brave that's
# already running is force-closed first (otherwise the debug flag is ignored and
# the port never opens -> "connection refused").
BRAVE="/c/Program Files/BraveSoftware/Brave-Browser/Application/brave.exe"
PORT="${1:-9222}"
if [ ! -f "$BRAVE" ]; then
  echo "Brave not found at: $BRAVE  (edit this script's BRAVE path)"; exit 1
fi

echo "Closing any running Brave…"
powershell -NoProfile -Command "Get-Process brave -ErrorAction SilentlyContinue | Stop-Process -Force" >/dev/null 2>&1
sleep 2

echo "Launching Brave with --remote-debugging-port=$PORT (your normal profile)…"
"$BRAVE" --remote-debugging-port="$PORT" >/dev/null 2>&1 &
disown 2>/dev/null || true

echo -n "Waiting for the debug port"
for i in $(seq 1 20); do
  if python3 -c "import urllib.request,sys; urllib.request.urlopen('http://127.0.0.1:$PORT/json',timeout=1)" >/dev/null 2>&1; then
    echo " — up."
    echo "Port $PORT is live. Sign in at https://gemini.google.com if needed, then:"
    echo "    python gemini_gen.py --limit 1"
    exit 0
  fi
  echo -n "."; sleep 1
done
echo
echo "Port $PORT still not reachable. Make sure Brave fully closed (Task Manager"
echo "-> no brave.exe), then run this script again."
exit 1
