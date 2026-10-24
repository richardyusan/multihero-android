// SPDX-License-Identifier: MIT
// bbrt.h - minimal Blitz2D-compatible runtime used by bb2cpp output.
#pragma once
#include <string>
#include <vector>
#include <cmath>
#include <cstdint>
#include <climits>
#include <cstdio>

typedef std::string BStr;

#define BB_FUNC_ENTER(name) ((void)0)

// ---------------------------------------------------------------- conversions
static inline int bb_f2i(float f) {
  if (!(f == f)) return INT_MIN;                 // NaN
  if (f >= 2147483647.0f) return INT_MAX;
  if (f <= -2147483648.0f) return INT_MIN;
  return (int)lrintf(f);                           // round to nearest (even), as the x87 FPU did
}
int bb_s2i(const BStr &s);
float bb_s2f(const BStr &s);
BStr bb_istr(int v);
BStr bb_fstr(float v);
static inline int bb_idiv(int a, int b) {
  if (b == 0) return 0;
  if (b == -1) return (int)(0u - (unsigned)a);
  return a / b;
}
static inline int bb_imod(int a, int b) {
  if (b == 0 || b == -1) return 0;
  return a % b;
}
static inline float bb_pow(float a, float b) { return powf(a, b); }

// ---------------------------------------------------------------- arrays
void bb_array_oob(const char *name, int idx);

template <class T>
struct BArray {
  const char *name;
  std::vector<T> d;
  int n[4] = {0, 0, 0, 0};   // extents (Dim value + 1)
  int nd = 0;
  T dummy{};
  explicit BArray(const char *nm) : name(nm) {}
  void dim(int a) { nd = 1; n[0] = a + 1 > 0 ? a + 1 : 0; d.assign((size_t)n[0], T()); }
  void dim(int a, int b) {
    nd = 2; n[0] = a + 1 > 0 ? a + 1 : 0; n[1] = b + 1 > 0 ? b + 1 : 0;
    d.assign((size_t)n[0] * n[1], T());
  }
  void dim(int a, int b, int c) {
    nd = 3; n[0] = a + 1 > 0 ? a + 1 : 0; n[1] = b + 1 > 0 ? b + 1 : 0; n[2] = c + 1 > 0 ? c + 1 : 0;
    d.assign((size_t)n[0] * n[1] * n[2], T());
  }
  T &bad(int i) { bb_array_oob(name, i); dummy = T(); return dummy; }
  T &operator()(int i) {
    if ((unsigned)i >= (unsigned)n[0] || nd != 1) return bad(i);
    return d[i];
  }
  T &operator()(int i, int j) {
    if ((unsigned)i >= (unsigned)n[0] || (unsigned)j >= (unsigned)n[1] || nd != 2) return bad(i);
    return d[(size_t)i * n[1] + j];
  }
  T &operator()(int i, int j, int k) {
    if ((unsigned)i >= (unsigned)n[0] || (unsigned)j >= (unsigned)n[1] || (unsigned)k >= (unsigned)n[2] || nd != 3)
      return bad(i);
    return d[((size_t)i * n[1] + j) * n[2] + k];
  }
};

// ---------------------------------------------------------------- program entry
void bb_main();
[[noreturn]] void bb_end();

// ---------------------------------------------------------------- graphics
void bb_graphics(int w, int h, int depth, int mode);
void bb_setbuffer(int buf);
int bb_backbuffer();
int bb_frontbuffer();
int bb_imagebuffer(int img, int frame);
void bb_flip(int vwait);
void bb_cls();
void bb_clscolor(int r, int g, int b);
void bb_color(int r, int g, int b);
void bb_rect(int x, int y, int w, int h, int solid);
void bb_oval(int x, int y, int w, int h, int solid);
void bb_line(int x1, int y1, int x2, int y2);
void bb_plot(int x, int y);
void bb_text(int x, int y, const BStr &s, int cx, int cy);
int bb_loadimage(const BStr &path);
int bb_createimage(int w, int h, int frames);
void bb_freeimage(int img);
void bb_drawimage(int img, int x, int y, int frame);
void bb_drawblock(int img, int x, int y, int frame);
void bb_tileimage(int img, int x, int y, int frame);
void bb_maskimage(int img, int r, int g, int b);
void bb_midhandle(int img);
int bb_imagewidth(int img);
int bb_imageheight(int img);
int bb_imagerectcollide(int img, int x, int y, int frame, int rx, int ry, int rw, int rh);
int bb_imagesoverlap(int i1, int x1, int y1, int i2, int x2, int y2);
int bb_imagescollide(int i1, int x1, int y1, int f1, int i2, int x2, int y2, int f2);
int bb_rectsoverlap(int x1, int y1, int w1, int h1, int x2, int y2, int w2, int h2);
void bb_copyrect(int sx, int sy, int w, int h, int dx, int dy, int src, int dst);
int bb_savebuffer(int buf, const BStr &path);
int bb_graphicswidth();
int bb_graphicsheight();
int bb_availvidmem();
int bb_totalvidmem();
void bb_apptitle(const BStr &t, const BStr &close);
void bb_hidepointer();
void bb_showpointer();
void bb_setgfxdriver(int d);
int bb_gfxmodeexists(int w, int h, int d);
void bb_vwait(int n);

