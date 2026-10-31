#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
out="${1:-bin}"
mkdir -p "$out"
cc="${CC:-cc}"
march=()
if [ "$(uname -m)" = x86_64 ] && grep -q sse4_2 /proc/cpuinfo && grep -q popcnt /proc/cpuinfo; then march=(-march=x86-64-v2); fi # older cpus and arm get a plain build
flags=(-O2 "${march[@]}" -std=gnu11 -Wall -Wextra -Wno-unused-parameter -Isrc -Ithird_party/include)
"$cc" "${flags[@]}" -o "$out/open-chease-engine" src/engine.c src/uci.c -lpthread
"$cc" "${flags[@]}" -o "$out/open-chease" \
  src/main.c src/engine.c src/vision.c src/tracker.c src/analyzer.c src/draw.c \
  src/wl.c src/x11.c src/gnome.c src/replay.c src/proto/*.c \
  -l:libwayland-client.so.0 -ldl -lpthread -lm
echo "built $out/open-chease and $out/open-chease-engine"
