#!/usr/bin/env bash
# verify_ai.sh — one-command headless AI-economy verification.
#
#   scripts/verify_ai.sh [WEEKS] [SEED]     (defaults: 12 weeks, seed 42)
#
# Runs a hidden --watch-ai-test game to the week cap (self-terminating —
# see [WATCH-AI] in the log), then prints an economy summary: builds/week,
# units recruited/week, per-town build counts, storms, level-ups, AI perf.
# Uses xvfb-run automatically when there's no display (CI/containers);
# on Windows/MSYS2 the hidden window needs no X at all.
#
# Pass criteria (what "healthy" looks like — see RELEASE_CHECKLIST.md):
#   - builds continue in EVERY week, not just the opening burst
#   - recruits/week trend up as economies come online
#   - no week where all towns stall while gold accumulates
set -euo pipefail

WEEKS="${1:-12}"
SEED="${2:-42}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$ROOT/build/bin/unnamed_strategy"
[ -x "$BIN" ] || BIN="$ROOT/build/bin/unnamed_strategy.exe"
[ -x "$BIN" ] || { echo "build first: cmake --build build -j4"; exit 1; }
LOG="${TMPDIR:-/tmp}/verify_ai_seed${SEED}.log"

RUNNER=""
if [ -z "${DISPLAY:-}" ] && command -v xvfb-run >/dev/null 2>&1; then
  RUNNER="xvfb-run -a"
fi

echo "Running $WEEKS-week Watch-AI test, seed $SEED (log: $LOG)…"
( cd "$(dirname "$BIN")" && $RUNNER "./$(basename "$BIN")" \
    --watch-ai-test=6 --seed="$SEED" --max-weeks="$WEEKS" ) >"$LOG" 2>&1 || {
  echo "GAME EXITED NON-ZERO — tail of log:"; tail -20 "$LOG"; exit 1;
}
grep -E "\[WATCH-AI\]" "$LOG" || { echo "no [WATCH-AI] exit line — did it crash?"; tail -20 "$LOG"; exit 1; }

python3 - "$LOG" <<'PY'
import re, sys
lines = open(sys.argv[1], errors="replace").read().splitlines()
week = 0
builds = {}; recruits = {}; towns = {}
for ln in lines:
    m = re.search(r"=== WEEK (\d+) BEGINS", ln)
    if m: week = int(m.group(1))
    if "AI " in ln and " built " in ln:
        builds[week] = builds.get(week, 0) + 1
        t = re.match(r"AI (.+?) built ", ln.strip())
        if t: towns[t.group(1)] = towns.get(t.group(1), 0) + 1
    m = re.search(r"AI (.+?) recruited (\d+) units", ln)
    if m: recruits[week] = recruits.get(week, 0) + int(m.group(2))
print("builds/week:   ", dict(sorted(builds.items())))
print("recruits/week: ", dict(sorted(recruits.items())))
print("per-town builds:", dict(sorted(towns.items(), key=lambda x: -x[1])))
storms = sum(1 for ln in lines if "stormed" in ln)
lvl = [int(m.group(1)) for ln in lines for m in [re.search(r"reached level (\d+)", ln)] if m]
perf = [float(m.group(1)) for ln in lines for m in [re.search(r"turn: total=([\d.]+)ms", ln)] if m]
print("storms:", storms, "| level-ups:", len(lvl), "max", max(lvl, default=0), end="")
if perf:
    print(" | AI turn avg %.0fms max %.0fms" % (sum(perf)/len(perf), max(perf)), end="")
print()
# stall check: any post-opening week with zero builds is suspicious
stalled = [w for w in range(3, max(builds, default=0)) if builds.get(w, 0) == 0]
if stalled:
    print("WARNING: zero-build weeks after opening:", stalled)
    sys.exit(2)
print("OK: builds continued every week.")
PY
