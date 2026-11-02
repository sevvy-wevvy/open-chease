#!/usr/bin/env bash
# the plugin safety scan should find the right stuff in tests/plug-bad and nothing in the clean ones
# B=bin/open-chease tests/plugscan.sh [more clean plugin folders...]
cd "$(dirname "$0")/.."
B=${B:-bin/open-chease}
bad=0 n=0
want() { # folder, then the kinds it should find
  local d=$1 got exp; shift
  got=$("$B" --check-plugin "$d" | grep -v '^ ' | tail -n +2 | sed 's/ [0-9]*$//' | sort | paste -sd, -)
  exp=$(printf '%s\n' "$@" | sort | paste -sd, -)
  n=$((n+1))
  if [ "$got" != "$exp" ]; then bad=$((bad+1)); echo "FAIL $d: got [$got] want [$exp]"; fi
}
P=tests/plug-bad
want $P/net-honest internet
want $P/runner "runs programs"
want $P/shady internet "runs programs" "looks shady"
want $P/stealer "reads secrets"
want $P/startup "starts with pc"
want $P/keys "reads keys"
want $P/wiper "deletes stuff"
want $P/binary "has programs"
want $P/build-cargo "build runs code"
want $P/build-npm "build runs code"
want $P/clean-c clean
want $P/lookalike clean
for d in plugins-examples/*/ "$@"; do want "$d" clean; done
echo "plugscan $((n-bad))/$n"
[ $bad = 0 ]
