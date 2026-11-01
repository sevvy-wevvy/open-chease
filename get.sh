#!/usr/bin/env bash
# curl -fsSL https://raw.githubusercontent.com/sevvy-wevvy/open-chease/dev/get.sh | bash
set -euo pipefail

main() { # so a half downloaded script doesnt run half way
repo="sevvy-wevvy/open-chease"
branch="${OPEN_CHEASE_BRANCH:-dev}"
if [ "$(uname -s)" = Darwin ]; then
  data="${OPEN_CHEASE_DATA:-$HOME/Library/Application Support/open-chease}" # macs clean out $TMPDIR after a few days
else
  data="${OPEN_CHEASE_DATA:-${XDG_DATA_HOME:-$HOME/.local/share}/open-chease}" # used to be /tmp, gone every reboot
  old="${TMPDIR:-/tmp}/open-chease-$(id -u)"
  if [ -z "${OPEN_CHEASE_DATA:-}" ] && [ ! -e "$data" ] && [ -d "$old" ] && [ ! -L "$old" ] && [ -O "$old" ]; then
    if mkdir -p "$(dirname "$data")" && mv "$old" "$data" 2>/dev/null; then # bring your settings and engines along
      for f in "$data/settings" "$data/engine-names" "$data"/engines/*; do # these have the old path baked in
        [ -f "$f" ] && [ "$(wc -c <"$f")" -lt 65536 ] && grep -qF "$old" "$f" 2>/dev/null && sed -i "s#$old#$data#g" "$f"
      done
    fi
  fi
fi
data="${data%/}"
app="$data/app"
sf_dir="$data/stockfish"
os="$(uname -s)"

update=0
fetch_only=0
want_sf=1
pass=()
for a in "$@"; do
  case "$a" in
    --update) update=1 ;;
    --no-stockfish) want_sf=0 ;;
    --fetch-only) fetch_only=1 ;;
    *) pass+=("$a") ;;
  esac
done

case "$os" in
  Linux) sf_url="https://github.com/official-stockfish/Stockfish/releases/latest/download/stockfish-linux-x86-64-universal.tar.gz" ;;
  Darwin) sf_url="https://github.com/official-stockfish/Stockfish/releases/latest/download/stockfish-macos-universal.tar.gz" ;; # one file for intel and apple silicon
  *)
    echo "this script is for linux and mac, on windows run this in powershell:" >&2
    echo "  irm https://raw.githubusercontent.com/$repo/$branch/get.ps1 | iex" >&2
    exit 1 ;;
esac

mkdir -p "$data"
trap 'rm -rf "${tmp:-}"' EXIT # a failed download or tar shouldnt leave junk behind
if [ -L "$data" ] || [ ! -O "$data" ]; then # someone else made it first, dont run their stuff
  echo "$data isnt a folder you own, delete it or set OPEN_CHEASE_DATA to somewhere else" >&2
  exit 1
fi
chmod 700 "$data"
export OPEN_CHEASE_DATA="$data"
export OPEN_CHEASE_BRANCH="$branch" # the app reads its engine lists from the same branch

dl() {
  if command -v curl >/dev/null 2>&1; then
    curl -fL --connect-timeout 15 --speed-time 30 --speed-limit 1000 --progress-bar "$1" -o "$2"
  elif command -v wget >/dev/null 2>&1; then
    wget -q -T 30 --show-progress -O "$2" "$1"
  else
    echo "you need curl or wget for this" >&2
    return 1
  fi
}

tmo() { # mac has no timeout command
  if command -v timeout >/dev/null 2>&1; then timeout "$@"; else perl -e 'alarm shift; exec @ARGV' "$@"; fi
}

if [ ! -x "$app/run.sh" ] || [ "$update" = 1 ]; then
  echo "grabbing open-chease..."
  tmp="$(mktemp -d "$data/dl.XXXXXX")"
  if ! dl "https://github.com/$repo/archive/refs/heads/$branch.tar.gz" "$tmp/app.tar.gz"; then
    rm -rf "$tmp"
    echo "couldnt download open-chease from github, check your internet and try again" >&2
    exit 1
  fi
  mkdir -p "$tmp/app"
  tar xzf "$tmp/app.tar.gz" -C "$tmp/app" --strip-components=1
  chmod +x "$tmp/app/run.sh" "$tmp/app/build.sh" "$tmp/app/bin/"* 2>/dev/null || true
  rm -rf "$app"
  mv "$tmp/app" "$app"
  rm -rf "$tmp"
  if [ -d "$data/build" ]; then # run.sh rebuilds, old one stays as a fallback
    rm -rf "$data/build.old"
    mv "$data/build" "$data/build.old"
  fi
fi

if [ "$want_sf" = 1 ] && { [ "$os" = Darwin ] || [ "$(uname -m)" = "x86_64" ]; } && ! ls "$sf_dir"/stockfish* >/dev/null 2>&1; then
  echo "grabbing stockfish too (about 80 MB, only happens the first time)..."
  tmp="$(mktemp -d "$data/sf.XXXXXX")"
  if dl "$sf_url" "$tmp/sf.tar.gz" && tar xzf "$tmp/sf.tar.gz" -C "$tmp"; then
    found="$(find "$tmp" -type f -name 'stockfish-*' -perm -u+x | head -1)"
    if [ -n "$found" ] && printf 'uci\nquit\n' | tmo 10 "$found" 2>/dev/null | grep -q uciok; then
      mkdir -p "$sf_dir"
      mv "$found" "$sf_dir/stockfish"
    else
      echo "stockfish wont start on this pc, using my engine instead"
    fi
  else
    echo "stockfish download failed, using my engine instead"
  fi
  rm -rf "$tmp"
fi

[ "$fetch_only" = 1 ] && exit 0
exec "$app/run.sh" ${pass[@]+"${pass[@]}"}
}

main "$@"
