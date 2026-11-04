# Notice – what is covered by which terms

| Part | Owner / terms |
|---|---|
| `tools/`, `runtime/`, `android/`, `scripts/`, `server/`, `config/`, `src/online.bb`, `build_android.sh`, `.github/`, `docs/` | Android port – MIT License (see `LICENSE`) |
| `src/*.bb` (except `online.bb`) | Original MultiHero 0.96 source code by **mhman**, the creator of MultiHero, released as the "0.96 SHARED" source package. It remains under its author's terms. The port only adds a few lines marked `;[android port]` in `mh.bb` and `menus.bb` (MULTIPLAYER ONLINE menu entry, touch-overlay hook). |
| Game data (`gfx/`, `sounds/`, `Maps/`, `*.cfg`, `maps.dat`) | **Not included** in this repository. |

## Third-party characters

MultiHero is a non-commercial fan game. Many of its characters, sprites and sounds come from games and
franchises owned by third parties (for example Capcom, Nintendo, Rare, Marvel, DC, Konami, Koei Tecmo and others). Those
names and assets belong to their respective owners; this project claims no rights to them and is not
affiliated with or endorsed by any of them.

For that reason the game data is kept out of this repository. If you publish a built APK (for
example in GitHub Releases), be aware that it contains that game data.

## Tools used to build

- Android NDK / SDK (Google) – used at build time only, not redistributed here.
- `android_native_app_glue` from the Android NDK (Apache License 2.0) is compiled into the APK.
- **Mbed TLS** 3.6 (Apache License 2.0, https://github.com/Mbed-TLS/mbedtls) is compiled into the Android,
  Windows and Linux programs for the secure connection to the online servers. It is downloaded at build time
  by `tools/fetch_mbedtls.py`, not stored in this repository.
- `runtime/net/ca_roots.inc` holds the Mozilla root certificate list (from the certifi package,
  Mozilla Public License 2.0).
- The built-in debug font in `runtime/font8x16.h` is rendered from DejaVu Sans Mono
  (Bitstream Vera / DejaVu font license, free to use and embed).
