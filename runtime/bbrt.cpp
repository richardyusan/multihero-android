// SPDX-License-Identifier: MIT
// bbrt.cpp - portable Blitz2D runtime: software renderer, sound mixer,
// virtual file system, input state and on-screen touch controls.
#include "bbrt.h"
#include "platform.h"
#include "font8x16.h"

#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <cstdarg>
#include <ctime>
#include <map>
#include <set>
#include <mutex>
#include <memory>
#include <random>
#include <sys/stat.h>

// =========================================================== logging / errors
static void logf_(const char *fmt, ...) {
  char buf[1024];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  plat_log(buf);
}

void bb_array_oob(const char *name, int idx) {
  static std::set<std::string> reported;
  if (reported.size() < 200 && reported.insert(name).second)
    logf_("array index out of bounds: %s(%d)", name, idx);
}

[[noreturn]] void bb_end() { plat_exit(0); }
[[noreturn]] void bb_runtimeerror(const BStr &s) {
  logf_("RuntimeError: %s", s.c_str());
  plat_exit(1);
}
void bb_debuglog(const BStr &s) { logf_("DebugLog: %s", s.c_str()); }
BStr bb_commandline() { return BStr(); }
void bb_print(const BStr &s) { logf_("%s", s.c_str()); }
void bb_write(const BStr &s) { logf_("%s", s.c_str()); }
void bb_stop() {}

// =========================================================== strings
int bb_s2i(const BStr &s) { return (int)strtol(s.c_str(), nullptr, 10); }
float bb_s2f(const BStr &s) { return (float)atof(s.c_str()); }
BStr bb_istr(int v) { char b[16]; snprintf(b, sizeof b, "%d", v); return b; }
BStr bb_fstr(float v) {
  char b[64];
  snprintf(b, sizeof b, "%.6f", (double)v);
  // trim trailing zeros but keep one decimal, like Blitz
  char *dot = strchr(b, '.');
  if (dot) {
    char *e = b + strlen(b) - 1;
    while (e > dot + 1 && *e == '0') *e-- = 0;
  }
  return b;
}
int bb_len(const BStr &s) { return (int)s.size(); }
BStr bb_mid(const BStr &s, int start, int count) {
  if (start < 1) start = 1;
  if ((size_t)start > s.size()) return BStr();
  if (count < 0) return s.substr(start - 1);
  return s.substr(start - 1, count);
}
BStr bb_left(const BStr &s, int n) { if (n <= 0) return BStr(); return s.substr(0, n); }
BStr bb_right(const BStr &s, int n) {
  if (n <= 0) return BStr();
  if ((size_t)n >= s.size()) return s;
  return s.substr(s.size() - n);
}
BStr bb_lower(const BStr &s) { BStr r = s; for (auto &c : r) c = (char)tolower((unsigned char)c); return r; }
BStr bb_upper(const BStr &s) { BStr r = s; for (auto &c : r) c = (char)toupper((unsigned char)c); return r; }
BStr bb_trim(const BStr &s) {
  size_t a = 0, b = s.size();
  while (a < b && (unsigned char)s[a] <= 32) a++;
  while (b > a && (unsigned char)s[b - 1] <= 32) b--;
  return s.substr(a, b - a);
}
BStr bb_chr(int c) { return BStr(1, (char)c); }
int bb_asc(const BStr &s) { return s.empty() ? -1 : (unsigned char)s[0]; }
int bb_instr(const BStr &s, const BStr &f, int start) {
  if (start < 1) start = 1;
  size_t p = s.find(f, start - 1);
  return p == BStr::npos ? 0 : (int)p + 1;
}
BStr bb_replace(const BStr &s, const BStr &f, const BStr &r) {
  if (f.empty()) return s;
  BStr out;
  size_t i = 0;
  for (;;) {
    size_t p = s.find(f, i);
    if (p == BStr::npos) { out += s.substr(i); break; }
    out += s.substr(i, p - i);
    out += r;
    i = p + f.size();
  }
  return out;
}
BStr bb_lset(const BStr &s, int n) { BStr r = s; r.resize(std::max(n, 0), ' '); return r; }
BStr bb_rset(const BStr &s, int n) {
  if (n <= 0) return BStr();
  if ((int)s.size() >= n) return s.substr(s.size() - n);
  return BStr(n - s.size(), ' ') + s;
}
BStr bb_hex(int v) { char b[16]; snprintf(b, sizeof b, "%08X", (unsigned)v); return b; }
BStr bb_bin(int v) { BStr r; for (int i = 31; i >= 0; i--) r += ((unsigned)v >> i) & 1 ? '1' : '0'; return r; }
BStr bb_string(const BStr &s, int n) { BStr r; for (int i = 0; i < n; i++) r += s; return r; }

// =========================================================== random
static std::mt19937 g_rng(1234);
void bb_seedrnd(int s) { g_rng.seed((uint32_t)s); }
static double rnd01() { return (g_rng() >> 8) * (1.0 / 16777216.0); }
int bb_rand(int a) { return bb_rand(1, a); }
int bb_rand(int a, int b) {
  if (a > b) std::swap(a, b);
  int64_t range = (int64_t)b - a + 1;
  return (int)(a + (int64_t)(rnd01() * range));
}
float bb_rnd(float a) { return bb_rnd(0.0f, a); }
float bb_rnd(float a, float b) { return (float)(a + rnd01() * (b - a)); }

// =========================================================== time
static int64_t g_t0 = 0;
int bb_millisecs() { return (int)(plat_ms() - g_t0); }

struct Timer { double period; double next; };
static std::map<int, Timer> g_timers;
static int g_timer_id = 0;

static void present_frame();
static bool g_pres_front_dirty = false;
void bb_delay(int ms) {
  int64_t end = plat_ms() + ms;
  if (g_pres_front_dirty) { g_pres_front_dirty = false; present_frame(); }
  for (;;) {
    plat_pump();
    int64_t now = plat_ms();
    if (now >= end) break;
    plat_sleep((int)std::min<int64_t>(end - now, 10));
  }
}
int bb_createtimer(int hz) {
  if (hz <= 0) hz = 60;
  Timer t;
  t.period = 1000.0 / hz;
  t.next = (double)plat_ms() + t.period;
  g_timers[++g_timer_id] = t;
  return g_timer_id;
}
int bb_waittimer(int id) {
  auto it = g_timers.find(id);
  if (it == g_timers.end()) return 0;
  Timer &t = it->second;
  plat_pump();
  double now = (double)plat_ms();
  if (g_pres_front_dirty) { g_pres_front_dirty = false; present_frame(); }
  // block until at least one tick has elapsed (like Blitz's WaitTimer)
  while (now < t.next) {
    plat_sleep(std::max(1, (int)ceil(t.next - now)));
    plat_pump();
    now = (double)plat_ms();
  }
  int ticks = 0;
  while (t.next <= now) { t.next += t.period; ticks++; }
  if (ticks > 10) {  // resumed after a long pause: don't try to catch up
    t.next = now + t.period;
    ticks = 1;
  }
  return ticks;
}
void bb_freetimer(int id) { g_timers.erase(id); }
BStr bb_currentdate() {
  time_t t = time(nullptr); char b[32]; strftime(b, sizeof b, "%d %b %Y", localtime(&t)); return b;
}
BStr bb_currenttime() {
  time_t t = time(nullptr); char b[32]; strftime(b, sizeof b, "%H:%M:%S", localtime(&t)); return b;
}

// =========================================================== virtual file system
// All paths are normalised to lower case with '/' separators. Reads look in the
// writable data dir first, then in the bundled assets.
static std::set<std::string> g_asset_files;
static std::set<std::string> g_asset_dirs;

