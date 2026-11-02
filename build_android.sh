#!/bin/bash
# Build MultiHero for Android (no Gradle needed).
#   ./build_android.sh [path/to/MultiHero096]      (default: ./game)
# Environment:
#   NDK  Android NDK dir   (default: $ANDROID_NDK_HOME, $ANDROID_NDK_ROOT)
#   SDK  Android SDK dir   (default: $ANDROID_HOME, $ANDROID_SDK_ROOT)
#   BUILD_TOOLS            (default: newest installed build-tools)
#   ABIS                   (default: "arm64-v8a armeabi-v7a x86_64")
#   EOS_SDK                Epic Online Services SDK folder with Include/ and Bin/Android/
#                          (default: third_party/eos/SDK, filled by tools/setup_eos.py; NO_EOS=1 to skip)
#   ANDROID_DEPS           androidx libraries for the EOS Java code (default: third_party/android-deps)
#   EXTRA_CFLAGS / EXTRA_LDFLAGS   extra compiler / linker flags
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
python3 "$HERE/tools/bb2cpp.py" "$HERE/src/mh.bb" "$OUT/gen/mh.cpp" "$HERE"/runtime/decls/*.decls

if [ -z "$LIBS_ONLY" ]; then
  echo "== 2. assets"
  python3 "$HERE/tools/prep_assets.py" "$GAME_DIR" "$OUT/assets" "$HERE/config"
fi

echo "== 2b. Epic Online Services"
EOS_SDK="${EOS_SDK:-$HERE/third_party/eos/SDK}"
ANDROID_DEPS="${ANDROID_DEPS:-$HERE/third_party/android-deps}"
EOS_AAR="$(ls "$EOS_SDK"/Bin/Android/*/aar/*.aar 2>/dev/null | head -1)"
WITH_EOS=""
JAVA_LIBS=""          # extra jars for javac + d8
RES_LIBS=""           # compiled resources of the .aar libraries
EXTRA_PKGS=""         # their packages (R classes)
if [ -z "$NO_EOS" ] && [ -f "$EOS_SDK/Include/eos_sdk.h" ] && [ -n "$EOS_AAR" ]; then
  [ -f "$ANDROID_DEPS/deps.txt" ] || { echo "androidx libraries missing: python3 tools/fetch_android_deps.py $ANDROID_DEPS"; exit 1; }
  case "$BUILD_TOOLS" in
    [12]*|3[0-4]*) echo "Epic Online Services needs Android build-tools 35 or newer (d8 of $BUILD_TOOLS crashes on the EOS classes):"
                   echo "   sdkmanager \"build-tools;35.0.0\"   (or NO_EOS=1 to build without Epic online)"; exit 1 ;;
  esac
  WITH_EOS=1
  mkdir -p "$OUT/aar"
  unpack_aar() {   # <aar> <name>: classes.jar -> JAVA_LIBS, res/ -> RES_LIBS, package -> EXTRA_PKGS
    local d="$OUT/aar/$2"
    mkdir -p "$d"; (cd "$d" && unzip -qo "$1")
    # ?attr/colorControlNormal comes from AppCompat (not used here): use the framework attribute
    [ -d "$d/res" ] && grep -rl '?attr/colorControlNormal' "$d/res" | xargs -r sed -i 's#?attr/colorControlNormal#?android:attr/colorControlNormal#g'
    [ -f "$d/classes.jar" ] && JAVA_LIBS="$JAVA_LIBS $d/classes.jar"
    for j in "$d"/libs/*.jar; do [ -f "$j" ] && JAVA_LIBS="$JAVA_LIBS $j"; done
    local pkg; pkg="$(grep -o 'package="[^"]*"' "$d/AndroidManifest.xml" | head -1 | cut -d'"' -f2)"
    if [ -d "$d/res" ] && [ -n "$(ls -A "$d/res")" ]; then
      "$BT/aapt2" compile --dir "$d/res" -o "$OUT/aar/$2.res.zip"
      RES_LIBS="$RES_LIBS -R $OUT/aar/$2.res.zip"
    fi
    [ -n "$pkg" ] && EXTRA_PKGS="$EXTRA_PKGS:$pkg"
    return 0
  }
  unpack_aar "$EOS_AAR" eossdk
  while read -r coord file; do
    [ -n "$file" ] || continue
    case "$file" in
      *.aar) unpack_aar "$ANDROID_DEPS/$file" "${file%.aar}" ;;
      *.jar) JAVA_LIBS="$JAVA_LIBS $ANDROID_DEPS/$file" ;;
    esac
  done < "$ANDROID_DEPS/deps.txt"
  EXTRA_PKGS="${EXTRA_PKGS#:}"
  # the EOS aar expects the app to define these two resources (login redirect scheme = "eos." + client id)
  CLIENT_ID="$( (cat "$HERE/config/online.cfg"; cat "$HERE/config/online.local.cfg" 2>/dev/null) | \
      sed -n 's/^[[:space:]]*eos_client_id[[:space:]]*=[[:space:]]*\([^[:space:]#]*\).*/\1/p' | grep . | tail -1)"
  [ -n "$CLIENT_ID" ] || echo "   warning: eos_client_id not set in config/online.local.cfg - the lobby will show an error"
  mkdir -p "$OUT/eosres/values"
  cat > "$OUT/eosres/values/eos.xml" <<EOX
