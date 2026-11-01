// SPDX-License-Identifier: MIT
// web_main.cpp - browser backend (Emscripten / WebAssembly).
//
// The game is a plain blocking Blitz loop, so the build uses ASYNCIFY: every
// plat_sleep() unwinds to the browser (which then paints the canvas, delivers
// input events and runs timers) and resumes the game afterwards.
//
//   video : 2D canvas, RGBA frame copied with putImageData, scaled by CSS (pixelated)
//   audio : Web Audio, 44.1 kHz stereo chunks from rt_audio_mix() scheduled back to back
//   input : keyboard (KeyboardEvent.code -> DirectInput scancode), mouse, touch, Gamepad API
//   files : read-only game data preloaded to /assets, saves in /data (IndexedDB, IDBFS)
#include "platform.h"
#include "bbrt.h"
#include <emscripten.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static std::vector<uint8_t> g_rgba;
static bool g_overlay = false;     // on-screen controls (touch devices)

// ------------------------------------------------------------------ JS side
EM_JS(void, js_present, (const uint8_t *p, int w, int h), {
  var c = Module.canvas;
  if (c.width !== w || c.height !== h) { c.width = w; c.height = h; Module.mhImg = null; if (Module.mhLayout) Module.mhLayout(); }
  var ctx = Module.mhCtx || (Module.mhCtx = c.getContext('2d', {alpha: false}));
  if (!Module.mhImg) Module.mhImg = ctx.createImageData(w, h);
  Module.mhImg.data.set(HEAPU8.subarray(p, p + w * h * 4));
  ctx.putImageData(Module.mhImg, 0, 0);
});

// frames (stereo) the audio queue wants right now; 0 if audio is off/suspended
EM_JS(int, js_audio_need, (), {
  var a = Module.mhAudio;
  if (!a || a.ctx.state !== 'running') return 0;
  var now = a.ctx.currentTime;
  if (a.next < now + 0.01) a.next = now + 0.03;          // underrun: restart slightly ahead
  var ahead = a.next - now;
  if (ahead >= 0.12) return 0;
  return Math.ceil((0.16 - ahead) * 44100 / 512) * 512;
});
EM_JS(void, js_audio_push, (const int16_t *p, int frames), {
  var a = Module.mhAudio;
  var buf = a.ctx.createBuffer(2, frames, 44100);
  var l = buf.getChannelData(0), r = buf.getChannelData(1);
  var s = HEAP16.subarray(p >> 1, (p >> 1) + frames * 2);
  for (var i = 0, j = 0; i < frames; i++, j += 2) { l[i] = s[j] / 32768; r[i] = s[j + 1] / 32768; }
  var src = a.ctx.createBufferSource();
  src.buffer = buf;
  src.connect(a.ctx.destination);
  src.start(a.next);
  a.next += frames / 44100;
});

EM_JS(void, js_poll_pads, (), { if (Module.mhPollPads) Module.mhPollPads(); });
EM_JS(double, js_aspect, (), { var c = Module.mhStage || document.body; return Math.max(1, c.clientWidth) / Math.max(1, c.clientHeight); });
EM_JS(void, js_exit, (int code), { if (Module.mhExit) Module.mhExit(code); });
EM_JS(void, js_log, (const char *m), { console.log('[mh] ' + UTF8ToString(m)); });

// ------------------------------------------------------------------ exports for the page
extern "C" {
EMSCRIPTEN_KEEPALIVE void web_key(int sc, int down) { if (down) g_overlay = false; rt_key(sc, down != 0); }
EMSCRIPTEN_KEEPALIVE void web_vbutton(int vb, int down) { g_overlay = false; rt_vbutton(vb, down != 0); }
EMSCRIPTEN_KEEPALIVE void web_touch(int id, float nx, float ny, int action) { g_overlay = true; rt_touch(id, nx, ny, action); }
EMSCRIPTEN_KEEPALIVE void web_touch_cancel() { rt_touch_cancel_all(); }
EMSCRIPTEN_KEEPALIVE void web_mouse(float nx, float ny, int button, int down) { rt_mouse(nx, ny, button, down != 0); }
EMSCRIPTEN_KEEPALIVE void web_set_overlay(int on) { g_overlay = on != 0; }
}

// ------------------------------------------------------------------ platform
static void pump_audio() {
  static int16_t buf[8192 * 2];
  int need = js_audio_need();
  while (need > 0) {
    int n = need > 8192 ? 8192 : need;
    rt_audio_mix(buf, n);
    js_audio_push(buf, n);
    need -= n;
  }
}

void plat_present(const uint32_t *pix, int w, int h) {
  size_t n = (size_t)w * h;
  g_rgba.resize(n * 4);
  uint8_t *d = g_rgba.data();
  for (size_t i = 0; i < n; i++) {
    uint32_t c = pix[i];
    d[0] = (uint8_t)(c >> 16); d[1] = (uint8_t)(c >> 8); d[2] = (uint8_t)c; d[3] = 255;
    d += 4;
  }
  js_present(g_rgba.data(), w, h);
}

void plat_pump() {
  static double last = 0;
  double now = emscripten_get_now();
  if (now - last < 8) return;   // called very often by the input functions
  last = now;
  js_poll_pads();
  pump_audio();
}

int64_t plat_ms() { return (int64_t)emscripten_get_now(); }

void plat_sleep(int ms) {
  pump_audio();
  emscripten_sleep(ms < 1 ? 1 : ms);   // yield to the browser (ASYNCIFY)
}

bool plat_asset_read(const std::string &path, std::vector<uint8_t> &out) {
  FILE *f = fopen(("/assets/" + path).c_str(), "rb");
  if (!f) return false;
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  out.resize(n);
  bool ok = fread(out.data(), 1, n, f) == (size_t)n;
  fclose(f);
  return ok;
}

std::string plat_data_dir() { return "/data/"; }
void plat_log(const char *m) { js_log(m); }

[[noreturn]] void plat_exit(int code) {
  js_exit(code);   // saves /data to IndexedDB and shows the "game ended" screen
  for (;;) emscripten_sleep(1000);
}

float plat_screen_aspect() { return g_overlay ? (float)js_aspect() : 4.0f / 3.0f; }
bool plat_touch_overlay() { return g_overlay; }

int main() {
  g_overlay = EM_ASM_INT({ return Module.mhTouch ? 1 : 0; }) != 0;
  rt_init();
  bb_main();
  plat_exit(0);
}
