# Building MultiHero for Android

This document covers the toolchain, the build commands, APK signing, the problems met during the
port (and how they were solved), and how assets and touch input work inside the runtime.
For a general overview see [README.md](README.md) and [docs/HOW_IT_WORKS.md](docs/HOW_IT_WORKS.md).

---

## 1. Toolchain versions

These are the exact versions the released APK (v1.0.1) was built and tested with.

| Component | Version | Notes |
|---|---|---|
| Android NDK | **r28c** (`28.2.13676358`) | clang 19.0.1; any NDK r26+ should work |
| Android SDK build-tools | **35.0.0** (34 without EOS) | `aapt2`, `d8`, `zipalign`, `apksigner`; d8 34 crashes on the EOS classes |
| Epic Online Services SDK | **1.19.2.1** (optional) | Android SDK zip, unpacked by `tools/setup_eos.py` into `third_party/` |
| Android SDK platform | **android-34** (rev 3) | only `android.jar` is used |
| Android SDK cmdline-tools | 10.0 (`commandlinetools-linux-9862592`) | only needed to install the packages above |
| JDK | OpenJDK **21** (17+ works) | `javac`, `keytool`; Java source compiled as Java 8 |
| Python | 3.13 (3.8+ works) | transpiler and asset tools |
| Pillow | 12.3 | asset preparation (JPG → BMP), icon generation |
| Host C++ compiler | g++ 13.3 (any C++17 compiler) | only for the Linux test build |
| `zip` | any | APK packaging |
| **Gradle** | **not used** | see below |

**Android target settings** (`android/AndroidManifest.xml`, `build_android.sh`):

| Setting | Value |
|---|---|
| Package name | `com.multihero.port` |
| `minSdkVersion` | 26 (Android 8.0, needed for AAudio) |
| `targetSdkVersion` | 34 |
| ABIs | `arm64-v8a`, `armeabi-v7a`, `x86_64` |
| Graphics / audio | OpenGL ES 2.0, AAudio |
| Orientation | `sensorLandscape` |

### Why no Gradle?

The app is one native library plus a 30-line Java activity, so it is built directly with the SDK
command-line tools (`clang`, `javac`, `d8`, `aapt2`, `zipalign`, `apksigner`). That avoids the Gradle
/ Android Gradle Plugin version maze, needs no network access during the build, and takes about
two minutes. If you ever want to open it in Android Studio, create an empty "Native C++" project, copy
`runtime/` + the generated `mh.cpp` into `app/src/main/cpp`, the Java class into `app/src/main/java`,
the prepared assets into `app/src/main/assets`, and list the same sources in `CMakeLists.txt`
(link `android log EGL GLESv2 aaudio`).

### Installing the toolchain (Linux / WSL)

```bash
sudo apt install openjdk-21-jdk-headless python3 python3-pip zip unzip g++
pip install pillow

mkdir -p ~/android && cd ~/android
curl -O https://dl.google.com/android/repository/commandlinetools-linux-9862592_latest.zip
unzip commandlinetools-linux-9862592_latest.zip
yes | cmdline-tools/bin/sdkmanager --sdk_root=$HOME/android/sdk \
      "build-tools;34.0.0" "platforms;android-34" "ndk;28.2.13676358"

export ANDROID_HOME=$HOME/android/sdk
export ANDROID_NDK_HOME=$HOME/android/sdk/ndk/28.2.13676358
```

On Windows, use WSL (Ubuntu); the script is a bash script and uses the Linux NDK toolchain.

---

## 2. Build commands

### Full APK

```bash
# game data: copy the original MultiHero096 folder contents into ./game first
./build_android.sh                       # or: ./build_android.sh /path/to/MultiHero096
# -> build/MultiHero.apk
```

Environment variables:

| Variable | Default | Meaning |
|---|---|---|
| `NDK` | `$ANDROID_NDK_HOME` / `$ANDROID_NDK_ROOT` | NDK folder |
| `SDK` | `$ANDROID_HOME` / `$ANDROID_SDK_ROOT` | SDK folder |
| `BUILD_TOOLS` | newest installed | build-tools version folder |
| `ABIS` | `arm64-v8a armeabi-v7a x86_64` | e.g. `ABIS=arm64-v8a` for a faster test build |
| `LIBS_ONLY=1` | – | stop after the native libraries (no game data needed; used by CI) |
| `EXTRA_CFLAGS`, `EXTRA_LDFLAGS` | – | extra compiler / linker flags, e.g. for the Epic Online Services SDK (docs/ONLINE.md) |
| `KEYSTORE`, `KS_PASS`, `KS_ALIAS` | `android/multihero.keystore`, `multihero`, `multihero` | signing key |