<?xml version="1.0" encoding="utf-8"?>
<resources>
    <string name="eos_login_protocol_scheme" translatable="false">eos.$(echo "${CLIENT_ID:-none}" | tr 'A-Z' 'a-z')</string>
    <color name="eos_customTabToolbarColor">#FF1A1A2E</color>
</resources>
EOX
  echo "   EOS SDK: $EOS_AAR"
  echo "   EOS ABIs: $(ls "$OUT/aar/eossdk/jni" | tr '\n' ' ')(other ABIs build without Epic online, they use the demo lobby)"
else
  echo "   EOS SDK not found ($EOS_SDK) - building without Epic online (see docs/ONLINE.md)"
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
  ABI_CFLAGS=""; ABI_LDFLAGS=""
  if [ -n "$WITH_EOS" ] && [ -f "$OUT/aar/eossdk/jni/$ABI/libEOSSDK.so" ]; then
    ABI_CFLAGS="-DMH_WITH_EOS -I$EOS_SDK/Include"
    ABI_LDFLAGS="-L$OUT/aar/eossdk/jni/$ABI -lEOSSDK"
    cp "$OUT/aar/eossdk/jni/$ABI/libEOSSDK.so" "$OUT/lib/$ABI/"
  fi
  # -ffp-contract=off: no fused multiply-add, so the game's float maths gives the same result on
  # every CPU (online matches run the same simulation on every phone)
  CFLAGS="-O2 -fPIC -fwrapv -ffp-contract=off -ffunction-sections -fdata-sections -w -I$HERE/runtime -I$GLUE $ABI_CFLAGS $EXTRA_CFLAGS"
  PIDS=""
  $TC/clang --target=$TRIPLE $CFLAGS -c "$GLUE/android_native_app_glue.c" -o "$OUT/obj/$ABI/glue.o" & PIDS="$PIDS $!"
  $CXX -std=c++17 $CFLAGS -c "$OUT/gen/mh.cpp" -o "$OUT/obj/$ABI/mh.o" & PIDS="$PIDS $!"
  $CXX -std=c++17 $CFLAGS -c "$HERE/runtime/bbrt.cpp" -o "$OUT/obj/$ABI/bbrt.o" & PIDS="$PIDS $!"
  $CXX -std=c++17 $CFLAGS -c "$HERE/runtime/mh_glue.cpp" -o "$OUT/obj/$ABI/mh_glue.o" & PIDS="$PIDS $!"
  $CXX -std=c++17 $CFLAGS -Werror=unguarded-availability -c "$HERE/runtime/android_main.cpp" -o "$OUT/obj/$ABI/android_main.o" & PIDS="$PIDS $!"
  for NS in "$HERE"/runtime/net/*.cpp; do
    $CXX -std=c++17 $CFLAGS -c "$NS" -o "$OUT/obj/$ABI/net_$(basename "$NS" .cpp).o" & PIDS="$PIDS $!"
  done
  for p in $PIDS; do wait $p || { echo "compile failed ($ABI)"; exit 1; }; done
  $CXX -shared -o "$OUT/lib/$ABI/libmultihero.so" "$OUT/obj/$ABI/"*.o \
      -static-libstdc++ -Wl,--gc-sections -Wl,-z,max-page-size=16384 -Wl,--exclude-libs,ALL \
      -Wl,--no-undefined -u ANativeActivity_onCreate -landroid -llog -lEGL -lGLESv2 -laaudio -lm $ABI_LDFLAGS $EXTRA_LDFLAGS
  "$TC/llvm-strip" --strip-unneeded "$OUT/lib/$ABI/libmultihero.so"
  ls -la "$OUT/lib/$ABI/libmultihero.so"
done

[ -n "$LIBS_ONLY" ] && { echo "OK (native libraries only) -> $OUT/lib"; exit 0; }

echo "== 4. resources"
"$BT/aapt2" compile --dir "$HERE/android/res" -o "$OUT/res.zip"
MANIFEST="$HERE/android/AndroidManifest.xml"
LINK_EXTRA=""
if [ -n "$WITH_EOS" ]; then
  "$BT/aapt2" compile --dir "$OUT/eosres" -o "$OUT/eosres.zip"
  # manifest additions of the EOS aar (login redirect activity, browser queries, network state)
  MANIFEST="$OUT/AndroidManifest.xml"
  python3 - "$HERE/android/AndroidManifest.xml" "$MANIFEST" <<'PYEOF'
import sys
s = open(sys.argv[1]).read()
perm = """    <uses-permission android:name="android.permission.ACCESS_NETWORK_STATE" />
    <uses-permission android:name="android.permission.ACCESS_WIFI_STATE" />
    <queries>
        <intent>
            <action android:name="android.intent.action.VIEW" />
            <category android:name="android.intent.category.BROWSABLE" />
            <data android:scheme="https" />
        </intent>
        <intent>
            <action android:name="android.support.customtabs.action.CustomTabsService" />
        </intent>
    </queries>
"""
act = """        <activity
            android:name="com.epicgames.mobile.eossdk.EOSAuthHandlerActivity"
            android:configChanges="keyboardHidden|orientation|screenSize"
            android:exported="true"
            android:noHistory="true">
            <intent-filter>
                <action android:name="android.intent.action.VIEW" />
                <category android:name="android.intent.category.DEFAULT" />
                <category android:name="android.intent.category.BROWSABLE" />
                <data android:scheme="@string/eos_login_protocol_scheme" />
            </intent-filter>
        </activity>
"""
s = s.replace('    <uses-feature android:glEsVersion', perm + '    <uses-feature android:glEsVersion', 1)
s = s.replace('    </application>', act + '    </application>', 1)
open(sys.argv[2], 'w').write(s)
PYEOF
  mkdir -p "$OUT/rjava"
  LINK_EXTRA="$RES_LIBS -R $OUT/eosres.zip --java $OUT/rjava --extra-packages $EXTRA_PKGS"
fi
"$BT/aapt2" link -o "$OUT/unsigned.apk" -I "$PLATFORM" --manifest "$MANIFEST" \
    "$OUT/res.zip" $LINK_EXTRA -A "$OUT/assets" --min-sdk-version $API --target-sdk-version 34 --auto-add-overlay

echo "== 5. java"
CP="$PLATFORM"
for j in $JAVA_LIBS; do CP="$CP:$j"; done
SRCS="$HERE/android/java/com/multihero/port/MHActivity.java"
[ -n "$WITH_EOS" ] && SRCS="$SRCS $(find "$OUT/rjava" -name '*.java' | tr '\n' ' ')"
javac -source 8 -target 8 -nowarn -encoding UTF-8 -bootclasspath "$PLATFORM" -classpath "$CP" -d "$OUT/classes" \
    $SRCS 2>&1 | grep -v "warning" || true
"$BT/d8" --release --min-api $API --lib "$PLATFORM" --output "$OUT/apk" $(find "$OUT/classes" -name '*.class') $JAVA_LIBS

echo "== 6. package"
cd "$OUT/apk"
zip -q "$OUT/unsigned.apk" classes*.dex
cd "$OUT"
for ABI in $ABIS; do zip -q "$OUT/unsigned.apk" lib/$ABI/*.so; done

echo "== 7. align + sign"
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
