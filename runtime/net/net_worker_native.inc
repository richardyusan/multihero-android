// net_worker_native.inc - included by net_worker.cpp in the native builds (Android, Windows,
// Linux). Implements the helpers that the web build writes in JavaScript (js_ws_open, js_probe,
// ...) with the WebSocket client of ws_client.cpp and a small JSON reader, and produces the
// same TAB separated event lines, so the NetWorker class is the same on every platform.
// The page bar of the web version (server picker, room code, name) is drawn by the game itself
// (src/online.bb) and reaches the backend through the Net* functions instead.
#include "ws_client.h"
#include <deque>
#include <map>
#include <vector>

namespace wkn {

// ------------------------------------------------------------------ JSON (just enough)
struct J {
  enum T { NUL, BOOL, NUM, STR, ARR, OBJ } t = NUL;
  bool b = false;
  double n = 0;
  std::string s;
  std::vector<J> a;
  std::vector<std::pair<std::string, J>> o;
  const J &operator[](const char *k) const {
    static const J none;
    for (auto &kv : o) if (kv.first == k) return kv.second;
    return none;
  }
  bool truthy() const { return t == BOOL ? b : t == NUM ? n != 0 : t == STR ? !s.empty() : t == ARR || t == OBJ; }
  int num() const { return t == NUM ? (int)(long long)n : t == BOOL ? (b ? 1 : 0) : t == STR ? atoi(s.c_str()) : 0; }
  std::string str() const {
    if (t == STR) return s;
    if (t == NUM) { char b2[32]; snprintf(b2, sizeof b2, "%.0f", n); return b2; }
    return std::string();
  }
};

struct JParser {
  const char *p, *e;
  bool ok = true;
  void ws() { while (p < e && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++; }
  static void utf8(std::string &o, unsigned cp) {
    if (cp < 0x80) o += (char)cp;
    else if (cp < 0x800) { o += (char)(0xC0 | cp >> 6); o += (char)(0x80 | (cp & 63)); }
    else if (cp < 0x10000) { o += (char)(0xE0 | cp >> 12); o += (char)(0x80 | (cp >> 6 & 63)); o += (char)(0x80 | (cp & 63)); }
    else { o += (char)(0xF0 | cp >> 18); o += (char)(0x80 | (cp >> 12 & 63)); o += (char)(0x80 | (cp >> 6 & 63)); o += (char)(0x80 | (cp & 63)); }
  }
  unsigned hex4() {
    if (e - p < 4) { ok = false; return 0; }
    unsigned v = 0;
    for (int i = 0; i < 4; i++) {
      char c = *p++;
      v <<= 4;
      if (c >= '0' && c <= '9') v |= (unsigned)(c - '0');
      else if (c >= 'a' && c <= 'f') v |= (unsigned)(c - 'a' + 10);
      else if (c >= 'A' && c <= 'F') v |= (unsigned)(c - 'A' + 10);
      else ok = false;
    }
    return v;
  }
  std::string str() {
    std::string o;
    p++;                                    // opening quote
    while (p < e && *p != '"') {
      char c = *p++;
      if (c != '\\') { o += c; continue; }
      if (p >= e) { ok = false; break; }
      char x = *p++;
      switch (x) {
        case 'n': o += '\n'; break;
        case 't': o += '\t'; break;
        case 'r': o += '\r'; break;
        case 'b': o += '\b'; break;
        case 'f': o += '\f'; break;
        case 'u': {
          unsigned cp = hex4();
          if (cp >= 0xD800 && cp < 0xDC00 && e - p >= 6 && p[0] == '\\' && p[1] == 'u') {
            p += 2;
            unsigned lo = hex4();
            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
          }
          utf8(o, cp);
          break;
        }
        default: o += x;
      }
    }
    if (p < e) p++; else ok = false;
    return o;
  }
  J value(int depth = 0) {
    J v;
    ws();
    if (p >= e || depth > 16) { ok = false; return v; }
    char c = *p;
    if (c == '{') {
      v.t = J::OBJ;
      p++;
      ws();
      if (p < e && *p == '}') { p++; return v; }
      while (ok) {
        ws();
        if (p >= e || *p != '"') { ok = false; break; }
        std::string k = str();
        ws();
        if (p >= e || *p != ':') { ok = false; break; }
        p++;
        v.o.push_back({k, value(depth + 1)});
        ws();
        if (p < e && *p == ',') { p++; continue; }
        if (p < e && *p == '}') { p++; break; }
        ok = false;
      }
    } else if (c == '[') {
      v.t = J::ARR;
      p++;
      ws();
      if (p < e && *p == ']') { p++; return v; }
      while (ok) {
        v.a.push_back(value(depth + 1));
        ws();
        if (p < e && *p == ',') { p++; continue; }
        if (p < e && *p == ']') { p++; break; }
        ok = false;
      }
    } else if (c == '"') {
      v.t = J::STR;
      v.s = str();
    } else if (c == 't' && e - p >= 4 && !strncmp(p, "true", 4)) { v.t = J::BOOL; v.b = true; p += 4; }
    else if (c == 'f' && e - p >= 5 && !strncmp(p, "false", 5)) { v.t = J::BOOL; p += 5; }
    else if (c == 'n' && e - p >= 4 && !strncmp(p, "null", 4)) { p += 4; }
    else {
      char *end = nullptr;
      std::string num(p, (size_t)std::min<ptrdiff_t>(e - p, 40));
      v.n = strtod(num.c_str(), &end);
      if (end == num.c_str()) { ok = false; return v; }
      v.t = J::NUM;
      p += end - num.c_str();
    }
    return v;
  }
};

bool parse_json(const std::string &s, J &out) {
  JParser jp{s.data(), s.data() + s.size()};
  out = jp.value();
  return jp.ok;
}

// ------------------------------------------------------------------ helpers like the JS side
std::string clean(const std::string &s) {
  std::string o = s;
  for (auto &c : o) if (c == '\t' || c == '\r' || c == '\n') c = ' ';
  return o;
}
std::string cut16(const std::string &s) {        // first 16 characters (UTF-8 safe)
  size_t n = 0, i = 0;
  while (i < s.size() && n < 16) {
    unsigned char c = (unsigned char)s[i];
    i += c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
    n++;
  }
  return s.substr(0, std::min(i, s.size()));
}
std::string players_list(const J &arr) {        // "id:slot:ready,..."
  std::string o;
  for (auto &pl : arr.a) {
    if (!o.empty()) o += ',';
    o += clean(pl["id"].str()) + ":" + std::to_string(pl["slot"].num()) + ":" + (pl["ready"].truthy() ? "1" : "0");
  }
  return o;
}
std::string join(const std::vector<std::string> &v) {
  std::string o;
  for (size_t i = 0; i < v.size(); i++) { if (i) o += '\t'; o += v[i]; }
  return o;
}

std::deque<std::string> g_q, g_uq;    // socket events / "page" events (probes, http, ...)
int g_main = 0;                       // the room socket
bool g_ask_code = false;              // "enter room code" chosen: src/online.bb opens its keypad

struct Http { int h; int kind; };     // kind 0 = ping ("T"), 1 = regions check ("Z")
std::vector<Http> g_http;
bool g_ping_busy = false;

struct Probe {
  int h = 0;
  std::string code;
  int waitMs = 900;
  int64_t deadline = 0, t0 = 0;
  bool welcomed = false;
  int rtt = -1;
  std::string me;
  std::vector<std::pair<std::string, int>> players;   // id, slot (without me)
  std::map<std::string, std::string> names;
  std::map<std::string, bool> rdy;
};
std::vector<Probe> g_probes;

void main_event(const mhws::Event &e) {
  if (e.type == mhws::EV_OPEN) { g_q.push_back("O"); return; }
  if (e.type == mhws::EV_CLOSE) {
    g_q.push_back("C\t" + std::to_string(e.code) + "\t" + clean(e.text));
    g_main = 0;
    return;
  }
  if (e.text == "pong") { g_q.push_back("P"); return; }
  J o;
  if (!parse_json(e.text, o) || o.t != J::OBJ) return;
  std::string t = o["t"].str();
  if (t == "welcome")
    g_q.push_back(join({"W", clean(o["id"].str()), std::to_string(o["slot"].num()), clean(o["host"].str()),
                        players_list(o["players"])}));
  else if (t == "join") g_q.push_back(join({"J", clean(o["id"].str()), std::to_string(o["slot"].num())}));
  else if (t == "leave") g_q.push_back(join({"L", clean(o["id"].str()), std::to_string(o["slot"].num())}));
  else if (t == "host") g_q.push_back(join({"H", clean(o["id"].str())}));
  else if (t == "start") {
    char sd[24];
    snprintf(sd, sizeof sd, "%u", (uint32_t)(int64_t)o["seed"].n);
    g_q.push_back(join({"S", sd, players_list(o["players"])}));
  } else if (t == "msg") {
    const J &d = o["d"];
    std::string f = clean(o["from"].str()), k = d["k"].str();
    if (k == "g" && d["a"].t == J::ARR) {
      for (auto &it : d["a"].a) g_q.push_back("G\t" + f + "\t" + clean(it.str()));
    } else if (k == "hi") {
      g_q.push_back(join({"I", f, cut16(clean(d["n"].str())), std::to_string(d["h"].num()), d["r"].truthy() ? "1" : "0",
                          std::to_string(d["p"].num())}));
    } else if (k == "room") g_q.push_back(join({"R", f, std::to_string(d["m"].num()), std::to_string(d["l"].num())}));
    else if (k == "go") g_q.push_back(join({"X", f, std::to_string(d["m"].num()), std::to_string(d["l"].num())}));
    else if (k == "fin") g_q.push_back(join({"F", f, std::to_string(d["h"].num())}));
  }
}

void probe_finish(Probe &p, bool ok) {
  mhws::ws_close(p.h);
  p.h = 0;
  if (!ok) { g_uq.push_back("Q\t" + p.code + "\tfail"); return; }
  std::string host;
  int hs = 99, nr = 0, n = 0;
  for (auto &pl : p.players) {
    auto it = p.names.find(pl.first);
    if (it == p.names.end()) continue;
    n++;
    if (p.rdy[pl.first]) nr++;
    if (pl.second < hs) { hs = pl.second; host = it->second; }
  }
  g_uq.push_back(join({"Q", p.code, "ok", std::to_string(n), cut16(clean(host)), std::to_string(nr), std::to_string(p.rtt)}));
}

void probe_step(Probe &p, int64_t now) {
  auto allIn = [&]() {
    for (auto &pl : p.players) if (!p.names.count(pl.first)) return false;
    return true;
  };
  mhws::Event e;
  while (p.h && mhws::ws_poll(p.h, e)) {
    if (e.type == mhws::EV_CLOSE) { probe_finish(p, false); return; }
    if (e.type != mhws::EV_TEXT) continue;
    if (e.text == "pong") {
      p.rtt = (int)(now - p.t0);
      if (allIn()) { probe_finish(p, true); return; }
      continue;
    }
    J o;
    if (!parse_json(e.text, o) || o.t != J::OBJ) continue;
    std::string t = o["t"].str();
    if (t == "welcome") {
      p.me = o["id"].str();
      p.players.clear();
      for (auto &pl : o["players"].a)
        if (pl["id"].str() != p.me) p.players.push_back({pl["id"].str(), pl["slot"].num()});
      // ready at once: a probe must never block a match that is starting (see net_worker.cpp)
      mhws::ws_send(p.h, "{\"t\":\"ready\"}");
      mhws::ws_send(p.h, "ping");
      p.t0 = now;
      p.welcomed = true;
      p.deadline = now + p.waitMs;
    } else if (t == "msg" && o["d"]["k"].str() == "hi") {
      std::string nm = o["d"]["n"].str();
      p.names[o["from"].str()] = clean(nm.empty() ? "player" : nm);
      p.rdy[o["from"].str()] = o["d"]["r"].truthy();
      if (allIn() && p.rtt >= 0) { probe_finish(p, true); return; }
    }
  }
  if (p.h && now >= p.deadline) probe_finish(p, p.welcomed);
}

void pump_ui() {
  int64_t now = net_now_ms();
  for (size_t i = 0; i < g_http.size();) {
    int st, ms;
    std::string body;
    if (!mhws::http_result(g_http[i].h, st, body, ms)) { i++; continue; }
    if (g_http[i].kind == 0) {
      g_ping_busy = false;
      g_uq.push_back("T\t" + std::to_string(st > 0 ? ms : -1));
    } else {
      J o;
      bool has = st == 200 && parse_json(body, o) && o.t == J::OBJ && !o["code"].str().empty();
      g_uq.push_back(std::string("Z\t") + (has ? "1" : "0"));
    }
    g_http.erase(g_http.begin() + (ptrdiff_t)i);
  }
  for (auto &p : g_probes) if (p.h) probe_step(p, now);
  g_probes.erase(std::remove_if(g_probes.begin(), g_probes.end(), [](const Probe &p) { return p.h == 0; }),
                 g_probes.end());
}

}  // namespace wkn

// ------------------------------------------------------------------ the helpers net_worker.cpp calls
static void js_ws_open(const char *url) {
  if (wkn::g_main) mhws::ws_close(wkn::g_main);
  wkn::g_q.clear();
  wkn::g_main = mhws::ws_open(url);
}
static int js_ws_send(const char *s) { return wkn::g_main && mhws::ws_send(wkn::g_main, s) ? 1 : 0; }
static void js_ws_close() {
  if (wkn::g_main) mhws::ws_close(wkn::g_main);
  wkn::g_main = 0;
  wkn::g_q.clear();
}
static int js_net_poll(int ui, char *buf, int cap) {
  if (ui) wkn::pump_ui();
  else {
    mhws::Event e;
    while (wkn::g_main && mhws::ws_poll(wkn::g_main, e)) wkn::main_event(e);
  }
  auto &q = ui ? wkn::g_uq : wkn::g_q;
  if (q.empty()) return -1;
  std::string s = q.front();
  q.pop_front();
  if ((int)s.size() >= cap) return 0;
  memcpy(buf, s.c_str(), s.size() + 1);
  return (int)s.size();
}
static void js_http_ping(const char *url) {
  if (wkn::g_ping_busy) return;
  wkn::g_ping_busy = true;
  wkn::g_http.push_back({mhws::http_get(url), 0});
}
static int js_url_room(char *, int) { return 0; }
static void js_net_ui(int, const char *, const char *, const char *, int) {}
static void js_check_regions(const char *url) { wkn::g_http.push_back({mhws::http_get(url), 1}); }
static void js_net_ask_code() { wkn::g_ask_code = true; }
static void js_probe(const char *url, const char *code, int waitMs) {
  wkn::Probe p;
  p.h = mhws::ws_open(url);
  p.code = code;
  p.waitMs = waitMs;
  p.deadline = net_now_ms() + 6000;     // until the welcome
  wkn::g_probes.push_back(p);
}

// for net.cpp: "enter room code" was chosen in the room list (once)
bool net_worker_ask_code() {
  bool r = wkn::g_ask_code;
  wkn::g_ask_code = false;
  return r;
}