### What the script does

| Step | Command (simplified) | Output |
|---|---|---|
| 1. Transpile | `python3 tools/bb2cpp.py src/mh.bb build/android/gen/mh.cpp runtime/decls/*.decls` | one C++ file (~29k lines) |
| 2. Assets | `python3 tools/prep_assets.py game build/android/assets config` | lower-case asset tree + `online.cfg` + `manifest.txt` |
| 3. Native | `clang++ --target=aarch64-linux-android26 -O2 -fPIC -fwrapv …` for `mh.cpp`, `bbrt.cpp`, `mh_glue.cpp`, `android_main.cpp`, every `runtime/net/*.cpp`, `android_native_app_glue.c`; link with `-static-libstdc++ -Wl,--no-undefined -Wl,-z,max-page-size=16384 -u ANativeActivity_onCreate -landroid -llog -lEGL -lGLESv2 -laaudio` | `lib/<abi>/libmultihero.so` (~2 MB each) |
| 2b. EOS (if `third_party/eos` exists) | unpack the EOS `.aar` and androidx `.aar`s, `aapt2 compile` their `res/`, write `eos.xml` (login scheme from the client id), merge EOS manifest entries | per-ABI `-DMH_WITH_EOS` + `-lEOSSDK` for arm64-v8a / x86_64 |
| 4. Resources | `aapt2 compile --dir android/res`, `aapt2 link --manifest … -A assets` (+ `-R` library resources, `--java` R classes, `--extra-packages`) | `unsigned.apk` |
| 5. Java | `javac -source 8 -target 8 MHActivity.java R.java…`, then `d8 --release --min-api 26` with the EOS / androidx jars | `classes.dex` |
| 6. Package | `zip` dex + `lib/<abi>/*.so` (incl. `libEOSSDK.so`) | `unsigned.apk` |
| 7. Sign | `zipalign -p 4`, `apksigner sign` | `build/MultiHero.apk` (~12 MB, ~32 MB with EOS) |

Important flags:

- `-fwrapv` – Blitz integers wrap around on overflow; without it C++ treats overflow as undefined.
- `-Wl,--no-undefined` – fail at link time instead of crashing on the phone with a missing symbol.
- `-z max-page-size=16384` – required for Android 15+ devices with 16 KB memory pages.
- `-Werror=unguarded-availability` on `android_main.cpp` – catches APIs newer than minSdk 26.

### Adding native functions for the Blitz code (`.decls`)

`bb2cpp.py` reads Blitz3D "userlib" style declaration files given after the output file. The build
scripts pass every `runtime/decls/*.decls`:

```
; runtime/decls/net.decls
.lib " "
NetPing%()                 ; returns int      -> C++: int  bb_netping()
NetRoomName$(i%)           ; returns string   -> C++: BStr bb_netroomname(int)
NetSetReady(ready%)        ; no suffix: void  -> C++: void bb_netsetready(int)
```

The generated `mh.cpp` contains the prototypes; implement the `bb_<lowercase name>` functions in any
C++ file that the build compiles (for a new folder, add it to `build_android.sh` and `scripts/test_linux.sh`).

### Install and run

```bash
adb install -r build/MultiHero.apk
adb logcat -s MultiHero          # runtime log: asset problems, FPS every 3 s, exit code
```

### Test on a PC without a phone

```bash
scripts/test_linux.sh            # builds build/mh_linux and runs a scripted smoke test
```