static std::string norm_path(const BStr &p) {
  std::string s;
  s.reserve(p.size());
  for (char c : p) {
    if (c == '\\') c = '/';
    s += (char)tolower((unsigned char)c);
  }
  // strip drive / leading "./" and duplicate slashes
  std::string out;
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] == '/' && !out.empty() && out.back() == '/') continue;
    out += s[i];
  }
  while (out.compare(0, 2, "./") == 0) out = out.substr(2);
  while (!out.empty() && out[0] == '/') out = out.substr(1);
  return out;
}

static std::string strip_slash(std::string s) {
  while (!s.empty() && s.back() == '/') s.pop_back();
  return s;
}

static void load_manifest() {
  std::vector<uint8_t> m;
  if (!plat_asset_read("manifest.txt", m)) {
    logf_("warning: no asset manifest");
    return;
  }
  std::string cur;
  for (size_t i = 0; i <= m.size(); i++) {
    char c = i < m.size() ? (char)m[i] : '\n';
    if (c == '\r') continue;
    if (c == '\n') {
      if (!cur.empty()) {
        std::string p = norm_path(cur);
        g_asset_files.insert(p);
        for (size_t k = p.find('/'); k != std::string::npos; k = p.find('/', k + 1))
          g_asset_dirs.insert(p.substr(0, k));
      }
      cur.clear();
    } else cur += c;
  }
  logf_("manifest: %d files", (int)g_asset_files.size());
}

static bool data_file_exists(const std::string &np) {
  FILE *f = fopen((plat_data_dir() + np).c_str(), "rb");
  if (f) { fclose(f); return true; }
  return false;
}

static bool vfs_read(const BStr &path, std::vector<uint8_t> &out) {
  std::string np = norm_path(path);
  FILE *f = fopen((plat_data_dir() + np).c_str(), "rb");
  if (f) {
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    out.resize(n > 0 ? n : 0);
    if (n > 0 && fread(out.data(), 1, n, f) != (size_t)n) out.clear();
    fclose(f);
    return true;
  }
  if (g_asset_files.count(np) || g_asset_files.empty()) return plat_asset_read(np, out);
  return false;
}

static void mkdirs_for(const std::string &np) {
  std::string base = plat_data_dir();
  for (size_t k = np.find('/'); k != std::string::npos; k = np.find('/', k + 1)) {
    std::string d = base + np.substr(0, k);
    mkdir(d.c_str(), 0777);
  }
}

int bb_filetype(const BStr &p) {
  std::string np = strip_slash(norm_path(p));
  if (np.empty()) return 2;
  if (data_file_exists(np)) return 1;
  if (g_asset_files.count(np)) return 1;
  if (g_asset_dirs.count(np)) return 2;
  return 0;
}
int bb_filesize(const BStr &p) {
  std::vector<uint8_t> d;
  return vfs_read(p, d) ? (int)d.size() : 0;
}

struct BFile {
  std::vector<uint8_t> data;
  size_t pos = 0;
  bool write = false;
  bool dirty = false;
  std::string np;
};
static std::map<int, std::unique_ptr<BFile>> g_files;
static int g_file_id = 0;

int bb_readfile(const BStr &p) {
  auto f = std::make_unique<BFile>();
  if (!vfs_read(p, f->data)) return 0;
  f->np = norm_path(p);
  g_files[++g_file_id] = std::move(f);
  return g_file_id;
}
int bb_writefile(const BStr &p) {
  auto f = std::make_unique<BFile>();
  f->np = norm_path(p);
  f->write = true;
  f->dirty = true;
  g_files[++g_file_id] = std::move(f);
  return g_file_id;
}
int bb_openfile(const BStr &p) {
  auto f = std::make_unique<BFile>();
  vfs_read(p, f->data);
  f->np = norm_path(p);
  f->write = true;
  g_files[++g_file_id] = std::move(f);
  return g_file_id;
}
static BFile *getf(int id) {
  auto it = g_files.find(id);
  return it == g_files.end() ? nullptr : it->second.get();
}
void bb_closefile(int id) {
  BFile *f = getf(id);
  if (!f) return;
  if (f->write && f->dirty) {
    mkdirs_for(f->np);
    std::string full = plat_data_dir() + f->np;
    FILE *o = fopen(full.c_str(), "wb");
    if (o) {
      if (!f->data.empty()) fwrite(f->data.data(), 1, f->data.size(), o);
      fclose(o);
    } else logf_("cannot write %s", full.c_str());
  }
  g_files.erase(id);
}
static void fput(BFile *f, const void *p, size_t n) {
  if (!f) return;
  if (f->pos + n > f->data.size()) f->data.resize(f->pos + n);
  memcpy(f->data.data() + f->pos, p, n);
  f->pos += n;
  f->dirty = true;
}
static bool fget(BFile *f, void *p, size_t n) {
  if (!f || f->pos + n > f->data.size()) {
    memset(p, 0, n);
    if (f) f->pos = f->data.size();
    return false;
  }
  memcpy(p, f->data.data() + f->pos, n);
  f->pos += n;
  return true;
}
int bb_readint(int id) { int32_t v; fget(getf(id), &v, 4); return v; }
void bb_writeint(int id, int v) { int32_t x = v; fput(getf(id), &x, 4); }
float bb_readfloat(int id) { float v; fget(getf(id), &v, 4); return v; }
void bb_writefloat(int id, float v) { fput(getf(id), &v, 4); }
int bb_readbyte(int id) { uint8_t v; fget(getf(id), &v, 1); return v; }
void bb_writebyte(int id, int v) { uint8_t x = (uint8_t)v; fput(getf(id), &x, 1); }
int bb_readshort(int id) { uint16_t v; fget(getf(id), &v, 2); return v; }
void bb_writeshort(int id, int v) { uint16_t x = (uint16_t)v; fput(getf(id), &x, 2); }
BStr bb_readstring(int id) {
  int32_t n = 0;
  BFile *f = getf(id);
  fget(f, &n, 4);
  if (!f || n <= 0 || f->pos + n > f->data.size()) return BStr();
  BStr s((const char *)f->data.data() + f->pos, n);
  f->pos += n;
  return s;
}
void bb_writestring(int id, const BStr &s) {
  int32_t n = (int32_t)s.size();
  BFile *f = getf(id);
  fput(f, &n, 4);
  fput(f, s.data(), s.size());
}
BStr bb_readline(int id) {
  BFile *f = getf(id);
  if (!f) return BStr();
  BStr s;
  while (f->pos < f->data.size()) {
    char c = (char)f->data[f->pos++];
    if (c == '\n') break;
    if (c != '\r') s += c;
  }
  return s;
}
void bb_writeline(int id, const BStr &s) {
  BFile *f = getf(id);
  fput(f, s.data(), s.size());
  fput(f, "\r\n", 2);
}
int bb_eof(int id) {
  BFile *f = getf(id);
  if (!f) return 1;
  return f->pos >= f->data.size() ? 1 : 0;
}

struct BDir { std::vector<std::string> names; size_t pos = 0; };
static std::map<int, BDir> g_dirs;
static int g_dir_id = 0;

