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
int bb_portrngpeek() { std::mt19937 copy = g_rng; return (int)copy(); }
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
static void pact_flush();
void bb_flushkeys() {
  pact_flush();
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
// The 640x480 game picture is centred in a frame with the phone's aspect ratio; the touch
// controls are drawn in the side bars (and over the picture edges on narrower screens).

// 16x16 pixel-art icons for the buttons ('#' = pixel)
static const char *ICON_FIST[16] = {
    "................", ".....##.##.##...", "....#########...", "....##########..",
    "....##########..", "..############..", ".#############..", ".#############..",
    ".############...", "..###########...", "...#########....", "....#######.....",
    "....#######.....", "....#######.....", "................", "................"};
static const char *ICON_JUMP[16] = {
    ".......##.......", "......####......", ".....######.....", "....########....",
    "...##########...", "..############..", ".....######.....", ".....######.....",
    ".....######.....", ".....######.....", ".....######.....", "................",
    "..############..", "..############..", "................", "................"};
static const char *ICON_BOLT[16] = {
    "........####....", ".......####.....", "......####......", ".....####.......",
    "....####........", "...##########...", "..##########....", "......####......",
    ".....####.......", "....####........", "...####.........", "..###...........",
    ".##.............", "................", "................", "................"};
static const char *ICON_SHIELD[16] = {
    "................", "..############..", "..############..", "..############..",
    "..############..", "..############..", "..############..", "...##########...",
    "...##########...", "....########....", ".....######.....", "......####......",
    ".......##.......", "................", "................", "................"};
static const char *ICON_THROW[16] = {
    "................", "........#######.", "..........#####.", ".........######.",
    "........####.##.", ".......####..##.", "......####...##.", ".....####.......",
    "....####........", "...####.........", "..####..........", ".####...........",
    ".###............", "................", "................", "................"};
static const char *ICON_LOCK[16] = {
    ".....######.....", "....########....", "....##....##....", "....##....##....",
    "....##..........", "..############..", "..############..", "..#####..#####..",
    "..#####..#####..", "..######.#####..", "..############..", "..############..",
    "................", "................", "................", "................"};
static const char *ICON_CHECK[16] = {
    "................", "..............##", ".............###", "............###.",
    "...........###..", "..........###...", ".##......###....", ".###....###.....",
    "..###..###......", "...######.......", "....####........", ".....##.........",
    "................", "................", "................", "................"};

static const char *ICON_JUTSU[16] = {
    ".......##.......", "......####......", "......####......", ".....##..##.....",
    "##..##......##..", "#######....#####", ".####...##...###", "..##...####...#.",
    ".#...####...##..", "###...##...####.", "#####....#######", "..##......##..##",
    ".....##..##.....", "......####......", "......####......", ".......##......."};

// UP + SPECIAL ("up special", gfx zuspecial*) and DOWN + SPECIAL ("down special", zDspecial*)
static const char *ICON_UPSP[16] = {
    ".......##.......", "......####......", ".....######.....", "....########....",
    "...###.##.###...", ".......##.......", ".......##.......", "................",
    "........####....", ".......####.....", "......#######...", "........###.....",
    ".......###......", "......##........", ".....#..........", "................"};
static const char *ICON_DNSP[16] = {
    "........####....", ".......####.....", "......#######...", "........###.....",
    ".......###......", "......##........", ".....#..........", "................",
    ".......##.......", ".......##.......", "...###.##.###...", "....########....",
    ".....######.....", "......####......", ".......##.......", "................"};
static const char *ICON_HEART[16] = {
    "................", "..####....####..", ".######..######.", "################",
    "################", "################", ".##############.", "..############..",
    "...##########...", "....########....", ".....######.....", "......####......",
    ".......##.......", "................", "................", "................"};

// VBK_COMBO: hold "vb" (BLOCK for JUTSU, UP, DOWN) and press SPECIAL on the next frame
enum { VBK_ACTION, VBK_KEY, VBK_UNLOCK, VBK_COMBO, VBK_CHEAT };
struct VButton {
  int kind;          // VBK_*
  int vb;            // VB_* for actions
  int scancode;      // for VBK_KEY
  float x, y, r;     // circle buttons: centre + radius; pills: x,y = top-left, r unused
  float w, h;        // pills only (w > 0 means pill)
  const char *label;
  const char *const *icon;
  uint32_t color;
  bool down;
  bool visible;
};
static std::vector<VButton> g_vbuttons;
static std::vector<uint32_t> g_screen;
static int g_scr_w = 640, g_scr_h = 480, g_game_x = 0;
static bool g_overlay = true;
static std::map<int, int> g_touch_owner;   // touch id -> button index (or -1 = mouse, -2 = dpad)
static int g_dpad_touch = -999;
static float g_dpad_cx, g_dpad_cy, g_dpad_r;
static int g_dpad_dir[4];                  // up, down, left, right
static int g_unlock_btn = -1;
static int g_cheat_btn[2] = {-1, -1};

// --- which menu screen the game shows (PortScreen), the unlock macro and toast messages
static int g_frame_no = 0;
static int g_screen_id = 0, g_screen_frame = -100;
static std::vector<std::pair<int, bool>> g_macro;   // queued key events, one per frame
static int g_macro_state = 0;                       // 0 idle, 1 running, 2 waiting for result
static int g_macro_was_unlocked = 0;
static std::string g_toast;
static int64_t g_toast_until = 0;

void bb_portscreen(int id) {
  g_screen_id = id;
  g_screen_frame = g_frame_no;
}
static bool on_main_menu() { return g_screen_id == 1 && g_frame_no - g_screen_frame <= 3; }
// the online lobby (PortScreen 2) has buttons where the D-pad / action buttons would be
static bool hide_fight_controls() { return g_screen_id == 2 && g_frame_no - g_screen_frame <= 3; }
static void toast(const std::string &t, int ms = 2500) {
  g_toast = t;
  g_toast_until = plat_ms() + ms;
}

// The game's own cheat (menus.bb, mainMenu): BACKSPACE x5 then ENTER unlocks all characters
// and all VS stages. The macro presses ENTER first so the game's counter starts from zero.
static void start_unlock_macro() {
  if (g_macro_state) return;
  g_macro_was_unlocked = glue_cheat_unlocked();
  g_macro.clear();
  g_macro.push_back({28, true});
  g_macro.push_back({28, false});
  for (int i = 0; i < 5; i++) { g_macro.push_back({14, true}); g_macro.push_back({14, false}); }
  g_macro.push_back({28, true});
  g_macro.push_back({28, false});
  g_macro_state = 1;
}
static void run_macro() {
  if (g_macro_state == 1) {
    if (!on_main_menu()) { g_macro.clear(); g_macro_state = 0; return; }
    if (!g_macro.empty()) {
      rt_key(g_macro.front().first, g_macro.front().second);
      g_macro.erase(g_macro.begin());
    } else g_macro_state = 2;
  } else if (g_macro_state == 2) {
    if (glue_cheat_unlocked()) {
      toast(g_macro_was_unlocked ? "ALREADY UNLOCKED" : "ALL CHARACTERS & MAPS UNLOCKED!");
    }
    g_macro_state = 0;
  }
}

static void layout_screen() {
  float aspect = plat_screen_aspect();
  if (aspect < 1.0f) aspect = 1.0f / aspect;
  g_overlay = plat_touch_overlay();
  g_scr_h = g_gfx_h;
  g_scr_w = std::max(g_gfx_w, (int)(g_gfx_h * aspect + 0.5f));
  g_game_x = (g_scr_w - g_gfx_w) / 2;
  g_screen.assign((size_t)g_scr_w * g_scr_h, 0);
  g_vbuttons.clear();
  float side = (float)g_game_x;
  float H = (float)g_scr_h, W = (float)g_scr_w;

  // D-pad, bottom left
  g_dpad_r = 108;
  g_dpad_cx = std::max(side / 2, g_dpad_r + 12);
  g_dpad_cy = H - g_dpad_r - 12;

  // action buttons, bottom right: ATTACK / JUMP / SPECIAL / BLOCK diamond + THROW
  // sizes are in the 480-pixel-high composite (x2.25 on a 1080p phone): main buttons 96 px wide
  float br = 48, d = br * 1.5f, sr = 36;
  float cx = W - std::max(side / 2, d + br + 12), cy = H - d - br - 10;
  auto act = [&](int vb, float x, float y, float r, const char *lab, const char *const *ic, uint32_t col) {
    VButton b{};
    b.kind = VBK_ACTION; b.vb = vb; b.x = x; b.y = y; b.r = r;
    b.label = lab; b.icon = ic; b.color = col; b.visible = true;
    g_vbuttons.push_back(b);
  };
  act(VB_SHOT, cx, cy + d, br, "ATTACK", ICON_FIST, 0xe03c3c);
  act(VB_JUMP, cx + d, cy, br, "JUMP", ICON_JUMP, 0x34b050);
  act(VB_SPECIAL, cx - d, cy, br, "SPECIAL", ICON_BOLT, 0x3c64e0);
  act(VB_BLOCK, cx, cy - d, br, "BLOCK", ICON_SHIELD, 0xe0a828);
  act(VB_GRAB, cx + d + 6, cy - d - 34, sr, "THROW", ICON_THROW, 0x9c50d0);
  // JUTSU = hold BLOCK + press SPECIAL (the game's super move, superKey in mh.bb)
  act(VB_BLOCK, cx - d - 6, cy - d - 34, sr, "JUTSU", ICON_JUTSU, 0xe0602c);
  g_vbuttons.back().kind = VBK_COMBO;
  // UP+SP above THROW, DN+SP above JUTSU: the game's "up + special" / "down + special" moves
  act(VB_UP, cx + d + 6, cy - d - 34 - 2 * sr - 10, sr, "UP+SP", ICON_UPSP, 0x2c8ce0);
  g_vbuttons.back().kind = VBK_COMBO;
  act(VB_DOWN, cx - d - 6, cy - d - 34 - 2 * sr - 10, sr, "DN+SP", ICON_DNSP, 0x2cb0a8);
  g_vbuttons.back().kind = VBK_COMBO;

  // pills at the top: ESC (left), OK (right), UNLOCK ALL (left, main menu only)
  auto pill = [&](int kind, int sc, float x, float y, float w, float h, const char *lab,
                  const char *const *ic, uint32_t col) {
    VButton b{};
    b.kind = kind; b.scancode = sc; b.x = x; b.y = y; b.w = w; b.h = h;
    b.label = lab; b.icon = ic; b.color = col; b.visible = kind != VBK_UNLOCK && kind != VBK_CHEAT;
    g_vbuttons.push_back(b);
  };
  float pw = std::min(std::max(side - 24, 76.0f), 110.0f);
  pill(VBK_KEY, 1, 12, 12, pw, 36, "ESC", nullptr, 0x505a78);
  pill(VBK_KEY, 28, W - 12 - pw, 12, pw, 36, "OK", nullptr, 0x2f8a5a);
  g_unlock_btn = (int)g_vbuttons.size();
  pill(VBK_UNLOCK, 0, 12, 58, std::max(pw, 150.0f), 36, "UNLOCK ALL", ICON_LOCK, 0xc08a20);
  // adventure cheats (main menu only): scancode field = cheat id
  g_cheat_btn[0] = (int)g_vbuttons.size();
  pill(VBK_CHEAT, 1, 12, 104, std::max(pw, 150.0f), 36, "INF HEALTH", ICON_HEART, 0x5a5f78);
  g_cheat_btn[1] = (int)g_vbuttons.size();
  pill(VBK_CHEAT, 2, 12, 150, std::max(pw, 150.0f), 36, "INF JUTSU", ICON_JUTSU, 0x5a5f78);
}

static void key_for_vb(int vb, bool down);

// ---- per-player actions read directly by the game (PortActDown / PortActHit in Getinput and the
// menus). Touch controls and game pads no longer depend on keys.cfg: remapping the keys, a key
// mapped twice or a player set to "joystick" cannot break them any more.
static int g_pact_count[5][VB_COUNT];
static int g_pact_hits[5][VB_COUNT];
static void pact_press(int p, int vb, bool down) {
  if (p < 1 || p > 4 || vb < 0 || vb >= VB_COUNT) return;
  std::lock_guard<std::mutex> lk(g_in_mx);
  int &c = g_pact_count[p][vb];
  if (down) { if (c++ == 0) g_pact_hits[p][vb]++; }
  else if (c > 0) c--;
}
int bb_portactdown(int p, int vb) {
  input_poll();
  return (p >= 1 && p <= 4 && vb >= 0 && vb < VB_COUNT && g_pact_count[p][vb] > 0) ? 1 : 0;
}
int bb_portacthit(int p, int vb) {
  input_poll();
  if (p < 1 || p > 4 || vb < 0 || vb >= VB_COUNT) return 0;
  std::lock_guard<std::mutex> lk(g_in_mx);
  int n = g_pact_hits[p][vb];
  g_pact_hits[p][vb] = 0;
  return n;
}
static void pact_flush() {
  std::lock_guard<std::mutex> lk(g_in_mx);
  memset(g_pact_hits, 0, sizeof g_pact_hits);
}
// the game calls PortScreen(3) every frame of a match: then the touch controls only send actions,
// in the menus they also press player 1's keys (the menus read the keyboard)
static bool in_match() { return g_screen_id == 3 && g_frame_no - g_screen_frame <= 3; }

// several sources may hold the same action (touch BLOCK + JUTSU, gamepad, ...): count presses and
// only report the press on the first and the release on the last
static int g_vb_count[VB_COUNT];
static bool g_vb_keydown[VB_COUNT];
static void vb_press(int vb, bool down) {
  if (vb < 0 || vb >= VB_COUNT) return;
  int &c = g_vb_count[vb];
  bool first = false, last = false;
  if (down) first = c++ == 0;
  else if (c > 0) last = --c == 0;
  if (!first && !last) return;
  pact_press(1, vb, down);
  if (down && !in_match()) { key_for_vb(vb, true); g_vb_keydown[vb] = true; }
  else if (!down && g_vb_keydown[vb]) { key_for_vb(vb, false); g_vb_keydown[vb] = false; }
}

// combo buttons: frame 0 hold the first action (BLOCK for JUTSU, UP, DOWN), frame 1 press SPECIAL
// (the game sees "BLOCK/UP/DOWN held + SPECIAL hit"), frame 3 release SPECIAL, release the first
// action when the finger is lifted
struct ComboState { bool active = false, held = false; int stage = 0, frame = 0; };
static ComboState g_combo[VB_COUNT];
static int g_frame_no_fwd() { return g_frame_no; }
static void combo_press(int vb, bool down) {
  ComboState &c = g_combo[vb];
  if (down) {
    if (c.active) return;
    c.active = c.held = true;
    c.stage = 1;
    c.frame = g_frame_no_fwd();
    vb_press(vb, true);
  } else c.held = false;
}
static void combo_tick() {
  for (int vb = 0; vb < VB_COUNT; vb++) {
    ComboState &c = g_combo[vb];
    if (!c.active) continue;
    int f = g_frame_no_fwd();
    if (c.stage == 1 && f >= c.frame + 1) { vb_press(VB_SPECIAL, true); c.stage = 2; c.frame = f; }
    else if (c.stage == 2 && f >= c.frame + 2) { vb_press(VB_SPECIAL, false); c.stage = 3; }
    else if (c.stage == 3 && !c.held) { vb_press(vb, false); c = ComboState(); }
  }
}

// ---- adventure cheats (main menu pills), saved in the data folder
static int g_cheat[3];
static bool g_cheat_loaded = false;
static void cheats_load() {
  if (g_cheat_loaded) return;
  g_cheat_loaded = true;
  FILE *f = fopen((plat_data_dir() + "port_cheats.cfg").c_str(), "r");
  if (f) { if (fscanf(f, "%d %d", &g_cheat[1], &g_cheat[2]) != 2) g_cheat[1] = g_cheat[2] = 0; fclose(f); }
}
static void cheats_save() {
  FILE *f = fopen((plat_data_dir() + "port_cheats.cfg").c_str(), "w");
  if (f) { fprintf(f, "%d %d\n", g_cheat[1], g_cheat[2]); fclose(f); }
}
int bb_portcheat(int id) { cheats_load(); return (id == 1 || id == 2) ? g_cheat[id] : 0; }
static void toggle_cheat(int id) {
  cheats_load();
  g_cheat[id] = !g_cheat[id];
  cheats_save();
  const char *what = id == 1 ? "INFINITE HEALTH" : "INFINITE JUTSU";
  toast(std::string(what) + (g_cheat[id] ? " ON (ADVENTURE)" : " OFF"));
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
    float dead = g_dpad_r * 0.22f;
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
      vb_press(vbmap[i], nd[i] != 0);
      // menus are driven by the arrow keys too (not during a match: they may be player 2's keys)
      static const int arrows[4] = {200, 208, 203, 205};
      static bool arrow_down[4];
      bool want = nd[i] != 0 && !in_match();
      if (glue_player_code(vbmap[i]) != arrows[i] && want != arrow_down[i]) { rt_key(arrows[i], want); arrow_down[i] = want; }
    }
}

