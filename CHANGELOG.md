# Changelog

## 1.5.0 – 2026-11-01
- **Ping indicator in online matches**: signal bars and ms at the bottom centre of the screen (for the host the
  slowest player, for the others their ping to the host).
- **Character picker that works**: "[change]" under your name opens a grid with the 10 fighters; the room shows
  every player's fighter. (In 1.4.0 the button was outside the lobby's button range, so taps were ignored.)
- **Lives selector**: the host chooses 1–9 lives per player (LIVES button, shared through the Epic lobby).
- versionCode 6, versionName 0.96-android1.5.

## 1.4.0 – 2026-11-01
- **Real-time online matches.** After START the match is played online: every player controls their own
  fighter and sees the others live. Deterministic lockstep: phones exchange only buttons (one 15-bit mask per
  frame, input delay 3–12 frames from the room's pings) over Epic P2P or the VPS relay, with a per-second state
  checksum. ESC twice leaves (the CPU plays on for you); lost connections return to the lobby. Tested with two
  clients on Epic's live servers: identical checksums for whole matches.
- Room: pick your character (tap the button next to your name), the host picks the stage (or random).
- Fixed: some adventure enemies never attacked. **Venom** (the monster of stage 50), **Shredder**, **Thug** and
  **Pig** had empty move functions in the 0.96 source, so after their first attack command they froze in the
  attack state. They now have full move sets made from their existing sprites (Venom: long tongue lunge, claw
  uppercut, web whip, air claw), and their attack distances match the new moves.
- Native code built with `-ffp-contract=off` (identical float maths on every CPU, needed for online play).
- Linux test runner: `click` script command (desktop mouse).
- versionCode 5, versionName 0.96-android1.4.

## 1.3.0 – 2026-11-01
- **Epic Online Services** backend for MULTIPLAYER ONLINE (`runtime/net/net_eos.cpp`): anonymous device-id
  login (no Epic account), room list from EOS lobby search, create / join / leave, READY and ping as lobby member
  attributes, START with a shared seed through lobby attributes, P2P (NAT traversal / Epic relays) for ping and
  game data. Tested host + guest against Epic's live servers.
- Android build links the EOS SDK when `third_party/eos` is present (`tools/setup_eos.py` unpacks Epic's zips,
  `tools/fetch_android_deps.py` gets the androidx libraries): `libEOSSDK.so` for arm64-v8a and x86_64, EOS Java
  classes + resources, manifest entries. armeabi-v7a (no Epic library) keeps the demo lobby.
- `MHActivity` loads `libEOSSDK` and calls `EOSSDK.init`. Extra permissions: `ACCESS_NETWORK_STATE`,
  `ACCESS_WIFI_STATE`.
- `config/online.local.cfg` (git-ignored, template `online.local.cfg.example`) holds the EOS credentials and
  overrides `online.cfg`.
- Online services keep ticking outside the lobby (`net_background_tick`), so leaving a room after START completes.
- Needs Android build-tools 35+ when EOS is included. APK ~32 MB.
- versionCode 4, versionName 0.96-android1.3.

## 1.2.0 – 2026-11-01
- **UNLOCK ALL** now also opens all 50 Adventure maps and the CTF maps (only maps whose files exist in the
  current mod). Added inside the original cheat in `menus.bb`, marked `;[android port]`.
- **JUTSU** touch button (shuriken icon, above-left of BLOCK): one tap holds BLOCK and presses SPECIAL on the
  next frame, which the game reads as its super move (`superKey`). BLOCK is released when the finger lifts.
- Bigger touch controls: action buttons radius 48 (was 40), THROW/JUTSU 36 (was 30), D-pad radius 108 (was 92),
  3x pixel-art icons on the big buttons.
- Overlapping presses are counted per action, so BLOCK + JUTSU (or touch + gamepad) never release a key early.
- versionCode 3, versionName 0.96-android1.2.

## Web version
- The game builds to WebAssembly and runs in the web browser (`build_web.sh`, `runtime/web_main.cpp`,
  `web/shell.html`). Keyboard, mouse, gamepad, touch controls, sound, saves in IndexedDB, 54 fps.
- Runtime: `rt_mouse()` for a real desktop mouse (pointer moves without a click).
- 1.2.0: `build_web.sh` also compiles the online layer and `.decls`; the relay backend falls back to
  the demo in the browser.

## 1.1.0 – 2026-10-29
- **UNLOCK ALL** touch button on the main menu: plays the game's own cheat (Backspace ×5, Enter) and
  confirms with a message. Unlocks all characters and VS stages.
- **MULTIPLAYER ONLINE** in the main menu: lobby with room list (players, ping), create / join room,
  member list with ping and READY state, START (host) with a 3-second countdown.
  - Pluggable network layer (`runtime/net/`): offline **demo** backend, **VPS relay** backend (TCP)
    with `server/lobby_server.py` for an Oracle Cloud free VM, **Epic Online Services** P2P skeleton.
  - Settings in `config/online.cfg`; guide in `docs/ONLINE.md`.
  - After START the room plays a local VS match with a shared seed; synchronised netplay comes later.
- **Redesigned touch controls**: about 30% bigger buttons with pixel-art icons and labels, shaded
  style, cross-shaped D-pad, ESC / OK pills, closest-button hit testing with a margin.
  Fighting controls are hidden in the online lobby so they never cover its buttons.
- Transpiler: Blitz3D-style `.decls` files (`runtime/decls/`) declare new native functions.
- `INTERNET` permission added (online lobby). versionCode 2, versionName 0.96-android1.1.

## 1.0.1 – 2026-10-24
- On-screen controls labelled in English (ATTACK, JUMP, SPECIAL, BLOCK, THROW, ESC, OK).
- Fixed: Options → Controls → CONFIG ignored touch/Android input while waiting for a key.
- Fixed: game ran at ~95 fps on Android; it now runs at its original PC speed (54 fps game timer)
  and the display is hard-capped at 60 fps. Current FPS is logged to logcat.

## 1.0.0 – 2026-10-24
- First Android build of MultiHero 0.96.
- Blitz BASIC → C++ transpiler (`tools/bb2cpp.py`) and portable 2D Blitz runtime.
- Android backend: NativeActivity, OpenGL ES 2 (sharp-bilinear scaling), AAudio, touch controls,
  hardware keyboard and gamepad support, immersive full screen.
- Headless Linux backend with scripted input and screenshots for testing.