#include <dirent.h>
int bb_readdir(const BStr &p) {
  std::string np = strip_slash(norm_path(p));
  std::string pre = np.empty() ? "" : np + "/";
  std::set<std::string> names;
  names.insert(".");
  names.insert("..");
  for (auto &f : g_asset_files) {
    if (f.compare(0, pre.size(), pre) != 0) continue;
    std::string rest = f.substr(pre.size());
    size_t k = rest.find('/');
    names.insert(k == std::string::npos ? rest : rest.substr(0, k));
  }
  DIR *d = opendir((plat_data_dir() + np).c_str());
  if (d) {
    while (dirent *e = readdir(d)) names.insert(bb_lower(e->d_name));
    closedir(d);
  }
  BDir bd;
  bd.names.assign(names.begin(), names.end());
  g_dirs[++g_dir_id] = bd;
  return g_dir_id;
}
BStr bb_nextfile(int id) {
  auto it = g_dirs.find(id);
  if (it == g_dirs.end() || it->second.pos >= it->second.names.size()) return BStr();
  return it->second.names[it->second.pos++];
}
void bb_closedir(int id) { g_dirs.erase(id); }
void bb_deletefile(const BStr &p) { remove((plat_data_dir() + norm_path(p)).c_str()); }
void bb_copyfile(const BStr &a, const BStr &b) {
  std::vector<uint8_t> d;
  if (!vfs_read(a, d)) return;
  int f = bb_writefile(b);
  fput(getf(f), d.data(), d.size());
  bb_closefile(f);
}
void bb_createdir(const BStr &p) { mkdirs_for(norm_path(p) + "/"); }
BStr bb_currentdir() { return BStr(""); }

// =========================================================== images
struct Image {
  int w = 0, h = 0, frames = 1;
  std::vector<uint32_t> px;   // frames stacked vertically
  uint32_t mask = 0;          // 0x00RRGGBB
  int hx = 0, hy = 0;
  uint32_t *frame(int f) {
    if (f < 0 || f >= frames) f = 0;
    return px.data() + (size_t)f * w * h;
  }
};

static std::vector<std::unique_ptr<Image>> g_images(1);   // index 0 unused
static std::vector<int> g_free_images;
static int g_gfx_w = 640, g_gfx_h = 480, g_depth = 16;
static int g_front = 0, g_back = 0;       // image ids of the two screen buffers
static int g_cur_buf_img = 0;             // image currently drawn to
static uint32_t g_color = 0xffffff, g_clscolor = 0;

const int BUF_IMAGE_BASE = 0x10000000;
const int BUF_BACK = 0x7ffffff1;
const int BUF_FRONT = 0x7ffffff2;

static int new_image(int w, int h, int frames) {
  auto im = std::make_unique<Image>();
  im->w = std::max(w, 1);
  im->h = std::max(h, 1);
  im->frames = std::max(frames, 1);
  im->px.assign((size_t)im->w * im->h * im->frames, 0);
  int id;
  if (!g_free_images.empty()) {
    id = g_free_images.back();
    g_free_images.pop_back();
    g_images[id] = std::move(im);
  } else {
    id = (int)g_images.size();
    g_images.push_back(std::move(im));
  }
  return id;
}
static Image *img(int id) {
  if (id <= 0 || id >= (int)g_images.size()) return nullptr;
  return g_images[id].get();
}

static inline bool is_masked(uint32_t p, uint32_t mask) {
  if (g_depth == 16) return (p & 0xF8FCF8) == (mask & 0xF8FCF8);
  return (p & 0xFFFFFF) == (mask & 0xFFFFFF);
}

// ---- BMP decoding
static bool decode_bmp(const std::vector<uint8_t> &d, int &w, int &h, std::vector<uint32_t> &px) {
  if (d.size() < 54 || d[0] != 'B' || d[1] != 'M') return false;
  auto u32 = [&](size_t o) { return (uint32_t)d[o] | d[o + 1] << 8 | d[o + 2] << 16 | (uint32_t)d[o + 3] << 24; };
  auto u16 = [&](size_t o) { return (uint32_t)d[o] | d[o + 1] << 8; };
  uint32_t off = u32(10), hsz = u32(14);
  int bw, bh, bpp, comp = 0;
  uint32_t ncol = 0;
  size_t palo;
  if (hsz == 12) {
    bw = (int16_t)u16(18); bh = (int16_t)u16(20); bpp = u16(24); palo = 26;
  } else {
    bw = (int32_t)u32(18); bh = (int32_t)u32(22); bpp = u16(28); comp = u32(30); ncol = u32(46); palo = 14 + hsz;
  }
  bool topdown = bh < 0;
  if (bh < 0) bh = -bh;
  if (bw <= 0 || bh <= 0 || bw > 16384 || bh > 16384) return false;
  w = bw; h = bh;
  px.assign((size_t)w * h, 0);
  uint32_t pal[256] = {0};
  int palent = hsz == 12 ? 3 : 4;
  if (bpp <= 8) {
    int n = ncol ? ncol : (1 << bpp);
    for (int i = 0; i < n && i < 256 && palo + i * palent + 2 < d.size(); i++)
      pal[i] = d[palo + i * palent + 2] << 16 | d[palo + i * palent + 1] << 8 | d[palo + i * palent];
  }
  uint32_t rmask = 0xF800, gmask = 0x7E0, bmask = 0x1F;
  if (comp == 3 && hsz >= 40) {
    rmask = u32(54); gmask = u32(58); bmask = u32(62);
    if (bpp == 16 && rmask == 0 && gmask == 0) { rmask = 0x7C00; gmask = 0x3E0; bmask = 0x1F; }
  } else if (bpp == 16) { rmask = 0x7C00; gmask = 0x3E0; bmask = 0x1F; }
  auto ext = [](uint32_t v, uint32_t m) -> uint32_t {
    if (!m) return 0;
    int sh = 0; while (!((m >> sh) & 1)) sh++;
    uint32_t mx = m >> sh, x = (v & m) >> sh;
    return mx ? x * 255 / mx : 0;
  };
  auto put = [&](int x, int y, uint32_t c) {
    int yy = topdown ? y : (h - 1 - y);
    if (x >= 0 && x < w && yy >= 0 && yy < h) px[(size_t)yy * w + x] = c;
  };
  if (comp == 1 || comp == 2) {  // RLE8 / RLE4
    size_t p = off;
    int x = 0, y = 0;
    while (p + 1 < d.size() && y < h) {
      int a = d[p], b = d[p + 1]; p += 2;
      if (a) {
        for (int i = 0; i < a; i++) {
          int idx = comp == 1 ? b : ((i & 1) ? (b & 15) : (b >> 4));
          put(x++, y, pal[idx]);
        }
      } else if (b == 0) { x = 0; y++; }
      else if (b == 1) break;
      else if (b == 2) { if (p + 1 >= d.size()) break; x += d[p]; y += d[p + 1]; p += 2; }
      else {
        for (int i = 0; i < b && p < d.size(); i++) {
          int idx;
          if (comp == 1) idx = d[p++];
          else { idx = (i & 1) ? (d[p++] & 15) : (d[p] >> 4); }
          put(x++, y, pal[idx]);
        }
        if (comp == 2 && (b & 1)) p++;
        size_t used = comp == 1 ? b : (b + 1) / 2;
        if (used & 1) p++;
      }
    }
    return true;
  }
  size_t stride = (((size_t)w * bpp + 31) / 32) * 4;
  for (int y = 0; y < h; y++) {
    size_t row = off + (size_t)y * stride;
    if (row >= d.size()) break;
    for (int x = 0; x < w; x++) {
      uint32_t c = 0;
      switch (bpp) {
        case 1: { size_t o = row + x / 8; if (o < d.size()) c = pal[(d[o] >> (7 - x % 8)) & 1]; } break;
        case 4: { size_t o = row + x / 2; if (o < d.size()) c = pal[(x & 1) ? d[o] & 15 : d[o] >> 4]; } break;
        case 8: { size_t o = row + x; if (o < d.size()) c = pal[d[o]]; } break;
        case 16: { size_t o = row + x * 2; if (o + 1 < d.size()) { uint32_t v = u16(o); c = ext(v, rmask) << 16 | ext(v, gmask) << 8 | ext(v, bmask); } } break;
        case 24: { size_t o = row + x * 3; if (o + 2 < d.size()) c = d[o + 2] << 16 | d[o + 1] << 8 | d[o]; } break;
        case 32: { size_t o = row + x * 4; if (o + 3 < d.size()) c = d[o + 2] << 16 | d[o + 1] << 8 | d[o]; } break;
        default: return false;
      }
      int yy = topdown ? y : (h - 1 - y);
      px[(size_t)yy * w + x] = c;
    }
  }
  return true;
}

