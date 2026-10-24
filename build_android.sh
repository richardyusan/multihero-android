#!/bin/bash
# Build MultiHero for Android (no Gradle needed).
#   ./build_android.sh [path/to/MultiHero096]      (default: ./game)
# Environment:
#   NDK  Android NDK dir   (default: $ANDROID_NDK_HOME, $ANDROID_NDK_ROOT)
#   SDK  Android SDK dir   (default: $ANDROID_HOME, $ANDROID_SDK_ROOT)
#   BUILD_TOOLS            (default: newest installed build-tools)
#   ABIS                   (default: "arm64-v8a armeabi-v7a x86_64")
#   LIBS_ONLY=1            only transpile + compile the native libraries (no game data needed)
#   KEYSTORE / KS_PASS / KS_ALIAS   release signing key (default: generated debug-style key)
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
GAME_DIR="${1:-$HERE/game}"
NDK="${NDK:-${ANDROID_NDK_HOME:-$ANDROID_NDK_ROOT}}"
SDK="${SDK:-${ANDROID_HOME:-$ANDROID_SDK_ROOT}}"
[ -d "$NDK" ] || { echo "Android NDK not found: set NDK=/path/to/ndk"; exit 1; }
[ -d "$SDK" ] || { echo "Android SDK not found: set SDK=/path/to/sdk"; exit 1; }
[ -n "$LIBS_ONLY" ] || [ -f "$GAME_DIR/maps.dat" ] || { echo "Game data not found in '$GAME_DIR' (see game/README.md)"; exit 1; }
BUILD_TOOLS="${BUILD_TOOLS:-$(ls "$SDK/build-tools" | sort -V | tail -1)}"
BT="$SDK/build-tools/$BUILD_TOOLS"
PLATFORM="$SDK/platforms/android-34/android.jar"
[ -f "$PLATFORM" ] || PLATFORM="$(ls -d "$SDK"/platforms/android-*/android.jar | sort -V | tail -1)"
API=26
ABIS="${ABIS:-arm64-v8a armeabi-v7a x86_64}"
OUT="$HERE/build/android"
TC="$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin"

rm -rf "$OUT"; mkdir -p "$OUT/lib" "$OUT/gen" "$OUT/classes" "$OUT/apk"

echo "== 1. Blitz -> C++"
python3 "$HERE/tools/bb2cpp.py" "$HERE/src/mh.bb" "$OUT/gen/mh.cpp"

if [ -z "$LIBS_ONLY" ]; then
  echo "== 2. assets"
  python3 "$HERE/tools/prep_assets.py" "$GAME_DIR" "$OUT/assets"
fi

echo "== 3. native code"
GLUE="$NDK/sources/android/native_app_glue"
for ABI in $ABIS; do
  case $ABI in
    arm64-v8a) TRIPLE=aarch64-linux-android$API ;;
    armeabi-v7a) TRIPLE=armv7a-linux-androideabi$API ;;
    x86_64) TRIPLE=x86_64-linux-android$API ;;
    x86) TRIPLE=i686-linux-android$API ;;
  esac
  mkdir -p "$OUT/obj/$ABI" "$OUT/lib/$ABI"
  CXX="$TC/clang++ --target=$TRIPLE"
  CFLAGS="-O2 -fPIC -fwrapv -ffunction-sections -fdata-sections -w -I$HERE/runtime -I$GLUE"
  PIDS=""
  $TC/clang --target=$TRIPLE $CFLAGS -c "$GLUE/android_native_app_glue.c" -o "$OUT/obj/$ABI/glue.o" & PIDS="$PIDS $!"
  $CXX -std=c++17 $CFLAGS -c "$OUT/gen/mh.cpp" -o "$OUT/obj/$ABI/mh.o" & PIDS="$PIDS $!"
  $CXX -std=c++17 $CFLAGS -c "$HERE/runtime/bbrt.cpp" -o "$OUT/obj/$ABI/bbrt.o" & PIDS="$PIDS $!"
  $CXX -std=c++17 $CFLAGS -c "$HERE/runtime/mh_glue.cpp" -o "$OUT/obj/$ABI/mh_glue.o" & PIDS="$PIDS $!"
  $CXX -std=c++17 $CFLAGS -Werror=unguarded-availability -c "$HERE/runtime/android_main.cpp" -o "$OUT/obj/$ABI/android_main.o" & PIDS="$PIDS $!"
  for p in $PIDS; do wait $p || { echo "compile failed ($ABI)"; exit 1; }; done
  $CXX -shared -o "$OUT/lib/$ABI/libmultihero.so" "$OUT/obj/$ABI/"*.o \
      -static-libstdc++ -Wl,--gc-sections -Wl,-z,max-page-size=16384 -Wl,--exclude-libs,ALL \
      -Wl,--no-undefined -u ANativeActivity_onCreate -landroid -llog -lEGL -lGLESv2 -laaudio -lm
  "$TC/llvm-strip" --strip-unneeded "$OUT/lib/$ABI/libmultihero.so"
  ls -la "$OUT/lib/$ABI/libmultihero.so"
done

[ -n "$LIBS_ONLY" ] && { echo "OK (native libraries only) -> $OUT/lib"; exit 0; }

echo "== 4. java"
javac -source 8 -target 8 -nowarn -bootclasspath "$PLATFORM" -classpath "$PLATFORM" -d "$OUT/classes" \
    "$HERE/android/java/com/multihero/port/MHActivity.java" 2>&1 | grep -v "warning" || true
"$BT/d8" --min-api $API --lib "$PLATFORM" --output "$OUT/apk" $(find "$OUT/classes" -name '*.class')

echo "== 5. resources + package"
"$BT/aapt2" compile --dir "$HERE/android/res" -o "$OUT/res.zip"
"$BT/aapt2" link -o "$OUT/unsigned.apk" -I "$PLATFORM" --manifest "$HERE/android/AndroidManifest.xml" \
    "$OUT/res.zip" -A "$OUT/assets" --min-sdk-version $API --target-sdk-version 34 --auto-add-overlay
cd "$OUT/apk"
zip -q "$OUT/unsigned.apk" classes.dex
cd "$OUT"
for ABI in $ABIS; do zip -q "$OUT/unsigned.apk" "lib/$ABI/libmultihero.so"; done

echo "== 6. align + sign"
"$BT/zipalign" -f -p 4 "$OUT/unsigned.apk" "$OUT/aligned.apk"
KS="${KEYSTORE:-$HERE/android/multihero.keystore}"
KS_PASS="${KS_PASS:-multihero}"
KS_ALIAS="${KS_ALIAS:-multihero}"
if [ ! -f "$KS" ]; then
  echo "   (creating signing key $KS - keep it, updates must use the same key; never commit it)"
  keytool -genkeypair -keystore "$KS" -storepass "$KS_PASS" -keypass "$KS_PASS" -alias "$KS_ALIAS" \
      -keyalg RSA -keysize 2048 -validity 10000 -dname "CN=MultiHero Android Port" >/dev/null 2>&1
fi
"$BT/apksigner" sign --ks "$KS" --ks-pass pass:"$KS_PASS" --key-pass pass:"$KS_PASS" --ks-key-alias "$KS_ALIAS" \
    --out "$HERE/build/MultiHero.apk" "$OUT/aligned.apk"
"$BT/apksigner" verify "$HERE/build/MultiHero.apk" && echo "OK -> $HERE/build/MultiHero.apk"
ls -la "$HERE/build/MultiHero.apk"
