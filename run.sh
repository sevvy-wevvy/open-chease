#!/usr/bin/env bash
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [ "$(uname -s)" = Darwin ]; then
  data="${OPEN_CHEASE_DATA:-$HOME/Library/Application Support/open-chease}"
else
  data="${OPEN_CHEASE_DATA:-${XDG_DATA_HOME:-$HOME/.local/share}/open-chease}"
  old="${TMPDIR:-/tmp}/open-chease-$(id -u)"
  case "$here" in "$old"/*) old=/nonexistent ;; esac # dont move the folder were running from
  if [ -z "${OPEN_CHEASE_DATA:-}" ] && [ ! -e "$data" ] && [ -d "$old" ] && [ ! -L "$old" ] && [ -O "$old" ]; then
    if mkdir -p "$(dirname "$data")" && mv "$old" "$data" 2>/dev/null; then
      for f in "$data/settings" "$data/engine-names" "$data"/engines/*; do
        [ -f "$f" ] && [ "$(wc -c <"$f")" -lt 65536 ] && grep -qF "$old" "$f" 2>/dev/null && sed -i "s#$old#$data#g" "$f"
      done
    fi
  fi
fi
mkdir -p "$data"
if [ -L "$data" ] || [ ! -O "$data" ]; then
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
if [ -d "$here/.git" ]; then
  export OPEN_CHEASE_UPDATE=git
elif [ "$here" = "$(cd "$data/app" 2>/dev/null && pwd)" ]; then # get.sh only ever updates this copy
  export OPEN_CHEASE_UPDATE=get
else
  unset OPEN_CHEASE_UPDATE
fi

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
    rm -rf "$data/build.new"
    if "$here/build.sh" "$data/build.new" >/dev/null; then
      rm -rf "$data/build"
      mv "$data/build.new" "$data/build"
      rm -rf "$data/build.old"
    elif "$data/build.old/open-chease" --version >/dev/null 2>&1; then
      echo "couldnt build the new version, running the one you had" >&2
      bin="$data/build.old/open-chease"
    else
      exit 1
    fi
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
  if [ -n "$engine" ]; then export OPEN_CHEASE_ENGINE="$engine"; fi # settings still win
fi

exec "$bin" ${args[@]+"${args[@]}"} "$@"