int bb_loadimage(const BStr &path) {
  std::vector<uint8_t> d;
  if (!vfs_read(path, d)) return 0;
  int w, h;
  std::vector<uint32_t> px;
  if (!decode_bmp(d, w, h, px)) {
    logf_("cannot decode image %s", path.c_str());
    return 0;
  }
  int id = new_image(w, h, 1);
  img(id)->px.swap(px);
  return id;
}
int bb_createimage(int w, int h, int frames) { return new_image(w, h, frames); }
void bb_freeimage(int id) {
  if (id <= 0 || id >= (int)g_images.size() || !g_images[id] || id == g_front || id == g_back) return;
  if (id == g_cur_buf_img) g_cur_buf_img = g_back;
  g_images[id].reset();
  g_free_images.push_back(id);
}
int bb_imagewidth(int id) { Image *i = img(id); return i ? i->w : 0; }
int bb_imageheight(int id) { Image *i = img(id); return i ? i->h : 0; }
void bb_maskimage(int id, int r, int g, int b) {
  if (Image *i = img(id)) i->mask = (r & 255) << 16 | (g & 255) << 8 | (b & 255);
}
void bb_midhandle(int id) { if (Image *i = img(id)) { i->hx = i->w / 2; i->hy = i->h / 2; } }

void bb_graphics(int w, int h, int depth, int mode) {
  g_gfx_w = w;
  g_gfx_h = h;
  g_depth = depth == 16 ? 16 : 32;
  // keep handles stable across repeated Graphics calls
  if (!g_front) { g_front = new_image(w, h, 1); g_back = new_image(w, h, 1); }
  else {
    *img(g_front) = Image(); img(g_front)->w = w; img(g_front)->h = h; img(g_front)->px.assign((size_t)w * h, 0);
    *img(g_back) = Image(); img(g_back)->w = w; img(g_back)->h = h; img(g_back)->px.assign((size_t)w * h, 0);
  }
  g_cur_buf_img = g_front;
  g_color = 0xffffff;
  g_clscolor = 0;
}
static int buf_to_img(int buf) {
  if (buf == BUF_BACK) return g_back;
  if (buf == BUF_FRONT) return g_front;
  if (buf >= BUF_IMAGE_BASE) return (buf - BUF_IMAGE_BASE) & 0xffffff;
  return 0;
}
void bb_setbuffer(int buf) {
  int id = buf_to_img(buf);
  if (img(id)) g_cur_buf_img = id;
}
int bb_backbuffer() { return BUF_BACK; }
int bb_frontbuffer() { return BUF_FRONT; }
int bb_imagebuffer(int id, int frame) { return img(id) ? BUF_IMAGE_BASE + id : 0; }
int bb_graphicswidth() { return g_gfx_w; }
int bb_graphicsheight() { return g_gfx_h; }
int bb_availvidmem() { return 64 << 20; }
int bb_totalvidmem() { return 128 << 20; }
void bb_apptitle(const BStr &, const BStr &) {}
void bb_hidepointer() {}
void bb_showpointer() {}
void bb_setgfxdriver(int) {}
int bb_gfxmodeexists(int, int, int) { return 1; }

static Image *cur() {
  if (g_cur_buf_img == g_front) g_pres_front_dirty = true;
  return img(g_cur_buf_img);
}

void bb_cls() {
  Image *t = cur();
  if (!t) return;
  std::fill(t->px.begin(), t->px.begin() + (size_t)t->w * t->h, g_clscolor);
  if (g_cur_buf_img == g_front) g_pres_front_dirty = true;
}
void bb_clscolor(int r, int g, int b) { g_clscolor = (r & 255) << 16 | (g & 255) << 8 | (b & 255); }
void bb_color(int r, int g, int b) { g_color = (r & 255) << 16 | (g & 255) << 8 | (b & 255); }

static void fill_rect(Image *t, int x, int y, int w, int h, uint32_t c) {
  int x0 = std::max(x, 0), y0 = std::max(y, 0);
  int x1 = std::min(x + w, t->w), y1 = std::min(y + h, t->h);
  for (int yy = y0; yy < y1; yy++) {
    uint32_t *row = t->px.data() + (size_t)yy * t->w;
    std::fill(row + x0, row + std::max(x1, x0), c);
  }
}
static inline void pset(Image *t, int x, int y, uint32_t c) {
  if ((unsigned)x < (unsigned)t->w && (unsigned)y < (unsigned)t->h) t->px[(size_t)y * t->w + x] = c;
}
void bb_rect(int x, int y, int w, int h, int solid) {
  Image *t = cur();
  if (!t || w <= 0 || h <= 0) return;
  if (solid) fill_rect(t, x, y, w, h, g_color);
  else {
    fill_rect(t, x, y, w, 1, g_color);
    fill_rect(t, x, y + h - 1, w, 1, g_color);
    fill_rect(t, x, y, 1, h, g_color);
    fill_rect(t, x + w - 1, y, 1, h, g_color);
  }
}
void bb_oval(int x, int y, int w, int h, int solid) {
  Image *t = cur();
  if (!t || w <= 0 || h <= 0) return;
  float rx = w / 2.0f, ry = h / 2.0f, cx = x + rx, cy = y + ry;
  for (int yy = y; yy < y + h; yy++) {
    float dy = (yy + 0.5f - cy) / ry;
    if (dy * dy > 1) continue;
    float dx = rx * sqrtf(1 - dy * dy);
    int a = (int)lrintf(cx - dx), b = (int)lrintf(cx + dx);
    if (solid) fill_rect(t, a, yy, b - a, 1, g_color);
    else { pset(t, a, yy, g_color); pset(t, b - 1, yy, g_color); }
  }
}
void bb_line(int x1, int y1, int x2, int y2) {
  Image *t = cur();
  if (!t) return;
  int dx = abs(x2 - x1), sx = x1 < x2 ? 1 : -1, dy = -abs(y2 - y1), sy = y1 < y2 ? 1 : -1, err = dx + dy;
  for (int n = 0; n < 100000; n++) {
    pset(t, x1, y1, g_color);
    if (x1 == x2 && y1 == y2) break;
    int e2 = 2 * err;
    if (e2 >= dy) { err += dy; x1 += sx; }
    if (e2 <= dx) { err += dx; y1 += sy; }
  }
}
void bb_plot(int x, int y) { if (Image *t = cur()) pset(t, x, y, g_color); }

static void draw_glyphs(Image *t, int x, int y, const BStr &s, uint32_t c) {
  for (size_t i = 0; i < s.size(); i++) {
    unsigned ch = (unsigned char)s[i];
    if (ch < 32 || ch > 126) ch = '?';
    const uint8_t *g = FONT8X16[ch - 32];
    for (int r = 0; r < FONT_H; r++)
      for (int b = 0; b < 8; b++)
        if (g[r] & (0x80 >> b)) pset(t, x + (int)i * 8 + b, y + r, c);
  }
}
void bb_text(int x, int y, const BStr &s, int cx, int cy) {
  Image *t = cur();
  if (!t) return;
  if (cx) x -= (int)s.size() * 4;
  if (cy) y -= FONT_H / 2;
  draw_glyphs(t, x, y, s, g_color);
}