// ---------------------------------------------------------------- sound
int bb_loadsound(const BStr &path);
void bb_freesound(int snd);
int bb_playsound(int snd);
void bb_loopsound(int snd);
void bb_stopchannel(int ch);
int bb_channelplaying(int ch);
void bb_soundvolume(int snd, float v);
void bb_channelvolume(int ch, float v);
void bb_pausechannel(int ch);
void bb_resumechannel(int ch);

// ---------------------------------------------------------------- input
int bb_keydown(int k);
int bb_keyhit(int k);
int bb_getkey();
int bb_waitkey();
void bb_flushkeys();
void bb_flushjoy();
void bb_flushmouse();
int bb_mousex();
int bb_mousey();
int bb_mousedown(int b);
int bb_mousehit(int b);
float bb_joyx(int port);
float bb_joyy(int port);
int bb_joyxdir(int port);
int bb_joyydir(int port);
int bb_joyhit(int b, int port);
int bb_joydown(int b, int port);
int bb_joytype(int port);
int bb_joygetkey(int port);

// ---------------------------------------------------------------- files
int bb_readfile(const BStr &p);
int bb_writefile(const BStr &p);
int bb_openfile(const BStr &p);
void bb_closefile(int f);
int bb_readint(int f);
void bb_writeint(int f, int v);
float bb_readfloat(int f);
void bb_writefloat(int f, float v);
BStr bb_readstring(int f);
void bb_writestring(int f, const BStr &s);
BStr bb_readline(int f);
void bb_writeline(int f, const BStr &s);
int bb_readbyte(int f);
void bb_writebyte(int f, int v);
int bb_readshort(int f);
void bb_writeshort(int f, int v);
int bb_eof(int f);
int bb_filetype(const BStr &p);
int bb_filesize(const BStr &p);
int bb_readdir(const BStr &p);
BStr bb_nextfile(int d);
void bb_closedir(int d);
void bb_deletefile(const BStr &p);
void bb_copyfile(const BStr &a, const BStr &b);
void bb_createdir(const BStr &p);
BStr bb_currentdir();

// ---------------------------------------------------------------- time
int bb_millisecs();
void bb_delay(int ms);
int bb_createtimer(int hz);
int bb_waittimer(int t);
void bb_freetimer(int t);
BStr bb_currentdate();
BStr bb_currenttime();

// ---------------------------------------------------------------- math
int bb_rand(int a);
int bb_rand(int a, int b);
float bb_rnd(float a);
float bb_rnd(float a, float b);
void bb_seedrnd(int s);
static inline int bb_abs(int v) { return v < 0 ? -v : v; }
static inline float bb_abs(float v) { return fabsf(v); }
static inline int bb_sgn(int v) { return v < 0 ? -1 : (v > 0 ? 1 : 0); }
static inline float bb_sgn(float v) { return v < 0 ? -1.0f : (v > 0 ? 1.0f : 0.0f); }
static inline float bb_sqr(float v) { return sqrtf(v); }
static inline float bb_sin(float d) { return sinf(d * 0.0174532925199f); }
static inline float bb_cos(float d) { return cosf(d * 0.0174532925199f); }
static inline float bb_tan(float d) { return tanf(d * 0.0174532925199f); }
static inline float bb_asin(float v) { return asinf(v) * 57.295779513f; }
static inline float bb_acos(float v) { return acosf(v) * 57.295779513f; }
static inline float bb_atan(float v) { return atanf(v) * 57.295779513f; }
static inline float bb_atan2(float y, float x) { return atan2f(y, x) * 57.295779513f; }
static inline float bb_floor(float v) { return floorf(v); }
static inline float bb_ceil(float v) { return ceilf(v); }
static inline float bb_exp(float v) { return expf(v); }
static inline float bb_log(float v) { return logf(v); }
static inline float bb_log10(float v) { return log10f(v); }

// ---------------------------------------------------------------- strings
int bb_len(const BStr &s);
BStr bb_mid(const BStr &s, int start, int count);
BStr bb_left(const BStr &s, int n);
BStr bb_right(const BStr &s, int n);
BStr bb_lower(const BStr &s);
BStr bb_upper(const BStr &s);
BStr bb_trim(const BStr &s);
BStr bb_chr(int c);
int bb_asc(const BStr &s);
int bb_instr(const BStr &s, const BStr &f, int start);
BStr bb_replace(const BStr &s, const BStr &f, const BStr &r);
BStr bb_lset(const BStr &s, int n);
BStr bb_rset(const BStr &s, int n);
BStr bb_hex(int v);
BStr bb_bin(int v);
BStr bb_string(const BStr &s, int n);

// ---------------------------------------------------------------- misc
[[noreturn]] void bb_runtimeerror(const BStr &s);
void bb_debuglog(const BStr &s);
BStr bb_commandline();
void bb_print(const BStr &s);
void bb_write(const BStr &s);
void bb_stop();
