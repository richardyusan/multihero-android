#!/bin/bash
# Build MultiHero for web browsers (WebAssembly, Emscripten).
#   ./build_web.sh [path/to/MultiHero096]      (default: ./game)
# Output: build/web/  (index.html, multihero.js, multihero.wasm, multihero.data, icon.png)
# Play it locally:   cd build/web && python3 -m http.server 8080   ->  http://localhost:8080
# Environment:
#   EMSDK         emsdk folder (default: ~/emsdk); emcc/em++ must work after "source $EMSDK/emsdk_env.sh"
#   CODE_ONLY=1   only compile the code (no game data needed, the page will not start)
#   DEBUG=1       unoptimised build with assertions
#   ASYNC=jspi    use JavaScript Promise Integration instead of ASYNCIFY: builds in seconds and runs
#                 faster, but needs Chrome/Edge 137+ (no Safari/iPhone yet). Default: asyncify
#                 (works in every modern browser, but the link step takes a long time)
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
GAME_DIR="${1:-$HERE/game}"
OUT="${OUT:-$HERE/build/web}"
GEN="${GEN:-$HERE/build/web-gen}"

if ! command -v em++ >/dev/null 2>&1; then
  EMSDK="${EMSDK:-$HOME/emsdk}"
  [ -f "$EMSDK/emsdk_env.sh" ] || { echo "Emscripten not found: install emsdk (see docs/WEB.md) or set EMSDK=/path/to/emsdk"; exit 1; }
  source "$EMSDK/emsdk_env.sh" >/dev/null 2>&1
fi
[ -n "$CODE_ONLY" ] || [ -f "$GAME_DIR/maps.dat" ] || { echo "Game data not found in '$GAME_DIR' (see game/README.md)"; exit 1; }

rm -rf "$OUT"; mkdir -p "$OUT" "$GEN"

echo "== 1. Blitz -> C++"
python3 "$HERE/tools/bb2cpp.py" "$HERE/src/mh.bb" "$GEN/mh.cpp" "$HERE"/runtime/decls/*.decls

DATA_FLAGS=""
if [ -z "$CODE_ONLY" ]; then
  echo "== 2. assets"
  python3 "$HERE/tools/prep_assets.py" "$GAME_DIR" "$GEN/assets" "$HERE/config"
  DATA_FLAGS="--preload-file $GEN/assets@/assets"
fi

echo "== 3. compile (WebAssembly)"
if [ -n "$DEBUG" ]; then OPT="-O1 -g -sASSERTIONS=2"; else OPT="-O2"; fi
CXXFLAGS="-std=c++17 $OPT -fwrapv -w -I$HERE/runtime"
# ASYNCIFY flattens every function that can sleep; with the game functions inlined into
# bb_main() that becomes one 400 KB function and the link never finishes (or the browser
# rejects it: "local count too large"). Not inlining mh.cpp keeps the link at about a minute.
MHFLAGS=""; [ "$ASYNC" = "jspi" ] || MHFLAGS="-fno-inline-functions"
PIDS=""
for f in "$GEN/mh.cpp" "$HERE/runtime/bbrt.cpp" "$HERE/runtime/mh_glue.cpp" "$HERE/runtime/web_main.cpp" "$HERE"/runtime/net/*.cpp; do
  X=""; [ "$(basename "$f")" = "mh.cpp" ] && X="$MHFLAGS"
  em++ $CXXFLAGS $X -c "$f" -o "$GEN/$(basename "${f%.cpp}").o" & PIDS="$PIDS $!"
done
for p in $PIDS; do wait $p || { echo "compile failed"; exit 1; }; done

echo "== 4. link (${ASYNC:-asyncify})"
if [ "$ASYNC" = "jspi" ]; then ASYNC_FLAGS="-sJSPI"; else ASYNC_FLAGS="-sASYNCIFY -sASYNCIFY_STACK_SIZE=262144"; fi
em++ $OPT "$GEN/mh.o" "$GEN/bbrt.o" "$GEN/mh_glue.o" "$GEN/web_main.o" "$GEN"/net*.o -o "$OUT/index.html" \
  --shell-file "$HERE/web/shell.html" $DATA_FLAGS \
  $ASYNC_FLAGS -sSTACK_SIZE=2097152 \
  -sALLOW_MEMORY_GROWTH -sINITIAL_MEMORY=134217728 \
  -sFORCE_FILESYSTEM -lidbfs.js -sENVIRONMENT=web \
  -sEXPORTED_FUNCTIONS=_main,_web_key,_web_vbutton,_web_touch,_web_touch_cancel,_web_mouse,_web_set_overlay \
  -sEXPORTED_RUNTIME_METHODS=callMain,FS,IDBFS,addRunDependency,removeRunDependency \
  -sINVOKE_RUN=0 -sEXIT_RUNTIME=0
# give the generated files a nicer name than index.*
for ext in js wasm data; do
  [ -f "$OUT/index.$ext" ] || continue
  mv "$OUT/index.$ext" "$OUT/multihero.$ext"
done
sed -i 's/index\.js/multihero.js/g; s/index\.wasm/multihero.wasm/g; s/index\.data/multihero.data/g' "$OUT/index.html" "$OUT/multihero.js"
cp "$HERE/android/res/mipmap-xxhdpi/ic_launcher.png" "$OUT/icon.png" 2>/dev/null || true
cp "$HERE/web/play_web.bat" "$OUT/"

echo "== done: $OUT"
ls -la "$OUT"
echo "Play: cd build/web && python3 -m http.server 8080   then open http://localhost:8080"