static void blit(Image *src, int frame, Image *dst, int x, int y, bool masked) {
  if (!src || !dst) return;
  x -= src->hx;
  y -= src->hy;
  const uint32_t *sp = src->frame(frame);
  int x0 = std::max(0, -x), y0 = std::max(0, -y);
  int x1 = std::min(src->w, dst->w - x), y1 = std::min(src->h, dst->h - y);
  if (x0 >= x1 || y0 >= y1) return;
  uint32_t m = src->mask;
  for (int yy = y0; yy < y1; yy++) {
    const uint32_t *s = sp + (size_t)yy * src->w;
    uint32_t *d = dst->px.data() + (size_t)(y + yy) * dst->w + x;
    if (masked) {
      for (int xx = x0; xx < x1; xx++) {
        uint32_t p = s[xx];
        if (!is_masked(p, m)) d[xx] = p;
      }
    } else memcpy(d + x0, s + x0, (x1 - x0) * 4);
  }
}
void bb_drawimage(int id, int x, int y, int frame) {
  blit(img(id), frame, cur(), x, y, true);
}
void bb_drawblock(int id, int x, int y, int frame) { blit(img(id), frame, cur(), x, y, false); }
void bb_tileimage(int id, int x, int y, int frame) {
  Image *s = img(id), *t = cur();
  if (!s || !t) return;
  int sx = x - s->hx, sy = y - s->hy;
  sx %= s->w; if (sx > 0) sx -= s->w;
  sy %= s->h; if (sy > 0) sy -= s->h;
  for (int yy = sy; yy < t->h; yy += s->h)
    for (int xx = sx; xx < t->w; xx += s->w) blit(s, frame, t, xx + s->hx, yy + s->hy, true);
}

int bb_imagerectcollide(int id, int x, int y, int frame, int rx, int ry, int rw, int rh) {
  Image *s = img(id);
  if (!s) return 0;
  x -= s->hx;
  y -= s->hy;
  int x0 = std::max(x, rx), y0 = std::max(y, ry);
  int x1 = std::min(x + s->w, rx + rw), y1 = std::min(y + s->h, ry + rh);
  if (x0 >= x1 || y0 >= y1) return 0;
  const uint32_t *p = s->frame(frame);
  for (int yy = y0; yy < y1; yy++) {
    const uint32_t *row = p + (size_t)(yy - y) * s->w - x;
    for (int xx = x0; xx < x1; xx++)
      if (!is_masked(row[xx], s->mask)) return 1;
  }
  return 0;
}
int bb_rectsoverlap(int x1, int y1, int w1, int h1, int x2, int y2, int w2, int h2) {
  return (x1 < x2 + w2 && x2 < x1 + w1 && y1 < y2 + h2 && y2 < y1 + h1) ? 1 : 0;
}
int bb_imagesoverlap(int i1, int x1, int y1, int i2, int x2, int y2) {
  Image *a = img(i1), *b = img(i2);
  if (!a || !b) return 0;
  return bb_rectsoverlap(x1 - a->hx, y1 - a->hy, a->w, a->h, x2 - b->hx, y2 - b->hy, b->w, b->h);
}
int bb_imagescollide(int i1, int x1, int y1, int f1, int i2, int x2, int y2, int f2) {
  Image *a = img(i1), *b = img(i2);
  if (!a || !b) return 0;
  x1 -= a->hx; y1 -= a->hy; x2 -= b->hx; y2 -= b->hy;
  int X0 = std::max(x1, x2), Y0 = std::max(y1, y2);
  int X1 = std::min(x1 + a->w, x2 + b->w), Y1 = std::min(y1 + a->h, y2 + b->h);
  const uint32_t *pa = a->frame(f1), *pb = b->frame(f2);
  for (int y = Y0; y < Y1; y++)
    for (int x = X0; x < X1; x++)
      if (!is_masked(pa[(size_t)(y - y1) * a->w + x - x1], a->mask) &&
          !is_masked(pb[(size_t)(y - y2) * b->w + x - x2], b->mask))
        return 1;
  return 0;
}
void bb_copyrect(int sx, int sy, int w, int h, int dx, int dy, int src, int dst) {
  Image *s = src ? img(buf_to_img(src)) : cur();
  Image *d = dst ? img(buf_to_img(dst)) : cur();
  if (!s || !d) return;
  std::vector<uint32_t> tmp((size_t)std::max(w, 0) * std::max(h, 0));
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++) {
      int xx = sx + x, yy = sy + y;
      tmp[(size_t)y * w + x] = ((unsigned)xx < (unsigned)s->w && (unsigned)yy < (unsigned)s->h) ? s->px[(size_t)yy * s->w + xx] : 0;
    }
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++) pset(d, dx + x, dy + y, tmp[(size_t)y * w + x]);
}
int bb_savebuffer(int buf, const BStr &path) {
  Image *s = img(buf_to_img(buf));
  if (!s) return 0;
  std::vector<uint8_t> o;
  int w = s->w, h = s->h;
  size_t stride = ((size_t)w * 3 + 3) & ~3u;
  uint32_t size = 54 + stride * h;
  o.resize(size, 0);
  auto w32 = [&](size_t off, uint32_t v) { for (int i = 0; i < 4; i++) o[off + i] = (uint8_t)(v >> (i * 8)); };
  o[0] = 'B'; o[1] = 'M'; w32(2, size); w32(10, 54); w32(14, 40); w32(18, w); w32(22, h);
  o[26] = 1; o[28] = 24;
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++) {
      uint32_t p = s->px[(size_t)(h - 1 - y) * w + x];
      size_t off = 54 + y * stride + x * 3;
      o[off] = p & 255; o[off + 1] = (p >> 8) & 255; o[off + 2] = (p >> 16) & 255;
    }
  int f = bb_writefile(path);
  fput(getf(f), o.data(), o.size());
  bb_closefile(f);
  return 1;
}

// =========================================================== sound
struct Sound {
  std::vector<int16_t> pcm;  // stereo interleaved
  int rate = 22050;
  bool loop = false;
  float vol = 1.0f;
};
struct Channel {
  std::shared_ptr<Sound> snd;
  double pos = 0;
  double step = 1;
  bool loop = false;
  bool paused = false;
  float vol = 1.0f;
  int id = 0;
};
static std::mutex g_audio_mx;
static std::map<int, std::shared_ptr<Sound>> g_sounds;
static std::vector<Channel> g_channels;
static int g_sound_id = 0, g_channel_id = 0;
static const int MIX_RATE = 44100;
static const size_t MAX_CHANNELS = 24;

static bool decode_wav(const std::vector<uint8_t> &d, Sound &s) {
  if (d.size() < 44 || memcmp(d.data(), "RIFF", 4) || memcmp(d.data() + 8, "WAVE", 4)) return false;
  size_t p = 12;
  int fmt = 0, ch = 0, rate = 0, bits = 0;
  while (p + 8 <= d.size()) {
    uint32_t sz = d[p + 4] | d[p + 5] << 8 | d[p + 6] << 16 | (uint32_t)d[p + 7] << 24;
    const uint8_t *c = d.data() + p + 8;
    if (!memcmp(d.data() + p, "fmt ", 4) && p + 8 + 16 <= d.size()) {
      fmt = c[0] | c[1] << 8; ch = c[2] | c[3] << 8;
      rate = c[4] | c[5] << 8 | c[6] << 16 | c[7] << 24; bits = c[14] | c[15] << 8;
    } else if (!memcmp(d.data() + p, "data", 4)) {
      if (fmt != 1 || ch < 1 || ch > 2 || (bits != 8 && bits != 16)) return false;
      size_t n = std::min<size_t>(sz, d.size() - (p + 8));
      size_t bps = bits / 8 * ch;
      size_t frames = n / bps;
      s.pcm.resize(frames * 2);
      s.rate = rate;
      for (size_t i = 0; i < frames; i++) {
        int16_t l, r;
        const uint8_t *f = c + i * bps;
        if (bits == 8) { l = (int16_t)((f[0] - 128) << 8); r = ch == 2 ? (int16_t)((f[1] - 128) << 8) : l; }
        else { l = (int16_t)(f[0] | f[1] << 8); r = ch == 2 ? (int16_t)(f[2] | f[3] << 8) : l; }
        s.pcm[i * 2] = l; s.pcm[i * 2 + 1] = r;
      }
      return true;
    }
    p += 8 + sz + (sz & 1);
  }
  return false;
}

