#!/usr/bin/env bash
# curl -fsSL https://raw.githubusercontent.com/sevvy-wevvy/open-chease/dev/get.sh | bash
set -euo pipefail

main() { # so a half downloaded script doesnt run half way
repo="sevvy-wevvy/open-chease"
branch="${OPEN_CHEASE_BRANCH:-dev}"
if [ "$(uname -s)" = Darwin ]; then
  data="${OPEN_CHEASE_DATA:-$HOME/Library/Application Support/open-chease}" # macs clean out $TMPDIR after a few days
else
  data="${OPEN_CHEASE_DATA:-${XDG_DATA_HOME:-$HOME/.local/share}/open-chease}" # was /tmp before, reboots wiped it
  old="${TMPDIR:-/tmp}/open-chease-$(id -u)"
  if [ -z "${OPEN_CHEASE_DATA:-}" ] && [ ! -e "$data" ] && [ -d "$old" ] && [ ! -L "$old" ] && [ -O "$old" ]; then
    if mkdir -p "$(dirname "$data")" && mv "$old" "$data" 2>/dev/null; then
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
stage_only=0
launch=0
pass=()
for a in "$@"; do
  case "$a" in
    --update) update=1 ;;
    --no-stockfish) want_sf=0 ;;
    --fetch-only) fetch_only=1 ;;
    --stage) stage_only=1 ;; # the app runs this to grab the next version in the background
    --launch) launch=1 ;; # what the menu entry runs
    *) pass+=("$a") ;;
  esac
done
if [ -z "${OPEN_CHEASE_BRANCH:-}" ] && { [ "$launch" = 1 ] || [ "$stage_only" = 1 ]; } && [ -f "$data/app/.commit" ]; then
  b="$(sed -n 2p "$data/app/.commit")" # stay on whatever branch it got installed from
  case "$b" in *[!A-Za-z0-9._/-]*|"") ;; *) branch="$b" ;; esac
fi

case "$os" in
  Linux) sf_url="https://github.com/official-stockfish/Stockfish/releases/latest/download/stockfish-linux-x86-64-universal.tar.gz" ;;
  Darwin) sf_url="https://github.com/official-stockfish/Stockfish/releases/latest/download/stockfish-macos-universal.tar.gz" ;; # one file for intel and apple silicon
  *)
    echo "this script is for linux and mac, on windows run this in powershell:" >&2
    echo "  irm https://raw.githubusercontent.com/$repo/$branch/get.ps1 | iex" >&2
    exit 1 ;;
esac

mkdir -p "$data"
trap 'rm -rf "${tmp:-}"; [ -n "${screen:-}" ] && kill "$screen" 2>/dev/null; true' EXIT
if [ -L "$data" ] || [ ! -O "$data" ]; then # someone else made it first, dont run their stuff
  echo "$data isnt a folder you own, delete it or set OPEN_CHEASE_DATA to somewhere else" >&2
  exit 1
fi
chmod 700 "$data"
export OPEN_CHEASE_DATA="$data"
export OPEN_CHEASE_BRANCH="$branch"

dl() {
  if command -v curl >/dev/null 2>&1; then
    if [ -t 2 ]; then bar=--progress-bar; else bar=-sS; fi # no bar spam in the launch log
    curl -fL --connect-timeout 15 --speed-time 30 --speed-limit 1000 "$bar" "$1" -o "$2"
  elif command -v wget >/dev/null 2>&1; then
    if [ -t 2 ]; then bar=--show-progress; else bar=-nv; fi
    wget -q -T 30 "$bar" -O "$2" "$1"
  else
    echo "you need curl or wget for this" >&2
    return 1
  fi
}

tmo() { # mac has no timeout command
  if command -v timeout >/dev/null 2>&1; then timeout "$@"; else perl -e 'alarm shift; exec @ARGV' "$@"; fi
}

