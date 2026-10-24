#!/bin/bash
# Build the headless Linux test binary and (if game data is present) run a smoke test
# that walks through the menus into a stage and saves screenshots to build/shots/.
set -e
HERE="$(cd "$(dirname "$0")/.." && pwd)"
GAME_DIR="${1:-$HERE/game}"
mkdir -p "$HERE/build"
python3 "$HERE/tools/bb2cpp.py" "$HERE/src/mh.bb" "$HERE/build/mh.cpp"
g++ -std=c++17 -O2 -fwrapv -w -I"$HERE/runtime" "$HERE/build/mh.cpp" "$HERE/runtime/bbrt.cpp" \
    "$HERE/runtime/mh_glue.cpp" "$HERE/runtime/linux_main.cpp" -o "$HERE/build/mh_linux" -lpthread
echo "built build/mh_linux"
if [ ! -f "$GAME_DIR/maps.dat" ]; then
  echo "no game data in '$GAME_DIR' - skipping the run (see game/README.md)"
  exit 0
fi
python3 "$HERE/tools/prep_assets.py" "$GAME_DIR" "$HERE/build/assets"
rm -rf "$HERE/build/shots" "$HERE/build/testdata"; mkdir -p "$HERE/build/shots"
cat > "$HERE/build/smoke.txt" <<'S'
60 shot
70 tap 0.5 0.43
120 shot
130 tap 0.49 0.955
180 tap 0.208 0.117
300 touch 1 0.12 0.75 0
301 touch 1 0.165 0.75 1
360 touch 1 0.165 0.75 2
370 touch 2 0.952 0.81 0
380 touch 2 0.952 0.81 2
400 shot
S
MH_ASSETS="$HERE/build/assets" MH_DATA="$HERE/build/testdata" MH_SHOTS="$HERE/build/shots" \
MH_SCRIPT="$HERE/build/smoke.txt" MH_MAXFRAMES=2000 MH_FAST=1 "$HERE/build/mh_linux"
echo "screenshots (PPM) in build/shots/"
