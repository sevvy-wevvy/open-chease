#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
e=bin/open-chease-engine
fail=0
check() {
  got=$("$e" perft "$1" "$2" | cut -d' ' -f1)
  if [ "$got" = "$3" ]; then echo "ok   $3"; else echo "FAIL got $got want $3 ($2)"; fail=1; fi
}
check 5 "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1" 4865609
check 4 "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1" 4085603
check 5 "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1" 674624
check 4 "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1" 422333
check 4 "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8" 2103487
exit $fail
