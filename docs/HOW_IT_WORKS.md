# How the port works

```
src/mh.bb (+ Include files)
      │  tools/bb2cpp.py   – Blitz BASIC → C++
      ▼
build/.../mh.cpp ──┐
runtime/bbrt.cpp ──┤  portable Blitz 2D runtime (software renderer, audio mixer, files, input)
runtime/mh_glue.cpp┤  game-specific glue (touch buttons follow player 1's keys.cfg)
runtime/android_main.cpp  or  runtime/linux_main.cpp   – platform backend
      ▼
libmultihero.so  →  APK (NativeActivity + tiny Java activity for immersive mode)
```

## The transpiler (`tools/bb2cpp.py`)

A single-file Python program: tokenizer → parser (AST) → typed C++ code generator. It covers the
subset of Blitz BASIC the game uses: `Global/Local/Const/Dim`, functions with default parameters,
`If/ElseIf/Else` (block and single-line forms), `For/Next`, `While/Wend`, `Repeat/Until/Forever`,
`Select/Case`, `Goto` and labels (emitted as C++ `goto`), `Include`, `$hex`/`%binary` literals.
`Type`/`New`/`Each`, `Gosub` and `Data/Read` are not used by the game and are not supported.

Blitz semantics that are reproduced on purpose:

| Blitz behaviour | How it is generated |
|---|---|
| identifiers are case-insensitive, `%` `#` `$` suffixes set the type | names are lower-cased, types tracked per variable |
| 32-bit int / float | `int` / `float`, compiled with `-fwrapv` |
| float → int rounds to nearest | `bb_f2i()` uses `lrintf` |
| `int / int` is integer division | `bb_idiv()` (division by zero returns 0 instead of crashing) |
| `And` / `Or` / `Xor` are bitwise | `&` `|` `^` on ints |
| `Not` has the lowest precedence | `Not a = b` → `!(a == b)` |
| statements may follow each other without `:` | accepted, like the Blitz compiler |
| `For` limit re-evaluated each iteration | plain C `for` loop |
| locals are zero on entry, implicit declaration | all locals declared and zeroed at function top |
| arrays are `Dim n` → `n+1` elements | `BArray<T>` with bounds checks (out-of-range reads return 0 and are logged) |
| `Exit` leaves the loop even inside `Select` | `Select` becomes an `if/else` chain |

## The runtime (`runtime/bbrt.cpp`)

- 640×480 software framebuffer; images are 32-bit with Blitz-style colour-key masking
  (16-bit mode compares masks the way a 16-bit display would).
- BMP (1/4/8/16/24/32-bit, RLE) and PCM WAV decoders; JPEGs are converted to BMP at build time.
- `ImageRectCollide`, `ImagesCollide`, `CopyRect`, `SaveBuffer`, `TileImage`, `Text` (built-in font) …
- 24-channel mixer at 44.1 kHz with looping and per-channel volume.
- Virtual file system: case-insensitive, `\` → `/`, writes go to app storage and shadow the
  read-only assets (that is how `save.dat` / `keys.cfg` persist).
- `WaitTimer`, `Delay`, `MilliSecs`; display capped at 60 fps.
- Screen composition: the 4:3 game picture is centred in a frame matching the phone's aspect ratio
  and the touch controls are drawn in the side bars.
- Input functions (`KeyHit`, `JoyHit`, `MouseHit` …) process OS events themselves, because some game
  loops wait for input without ever calling `Flip`.

## Testing without a phone

`runtime/linux_main.cpp` runs the game headless. Environment variables:

| Variable | Meaning |
|---|---|
| `MH_ASSETS` | prepared asset folder (`tools/prep_assets.py` output) |
| `MH_DATA` | writable folder for saves |
| `MH_SCRIPT` | input script, one event per line (see below) |
| `MH_SHOTS` | folder for PPM screenshots |
| `MH_MAXFRAMES` | stop after N frames |
| `MH_FAST=1` | virtual clock: run as fast as possible |
| `MH_ASPECT` | simulated screen aspect ratio (default 2.0) |

Script lines: `<frame> key <scancode> <1|0>`, `<frame> tap <x> <y>`,
`<frame> touch <id> <x> <y> <0=down|1=move|2=up>`, `<frame> shot`, `<frame> quit`
(x/y are 0..1 of the whole screen).
