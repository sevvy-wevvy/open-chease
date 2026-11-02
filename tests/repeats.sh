#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
out="${TMPDIR:-/tmp}/repeat_test"
cc -O2 -std=gnu11 -Wall -Wextra -Wno-unused-parameter -Isrc -o "$out" tests/repeat_test.c src/repeat.c src/tracker.c src/vision.c src/gpu.c src/engine.c -lpthread -lm -ldl
"$out" lines | tee "$out.log"
line=$(sed -n 's/^LINE //p' "$out.log")
E=${E:-bin/open-chease-engine}
with=$(printf 'uci\nposition %s\ngo movetime 300\n' "$line" | timeout 10 "$E" | grep -E '^(info.*score|bestmove)' | tail -2 | tr '\n' ' ')
fen=$(printf 'uci\nposition fen 6k1/8/8/8/8/8/8/K2Q4 b - - 0 1\ngo movetime 300\n' | timeout 10 "$E" | grep -E '^(info.*score|bestmove)' | tail -2 | tr '\n' ' ')
case "$with" in *"score cp 0 "*"bestmove g8h8"*) echo "ok   uci engine sees the repeat";; *) echo "FAIL uci engine with moves: $with"; exit 1;; esac
case "$fen" in *"score cp 0 "*) echo "FAIL uci engine without moves should not see a draw: $fen"; exit 1;; *) echo "ok   without the moves it doesnt";; esac
