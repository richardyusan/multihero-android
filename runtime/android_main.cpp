// SPDX-License-Identifier: MIT
// android_main.cpp - Android backend: NativeActivity + EGL/GLES2 + AAudio.
#include "platform.h"
#include "bbrt.h"

#include <android_native_app_glue.h>
#include <android/log.h>
#include <android/asset_manager.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <aaudio/AAudio.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <unistd.h>
#include <sys/stat.h>

#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "MultiHero", __VA_ARGS__)

static android_app *g_app = nullptr;
static EGLDisplay g_dpy = EGL_NO_DISPLAY;
static EGLSurface g_surf = EGL_NO_SURFACE;
static EGLContext g_ctx = EGL_NO_CONTEXT;
static EGLConfig g_cfg = nullptr;
static int g_win_w = 1, g_win_h = 1;
static bool g_resumed = false, g_has_window = false;
static GLuint g_prog = 0, g_tex = 0;
static GLint g_u_tex = -1, g_u_texsize = -1, g_u_outsize = -1;
static int g_tex_w = 0, g_tex_h = 0;
static float g_aspect = 16.0f / 9.0f;
static bool g_overlay = true;
static std::string g_datadir;
static std::atomic<bool> g_audio_restart{false};
static AAudioStream *g_stream = nullptr;

// ------------------------------------------------------------------ audio
static aaudio_data_callback_result_t audio_cb(AAudioStream *, void *, void *data, int32_t frames) {
  rt_audio_mix((int16_t *)data, frames);
  return AAUDIO_CALLBACK_RESULT_CONTINUE;
}
static void audio_err(AAudioStream *, void *, aaudio_result_t err) {
  if (err == AAUDIO_ERROR_DISCONNECTED) g_audio_restart = true;
}
static void audio_close() {
  if (g_stream) {
    AAudioStream_requestStop(g_stream);
    AAudioStream_close(g_stream);
    g_stream = nullptr;
  }
}
static void audio_open() {
  audio_close();
  AAudioStreamBuilder *b = nullptr;
  if (AAudio_createStreamBuilder(&b) != AAUDIO_OK) return;
  AAudioStreamBuilder_setFormat(b, AAUDIO_FORMAT_PCM_I16);
  AAudioStreamBuilder_setChannelCount(b, 2);
  AAudioStreamBuilder_setSampleRate(b, 44100);
  AAudioStreamBuilder_setPerformanceMode(b, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
  AAudioStreamBuilder_setSharingMode(b, AAUDIO_SHARING_MODE_SHARED);
  AAudioStreamBuilder_setDataCallback(b, audio_cb, nullptr);
  AAudioStreamBuilder_setErrorCallback(b, audio_err, nullptr);
  if (AAudioStreamBuilder_openStream(b, &g_stream) != AAUDIO_OK) g_stream = nullptr;
  AAudioStreamBuilder_delete(b);
  if (g_stream) AAudioStream_requestStart(g_stream);
}

// ------------------------------------------------------------------ GL
static const char *VS =
    "attribute vec2 p; varying vec2 uv;\n"
    "void main(){ uv = vec2(p.x*0.5+0.5, 0.5-p.y*0.5); gl_Position = vec4(p,0.0,1.0); }\n";
// "sharp bilinear": crisp pixels without uneven nearest-neighbour scaling
static const char *FS =
    "#ifdef GL_FRAGMENT_PRECISION_HIGH\nprecision highp float;\n#else\nprecision mediump float;\n#endif\n"
    "varying vec2 uv; uniform sampler2D t; uniform vec2 ts; uniform vec2 os;\n"
    "void main(){ vec2 tx = uv*ts; vec2 sc = max(floor(os/ts), vec2(1.0));\n"
    " vec2 fl = floor(tx); vec2 s = fract(tx); vec2 rr = 0.5 - 0.5/sc; vec2 cd = s - 0.5;\n"
    " vec2 f = (cd - clamp(cd, -rr, rr))*sc + 0.5;\n"
    " gl_FragColor = vec4(texture2D(t, (fl+f)/ts).bgr, 1.0); }\n";

static GLuint compile(GLenum type, const char *src) {
  GLuint s = glCreateShader(type);
  glShaderSource(s, 1, &src, nullptr);
  glCompileShader(s);
  GLint ok = 0;
  glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[512];
    glGetShaderInfoLog(s, sizeof log, nullptr, log);
    LOG("shader error: %s", log);
  }
  return s;
}

