#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
out="${TMPDIR:-/tmp}/style_test"
cc -O2 -std=gnu11 -Wall -Wextra -Wno-unused-parameter -Isrc -o "$out" tests/style_test.c src/style.c src/trap.c src/engine.c -lpthread -lm
"$out" "$@"
