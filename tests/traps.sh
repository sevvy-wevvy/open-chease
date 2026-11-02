#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
out="${TMPDIR:-/tmp}/trap_test"
cc -O2 -std=gnu11 -Wall -Wextra -Wno-unused-parameter -Isrc -o "$out" tests/trap_test.c src/trap.c src/engine.c -lpthread
"$out" "$@"