static void gl_setup() {
  g_prog = glCreateProgram();
  glAttachShader(g_prog, compile(GL_VERTEX_SHADER, VS));
  glAttachShader(g_prog, compile(GL_FRAGMENT_SHADER, FS));
  glBindAttribLocation(g_prog, 0, "p");
  glLinkProgram(g_prog);
  g_u_tex = glGetUniformLocation(g_prog, "t");
  g_u_texsize = glGetUniformLocation(g_prog, "ts");
  g_u_outsize = glGetUniformLocation(g_prog, "os");
  glGenTextures(1, &g_tex);
  glBindTexture(GL_TEXTURE_2D, g_tex);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  g_tex_w = g_tex_h = 0;
}

static void egl_init(ANativeWindow *win) {
  if (g_dpy == EGL_NO_DISPLAY) {
    g_dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    eglInitialize(g_dpy, nullptr, nullptr);
    const EGLint attrs[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
                            EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_NONE};
    EGLint n = 0;
    eglChooseConfig(g_dpy, attrs, &g_cfg, 1, &n);
  }
  if (g_ctx == EGL_NO_CONTEXT) {
    const EGLint cattrs[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
    g_ctx = eglCreateContext(g_dpy, g_cfg, EGL_NO_CONTEXT, cattrs);
  }
  g_surf = eglCreateWindowSurface(g_dpy, g_cfg, win, nullptr);
  if (eglMakeCurrent(g_dpy, g_surf, g_surf, g_ctx) == EGL_FALSE) {
    // context lost: recreate everything
    eglDestroyContext(g_dpy, g_ctx);
    const EGLint cattrs[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
    g_ctx = eglCreateContext(g_dpy, g_cfg, EGL_NO_CONTEXT, cattrs);
    eglMakeCurrent(g_dpy, g_surf, g_surf, g_ctx);
    g_prog = 0;
  }
  eglSwapInterval(g_dpy, 0);
  eglQuerySurface(g_dpy, g_surf, EGL_WIDTH, &g_win_w);
  eglQuerySurface(g_dpy, g_surf, EGL_HEIGHT, &g_win_h);
  if (g_win_h > 0) g_aspect = (float)g_win_w / g_win_h;
  if (!g_prog) gl_setup();
  g_has_window = true;
}

static void egl_term() {
  if (g_dpy != EGL_NO_DISPLAY) {
    eglMakeCurrent(g_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (g_surf != EGL_NO_SURFACE) eglDestroySurface(g_dpy, g_surf);
  }
  g_surf = EGL_NO_SURFACE;
  g_has_window = false;
}

// ------------------------------------------------------------------ input
static int key_to_scancode(int kc) {
  if (kc >= AKEYCODE_A && kc <= AKEYCODE_Z) {
    static const int letters[26] = {30, 48, 46, 32, 18, 33, 34, 35, 23, 36, 37, 38, 50,
                                    49, 24, 25, 16, 19, 31, 20, 22, 47, 17, 45, 21, 44};
    return letters[kc - AKEYCODE_A];
  }
  if (kc >= AKEYCODE_1 && kc <= AKEYCODE_9) return 2 + (kc - AKEYCODE_1);
  switch (kc) {
    case AKEYCODE_0: return 11;
    case AKEYCODE_ESCAPE: return 1;
    case AKEYCODE_ENTER: case AKEYCODE_NUMPAD_ENTER: return 28;
    case AKEYCODE_SPACE: return 57;
    case AKEYCODE_DEL: return 14;
    case AKEYCODE_TAB: return 15;
    case AKEYCODE_SHIFT_LEFT: return 42;
    case AKEYCODE_SHIFT_RIGHT: return 54;
    case AKEYCODE_CTRL_LEFT: return 29;
    case AKEYCODE_CTRL_RIGHT: return 157;
    case AKEYCODE_ALT_LEFT: return 56;
    case AKEYCODE_ALT_RIGHT: return 184;
    case AKEYCODE_MINUS: return 12;
    case AKEYCODE_EQUALS: return 13;
    case AKEYCODE_LEFT_BRACKET: return 26;
    case AKEYCODE_RIGHT_BRACKET: return 27;
    case AKEYCODE_SEMICOLON: return 39;
    case AKEYCODE_APOSTROPHE: return 40;
    case AKEYCODE_GRAVE: return 41;
    case AKEYCODE_BACKSLASH: return 43;
    case AKEYCODE_COMMA: return 51;
    case AKEYCODE_PERIOD: return 52;
    case AKEYCODE_SLASH: return 53;
    case AKEYCODE_F1: return 59; case AKEYCODE_F2: return 60; case AKEYCODE_F3: return 61;
    case AKEYCODE_F4: return 62; case AKEYCODE_F5: return 63; case AKEYCODE_F6: return 64;
    case AKEYCODE_F7: return 65; case AKEYCODE_F8: return 66; case AKEYCODE_F9: return 67;
    case AKEYCODE_F10: return 68; case AKEYCODE_F11: return 87; case AKEYCODE_F12: return 88;
    case AKEYCODE_DPAD_UP: return 200;
    case AKEYCODE_DPAD_DOWN: return 208;
    case AKEYCODE_DPAD_LEFT: return 203;
    case AKEYCODE_DPAD_RIGHT: return 205;
    case AKEYCODE_NUMPAD_8: return 72; case AKEYCODE_NUMPAD_2: return 80;
    case AKEYCODE_NUMPAD_4: return 75; case AKEYCODE_NUMPAD_6: return 77;
    case AKEYCODE_NUMPAD_0: return 82; case AKEYCODE_NUMPAD_5: return 76;
    case AKEYCODE_INSERT: return 210; case AKEYCODE_FORWARD_DEL: return 211;
    case AKEYCODE_MOVE_HOME: return 199; case AKEYCODE_MOVE_END: return 207;
    case AKEYCODE_PAGE_UP: return 201; case AKEYCODE_PAGE_DOWN: return 209;
  }
  return 0;
}

static int pad_button_vb(int kc) {
  switch (kc) {
    case AKEYCODE_BUTTON_A: return VB_SHOT;
    case AKEYCODE_BUTTON_B: return VB_JUMP;
    case AKEYCODE_BUTTON_X: return VB_SPECIAL;
    case AKEYCODE_BUTTON_Y: return VB_BLOCK;
    case AKEYCODE_BUTTON_L1: case AKEYCODE_BUTTON_R1: return VB_GRAB;
    case AKEYCODE_BUTTON_L2: case AKEYCODE_BUTTON_R2: return VB_BLOCK;
  }
  return -1;
}

static float g_axis_x = 0, g_axis_y = 0, g_hat_x = 0, g_hat_y = 0;
static void update_pad_dirs() {
  float x = fabsf(g_hat_x) > 0.5f ? g_hat_x : g_axis_x;
  float y = fabsf(g_hat_y) > 0.5f ? g_hat_y : g_axis_y;
  rt_vbutton(VB_LEFT, x < -0.5f);
  rt_vbutton(VB_RIGHT, x > 0.5f);
  rt_vbutton(VB_UP, y < -0.5f);
  rt_vbutton(VB_DOWN, y > 0.5f);
}

static int32_t on_input(android_app *, AInputEvent *ev) {
  int type = AInputEvent_getType(ev);
  int src = AInputEvent_getSource(ev);
  if (type == AINPUT_EVENT_TYPE_MOTION) {
    if ((src & AINPUT_SOURCE_JOYSTICK) == AINPUT_SOURCE_JOYSTICK || (src & AINPUT_SOURCE_GAMEPAD) == AINPUT_SOURCE_GAMEPAD) {
      g_axis_x = AMotionEvent_getAxisValue(ev, AMOTION_EVENT_AXIS_X, 0);
      g_axis_y = AMotionEvent_getAxisValue(ev, AMOTION_EVENT_AXIS_Y, 0);
      g_hat_x = AMotionEvent_getAxisValue(ev, AMOTION_EVENT_AXIS_HAT_X, 0);
      g_hat_y = AMotionEvent_getAxisValue(ev, AMOTION_EVENT_AXIS_HAT_Y, 0);
      update_pad_dirs();
      g_overlay = false;
      return 1;
    }
    int action = AMotionEvent_getAction(ev);
    int am = action & AMOTION_EVENT_ACTION_MASK;
    int idx = (action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;
    float w = (float)std::max(g_win_w, 1), h = (float)std::max(g_win_h, 1);
    g_overlay = true;
    switch (am) {
      case AMOTION_EVENT_ACTION_DOWN:
      case AMOTION_EVENT_ACTION_POINTER_DOWN:
        rt_touch(AMotionEvent_getPointerId(ev, idx), AMotionEvent_getX(ev, idx) / w, AMotionEvent_getY(ev, idx) / h, 0);
        break;
      case AMOTION_EVENT_ACTION_MOVE: {
        size_t n = AMotionEvent_getPointerCount(ev);
        for (size_t i = 0; i < n; i++)
          rt_touch(AMotionEvent_getPointerId(ev, i), AMotionEvent_getX(ev, i) / w, AMotionEvent_getY(ev, i) / h, 1);
      } break;
      case AMOTION_EVENT_ACTION_UP:
      case AMOTION_EVENT_ACTION_POINTER_UP:
        rt_touch(AMotionEvent_getPointerId(ev, idx), AMotionEvent_getX(ev, idx) / w, AMotionEvent_getY(ev, idx) / h, 2);
        break;
      case AMOTION_EVENT_ACTION_CANCEL:
        rt_touch_cancel_all();
        break;
    }
    return 1;
  }
  if (type == AINPUT_EVENT_TYPE_KEY) {
    int kc = AKeyEvent_getKeyCode(ev);
    int act = AKeyEvent_getAction(ev);
    if (act == AKEY_EVENT_ACTION_MULTIPLE) return 0;
    bool down = act == AKEY_EVENT_ACTION_DOWN;
    if (kc == AKEYCODE_VOLUME_UP || kc == AKEYCODE_VOLUME_DOWN || kc == AKEYCODE_VOLUME_MUTE) return 0;
    if (kc == AKEYCODE_BACK || kc == AKEYCODE_BUTTON_SELECT) { rt_key(1, down); return 1; }
    if (kc == AKEYCODE_BUTTON_START) { rt_key(28, down); return 1; }
    bool from_pad = (src & AINPUT_SOURCE_GAMEPAD) == AINPUT_SOURCE_GAMEPAD || (src & AINPUT_SOURCE_JOYSTICK) == AINPUT_SOURCE_JOYSTICK;
    int vb = pad_button_vb(kc);
    if (vb >= 0) { rt_vbutton(vb, down); g_overlay = false; return 1; }
    if (from_pad && (kc == AKEYCODE_DPAD_UP || kc == AKEYCODE_DPAD_DOWN || kc == AKEYCODE_DPAD_LEFT || kc == AKEYCODE_DPAD_RIGHT)) {
      int d = kc == AKEYCODE_DPAD_UP ? VB_UP : kc == AKEYCODE_DPAD_DOWN ? VB_DOWN : kc == AKEYCODE_DPAD_LEFT ? VB_LEFT : VB_RIGHT;
      rt_vbutton(d, down);
      g_overlay = false;
      return 1;
    }
    int sc = key_to_scancode(kc);
    if (sc) { rt_key(sc, down); return 1; }
  }
  return 0;
}

// ------------------------------------------------------------------ lifecycle
static void on_cmd(android_app *app, int32_t cmd) {
  switch (cmd) {
    case APP_CMD_INIT_WINDOW:
      if (app->window) egl_init(app->window);
      break;
    case APP_CMD_TERM_WINDOW:
      egl_term();
      break;
    case APP_CMD_WINDOW_RESIZED:
    case APP_CMD_CONFIG_CHANGED:
    case APP_CMD_CONTENT_RECT_CHANGED:
      if (g_surf != EGL_NO_SURFACE) {
        eglQuerySurface(g_dpy, g_surf, EGL_WIDTH, &g_win_w);
        eglQuerySurface(g_dpy, g_surf, EGL_HEIGHT, &g_win_h);
        if (g_win_h > 0) g_aspect = (float)g_win_w / g_win_h;
      }
      break;
    case APP_CMD_RESUME:
      g_resumed = true;
      audio_open();
      break;
    case APP_CMD_PAUSE:
      g_resumed = false;
      audio_close();
      rt_touch_cancel_all();
      break;
    case APP_CMD_LOST_FOCUS:
      rt_touch_cancel_all();
      break;
  }
}

static void process_events(int timeout_ms) {
  int events;
  android_poll_source *source;
  while (ALooper_pollOnce(timeout_ms, nullptr, &events, (void **)&source) >= 0) {
    if (source) source->process(g_app, source);
    if (g_app->destroyRequested) {
      audio_close();
      egl_term();
      _exit(0);
    }
    if (g_resumed && g_has_window) timeout_ms = 0;
  }
}

void plat_pump() {
  process_events(0);
  // while in background: block here (the game simply freezes) until we're visible again
  while (!(g_resumed && g_has_window)) process_events(-1);
  if (g_audio_restart.exchange(false)) audio_open();
}

void plat_present(const uint32_t *pix, int w, int h) {
  plat_pump();
  if (g_surf == EGL_NO_SURFACE) return;
  glBindTexture(GL_TEXTURE_2D, g_tex);
  if (w != g_tex_w || h != g_tex_h) {
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pix);
    g_tex_w = w;
    g_tex_h = h;
  } else {
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pix);
  }
  // letterbox, keeping the composite's aspect ratio
  float sa = (float)w / h, wa = (float)g_win_w / g_win_h;
  int vw = g_win_w, vh = g_win_h;
  if (wa > sa) vw = (int)(g_win_h * sa + 0.5f);
  else vh = (int)(g_win_w / sa + 0.5f);
  glViewport(0, 0, g_win_w, g_win_h);
  glClearColor(0, 0, 0, 1);
  glClear(GL_COLOR_BUFFER_BIT);
  glViewport((g_win_w - vw) / 2, (g_win_h - vh) / 2, vw, vh);
  glUseProgram(g_prog);
  glUniform1i(g_u_tex, 0);
  glUniform2f(g_u_texsize, (float)w, (float)h);
  glUniform2f(g_u_outsize, (float)vw, (float)vh);
  static const float quad[] = {-1, -1, 1, -1, -1, 1, 1, 1};
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, quad);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  eglSwapBuffers(g_dpy, g_surf);
}

int64_t plat_ms() {
  timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}
void plat_sleep(int ms) {
  if (ms > 0) usleep(ms * 1000);
}
bool plat_asset_read(const std::string &path, std::vector<uint8_t> &out) {
  AAsset *a = AAssetManager_open(g_app->activity->assetManager, path.c_str(), AASSET_MODE_BUFFER);
  if (!a) return false;
  off_t n = AAsset_getLength(a);
  out.resize(n);
  bool ok = n == 0 || AAsset_read(a, out.data(), n) == n;
  AAsset_close(a);
  return ok;
}
std::string plat_data_dir() { return g_datadir; }
void plat_log(const char *m) { LOG("%s", m); }
[[noreturn]] void plat_exit(int code) {
  LOG("exit(%d)", code);
  audio_close();
  ANativeActivity_finish(g_app->activity);
  int64_t until = plat_ms() + 1500;
  while (!g_app->destroyRequested && plat_ms() < until) {
    int events;
    android_poll_source *source;
    if (ALooper_pollOnce(100, nullptr, &events, (void **)&source) >= 0 && source) source->process(g_app, source);
  }
  _exit(0);
}
float plat_screen_aspect() { return g_aspect; }
bool plat_touch_overlay() { return g_overlay; }

void android_main(android_app *app) {
  g_app = app;
  app->onAppCmd = on_cmd;
  app->onInputEvent = on_input;
  const char *dd = app->activity->internalDataPath;
  g_datadir = std::string(dd ? dd : "/data/local/tmp") + "/";
  mkdir(g_datadir.c_str(), 0700);
  LOG("MultiHero starting, data dir %s", g_datadir.c_str());
  // wait for a window before starting the game
  while (!(g_resumed && g_has_window)) process_events(-1);
  rt_init();
  bb_main();
  plat_exit(0);
}