say() { printf '%s\n' "$1" > "$data/update-status.new" && mv "$data/update-status.new" "$data/update-status"; }

latest_sha() {
  local api="${OPEN_CHEASE_LATEST_URL:-https://api.github.com/repos/$repo/commits/$branch}" # test hook, same one the app reads
  curl -fsL --max-time 10 "$api" 2>/dev/null || wget -q -T 10 -O - "$api" 2>/dev/null || true
}

stage() { # newest copy goes into app.new, only gets marked ready once it looks right
  local sha ref
  say "downloading the new version..."
  tmp="$(mktemp -d "$data/dl.XXXXXX")"
  sha="$(latest_sha | grep -o '"sha": *"[0-9a-f]\{40\}"' | sed -n '1s/.*"\([0-9a-f]\{40\}\)"/\1/p')" || sha=""
  ref="${sha:-refs/heads/$branch}" # by sha so .commit matches what got downloaded
  if ! dl "${OPEN_CHEASE_ARCHIVE:-https://github.com/$repo/archive}/$ref.tar.gz" "$tmp/app.tar.gz"; then
    rm -rf "$tmp"
    return 1
  fi
  say "unpacking it..."
  mkdir -p "$tmp/app"
  if ! tar xzf "$tmp/app.tar.gz" -C "$tmp/app" --strip-components=1 ||
    [ ! -f "$tmp/app/run.sh" ] || [ ! -f "$tmp/app/build.sh" ] || [ ! -f "$tmp/app/src/main.c" ] ||
    ! bash -n "$tmp/app/run.sh" || ! bash -n "$tmp/app/get.sh"; then
    echo "the download looks broken, keeping the one you have" >&2
    rm -rf "$tmp"
    return 1
  fi
  chmod +x "$tmp/app/run.sh" "$tmp/app/build.sh" "$tmp/app/get.sh" "$tmp/app/bin/"* 2>/dev/null || true
  [ -n "$sha" ] && printf '%s\n%s\n' "$sha" "$branch" > "$tmp/app/.commit"
  rm -rf "$app.new"
  mv "$tmp/app" "$app.new"
  : > "$app.new/.ready"
  rm -rf "$tmp"
}

swap() { # a ready app.new takes over app
  [ -f "$app.new/.ready" ] || return 1
  rm -f "$app.new/.ready"
  rm -rf "$app.old"
  [ -e "$app" ] && mv "$app" "$app.old"
  if ! mv "$app.new" "$app"; then
    [ -e "$app.old" ] && mv "$app.old" "$app"
    return 1
  fi
  rm -rf "$app.old"
  if [ -d "$data/build" ]; then # run.sh rebuilds, old one stays as a fallback
    rm -rf "$data/build.old"
    mv "$data/build" "$data/build.old"
  fi
}

old_bin() { # whatever can draw the updating screen right now
  if [ "$os" = Linux ] && [ "$(uname -m)" = x86_64 ] && "$app/bin/open-chease" --version >/dev/null 2>&1; then
    echo "$app/bin/open-chease"
  elif "$data/build/open-chease" --version >/dev/null 2>&1; then
    echo "$data/build/open-chease"
  fi
}

esc() { # for a quoted Exec= arg
  printf '%s' "$1" | sed -e 's/[\\"`$]/\\&/g' -e 's/\\/\\\\/g' -e 's/%/%%/g'
}

