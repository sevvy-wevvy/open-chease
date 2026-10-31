#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
sets="${*:-cburnett}"
for set in $sets; do
  mkdir -p "pieces/$set"
  for c in w b; do
    for p in P N B R Q K; do
      curl -fsSL -o "pieces/$set/$c$p.svg" "https://raw.githubusercontent.com/lichess-org/lila/master/public/piece/$set/$c$p.svg"
    done
  done
done
cp pieces/cburnett/*.svg page/ 2>/dev/null || true
echo "got the pieces, theyre in tests/pieces and the test page is ready in tests/page"
