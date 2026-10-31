#!/usr/bin/env bash
set -euo pipefail

repo="sevvy-wevvy/open-chease"
branch="main"
data="${OPEN_CHEASE_DATA:-${TMPDIR:-/tmp}/open-chease-$(id -u)}"
app="$data/app"
sf_dir="$data/stockfish"
sf_url="https://github.com/official-stockfish/Stockfish/releases/latest/download/stockfish-linux-x86-64-universal.tar.gz"

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

if [ "$(uname -s)" != "Linux" ]; then
  echo "open-chease only works on linux sorry" >&2
  exit 1
fi

mkdir -p "$data"
trap 'rm -rf "${tmp:-}"' EXIT # a failed download or tar shouldnt leave junk behind
if [ -L "$data" ] || [ ! -O "$data" ]; then # someone else made it first, dont run their stuff
  echo "$data isnt a folder you own, delete it or set OPEN_CHEASE_DATA to somewhere else" >&2
  exit 1
fi
chmod 700 "$data"
export OPEN_CHEASE_DATA="$data"

dl() {
  if command -v curl >/dev/null 2>&1; then
    curl -fL --progress-bar "$1" -o "$2"
  elif command -v wget >/dev/null 2>&1; then
    wget -q --show-progress -O "$2" "$1"
  else
    echo "you need curl or wget for this" >&2
    return 1
  fi
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
fi

if [ "$want_sf" = 1 ] && [ "$(uname -m)" = "x86_64" ] && ! ls "$sf_dir"/stockfish* >/dev/null 2>&1; then
  echo "grabbing stockfish too (about 80 MB, only happens the first time)..."
  tmp="$(mktemp -d "$data/sf.XXXXXX")"
  if dl "$sf_url" "$tmp/sf.tar.gz" && tar xzf "$tmp/sf.tar.gz" -C "$tmp"; then
    found="$(find "$tmp" -type f -name 'stockfish-*' -perm -u+x | head -1)"
    if [ -n "$found" ] && printf 'uci\nquit\n' | timeout 10 "$found" 2>/dev/null | grep -q uciok; then
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