The headless backend is described in [docs/HOW_IT_WORKS.md](docs/HOW_IT_WORKS.md#testing-without-a-phone)
(`MH_SCRIPT` input scripts, `MH_SHOTS` screenshots, `MH_FAST=1` virtual clock).

---

## 3. Signing

Android only installs signed APKs, and an **update must be signed with the same key** as the version
already installed.

### Default key

On the first build the script creates `android/multihero.keystore`
(alias `multihero`, password `multihero`, RSA 2048, valid 10,000 days). The published v1.0.x APKs
are signed with this key.

- Keep a backup of this file. If it is lost, users must uninstall the old app (losing their
  saves) before installing a new build.
- Never commit it: `*.keystore` and `*.jks` are in `.gitignore`.

### Your own release key

```bash
keytool -genkeypair -v -keystore ~/keys/multihero-release.jks -alias release \
        -keyalg RSA -keysize 2048 -validity 10000

KEYSTORE=~/keys/multihero-release.jks KS_ALIAS=release KS_PASS='your-password' ./build_android.sh
```

Switching keys means the new APK cannot update an APK signed with the old key.

### Checking a signature

```bash
$ANDROID_HOME/build-tools/34.0.0/apksigner verify --print-certs build/MultiHero.apk
```

The script signs with APK Signature Scheme v2/v3 (apksigner defaults for minSdk 26).

---

## 4. Problems and pitfalls, with solutions

Rows marked **●** are problems that actually occurred while building and testing this port; the
others are known Blitz/Android pitfalls that were handled up front.

### Transpiler / Blitz language

| Symptom | Cause | Solution |
|---|---|---|
| **●** `moves1.bb:49: end of statement expected` at `zFallTime(en)=0zHitSeq(en)=0` | Blitz accepts two statements on one line **without** a `:` separator | Parser no longer requires a terminator between statements (same as the Blitz compiler) |
| **●** `error: duplicate 'const'` in generated C++ | string `Const` emitted as `static const const char*` | String constants are generated as `static const BStr` |
| `Not a = 0` must mean `Not (a = 0)` | in Blitz, `Not` has the **lowest** precedence (`Not (a = 0)`) | Parser applies `Not` to the whole And/Or expression |
| Arithmetic must match the PC version | Blitz rounds float→int, `int/int` is integer division, `And/Or` are bitwise | Typed code generation: `bb_f2i()` (`lrintf`), `bb_idiv()`, `&`/`|` on ints |
| `Exit` inside `Select` left only the `Select` | C++ `break` in a `switch` | `Select` is generated as an `if/else` chain |
| Last line of one file merging with the next file | `.bb` files without a final newline | Each `Include` file is tokenized separately |

### Game data

| Symptom | Cause | Solution |
|---|---|---|
| Missing images on Android | Windows paths are case-insensitive and use `\` | `prep_assets.py` lower-cases every path; the runtime lower-cases and converts `\` → `/` |
| Map thumbnails not shown | they are **JPG** files loaded with `LoadImage` | converted to BMP at build time (file name kept, the BMP decoder checks the `BM` header) |
| Wrong colours on some sprites | 358 BMPs are 16-bit (`BI_RGB` = 5-5-5) | BMP decoder handles 1/4/8/16/24/32-bit and RLE4/RLE8 |
| Edges around sprites | game default is 16-bit video, where near-black pixels become the black mask colour | in 16-bit mode masks are compared at 5-6-5 precision; `game.cfg` selects 32-bit (exact compare) |
| `ReadDir` found no mod folders | APK assets have no directory listing | `manifest.txt` lists every asset; `ReadDir`/`FileType` use it plus the writable data folder |

### Android build

| Symptom | Cause | Solution |
|---|---|---|
| **●** `'AAudioStreamBuilder_setUsage' is unavailable: introduced in Android 28` | minSdk is 26 | call removed (default usage is fine) |
| **●** APK built "successfully" but would crash on start | compile steps ran in the background and a failure was ignored, so `android_main.o` was missing | script waits for each job and exits on failure; `--no-undefined` at link time |
| Blurry or uneven pixels | plain bilinear / nearest scaling of 480p to 1080p | "sharp bilinear" fragment shader |
| Wrong sampling on some GPUs | `mediump` floats lose precision at texel coordinates near 960 | shader uses `highp` when `GL_FRAGMENT_PRECISION_HIGH` is defined |
| Colours swapped red/blue | framebuffer is `0x00RRGGBB` (bytes B,G,R,X) uploaded as `GL_RGBA` | shader reads `.bgr` |
| `d8`: `NullPointerException … EOSLog$Category.class` | bug in d8 8.2 (build-tools 34) | build-tools 35+ (`build_android.sh` checks it) |
| `aapt2`: `attr/colorControlNormal not found` in `close_24px.xml` | EOS drawable expects AppCompat | build script rewrites it to `?android:attr/colorControlNormal` |
| EOS classes need `androidx.core.graphics.Insets` | `androidx.browser` only pulls `core` 1.1.0 | `fetch_android_deps.py` adds `core` 1.6.0 (newest without Kotlin) |
| Linux test: `Fatal error … openssl_certificate.cc Check failed: bio` at exit | EOS P2P threads still running while OpenSSL cleans up | `EOS_Platform_Release` + `EOS_Shutdown` in an `atexit` handler |
| Guest READY not seen by the host | two lobby updates in flight, the older one won | one update at a time, each with all member attributes |

### Runtime behaviour on the phone

| Symptom | Cause | Solution |
|---|---|---|
| **●** Game ran too fast (~95 fps instead of 54) | `WaitTimer` returned without waiting when less than 1 ms was left (the sleep was truncated to 0 ms) | `WaitTimer` sleeps until at least one full tick has passed; display hard-capped at 60 fps |
| **●** Options → Controls → CONFIG ignored touch and buttons | the game's `getTheKey()` loops on `KeyHit` without `Flip`/`Delay`, so Android events were never read | `KeyHit`, `KeyDown`, `JoyHit`, `JoyDown`, `MouseHit`, `MouseDown`, `GetKey` pump OS events themselves (`input_poll()`), sleeping 1 ms now and then to avoid 100% CPU |
| Screens that rely on `Flip` vsync ran too fast | Android swap interval 0 | covered by the 60 fps cap in `present_frame()` |
| Saves lost / not written | assets are read-only | writes go to the app's internal storage and override the asset of the same name |
| Game state reset after rotation | activity recreated on configuration change | `configChanges` set in the manifest, landscape only, `singleTask` |

### Installing

| Message | Fix |
|---|---|
| "App not installed" / `INSTALL_FAILED_UPDATE_INCOMPATIBLE` | installed version was signed with a different key – uninstall it first (saves are lost) |
| `INSTALL_FAILED_NO_MATCHING_ABIS` | build with the right `ABIS` (32-bit phones need `armeabi-v7a`) |
| "Blocked by Play Protect" | sideloaded, self-signed APK – choose "Install anyway" |
| Black screen | check `adb logcat -s MultiHero` for "cannot decode image" or "RuntimeError" lines |

---

## 5. How assets are loaded

```
game/ (original data) --prep_assets.py--> build/android/assets/  --aapt2 -A--> APK assets/
                                             ├── gfx/..., sounds/..., maps/original/...  (all lower case)
                                             └── manifest.txt                              (one path per line)
```

1. **Build time** (`tools/prep_assets.py`): copies everything except `.exe/.dll/.zip/.psd/.bat`,
   lower-cases every path, converts `.jpg/.png` to BMP content (same file name) and writes
   `manifest.txt`. `aapt2` stores the assets compressed in the APK.
2. **Start-up** (`rt_init()`): `manifest.txt` is read into a set of files and directories.
3. **Every file access from the game** (`LoadImage`, `LoadSound`, `ReadFile`, `FileType`, `ReadDir`):
   - the path is normalised: `\` → `/`, lower case, `./` and duplicate slashes removed;
   - **writable storage first**: `<internalDataPath>/<path>` (this is where `save.dat`, `keys.cfg`,
     `game.cfg` and F11 screenshots are written);
   - **then the APK**: `AAssetManager_open(path, AASSET_MODE_BUFFER)` reads the whole file into memory.
4. **Decoding**: BMP → 32-bit image (`0x00RRGGBB`) with a mask colour (black by default);
   WAV (8/16-bit PCM, mono/stereo, any rate) → stereo 16-bit, resampled to 44.1 kHz while mixing.
5. **Writing**: `WriteFile` buffers in memory; `CloseFile` creates the folders and writes the file to
   internal storage.

Images are loaded exactly when the original code calls `LoadImage`: the shared graphics (items,
objects, menus, icons) at start-up, character sprites when a character is first used, and map
graphics when a stage is loaded.

---

## 6. How touch input works

### Screen layout

The game draws into a 640×480 buffer. Each frame the runtime builds a **composite frame** with the
same aspect ratio as the phone (480 pixels high, e.g. 1067 wide on a 20:9 screen):

```
┌────────────┬──────────────────────────┬────────────┐
│ ESC        │                          │         OK │
│ UNLOCK ALL │      game 640 × 480      │JUTSU  THROW│
│ (main menu)│   (touch = mouse click)  │   BLOCK    │
│   D-pad    │                          │ SPEC.  JUMP│
│            │                          │   ATTACK   │
└────────────┴──────────────────────────┴────────────┘
```

Sizes (in the 480-pixel-high composite, so they scale with the screen): D-pad radius 108, action
buttons radius 48 (THROW and JUTSU 36), ESC/OK pills 36 high. Each button has a pixel-art icon and a label.
On 16:9 and 4:3 screens the controls overlap the picture edges and are drawn semi-transparent there.

The Blitz code tells the overlay which screen is shown with `PortScreen(id)` (`runtime/decls/port.decls`):
`1` = main menu (shows **UNLOCK ALL**), `2` = online lobby (D-pad and action buttons hidden, so they
never cover the lobby buttons).
The composite is uploaded as a GL texture and drawn full screen.

### Event path

```
Android MotionEvent ──▶ on_input() (android_main.cpp)
   pointer id, x / window width, y / window height, DOWN / MOVE / UP
      ──▶ rt_touch(id, nx, ny, action) (bbrt.cpp)
            ├── hit a button?  → set_button() → key down/up   (closest button wins, with a small margin)
            ├── hit the D-pad? → update_dpad() → direction keys
            └── else           → mouse: MouseX/Y in game coordinates, MouseHit(1), MouseDown(1)
```

- **Multi-touch**: each pointer id is remembered with what it pressed (`g_touch_owner`), so you can
  hold the D-pad and press ATTACK at the same time; lifting a finger releases only its own button.
- **D-pad**: the angle from the centre selects up to two directions (8-way), with a dead zone of 22%
  of the radius; sliding the finger changes direction without lifting it.
- **Buttons → game keys**: `mh_glue.cpp` reads player 1's configuration from the game's own arrays
  (`zController(1)`, `upK(1)`, `shotK(1)`, …, loaded from `keys.cfg`). A button sends that scancode,
  or a joystick button if player 1 is set to a controller. The D-pad also sends the arrow keys so
  menus that use the arrows work.
- **ESC / OK**: fixed scancodes 1 (Esc) and 28 (Enter). The Android Back key also sends Esc.
- **UNLOCK ALL**: queues the game's cheat as key events, one per frame (Enter, Backspace ×5, Enter),
  because `mainMenu()` counts one Backspace per frame. When `cheat(1)` becomes 1 a message is shown.
  The cheat block in `menus.bb` was extended to open every Adventure and CTF map that exists.
- **JUTSU**: frame 0 BLOCK down, frame 1 SPECIAL down, frame 3 SPECIAL up, BLOCK up when the finger
  lifts. `Getinput` sees BLOCK held + SPECIAL hit in the same frame and sets `superKey`.
- **Press counting**: every action keeps a press count (`vb_press`), so two sources holding the same
  action (BLOCK + JUTSU, touch + gamepad) only send one key down and one key up.
- **Key state**: `rt_key()` keeps a "down" table for `KeyDown` and a hit counter for `KeyHit`
  (number of presses since the last call, as in Blitz).
- **Pause**: on `APP_CMD_PAUSE` / focus loss all touches are released (`rt_touch_cancel_all()`) so no
  key stays stuck.

### Gamepads and keyboards

- Gamepad buttons: A = ATTACK, B = JUMP, X = SPECIAL, Y / L2 / R2 = BLOCK, L1 / R1 = THROW,
  Start = Enter, Select = Esc. Sticks and the hat switch drive the directions (threshold 0.5).
  They go through `rt_vbutton()`, which uses the same mapping as the touch buttons.
- The touch overlay hides while a gamepad is used and comes back on the next touch.
- Hardware keyboards are mapped from Android key codes to the DirectInput scancodes Blitz uses,
  so the game's own key configuration applies.

### Threading

`android_main()` runs the game code on the NativeActivity glue thread. OS events are processed inside
`Flip`, `Delay`, `WaitTimer` and the input functions, on that same thread, so input state needs no extra
synchronisation. While the app is in the background `plat_pump()` blocks, so the game simply freezes
and resumes. Audio runs in the AAudio callback thread and shares the channel list under a mutex.
