// SPDX-License-Identifier: MIT
// linux_main.cpp - headless test backend.
// Env: MH_ASSETS=dir  MH_DATA=dir  MH_SCRIPT=file  MH_SHOTS=dir  MH_MAXFRAMES=n  MH_FAST=1  MH_ASPECT=1.78
// Script lines: "<frame> key <scancode> <0|1>" | "<frame> tap <nx> <ny>" | "<frame> touch <id> <nx> <ny> <action>"
//               "<frame> shot" (save screenshot) | "<frame> quit"
#include "platform.h"
#include "bbrt.h"
#include <chrono>
#include <thread>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <sstream>
#include <fstream>
#include <sys/stat.h>

static std::string g_assets = "assets/", g_data = "data/", g_shots = "";
static int g_frame = 0, g_maxframes = 0;
static bool g_fast = false;
static int64_t g_vclock = 0;
struct Ev { int frame; std::string cmd; std::vector<std::string> a; };
static std::vector<Ev> g_script;
static size_t g_si = 0;
static std::vector<uint32_t> g_last;
static int g_lw = 0, g_lh = 0;

static void save_shot(const char *name) {
  if (g_shots.empty() || g_last.empty()) return;
  std::string p = g_shots + name;
  FILE *f = fopen(p.c_str(), "wb");
  if (!f) return;
  fprintf(f, "P6\n%d %d\n255\n", g_lw, g_lh);
  for (uint32_t c : g_last) { unsigned char rgb[3] = {(unsigned char)(c >> 16), (unsigned char)(c >> 8), (unsigned char)c}; fwrite(rgb, 1, 3, f); }
  fclose(f);
}

static int64_t g_last_tick = 0;
static void run_script();
void plat_present(const uint32_t *pix, int w, int h) {
  g_last.assign(pix, pix + (size_t)w * h);
  g_lw = w; g_lh = h;
  g_frame++;
  g_last_tick = plat_ms();
  run_script();
  static int16_t abuf[44100 / 54 * 2 + 4];
  rt_audio_mix(abuf, 44100 / 54);   // exercise the mixer
}
static void run_script() {
  while (g_si < g_script.size() && g_script[g_si].frame <= g_frame) {
    Ev &e = g_script[g_si++];
    if (e.cmd == "key") rt_key(atoi(e.a[0].c_str()), atoi(e.a[1].c_str()) != 0);
    else if (e.cmd == "tap") { rt_touch(99, atof(e.a[0].c_str()), atof(e.a[1].c_str()), 0); rt_touch(99, atof(e.a[0].c_str()), atof(e.a[1].c_str()), 2); }
    else if (e.cmd == "touch") rt_touch(atoi(e.a[0].c_str()), atof(e.a[1].c_str()), atof(e.a[2].c_str()), atoi(e.a[3].c_str()));
    else if (e.cmd == "shot") { char n[64]; snprintf(n, sizeof n, "f%05d.ppm", g_frame); save_shot(n); }
    else if (e.cmd == "quit") { save_shot("last.ppm"); exit(0); }
  }
  if (g_maxframes && g_frame >= g_maxframes) {
    save_shot("last.ppm");
    fprintf(stderr, "max frames reached\n");
    exit(0);
  }
}
void plat_pump() {
  // when the game waits without flipping, keep the script clock running
  if (plat_ms() - g_last_tick >= 18) { g_last_tick = plat_ms(); g_frame++; run_script(); }
}
int64_t plat_ms() {
  if (g_fast) return g_vclock;
  using namespace std::chrono;
  return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}
void plat_sleep(int ms) {
  if (ms <= 0) return;
  if (g_fast) { g_vclock += ms; return; }
  std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}
bool plat_asset_read(const std::string &path, std::vector<uint8_t> &out) {
  FILE *f = fopen((g_assets + path).c_str(), "rb");
  if (!f) return false;
  fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
  out.resize(n);
  bool ok = fread(out.data(), 1, n, f) == (size_t)n;
  fclose(f);
  return ok;
}
std::string plat_data_dir() { return g_data; }
void plat_log(const char *m) { fprintf(stderr, "[mh] %s\n", m); }
[[noreturn]] void plat_exit(int code) { save_shot("last.ppm"); fprintf(stderr, "exit(%d) at frame %d\n", code, g_frame); exit(code); }
float plat_screen_aspect() { const char *a = getenv("MH_ASPECT"); return a ? (float)atof(a) : 2.0f; }
bool plat_touch_overlay() { return true; }

int main() {
  if (const char *a = getenv("MH_ASSETS")) g_assets = std::string(a) + "/";
  if (const char *a = getenv("MH_DATA")) g_data = std::string(a) + "/";
  if (const char *a = getenv("MH_SHOTS")) g_shots = std::string(a) + "/";
  if (const char *a = getenv("MH_MAXFRAMES")) g_maxframes = atoi(a);
  g_fast = getenv("MH_FAST") != nullptr;
  mkdir(g_data.c_str(), 0777);
  if (const char *s = getenv("MH_SCRIPT")) {
    std::ifstream in(s);
    std::string line;
    while (std::getline(in, line)) {
      std::istringstream ls(line);
      Ev e;
      if (!(ls >> e.frame >> e.cmd)) continue;
      std::string x;
      while (ls >> x) e.a.push_back(x);
      g_script.push_back(e);
    }
  }
  rt_init();
  bb_main();
  plat_exit(0);
}