int bb_loadsound(const BStr &path) {
  std::vector<uint8_t> d;
  if (!vfs_read(path, d)) return 0;
  auto s = std::make_shared<Sound>();
  if (!decode_wav(d, *s)) {
    logf_("cannot decode sound %s", path.c_str());
    return 0;
  }
  std::lock_guard<std::mutex> lk(g_audio_mx);
  g_sounds[++g_sound_id] = s;
  return g_sound_id;
}
void bb_freesound(int id) {
  std::lock_guard<std::mutex> lk(g_audio_mx);
  g_sounds.erase(id);   // playing channels keep their shared_ptr
}
int bb_playsound(int id) {
  std::lock_guard<std::mutex> lk(g_audio_mx);
  auto it = g_sounds.find(id);
  if (it == g_sounds.end()) return 0;
  if (g_channels.size() >= MAX_CHANNELS) {
    // drop the oldest non-looping channel
    auto victim = std::find_if(g_channels.begin(), g_channels.end(), [](const Channel &c) { return !c.loop; });
    if (victim == g_channels.end()) victim = g_channels.begin();
    g_channels.erase(victim);
  }
  Channel c;
  c.snd = it->second;
  c.step = (double)c.snd->rate / MIX_RATE;
  c.loop = c.snd->loop;
  c.vol = c.snd->vol;
  c.id = ++g_channel_id;
  g_channels.push_back(c);
  return c.id;
}
void bb_loopsound(int id) {
  std::lock_guard<std::mutex> lk(g_audio_mx);
  auto it = g_sounds.find(id);
  if (it != g_sounds.end()) it->second->loop = true;
}
void bb_soundvolume(int id, float v) {
  std::lock_guard<std::mutex> lk(g_audio_mx);
  auto it = g_sounds.find(id);
  if (it != g_sounds.end()) it->second->vol = v;
}
static Channel *findch(int id) {
  for (auto &c : g_channels) if (c.id == id) return &c;
  return nullptr;
}
void bb_stopchannel(int id) {
  std::lock_guard<std::mutex> lk(g_audio_mx);
  g_channels.erase(std::remove_if(g_channels.begin(), g_channels.end(), [&](const Channel &c) { return c.id == id; }),
                   g_channels.end());
}
int bb_channelplaying(int id) { std::lock_guard<std::mutex> lk(g_audio_mx); return findch(id) ? 1 : 0; }
void bb_channelvolume(int id, float v) { std::lock_guard<std::mutex> lk(g_audio_mx); if (auto c = findch(id)) c->vol = v; }
void bb_pausechannel(int id) { std::lock_guard<std::mutex> lk(g_audio_mx); if (auto c = findch(id)) c->paused = true; }
void bb_resumechannel(int id) { std::lock_guard<std::mutex> lk(g_audio_mx); if (auto c = findch(id)) c->paused = false; }

void rt_audio_mix(int16_t *out, int frames) {
  static std::vector<int32_t> acc;
  acc.assign((size_t)frames * 2, 0);
  {
    std::lock_guard<std::mutex> lk(g_audio_mx);
    for (auto &c : g_channels) {
      if (c.paused) continue;
      const auto &pcm = c.snd->pcm;
      size_t n = pcm.size() / 2;
      if (!n) { c.pos = 1e18; continue; }
      int vol = (int)(c.vol * 256);
      for (int i = 0; i < frames; i++) {
        size_t ip = (size_t)c.pos;
        if (ip >= n) {
          if (c.loop) { c.pos -= n; ip = (size_t)c.pos; if (ip >= n) ip = 0; }
          else break;
        }
        acc[i * 2] += pcm[ip * 2] * vol >> 8;
        acc[i * 2 + 1] += pcm[ip * 2 + 1] * vol >> 8;
        c.pos += c.step;
      }
    }
    g_channels.erase(std::remove_if(g_channels.begin(), g_channels.end(),
                                    [](const Channel &c) { return !c.loop && (size_t)c.pos >= c.snd->pcm.size() / 2; }),
                     g_channels.end());
  }
  for (int i = 0; i < frames * 2; i++) {
    int v = acc[i] * 3 / 4;
    out[i] = (int16_t)std::max(-32768, std::min(32767, v));
  }
}

// =========================================================== input
static std::mutex g_in_mx;
static uint8_t g_keydown[256];
static int g_keyhits[256];
static int g_mouse_x = 0, g_mouse_y = 0;
static bool g_mouse_down[4];
static int g_mouse_hits[4];
static float g_joy_x[4], g_joy_y[4];
static bool g_joy_down[4][33];
static int g_joy_hits[4][33];
static std::vector<int> g_key_queue;

// Busy-wait loops in the game (e.g. getTheKey) poll KeyHit without ever calling
// Flip or Delay, so the input functions must process OS events themselves.
static int64_t g_last_present_ms = 0;
static void input_poll() {
  static unsigned calls = 0;
  if ((++calls & 63) != 0) return;
  plat_pump();
  // don't spin at 100% CPU in loops that never present a frame
  if ((calls & 1023) == 0 || (plat_ms() - g_last_present_ms > 40 && (calls & 255) == 0)) plat_sleep(1);
}

void rt_key(int sc, bool down) {
  if (sc <= 0 || sc > 255) return;
  std::lock_guard<std::mutex> lk(g_in_mx);
  if (down && !g_keydown[sc]) { g_keyhits[sc]++; g_key_queue.push_back(sc); }
  g_keydown[sc] = down;
}
void rt_joy_axis(int port, float x, float y) {
  if (port < 0 || port > 3) return;
  std::lock_guard<std::mutex> lk(g_in_mx);
  g_joy_x[port] = x; g_joy_y[port] = y;
}
void rt_joy_button(int port, int b, bool down) {
  if (port < 0 || port > 3 || b < 1 || b > 32) return;
  std::lock_guard<std::mutex> lk(g_in_mx);
  if (down && !g_joy_down[port][b]) g_joy_hits[port][b]++;
  g_joy_down[port][b] = down;
}

int bb_keydown(int k) { input_poll(); std::lock_guard<std::mutex> lk(g_in_mx); return (k > 0 && k < 256) ? g_keydown[k] : 0; }
int bb_keyhit(int k) {
  input_poll();
  if (k <= 0 || k > 255) return 0;
  std::lock_guard<std::mutex> lk(g_in_mx);
  int n = g_keyhits[k];
  g_keyhits[k] = 0;
  return n;
}
static const char *SC_ASCII = "\0\0331234567890-=\b\tqwertyuiop[]\r\0asdfghjkl;'`\0\\zxcvbnm,./\0*\0 ";
int bb_getkey() {
  input_poll();
  std::lock_guard<std::mutex> lk(g_in_mx);
  while (!g_key_queue.empty()) {
    int sc = g_key_queue.front();
    g_key_queue.erase(g_key_queue.begin());
    if (sc < 58 && SC_ASCII[sc]) return (unsigned char)SC_ASCII[sc];
  }
  return 0;
}
int bb_waitkey() {
  for (;;) {
    plat_pump();
    {
      std::lock_guard<std::mutex> lk(g_in_mx);
      if (!g_key_queue.empty()) { int sc = g_key_queue.front(); g_key_queue.clear(); return sc; }
    }
    plat_sleep(10);
  }
}
void bb_flushkeys() {
  std::lock_guard<std::mutex> lk(g_in_mx);
  memset(g_keyhits, 0, sizeof g_keyhits);
  g_key_queue.clear();
}
void bb_flushjoy() { std::lock_guard<std::mutex> lk(g_in_mx); memset(g_joy_hits, 0, sizeof g_joy_hits); }
void bb_flushmouse() { std::lock_guard<std::mutex> lk(g_in_mx); memset(g_mouse_hits, 0, sizeof g_mouse_hits); }
int bb_mousex() { return g_mouse_x; }
int bb_mousey() { return g_mouse_y; }
int bb_mousedown(int b) {
  input_poll(); return (b >= 1 && b <= 3) ? g_mouse_down[b] : 0; }