launcher() { # menu entry so it starts without a terminal
  [ -f "$app/get.sh" ] && ! cmp -s "$app/get.sh" "$data/get.sh" && cp "$app/get.sh" "$data/get.sh.new" && mv "$data/get.sh.new" "$data/get.sh"
  [ -f "$data/get.sh" ] || return 0
  if [ "$os" = Darwin ]; then
    local a="${OPEN_CHEASE_APPS:-$HOME/Applications}/open-chease.app"
    [ -e "$data/.launcher" ] && [ ! -d "$a" ] && return 0 # they deleted it, dont bring it back
    mkdir -p "$a/Contents/MacOS" "$a/Contents/Resources"
    printf '#!/bin/bash\nexec /bin/bash %q --launch\n' "$data/get.sh" > "$a/Contents/MacOS/open-chease"
    chmod +x "$a/Contents/MacOS/open-chease"
    [ -f "$app/assets/open-chease.icns" ] && cp "$app/assets/open-chease.icns" "$a/Contents/Resources/"
    cat > "$a/Contents/Info.plist" <<EOP
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleName</key><string>open-chease</string>
  <key>CFBundleIdentifier</key><string>io.github.sevvy-wevvy.open-chease</string>
  <key>CFBundleExecutable</key><string>open-chease</string>
  <key>CFBundleIconFile</key><string>open-chease</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>LSUIElement</key><true/>
</dict>
</plist>
EOP
  else
    local dir="${OPEN_CHEASE_APPS:-${XDG_DATA_HOME:-$HOME/.local/share}/applications}" f
    f="$dir/open-chease.desktop"
    [ -e "$data/.launcher" ] && [ ! -f "$f" ] && return 0
    mkdir -p "$dir"
    cat > "$f.new" <<EOD
[Desktop Entry]
Type=Application
Name=open-chease
GenericName=Chess helper
Comment=Shows the best move on any chess board on your screen
Exec=bash "$(esc "$data/get.sh")" --launch
Icon=$app/assets/open-chease.png
Terminal=false
Categories=Game;BoardGame;
Keywords=chess;stockfish;overlay;
StartupNotify=false
EOD
    mv "$f.new" "$f"
    command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database "$dir" >/dev/null 2>&1 || true
  fi
  : > "$data/.launcher"
}

if [ "$stage_only" = 1 ]; then
  r=0
  stage || r=1
  rm -f "$data/update-status"
  exit $r
fi

swapped=0
swap && swapped=1 # one the app grabbed in the background

if [ "$launch" = 1 ]; then
  [ -f "$data/launch.log" ] && [ "$(wc -c <"$data/launch.log")" -gt 1000000 ] && mv "$data/launch.log" "$data/launch.log.old"
  exec >>"$data/launch.log" 2>&1
  echo "--- $(date)"
  export OPEN_CHEASE_LAUNCHED=1
  have="$(head -1 "$app/.commit" 2>/dev/null)" || have=""
  if [ -x "$app/run.sh" ] && [ -n "$have" ]; then
    new="$(latest_sha | grep -o '"sha": *"[0-9a-f]\{40\}"' | sed -n '1s/.*"\([0-9a-f]\{40\}\)"/\1/p')" || new=""
    if [ -n "$new" ] && [ "$new" != "$have" ]; then
      say "downloading the new version..."
      b="$(old_bin)"
      [ -n "$b" ] && { "$b" --updating & screen=$!; }
      if stage && swap; then
        if [ "$os" = Darwin ] || [ "$(uname -m)" != x86_64 ] || ! "$app/bin/open-chease" --version >/dev/null 2>&1; then
          say "building it for your pc, takes a minute..."
          "$app/run.sh" --build-only || true
        fi
        say done
      else
        say "update failed, starting the one you have"
        sleep 2
      fi
      if [ -n "${screen:-}" ]; then
        for _ in 1 2 3 4 5 6 7 8 9 10; do kill -0 "$screen" 2>/dev/null || break; sleep 0.3; done
        kill "$screen" 2>/dev/null || true
        screen=""
      fi
    fi
  fi
  rm -f "$data/update-status"
fi

if [ ! -x "$app/run.sh" ] || { [ "$update" = 1 ] && [ "$swapped" = 0 ]; }; then
  echo "grabbing open-chease..."
  if ! stage || ! swap; then
    echo "couldnt download open-chease from github, check your internet and try again" >&2
    exit 1
  fi
fi
rm -f "$data/update-status"
launcher || true

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
