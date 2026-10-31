#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
target=linux
[ "$(uname -s)" = Darwin ] && target=mac
if [ "${1:-}" = win ]; then target=win; shift; fi # cross compile the windows exe, needs mingw-w64
out="${1:-bin}"
mkdir -p "$out"
common=(src/main.c src/engine.c src/vision.c src/tracker.c src/analyzer.c src/second.c src/draw.c src/replay.c)
warn=(-std=gnu11 -Wall -Wextra -Wno-unused-parameter -Isrc -Ithird_party/include)

case "$target" in
win)
  cc="${CC:-x86_64-w64-mingw32-gcc-posix}" # the posix flavour has winpthreads
  command -v "$cc" >/dev/null || cc=x86_64-w64-mingw32-gcc
  flags=(-O2 "${warn[@]}")
  rc="${cc%gcc*}windres"
  "$rc" src/win.rc -O coff -o "$out/win.res" # utf-8 manifest for user names with accents
  "$cc" "${flags[@]}" -o "$out/open-chease-engine.exe" src/engine.c src/uci.c -static -lpthread
  "$cc" "${flags[@]}" -o "$out/open-chease.exe" "${common[@]}" src/plat_win.c src/win.c "$out/win.res" \
    -static -lgdi32 -luser32 -lshell32 -lpthread -lm
  rm -f "$out/win.res"
  echo "built $out/open-chease.exe and $out/open-chease-engine.exe"
  ;;
mac)
  cc="${CC:-cc}"
  flags=(-O2 "${warn[@]}")
  "$cc" "${flags[@]}" -o "$out/open-chease-engine" src/engine.c src/uci.c -lpthread
  "$cc" "${flags[@]}" -fobjc-arc -o "$out/open-chease" "${common[@]}" src/plat_posix.c src/mac.m \
    -framework AppKit -framework ScreenCaptureKit -framework CoreGraphics -framework CoreMedia -lpthread -lm
  echo "built $out/open-chease and $out/open-chease-engine"
  ;;
*)
  cc="${CC:-cc}"
  march=()
  if [ "$(uname -m)" = x86_64 ] && grep -q sse4_2 /proc/cpuinfo && grep -q popcnt /proc/cpuinfo; then march=(-march=x86-64-v2); fi # older cpus and arm get a plain build
  flags=(-O2 "${march[@]}" "${warn[@]}")
  "$cc" "${flags[@]}" -o "$out/open-chease-engine" src/engine.c src/uci.c -lpthread
  "$cc" "${flags[@]}" -o "$out/open-chease" "${common[@]}" src/plat_posix.c \
    src/wl.c src/x11.c src/gnome.c src/proto/*.c \
    -l:libwayland-client.so.0 -ldl -lpthread -lm
  echo "built $out/open-chease and $out/open-chease-engine"
  ;;
esac