int bb_mousehit(int b) {
  input_poll();
  if (b < 1 || b > 3) return 0;
  std::lock_guard<std::mutex> lk(g_in_mx);
  int n = g_mouse_hits[b];
  g_mouse_hits[b] = 0;
  return n;
}
static int port_ok(int p) { return p >= 0 && p <= 3; }
float bb_joyx(int p) { return port_ok(p) ? g_joy_x[p] : 0; }
float bb_joyy(int p) { return port_ok(p) ? g_joy_y[p] : 0; }
int bb_joyxdir(int p) { float v = bb_joyx(p); return v < -0.5f ? -1 : (v > 0.5f ? 1 : 0); }
int bb_joyydir(int p) { float v = bb_joyy(p); return v < -0.5f ? -1 : (v > 0.5f ? 1 : 0); }
int bb_joyhit(int b, int p) {
  input_poll();
  if (!port_ok(p) || b < 1 || b > 32) return 0;
  std::lock_guard<std::mutex> lk(g_in_mx);
  int n = g_joy_hits[p][b];
  g_joy_hits[p][b] = 0;
  return n;
}
int bb_joydown(int b, int p) { input_poll(); return (port_ok(p) && b >= 1 && b <= 32) ? g_joy_down[p][b] : 0; }
int bb_joytype(int p) { return port_ok(p) ? 1 : 0; }
int bb_joygetkey(int) { return 0; }

// =========================================================== screen composition + touch overlay
struct VButton {
  int vb;            // VB_* or -1 for fixed scancode
  int scancode;      // when vb == -1
  float x, y, r;     // in screen pixels
  const char *label;
  bool down;
};
static std::vector<VButton> g_vbuttons;
static std::vector<uint32_t> g_screen;
static int g_scr_w = 640, g_scr_h = 480, g_game_x = 0;
static bool g_overlay = true;
static std::map<int, int> g_touch_owner;   // touch id -> button index (or -1 = mouse, -2 = dpad)
static int g_dpad_touch = -999;
static float g_dpad_cx, g_dpad_cy, g_dpad_r;
static int g_dpad_dir[4];                  // up, down, left, right

static void layout_screen() {
  float aspect = plat_screen_aspect();
  if (aspect < 1.0f) aspect = 1.0f / aspect;
  g_overlay = plat_touch_overlay();
  g_scr_h = g_gfx_h;
  g_scr_w = std::max(g_gfx_w, (int)(g_gfx_h * aspect + 0.5f));
  g_game_x = (g_scr_w - g_gfx_w) / 2;
  g_screen.assign((size_t)g_scr_w * g_scr_h, 0);
  g_vbuttons.clear();
  float side = (float)g_game_x;           // width of each side panel
  bool roomy = side >= 110;
  // d-pad on the left
  g_dpad_r = roomy ? std::min(side * 0.42f, 95.0f) : 70.0f;
  g_dpad_cx = roomy ? side / 2 : 85;
  g_dpad_cy = g_scr_h - g_dpad_r - 40;
  // action buttons on the right, arranged like a game pad (diamond + one extra)
  float br = roomy ? std::min(std::min(side * 0.19f, 36.0f), (side - 8) / 5.1f) : 28.0f;
  float rx = roomy ? g_scr_w - side / 2 : g_scr_w - 95;
  float cy = g_scr_h - (br * 2.6f) - 30;
  float d = br * 1.55f;
  g_vbuttons.push_back({VB_SHOT, 0, rx, cy + d, br, "ATTACK", false});
  g_vbuttons.push_back({VB_JUMP, 0, rx + d, cy, br, "JUMP", false});
  g_vbuttons.push_back({VB_SPECIAL, 0, rx - d, cy, br, "SPECIAL", false});
  g_vbuttons.push_back({VB_BLOCK, 0, rx, cy - d, br, "BLOCK", false});
  g_vbuttons.push_back({VB_GRAB, 0, rx, cy - d * 2.25f, br * 0.8f, "THROW", false});
  // system buttons at the top
  float sr = 20;
  g_vbuttons.push_back({-1, 1, roomy ? side / 2 : 30.0f, 30, sr, "ESC", false});
  g_vbuttons.push_back({-1, 28, roomy ? g_scr_w - side / 2 : g_scr_w - 30.0f, 30, sr, "OK", false});
}

static void key_for_vb(int vb, bool down) {
  int code = glue_player_code(vb);
  if (glue_player_uses_joy()) {
    int port = glue_player_port();
    if (vb >= VB_SHOT) rt_joy_button(port, code, down);
  } else if (code > 0) rt_key(code, down);
}
static void update_dpad(float x, float y, bool active) {
  int nd[4] = {0, 0, 0, 0};
  if (active) {
    float dx = x - g_dpad_cx, dy = y - g_dpad_cy;
    float dead = g_dpad_r * 0.25f;
    if (dx * dx + dy * dy > dead * dead) {
      float ang = atan2f(dy, dx) * 57.2958f;    // 0 = right, 90 = down
      if (ang > -67.5f && ang < 67.5f) nd[3] = 1;
      if (ang > 112.5f || ang < -112.5f) nd[2] = 1;
      if (ang > 22.5f && ang < 157.5f) nd[1] = 1;
      if (ang < -22.5f && ang > -157.5f) nd[0] = 1;
    }
  }
  static const int vbmap[4] = {VB_UP, VB_DOWN, VB_LEFT, VB_RIGHT};
  for (int i = 0; i < 4; i++)
    if (nd[i] != g_dpad_dir[i]) {
      g_dpad_dir[i] = nd[i];
      key_for_vb(vbmap[i], nd[i] != 0);
      // menus are driven by the arrow keys too
      static const int arrows[4] = {200, 208, 203, 205};
      if (glue_player_code(vbmap[i]) != arrows[i]) rt_key(arrows[i], nd[i] != 0);
    }
  if (glue_player_uses_joy()) {
    int port = glue_player_port();
    rt_joy_axis(port, nd[3] ? 1.0f : (nd[2] ? -1.0f : 0.0f), nd[1] ? 1.0f : (nd[0] ? -1.0f : 0.0f));
  }
}

static void set_button(int i, bool down) {
  VButton &b = g_vbuttons[i];
  if (b.down == down) return;
  b.down = down;
  if (b.vb < 0) rt_key(b.scancode, down);
  else key_for_vb(b.vb, down);
}

