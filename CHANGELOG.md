# Changelog

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