static void set_button(int i, bool down) {
  VButton &b = g_vbuttons[i];
  if (b.down == down) return;
  b.down = down;
  if (b.kind == VBK_KEY) rt_key(b.scancode, down);
  else if (b.kind == VBK_ACTION) vb_press(b.vb, down);
  else if (b.kind == VBK_COMBO) combo_press(b.vb, down);
  else if (b.kind == VBK_UNLOCK && down) start_unlock_macro();
  else if (b.kind == VBK_CHEAT && down) toggle_cheat(b.scancode);
}

// distance from a touch to a button (<= 0 inside); pills are rounded rectangles
static float button_distance(const VButton &b, float x, float y) {
  if (b.w > 0) {
    float dx = std::max(std::max(b.x - x, x - (b.x + b.w)), 0.0f);
    float dy = std::max(std::max(b.y - y, y - (b.y + b.h)), 0.0f);
    return sqrtf(dx * dx + dy * dy);
  }
  return sqrtf((x - b.x) * (x - b.x) + (y - b.y) * (y - b.y)) - b.r;
}

void rt_touch(int id, float nx, float ny, int action) {
  float x = nx * g_scr_w, y = ny * g_scr_h;
  if (action == 0) {
    if (g_overlay) {
      // the closest button wins; a little slack around each button makes them easy to hit
      int best = -1;
      float bestd = 1e9f;
      for (size_t i = 0; i < g_vbuttons.size(); i++) {
        const VButton &b = g_vbuttons[i];
        if (!b.visible) continue;
        if ((b.kind == VBK_ACTION || b.kind == VBK_COMBO) && hide_fight_controls()) continue;
        float d = button_distance(b, x, y);
        float slack = b.w > 0 ? 8.0f : b.r * 0.3f;
        if (d <= slack && d < bestd) { bestd = d; best = (int)i; }
      }
      if (best >= 0) {
        g_touch_owner[id] = best;
        set_button(best, true);
        return;
      }
      float dx = x - g_dpad_cx, dy = y - g_dpad_cy;
      if (!hide_fight_controls() && dx * dx + dy * dy <= g_dpad_r * g_dpad_r * 1.6f && g_dpad_touch == -999) {
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
// desktop mouse (web backend): x,y in 0..1 of the screen; button 0 = move only
void rt_mouse(float nx, float ny, int button, bool down) {
  std::lock_guard<std::mutex> lk(g_in_mx);
  g_mouse_x = std::max(0, std::min(g_gfx_w - 1, (int)(nx * g_scr_w - g_game_x)));
  g_mouse_y = std::max(0, std::min(g_gfx_h - 1, (int)(ny * g_scr_h)));
  if (button >= 1 && button <= 3) {
    if (down && !g_mouse_down[button]) g_mouse_hits[button]++;
    g_mouse_down[button] = down;
  }
}
void rt_vbutton(int vb, bool down) {
  // first game pad = player 1 (also drives the menus with the arrow keys)
  static bool state[VB_COUNT];
  static bool arrow_down[4];
  if (vb < 0 || vb >= VB_COUNT || state[vb] == down) return;
  state[vb] = down;
  vb_press(vb, down);
  if (vb <= VB_RIGHT) {
    static const int arrows[4] = {200, 208, 203, 205};
    bool want = down && !in_match();
    if (glue_player_code(vb) != arrows[vb] && want != arrow_down[vb]) { rt_key(arrows[vb], want); arrow_down[vb] = want; }
  }
}
// game pads by device: the first pad that is used plays player 1, the second player 2, ...
// Returns the player number of that pad.
int rt_pad(int device, int vb, bool down) {
  static std::vector<int> ids;
  static bool state[8][VB_COUNT];
  size_t slot = 0;
  while (slot < ids.size() && ids[slot] != device) slot++;
  if (slot == ids.size()) {
    if (!down || ids.size() >= 4) return 0;     // a fifth pad is ignored
    ids.push_back(device);
    logf_("game pad %d -> player %d", device, (int)slot + 1);
  }
  int player = (int)slot + 1;
  if (vb < 0 || vb >= VB_COUNT || state[slot][vb] == down) return player;
  state[slot][vb] = down;
  if (player == 1) rt_vbutton(vb, down);
  else pact_press(player, vb, down);
  return player;
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

// ---------------------------------------------------------------- overlay drawing
static inline uint32_t blend(uint32_t d, uint32_t s, int a) {
  if (a <= 0) return d;
  if (a >= 256) return s;
  uint32_t rb = ((d & 0xff00ff) * (256 - a) + (s & 0xff00ff) * a) >> 8;
  uint32_t g = ((d & 0x00ff00) * (256 - a) + (s & 0x00ff00) * a) >> 8;
  return (rb & 0xff00ff) | (g & 0x00ff00);
}
static inline uint32_t shade(uint32_t c, float f) {   // f < 1 darker, > 1 lighter
  int r = (c >> 16) & 255, g = (c >> 8) & 255, b = c & 255;
  auto ch = [f](int v) {
    float o = f <= 1 ? v * f : v + (255 - v) * (f - 1);
    return std::max(0, std::min(255, (int)o));
  };
  return (uint32_t)(ch(r) << 16 | ch(g) << 8 | ch(b));
}
static inline void spix(int x, int y, uint32_t c, int a) {
  if ((unsigned)x < (unsigned)g_scr_w && (unsigned)y < (unsigned)g_scr_h) {
    uint32_t &p = g_screen[(size_t)y * g_scr_w + x];
    p = blend(p, c, a);
  }
}
// anti-aliased disc with a vertical light-to-dark gradient
static void disc(float cx, float cy, float r, uint32_t top, uint32_t bottom, int alpha) {
  for (int y = (int)(cy - r - 1); y <= (int)(cy + r + 1); y++)
    for (int x = (int)(cx - r - 1); x <= (int)(cx + r + 1); x++) {
      float dx = x + 0.5f - cx, dy = y + 0.5f - cy, d = sqrtf(dx * dx + dy * dy);
      float cov = std::min(1.0f, std::max(0.0f, r - d + 0.5f));
      if (cov <= 0) continue;
      float t = std::min(1.0f, std::max(0.0f, (dy + r) / (2 * r)));
      uint32_t c = blend(top, bottom, (int)(t * 256));
      spix(x, y, c, (int)(alpha * cov));
    }
}
static void ring(float cx, float cy, float r, float width, uint32_t c, int alpha) {
  for (int y = (int)(cy - r - 1); y <= (int)(cy + r + 1); y++)
    for (int x = (int)(cx - r - 1); x <= (int)(cx + r + 1); x++) {
      float dx = x + 0.5f - cx, dy = y + 0.5f - cy, d = sqrtf(dx * dx + dy * dy);
      float cov = std::min(std::min(1.0f, std::max(0.0f, r - d + 0.5f)), std::min(1.0f, std::max(0.0f, d - (r - width) + 0.5f)));
      if (cov > 0) spix(x, y, c, (int)(alpha * cov));
    }
}
// anti-aliased rounded rectangle with a vertical gradient
static void rrect(float x0, float y0, float w, float h, float rad, uint32_t top, uint32_t bottom, int alpha) {
  for (int y = (int)y0; y <= (int)(y0 + h); y++)
    for (int x = (int)x0; x <= (int)(x0 + w); x++) {
      float px = x + 0.5f, py = y + 0.5f;
      float qx = std::max(std::max(x0 + rad - px, px - (x0 + w - rad)), 0.0f);
      float qy = std::max(std::max(y0 + rad - py, py - (y0 + h - rad)), 0.0f);
      float d = sqrtf(qx * qx + qy * qy) - rad;
      float edge = std::min(std::min(px - x0, x0 + w - px), std::min(py - y0, y0 + h - py));
      float cov = std::min(1.0f, std::max(0.0f, std::min(-d + 0.5f, edge + 0.5f)));
      if (cov <= 0) continue;
      float t = std::min(1.0f, std::max(0.0f, (py - y0) / h));
      spix(x, y, blend(top, bottom, (int)(t * 256)), (int)(alpha * cov));
    }
}
static void icon(const char *const *ic, float cx, float cy, int scale, uint32_t c, int alpha) {
  int x0 = (int)(cx - 8 * scale), y0 = (int)(cy - 8 * scale);
  for (int pass = 0; pass < 2; pass++)     // drop shadow, then the icon
    for (int r = 0; r < 16; r++)
      for (int q = 0; q < 16; q++)
        if (ic[r][q] == '#')
          for (int sy = 0; sy < scale; sy++)
            for (int sx = 0; sx < scale; sx++) {
              if (pass == 0) spix(x0 + q * scale + sx + 2, y0 + r * scale + sy + 2, 0x000000, alpha / 3);
              else spix(x0 + q * scale + sx, y0 + r * scale + sy, c, alpha);
            }
}
static void text(int x, int y, const char *s, uint32_t c, int alpha = 256) {
  for (size_t i = 0; s[i]; i++) {
    unsigned ch = (unsigned char)s[i];
    if (ch < 32 || ch > 126) ch = '?';
    const uint8_t *g = FONT8X16[ch - 32];
    for (int r = 0; r < FONT_H; r++)
      for (int b = 0; b < 8; b++)
        if (g[r] & (0x80 >> b)) {
          spix(x + (int)i * 8 + b + 1, y + r + 1, 0x000000, alpha / 2);
          spix(x + (int)i * 8 + b, y + r, c, alpha);
        }
  }
}
static void text_center(float cx, int y, const char *s, uint32_t c, int alpha = 256) {
  text((int)(cx - strlen(s) * 4), y, s, c, alpha);
}
static bool over_game(float x0, float x1) { return x1 > g_game_x && x0 < g_game_x + g_gfx_w; }
static void triangle(float cx, float cy, float size, int dir, uint32_t c, int alpha) {
  // dir: 0 up, 1 down, 2 left, 3 right
  for (int y = (int)(cy - size); y <= (int)(cy + size); y++)
    for (int x = (int)(cx - size); x <= (int)(cx + size); x++) {
      float u = x + 0.5f - cx, v = y + 0.5f - cy;
      float along = dir == 0 ? -v : dir == 1 ? v : dir == 2 ? -u : u;      // towards the tip
      float across = (dir < 2) ? u : v;
      if (along < -size * 0.5f || along > size * 0.6f) continue;
      float half = (size * 0.6f - along) * 0.75f;
      float cov = std::min(1.0f, std::max(0.0f, half - fabsf(across) + 0.5f));
      if (cov > 0) spix(x, y, c, (int)(alpha * cov));
    }
}

static void draw_dpad() {
  float cx = g_dpad_cx, cy = g_dpad_cy, R = g_dpad_r;
  bool faint = over_game(cx - R, cx + R);
  int a = faint ? 150 : 225;
  disc(cx, cy, R, 0x2a2e40, 0x14161f, a * 3 / 4);
  ring(cx, cy, R, 3, 0x6a7090, a);
  float arm = R * 0.36f, len = R * 0.80f;
  // cross
  rrect(cx - arm, cy - len, arm * 2, len * 2, 10, 0x4a4f66, 0x2c3042, a);
  rrect(cx - len, cy - arm, len * 2, arm * 2, 10, 0x4a4f66, 0x2c3042, a);
  disc(cx, cy, arm * 0.7f, 0x23263a, 0x34384e, a);
  const float ax[4] = {0, 0, -1, 1}, ay[4] = {-1, 1, 0, 0};
  for (int i = 0; i < 4; i++) {
    float px = cx + ax[i] * len * 0.62f, py = cy + ay[i] * len * 0.62f;
    if (g_dpad_dir[i]) disc(px, py, arm * 0.95f, 0xffe27a, 0xd8a020, 230);
    triangle(px, py, arm * 0.75f, i, g_dpad_dir[i] ? 0x3a2a00 : 0xe8ecff, a);
  }
}

static void draw_action(const VButton &b) {
  bool faint = over_game(b.x - b.r, b.x + b.r);
  int a = faint ? 170 : 240;
  float r = b.down ? b.r * 0.93f : b.r;
  uint32_t base = b.down ? shade(b.color, 1.35f) : b.color;
  disc(b.x + 2, b.y + 4, r, 0x000000, 0x000000, a / 3);                 // shadow
  disc(b.x, b.y, r, shade(base, 1.25f), shade(base, 0.55f), a);           // body
  disc(b.x, b.y - r * 0.35f, r * 0.62f, shade(base, 1.6f), shade(base, 1.05f), a / 3);  // gloss
  ring(b.x, b.y, r, 2.5f, shade(base, 0.35f), a);
  ring(b.x, b.y, r - 2.5f, 1.5f, shade(base, 1.7f), a / 2);
  int scale = r >= 44 ? 3 : 2;
  bool label = b.r >= 34;
  icon(b.icon, b.x, b.y - (label ? (scale == 3 ? 9 : 7) : 0), scale, 0xffffff, a);
  if (label) text_center(b.x, (int)(b.y + r * 0.38f), b.label, 0xffffff, a);
  else if (!faint) text_center(b.x, (int)(b.y + r + 2), b.label, 0xd8d8e8, a);
}

static void draw_pill(const VButton &b, bool done) {
  int a = 235;
  uint32_t base = done ? 0x2f8a4a : (b.down ? shade(b.color, 1.35f) : b.color);
  rrect(b.x + 2, b.y + 3, b.w, b.h, b.h / 2, 0x000000, 0x000000, a / 3);
  rrect(b.x, b.y, b.w, b.h, b.h / 2, shade(base, 1.3f), shade(base, 0.6f), a);
  const char *lab = done ? "UNLOCKED" : b.label;
  float tx = b.x + b.w / 2;
  if (b.icon) {
    const char *const *ic = done ? ICON_CHECK : b.icon;
    float total = 16 + 6 + strlen(lab) * 8;
    float ix = b.x + (b.w - total) / 2 + 8;
    icon(ic, ix, b.y + b.h / 2, 1, 0xffffff, a);
    text((int)(ix + 14), (int)(b.y + b.h / 2 - 8), lab, 0xffffff, a);
  } else {
    if (b.scancode == 1) {   // ESC: back arrow
      triangle(b.x + 18, b.y + b.h / 2, 8, 2, 0xffffff, a);
      text((int)(b.x + 28 + (b.w - 28 - strlen(lab) * 8) / 2), (int)(b.y + b.h / 2 - 8), lab, 0xffffff, a);
    } else {
      text_center(tx, (int)(b.y + b.h / 2 - 8), lab, 0xffffff, a);
    }
  }
}

static void draw_toast() {
  if (g_toast.empty() || plat_ms() > g_toast_until) return;
  float w = g_toast.size() * 8 + 40, h = 40;
  float x = (g_scr_w - w) / 2, y = 96;
  rrect(x + 2, y + 3, w, h, 12, 0x000000, 0x000000, 90);
  rrect(x, y, w, h, 12, 0x3a9a5a, 0x1d5c34, 240);
  text_center(g_scr_w / 2.0f, (int)(y + 12), g_toast.c_str(), 0xffffff);
}

static void present_frame() {
  Image *f = img(g_front);
  if (!f) return;
  g_frame_no++;
  run_macro();
  combo_tick();
  int aspect_now = (int)(plat_screen_aspect() * 1000);
  static int aspect_prev = -1;
  static int overlay_prev = -1;
  if (aspect_now != aspect_prev || (int)plat_touch_overlay() != overlay_prev || g_screen.empty()) {
    aspect_prev = aspect_now;
    overlay_prev = plat_touch_overlay();
    layout_screen();
  }
  // side panels: dark gradient with a thin frame next to the picture
  for (int y = 0; y < g_scr_h; y++) {
    uint32_t c = blend(0x1a1d2a, 0x0c0d14, y * 256 / g_scr_h);
    std::fill(g_screen.begin() + (size_t)y * g_scr_w, g_screen.begin() + (size_t)(y + 1) * g_scr_w, c);
  }
  for (int y = 0; y < g_gfx_h && y < g_scr_h; y++)
    memcpy(&g_screen[(size_t)y * g_scr_w + g_game_x], &f->px[(size_t)y * f->w], g_gfx_w * 4);
  if (g_game_x > 2)
    for (int y = 0; y < g_scr_h; y++) {
      g_screen[(size_t)y * g_scr_w + g_game_x - 1] = 0x3a3f58;
      g_screen[(size_t)y * g_scr_w + g_game_x + g_gfx_w] = 0x3a3f58;
    }
  if (g_overlay) {
    if (g_unlock_btn >= 0) {
      VButton &u = g_vbuttons[g_unlock_btn];
      u.visible = on_main_menu();
      if (!u.visible && u.down) u.down = false;
    }
    for (int ci = 0; ci < 2; ci++)
      if (g_cheat_btn[ci] >= 0) {
        VButton &c = g_vbuttons[g_cheat_btn[ci]];
        c.visible = on_main_menu();
        if (!c.visible && c.down) c.down = false;
      }
    bool fight = !hide_fight_controls();
    if (fight) draw_dpad();
    for (auto &b : g_vbuttons) {
      if (!b.visible) continue;
      if ((b.kind == VBK_ACTION || b.kind == VBK_COMBO) && !fight) continue;
      if (b.kind == VBK_CHEAT) {
        cheats_load();
        VButton c = b;
        bool on = g_cheat[b.scancode] != 0;
        c.color = on ? 0x2f8a4a : 0x5a5f78;
        c.label = b.scancode == 1 ? (on ? "HEALTH: ON" : "HEALTH: OFF") : (on ? "JUTSU: ON" : "JUTSU: OFF");
        draw_pill(c, false);
        continue;
      }
      if (b.w > 0) draw_pill(b, b.kind == VBK_UNLOCK && glue_cheat_unlocked() && g_macro_state == 0);
      else draw_action(b);
    }
  }
  draw_toast();
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

void net_background_tick();   // runtime/net/net.cpp: keeps an online service ticking outside the lobby

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
  net_background_tick();
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
