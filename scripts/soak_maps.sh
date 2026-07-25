#!/usr/bin/env bash
# soak_maps.sh — do games actually RESOLVE on every map shape?
#
#   scripts/soak_maps.sh [SEED]        (default seed 999)
#
# Plays one full headless AI game per map shape and reports how each ended.
# This exists because map shape turned out to decide whether a game can be
# won at all: on water-separated shapes the AI used to farm its own island
# to the week-80 backstop with almost everyone still alive (2026-07-25 —
# the boat-gate bug, see AI_ROADMAP). This is the regression net for that.
#
# Read the output like this:
#   "resolved week N"     — someone actually won. Good.
#   "BACKSTOP week 80"    — nobody could win; the harness handed it to the
#                           biggest pile. Investigate, especially if several
#                           players are still alive and boats=0 on a
#                           water-separated shape.
#
# Each game runs to completion, so this takes a while (tens of minutes).
# Sizes are deliberately pinned to Small: a measured XLarge JebusCross3 game
# was still going at week 63 after 40 minutes, which is too slow to be a
# routine check. Soak shape coverage here; test big maps individually.
set -uo pipefail

SEED="${1:-999}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$ROOT/build/bin/unnamed_strategy"
[ -x "$BIN" ] || BIN="$ROOT/build/bin/unnamed_strategy.exe"
[ -x "$BIN" ] || { echo "build first: cmake --build build -j4"; exit 1; }

RUNNER=""
if [ -z "${DISPLAY:-}" ] && command -v xvfb-run >/dev/null 2>&1; then
  RUNNER="xvfb-run -a"
fi

SHAPES=("0:Hexagon" "1:JebusCross" "2:JebusCross3" "3:Ring")
OUT="${TMPDIR:-/tmp}"
fails=0

printf '%-14s %-22s %8s %8s %8s %8s\n' SHAPE OUTCOME ALIVE BOATS COMBATS STORMS
for entry in "${SHAPES[@]}"; do
  shape="${entry%%:*}"; name="${entry#*:}"
  log="$OUT/soak_${name}_${SEED}.log"
  ( cd "$(dirname "$BIN")" && $RUNNER "./$(basename "$BIN")" \
      --watch-ai-test="6:${shape}:0" --seed="$SEED" ) >"$log" 2>&1

  over=$(grep "WATCH GAME OVER" "$log" | tail -1)
  week=$(sed -n 's/.*WATCH GAME OVER (week \([0-9]*\)).*/\1/p' <<<"$over")
  alive=$(sed -n 's/.*(\([0-9]*\) players left.*/\1/p' <<<"$over")
  boats=$(grep -ciE "boat" "$log")
  combats=$(grep -c "Combat ended" "$log")
  storms=$(grep -c "stormed" "$log")

  if [ -z "$week" ]; then
    outcome="NO RESULT (crash?)"; fails=$((fails+1))
  elif [ "$week" -ge 80 ]; then
    outcome="BACKSTOP week $week"; fails=$((fails+1))
  else
    outcome="resolved week $week"
  fi
  printf '%-14s %-22s %8s %8s %8s %8s\n' \
    "$name" "$outcome" "${alive:-?}" "$boats" "$combats" "$storms"
done

echo
if [ "$fails" -gt 0 ]; then
  echo "$fails of ${#SHAPES[@]} shapes did not resolve on their own — see logs in $OUT."
  exit 2
fi
echo "All ${#SHAPES[@]} shapes resolved without hitting the backstop."
