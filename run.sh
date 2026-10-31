#!/usr/bin/env bash
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [ "$(uname -s)" = Darwin ]; then
  data="${OPEN_CHEASE_DATA:-$HOME/Library/Application Support/open-chease}"
else
  data="${OPEN_CHEASE_DATA:-${TMPDIR:-/tmp}/open-chease-$(id -u)}"
fi
mkdir -p "$data"
if [ -L "$data" ] || [ ! -O "$data" ]; then # someone else made it first, dont run their stuff
  echo "$data isnt a folder you own, delete it or set OPEN_CHEASE_DATA to somewhere else" >&2
  exit 1
fi
chmod 700 "$data"
export OPEN_CHEASE_DATA="$data"

os="$(uname -s)"
if [ "$os" != Linux ] && [ "$os" != Darwin ]; then
  echo "run.sh is for linux and mac, on windows use run.ps1 (or get.ps1, see the readme)" >&2
  exit 1
fi

if [ "$os" = Linux ] && [ -z "${WAYLAND_DISPLAY:-}" ] && [ -z "${DISPLAY:-}" ]; then
  echo "run this from a terminal on your actual desktop (hyprland, gnome or xfce), not over ssh" >&2
  exit 1
fi

export OPEN_CHEASE_HOME="$here"
if [ -d "$here/.git" ]; then export OPEN_CHEASE_UPDATE=git; else export OPEN_CHEASE_UPDATE=get; fi

# prebuilt is x86_64 linux only, macs always build it the first time
bin="$here/bin/open-chease"
if [ "$os" = Darwin ] || [ "$(uname -m)" != "x86_64" ] || ! "$bin" --version >/dev/null 2>&1; then
  bin="$data/build/open-chease"
  if ! "$bin" --version >/dev/null 2>&1 || [ -n "$(find "$here/src" -newer "$bin" 2>/dev/null | head -1)" ]; then
    if ! command -v "${CC:-cc}" >/dev/null 2>&1 || { [ "$os" = Darwin ] && ! xcode-select -p >/dev/null 2>&1; }; then
      if [ "$os" = Darwin ]; then
        echo "open-chease gets built on your mac the first time, that needs apples command line tools" >&2
        echo "run: xcode-select --install   then run this again" >&2
      else
        echo "the prebuilt binary doesnt run on your system and theres no gcc to rebuild it" >&2
        echo "install gcc (sudo pacman -S gcc or sudo apt install gcc) and run this again" >&2
      fi
      exit 1
    fi
    echo "building open-chease for your system, one sec..."
    "$here/build.sh" "$data/build" >/dev/null
  fi
fi

pick_engine=1
for a in "$@"; do
  case "$a" in
    --engine|--builtin) pick_engine=0 ;;
  esac
done

args=()
if [ "$pick_engine" = 1 ]; then
  engine="${OPEN_CHEASE_ENGINE:-}"
  if [ -z "$engine" ]; then
    for f in "$data"/stockfish/stockfish*; do
      if [ -f "$f" ] && [ -x "$f" ]; then engine="$f"; break; fi
    done
  fi
  if [ -z "$engine" ] && command -v stockfish >/dev/null 2>&1; then
    engine="$(command -v stockfish)"
  fi
  if [ -n "$engine" ]; then export OPEN_CHEASE_ENGINE="$engine"; fi # just the default, whatever you pick in settings wins
fi

exec "$bin" ${args[@]+"${args[@]}"} "$@"