void rt_touch(int id, float nx, float ny, int action) {
  float x = nx * g_scr_w, y = ny * g_scr_h;
  if (action == 0) {
    if (g_overlay) {
      for (size_t i = 0; i < g_vbuttons.size(); i++) {
        VButton &b = g_vbuttons[i];
        float dx = x - b.x, dy = y - b.y, rr = b.r * 1.35f;
        if (dx * dx + dy * dy <= rr * rr) {
          g_touch_owner[id] = (int)i;
          set_button((int)i, true);
          return;
        }
      }
      float dx = x - g_dpad_cx, dy = y - g_dpad_cy;
      if (dx * dx + dy * dy <= g_dpad_r * g_dpad_r * 1.8f && g_dpad_touch == -999) {
        g_dpad_touch = id;
        g_touch_owner[id] = -2;
        update_dpad(x, y, true);
        return;
      }
    }
    // anything else behaves like the mouse inside the game area
    g_touch_owner[id] = -1;
    std::lock_guard<std::mutex> lk(g_in_mx);
    g_mouse_x = std::max(0, std::min(g_gfx_w - 1, (int)(x - g_game_x)));
    g_mouse_y = std::max(0, std::min(g_gfx_h - 1, (int)y));
    g_mouse_down[1] = true;
    g_mouse_hits[1]++;
    return;
  }
  auto it = g_touch_owner.find(id);
  if (it == g_touch_owner.end()) return;
  int owner = it->second;
  if (owner == -2) {
    update_dpad(x, y, action != 2);
    if (action == 2) g_dpad_touch = -999;
  } else if (owner == -1) {
    std::lock_guard<std::mutex> lk(g_in_mx);
    g_mouse_x = std::max(0, std::min(g_gfx_w - 1, (int)(x - g_game_x)));
    g_mouse_y = std::max(0, std::min(g_gfx_h - 1, (int)y));
    if (action == 2) g_mouse_down[1] = false;
  } else if (action == 2) {
    set_button(owner, false);
  }
  if (action == 2) g_touch_owner.erase(it);
}
void rt_vbutton(int vb, bool down) {
  // physical game pad / keyboard mapped to the player's actions
  static bool state[VB_COUNT];
  if (vb < 0 || vb >= VB_COUNT || state[vb] == down) return;
  state[vb] = down;
  key_for_vb(vb, down);
  if (vb <= VB_RIGHT) {
    static const int arrows[4] = {200, 208, 203, 205};
    if (glue_player_code(vb) != arrows[vb]) rt_key(arrows[vb], down);
  }
}
void rt_touch_cancel_all() {
  for (auto &kv : g_touch_owner) {
    if (kv.second >= 0) set_button(kv.second, false);
  }
  update_dpad(0, 0, false);
  g_dpad_touch = -999;
  g_touch_owner.clear();
  std::lock_guard<std::mutex> lk(g_in_mx);
  g_mouse_down[1] = false;
}

static inline uint32_t blend(uint32_t d, uint32_t s, int a) {
  uint32_t rb = ((d & 0xff00ff) * (256 - a) + (s & 0xff00ff) * a) >> 8;
  uint32_t g = ((d & 0x00ff00) * (256 - a) + (s & 0x00ff00) * a) >> 8;
  return (rb & 0xff00ff) | (g & 0x00ff00);
}
static void circle(float cx, float cy, float r, uint32_t col, int alpha, bool ring) {
  int x0 = (int)(cx - r - 1), x1 = (int)(cx + r + 1), y0 = (int)(cy - r - 1), y1 = (int)(cy + r + 1);
  for (int y = std::max(0, y0); y <= std::min(g_scr_h - 1, y1); y++)
    for (int x = std::max(0, x0); x <= std::min(g_scr_w - 1, x1); x++) {
      float dx = x + 0.5f - cx, dy = y + 0.5f - cy, d = sqrtf(dx * dx + dy * dy);
      if (d > r) continue;
      if (ring && d < r - 3) continue;
      uint32_t &p = g_screen[(size_t)y * g_scr_w + x];
      p = blend(p, col, alpha);
    }
}
static void screen_text(int x, int y, const char *s, uint32_t c) {
  Image tmp;
  tmp.w = g_scr_w; tmp.h = g_scr_h;
  tmp.px.swap(g_screen);
  draw_glyphs(&tmp, x, y, s, c);
  tmp.px.swap(g_screen);
}

static void present_frame() {
  Image *f = img(g_front);
  if (!f) return;
  int aspect_now = (int)(plat_screen_aspect() * 1000);
  static int aspect_prev = -1;
  static int overlay_prev = -1;
  if (aspect_now != aspect_prev || (int)plat_touch_overlay() != overlay_prev || g_screen.empty()) {
    aspect_prev = aspect_now;
    overlay_prev = plat_touch_overlay();
    layout_screen();
  }
  // side panels
  std::fill(g_screen.begin(), g_screen.end(), 0x101018);
  for (int y = 0; y < g_gfx_h && y < g_scr_h; y++)
    memcpy(&g_screen[(size_t)y * g_scr_w + g_game_x], &f->px[(size_t)y * f->w], g_gfx_w * 4);
  if (g_overlay) {
    bool inside = g_game_x < 110;     // controls overlap the picture: make them fainter
    int a = inside ? 70 : 140;
    circle(g_dpad_cx, g_dpad_cy, g_dpad_r, 0x808090, a / 2, false);
    circle(g_dpad_cx, g_dpad_cy, g_dpad_r, 0xc0c0d0, a, true);
    float o = g_dpad_r * 0.62f, rr = g_dpad_r * 0.28f;
    const float ax[4] = {0, 0, -1, 1}, ay[4] = {-1, 1, 0, 0};
    for (int i = 0; i < 4; i++)
      circle(g_dpad_cx + ax[i] * o, g_dpad_cy + ay[i] * o, rr, g_dpad_dir[i] ? 0xffd040 : 0xd0d0e0, g_dpad_dir[i] ? 200 : a, false);
    for (auto &b : g_vbuttons) {
      uint32_t col = b.vb < 0 ? 0x6080a0 : 0xd04040;
      if (b.vb == VB_JUMP) col = 0x40a040;
      if (b.vb == VB_SPECIAL) col = 0x4060d0;
      if (b.vb == VB_BLOCK) col = 0xc0a030;
      if (b.vb == VB_GRAB) col = 0xa050c0;
      circle(b.x, b.y, b.r, b.down ? 0xffffff : col, b.down ? 220 : a + 40, false);
      const char *lab = b.label;
      char shortlab[2] = {b.label[0], 0};
      if ((int)strlen(lab) * 8 > b.r * 2 + 6) lab = shortlab;
      int len = (int)strlen(lab);
      screen_text((int)b.x - len * 4, (int)b.y - 8, lab, 0xffffff);
    }
  }
  // hard frame cap: never present faster than 60 fps
  static double next_present = 0;
  double tnow = (double)plat_ms();
  if (next_present - tnow > 1000.0 / 60) next_present = tnow;   // clock jumped
  while (tnow < next_present) {
    plat_sleep(std::max(1, (int)ceil(next_present - tnow)));
    tnow = (double)plat_ms();
  }
  next_present = std::max(next_present + 1000.0 / 60, tnow - 1000.0 / 60);
  plat_present(g_screen.data(), g_scr_w, g_scr_h);
  g_last_present_ms = plat_ms();
  static int fcount = 0;
  static int64_t fstart = 0;
  fcount++;
  int64_t now = plat_ms();
  if (now - fstart >= 3000) {
    if (fstart) logf_("fps %.1f", fcount * 1000.0 / (now - fstart));
    fstart = now;
    fcount = 0;
  }
}

void bb_flip(int vwait) {
  // the 60 fps cap in present_frame() plays the role of the vertical blank
  (void)vwait;
  std::swap(g_front, g_back);
  if (g_cur_buf_img == g_back) g_cur_buf_img = g_front;
  else if (g_cur_buf_img == g_front) g_cur_buf_img = g_back;
  // keep the "back buffer" handle pointing to the drawing target
  g_pres_front_dirty = false;
  present_frame();
  plat_pump();
}
void bb_vwait(int) { plat_pump(); }

// fix up: BUF_BACK must always mean "the buffer that is not on screen"
// (handled by swapping ids in bb_flip; g_back always holds the back buffer)

void rt_init() {
  g_t0 = plat_ms();
  load_manifest();
  memset(g_keydown, 0, sizeof g_keydown);
  memset(g_keyhits, 0, sizeof g_keyhits);
}
