# MultiHero in the web browser

The same transpiled game code (`src/*.bb` → `mh.cpp`) and the same portable runtime (`runtime/bbrt.cpp`)
also build to **WebAssembly** with Emscripten. Only the OS backend is new: `runtime/web_main.cpp` +
the page `web/shell.html`.

## Build

Requirements: Linux or WSL, Python 3 + Pillow, and the [Emscripten SDK](https://emscripten.org/docs/getting_started/downloads.html):

```bash
git clone https://github.com/emscripten-core/emsdk.git ~/emsdk
cd ~/emsdk && ./emsdk install latest && ./emsdk activate latest
```

Then, from the repository root:

```bash
./build_web.sh /path/to/MultiHero096      # or ./build_web.sh with the data in ./game
```

The result is in `build/web/`:

| File | Size | What |
|---|---|---|
| `index.html` | 9 KB | page, input, audio, saving |
| `multihero.js` | 250 KB | Emscripten loader |
| `multihero.wasm` | 1.5 MB | the game |
| `multihero.data` | 25 MB | gfx, sounds, maps (preloaded) |
| `icon.png` | | favicon |

The build takes about 1½ minutes. Options: `ASYNC=jspi` (faster build and code, but Chrome/Edge
137+ only), `DEBUG=1`, `CODE_ONLY=1` (no game data).

## Play

The page must be served over HTTP (opening `index.html` as a file does not work):

```bash
cd build/web && python3 -m http.server 8080
```

and open <http://localhost:8080>. On Windows, double-click `play_web.bat` in the web folder (needs Python).
To put it online, upload the five files to any static host (GitHub Pages, Netlify, itch.io as an
HTML5 game…). No special server headers are needed. Remember that the game data contains
third-party characters (see `NOTICE.md`).

## Controls

| Keyboard (P1 default) | Gamepad | Touch | Action |
|---|---|---|---|
| W A S D | D-pad / left stick | D-pad | move |
| N | A | ATTACK | attack |
| M | B | JUMP | jump |
| H | X | SPECIAL | special |
| Space | Y, L2/R2 | BLOCK | block |
| J | L1/R1 | THROW | throw / pick up |
| Esc | Select | ESC | menu |
| Enter | Start | OK | confirm |
| Mouse | – | tap the picture | menus |

Keys follow `keys.cfg`, so Options → Controls works as in the original. On phones/tablets the
on-screen controls appear in the side bars (turn the phone sideways; Play goes full screen).
The ⛶ button at the top switches full screen, 🔊 mutes.

## How it works

- **Blocking game loop:** the Blitz code never returns to the browser, so the build uses
  Emscripten **ASYNCIFY**: `plat_sleep()` calls `emscripten_sleep()`, which unwinds to the browser
  (it paints, delivers input, plays audio) and resumes the game afterwards. `WaitTimer`, `Flip`,
  `Delay` and the input functions (for busy loops such as `getTheKey`) all reach `plat_sleep()`.
  `mh.cpp` is compiled with `-fno-inline-functions`: otherwise all game functions get inlined into
  `bb_main()` (≈400 KB), and ASYNCIFY turns that into a function the browser refuses ("local count
  too large") after a >30 minute link.
- **Video:** the 32-bit frame from the runtime is converted to RGBA and drawn with `putImageData`
  on a canvas; CSS scales it with `image-rendering: pixelated`.
- **Audio:** `rt_audio_mix()` (44.1 kHz stereo) is pulled in chunks and scheduled back to back with
  Web Audio, ~120–160 ms ahead. Browsers only allow sound after a click, hence the Play button.
- **Files:** game data is preloaded read-only at `/assets`; `/data` (where `game.cfg`, `keys.cfg`
  and other writes go) is IndexedDB (IDBFS), saved every 3 s, when the tab is hidden and when the
  game exits.
- **Speed:** game logic at the original 54 fps (`CreateTimer(54)`), display capped at 60 fps,
  same as Android.

## Tested

Headless Chromium: title menu, character select, stage select, a stage with keyboard movement,
mouse menus, phone-sized touch layout, 54 fps. Not yet tested by hand in Firefox / Safari / on a phone.
