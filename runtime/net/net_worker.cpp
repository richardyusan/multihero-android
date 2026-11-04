// SPDX-License-Identifier: MIT
// net_worker.cpp - "cloudflare" backend: WebSocket client for the multihero-server Worker
// (github.com/richardyusan/multihero-server, a Cloudflare Worker + Durable Object per room).
//
// The server is a small relay: one Durable Object per room code (4-8 letters/digits), up to
// 4 players, "ready" -> "start" with a random seed, and every other JSON message is relayed
// to the rest of the room. It has no room list and no player names, so this backend adds them
// on top of the relayed messages:
//
//   wss://<server>/room/<CODE>
//   server -> client: {t:welcome,id,slot,host,players[]} {t:join,id,slot} {t:leave,id,slot}
//                     {t:host,id} {t:start,seed,players[]} {t:msg,from,slot,d} and "pong"
//   client -> server: {t:ready} (only when the match really starts) and "ping"
//   relayed (d):      {k:hi,n,h,r,p}  name, hero, ready (lobby), ping   - everybody, on change
//                     {k:room,m,l}    stage, lives                       - host
//                     {k:go,m,l}      host pressed START: send fin + {t:ready}
//                     {k:fin,h}       my final character, sent just before {t:ready}
//                     {k:g,a:[...]}   lockstep game messages (net.cpp), batched
//
// Limits of the server (src/index.js): 30 messages/s per player (burst 40), 512 characters
// per message; anything over the limit is dropped silently. The lockstep code sends one input
// message per frame (54/s), so game messages are queued and sent together every 40 ms through
// a token bucket that stays under the limit.
//
// Servers: the Worker places every room in a region chosen by the first letter of its code
// (multihero-server/src/regions.js): S = Server 1 Asia Tenggara, N = Server 2 Asia Timur,
// U = Server 3 US, E = Server 4 Eropa. The lobby shows one server at a time (picker in the page
// bar, with ping and players per server; the choice is saved, the first time the lowest ping wins).
// A server without regions (no GET /where) gets the old single "Server 1" with PUBLIC1-3.
//
// Rooms: three public rooms per server ("SPUB1".."SPUB3" etc.) are always in the room list; CREATE ROOM
// makes a private room with a random code on the chosen server ("enter room code" + recent rooms in the list, the page
// shows the code with a "copy invite link" button, index.html?room=CODE joins it directly).
//
// Public room list / online count without help from the server: while the room list is shown,
// one public room is "probed" every ~2.5 s - a short WebSocket that reads the welcome (who is in
// the room) and the "hi" every player sends when somebody joins (names, ready), then closes
// (< 1 s). Players only show sockets that sent "hi", so probes are invisible in the room. A probe
// also sends {t:ready} before closing: the server starts a match only when every socket is ready
// and checks that only when a ready arrives, so a probe that drops in while a match is starting
// must not block it (the start message then lists the probe, which everybody ignores).
// "Online" = players in the public rooms (private rooms cannot be counted).
#include "net.h"

#include "../bbrt.h"
#include "../platform.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <random>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
EM_JS_DEPS(mhnet, "$stringToUTF8,$lengthBytesUTF8");

// ------------------------------------------------------------------ JS side
// Events from the socket are turned into TAB separated lines (parsed in JS, so C++ needs no
// JSON parser) and queued until update() polls them.
EM_JS(void, js_ws_open, (const char *url), {
  var N = Module.mhNet || (Module.mhNet = {q: [], uq: []});
  if (N.ws) { try { N.ws.onclose = null; N.ws.close(); } catch (e) {} N.ws = null; }
  N.q = [];
  var bad = new RegExp('[\t\r\n]', 'g');   /* no regex literal: the C preprocessor would turn \r into a newline */
  var clean = function (s) { return String(s == null ? '' : s).replace(bad, ' '); };
  var pl = function (a) {
    return (a || []).map(function (p) { return clean(p.id) + ':' + (p.slot | 0) + ':' + (p.ready ? 1 : 0); }).join(',');
  };
  var ws;
  try { ws = new WebSocket(UTF8ToString(url)); } catch (e) { N.q.push('C\t0\t' + clean(e.message)); return; }
  N.ws = ws;
  ws.onopen = function () { if (N.ws === ws) N.q.push('O'); };
  ws.onerror = function () {};
  ws.onclose = function (e) { if (N.ws === ws) { N.q.push('C\t' + e.code + '\t' + clean(e.reason)); N.ws = null; } };
  ws.onmessage = function (e) {
    if (N.ws !== ws || typeof e.data !== 'string') return;
    if (e.data === 'pong') { N.q.push('P'); return; }
    var o;
    try { o = JSON.parse(e.data); } catch (x) { return; }
    if (!o) return;
    if (o.t === 'welcome') N.q.push(['W', clean(o.id), o.slot | 0, clean(o.host), pl(o.players)].join('\t'));
    else if (o.t === 'join') N.q.push(['J', clean(o.id), o.slot | 0].join('\t'));
    else if (o.t === 'leave') N.q.push(['L', clean(o.id), o.slot | 0].join('\t'));
    else if (o.t === 'host') N.q.push(['H', clean(o.id)].join('\t'));
    else if (o.t === 'start') N.q.push(['S', (o.seed >>> 0), pl(o.players)].join('\t'));
    else if (o.t === 'msg') {
      var d = o.d || {}, f = clean(o.from);
      if (d.k === 'g' && Array.isArray(d.a)) {
        for (var i = 0; i < d.a.length; i++) N.q.push('G\t' + f + '\t' + clean(d.a[i]));
      }
      else if (d.k === 'hi') N.q.push(['I', f, clean(d.n).slice(0, 16), d.h | 0, d.r ? 1 : 0, d.p | 0].join('\t'));
      else if (d.k === 'room') N.q.push(['R', f, d.m | 0, d.l | 0].join('\t'));
      else if (d.k === 'go') N.q.push(['X', f, d.m | 0, d.l | 0].join('\t'));
      else if (d.k === 'fin') N.q.push(['F', f, d.h | 0].join('\t'));
    }
  };
});
EM_JS(int, js_ws_send, (const char *s), {
  var N = Module.mhNet;
  if (!N || !N.ws || N.ws.readyState !== 1) return 0;
  try { N.ws.send(UTF8ToString(s)); return 1; } catch (e) { return 0; }
});
EM_JS(void, js_ws_close, (), {
  var N = Module.mhNet;
  if (!N) return;
  if (N.ws) { try { N.ws.onclose = null; N.ws.close(1000, 'bye'); } catch (e) {} N.ws = null; }
  N.q = [];
});
// next queued line (socket events, then page events such as a typed room code) into buf;
// returns its length, -1 = nothing queued
EM_JS(int, js_net_poll, (int ui, char *buf, int cap), {
  var N = Module.mhNet || (Module.mhNet = {q: [], uq: []});
  var q = ui ? N.uq : N.q;
  if (!q.length) return -1;
  var s = q.shift();
  if (lengthBytesUTF8(s) >= cap) return 0;
  stringToUTF8(s, buf, cap);
  return lengthBytesUTF8(s);
});
// round trip to the server while not in a room (HTTP, the response itself is not read)
EM_JS(void, js_http_ping, (const char *url), {
  var N = Module.mhNet || (Module.mhNet = {q: [], uq: []});
  if (N.httpBusy || typeof fetch !== 'function') return;
  N.httpBusy = true;
  var t0 = performance.now();
  fetch(UTF8ToString(url), {mode: 'no-cors', cache: 'no-store'}).then(function () {
    N.uq.push('T\t' + Math.round(performance.now() - t0));
  }).catch(function () { N.uq.push('T\t-1'); }).then(function () { N.httpBusy = false; });
});
// room code from the page address (index.html?room=CODE), only the first time
EM_JS(int, js_url_room, (char *buf, int cap), {
  var N = Module.mhNet || (Module.mhNet = {q: [], uq: []});
  if (N.urlUsed) return 0;
  N.urlUsed = true;
  var m = /[?&]room=([A-Za-z0-9]{4,8})/.exec(location.search || '');
  if (!m) return 0;
  stringToUTF8(m[1].toUpperCase(), buf, cap);
  return m[1].length;
});
// bar on the web page: 0 = hidden, 1 = room list, 2 = in a room (code + invite link)
EM_JS(void, js_net_ui, (int mode, const char *code, const char *name, const char *servers, int cur), {
  if (Module.mhNetUi) Module.mhNetUi(mode, UTF8ToString(code), UTF8ToString(name), UTF8ToString(servers), cur);
});
// does the server place rooms by region? (GET /where/<code> answers JSON) -> "Z 1" / "Z 0"
EM_JS(void, js_check_regions, (const char *url), {
  var N = Module.mhNet || (Module.mhNet = {q: [], uq: []});
  if (typeof fetch !== 'function') { N.uq.push('Z\t0'); return; }
  fetch(UTF8ToString(url), {cache: 'no-store'}).then(function (r) { return r.json(); }).then(function (j) {
    N.uq.push('Z\t' + (j && j.code ? 1 : 0));
  }).catch(function () { N.uq.push('Z\t0'); });
});
EM_JS(void, js_net_ask_code, (), { if (Module.mhNetAskCode) Module.mhNetAskCode(); });
// look into a public room for a moment: result "Q CODE ok players host ready ping" / "Q CODE fail"
// (full or playing: the server refuses the socket) / "Q CODE skip" (page hidden)
EM_JS(void, js_probe, (const char *url, const char *code, int waitMs), {
  var N = Module.mhNet || (Module.mhNet = {q: [], uq: []});
  var c = UTF8ToString(code), u = UTF8ToString(url);
  if (document.hidden) { N.uq.push('Q\t' + c + '\tskip'); return; }
  var bad = new RegExp('[\t\r\n]', 'g');
  var clean = function (s) { return String(s == null ? '' : s).replace(bad, ' '); };
  var ws, done = false, me = '', players = [], names = {}, rdy = {}, timer = 0, t0 = 0, rtt = -1;
  var finish = function (ok) {
    if (done) return;
    done = true;
    clearTimeout(timer);
    try { ws.onclose = null; ws.close(1000, 'probe'); } catch (e) {}
    if (!ok) { N.uq.push('Q\t' + c + '\tfail'); return; }
    var host = '', hs = 99, nr = 0, n = 0;
    players.forEach(function (p) {
      if (names[p.id] === undefined) return;
      n++;
      if (rdy[p.id]) nr++;
      if (p.slot < hs) { hs = p.slot; host = names[p.id]; }
    });
    N.uq.push(['Q', c, 'ok', n, clean(host).slice(0, 16), nr, rtt].join('\t'));
  };
  try { ws = new WebSocket(u); } catch (e) { N.uq.push('Q\t' + c + '\tfail'); return; }
  timer = setTimeout(function () { finish(false); }, 6000);
  ws.onerror = function () {};
  ws.onclose = function () { finish(false); };
  ws.onmessage = function (e) {
    if (typeof e.data !== 'string') return;
    var allIn = function () { return players.every(function (p) { return names[p.id] !== undefined; }); };
    if (e.data === 'pong') {                    /* answered by the room itself: real ping */
      rtt = Math.round(performance.now() - t0);
      if (allIn()) finish(true);
      return;
    }
    var o;
    try { o = JSON.parse(e.data); } catch (x) { return; }
    if (!o) return;
    if (o.t === 'welcome') {
      me = o.id;
      players = (o.players || []).filter(function (p) { return p.id !== me; });
      try { ws.send(JSON.stringify({t: 'ready'})); t0 = performance.now(); ws.send('ping'); } catch (x) {}
      clearTimeout(timer);
      timer = setTimeout(function () { finish(true); }, waitMs);
    } else if (o.t === 'msg' && o.d && o.d.k === 'hi') {
      names[o.from] = clean(o.d.n || 'player');
      rdy[o.from] = !!o.d.r;
      if (allIn() && rtt >= 0) finish(true);
    }
  };
});

bool net_worker_ask_code() { return false; }   // the page asks for the code itself
#else
#include "net_worker_native.inc"   // the same helpers for Android / Windows / Linux (ws_client.cpp)
#endif

namespace {

const char *CODE_CHARS = "ABCDEFGHJKMNPQRSTUVWXYZ23456789";   // no 0/O, 1/I/L
const int64_t SEND_EVERY_MS = 40;      // batch game messages: 25 packets/s at most
const double TOKENS_PER_MS = 25.0 / 1000.0;
const double TOKENS_MAX = 20;
const size_t PACKET_MAX = 480;         // server limit is 512 characters
const int64_t START_TIMEOUT_MS = 12000;
struct ServerInfo { char letter; const char *shortName; const char *longName; };
const ServerInfo SERVERS[] = {
  {'S', "Server 1 Asia SE", "Server 1 - Asia Tenggara"},
  {'N', "Server 2 Asia NE", "Server 2 - Asia Timur"},
  {'U', "Server 3 US", "Server 3 - US"},
  {'E', "Server 4 Europe", "Server 4 - Eropa"},
};
const int SERVER_N = 4;
const int PUB_N = 3;                    // public rooms per server
const int PROBE_WAIT_MS = 900;          // how long a probe waits for the players' names
const int64_t PROBE_EVERY_MS = 2500;    // the shown server's rooms every ~10 s, the others slower

int server_of_code(const std::string &code) {
  for (int i = 0; i < SERVER_N; i++) if (!code.empty() && code[0] == SERVERS[i].letter) return i;
  return -1;
}

// after a match the lobby opens again: rejoin the same room for the next round
std::string g_rejoin_code;
int64_t g_rejoin_until = 0;
int g_last_hero = 0;                   // keep my character between lobby visits

std::string json_str(const std::string &s) {
  std::string o = "\"";
  for (unsigned char c : s) {
    if (c == '"' || c == '\\') { o += '\\'; o += (char)c; }
    else if (c < 32) { char b[8]; snprintf(b, sizeof b, "\\u%04x", c); o += b; }
    else o += (char)c;
  }
  return o + "\"";
}

std::vector<std::string> split(const std::string &s, char sep) {
  std::vector<std::string> out;
  size_t a = 0;
  for (;;) {
    size_t b = s.find(sep, a);
    out.push_back(s.substr(a, b == std::string::npos ? std::string::npos : b - a));
    if (b == std::string::npos) break;
    a = b + 1;
  }
  return out;
}

std::string clean_code(const std::string &in) {
  std::string c;
  for (char ch : in) {
    if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 32);
    if ((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9')) c += ch;
  }
  if (c.size() > 8) c.resize(8);
  return c.size() >= 4 ? c : std::string();
}

class NetWorker : public NetBackend {
 public:
  ~NetWorker() override { disconnect(); }
  const char *name() const override {
    if (regions_ != 1) return "Server 1";
    int s = inRoom() || state == NET_CONNECTING ? server_of_code(roomId) : cur_;
    return s >= 0 ? SERVERS[s].shortName : "private server";
  }
  bool supportsHeroes() const override { return true; }

  void connect() override {
    std::string base = cfg.get("server");
    while (!base.empty() && base.back() == '/') base.pop_back();
    if (base.empty()) {
      state = NET_ERROR;
      lastError = "no server set in online.cfg";
      return;
    }
    httpBase_ = base;
    if (base.compare(0, 8, "https://") == 0) wsBase_ = "wss://" + base.substr(8);
    else if (base.compare(0, 7, "http://") == 0) wsBase_ = "ws://" + base.substr(7);
    else { wsBase_ = "wss://" + base; httpBase_ = "https://" + base; }
    std::random_device rd;
    rng_.seed(rd() ^ (uint32_t)std::chrono::steady_clock::now().time_since_epoch().count());
    if (g_last_hero) myHero = g_last_hero;
    state = NET_ONLINE;
    onlineCount = -1;              // known after the public rooms were probed
    probeBusy_ = false;
    nextProbe_ = 0;
    loadServerChoice();
    if (regions_ < 0) js_check_regions((httpBase_ + "/where/SPUB1").c_str());
    else startSweep(true);
    lastError.clear();
    loadRecent();
    buildRooms();
    int64_t now = net_now_ms();
    char buf[16];
    if (js_url_room(buf, sizeof buf) > 0) {
      wantJoin_ = clean_code(buf);
      wantTries_ = 1;
    } else if (!g_rejoin_code.empty() && now < g_rejoin_until) {
      wantJoin_ = g_rejoin_code;   // the others come back to the same room after a match
      wantTries_ = 6;
    }
    g_rejoin_code.clear();
    nextTry_ = now + (wantTries_ > 1 ? 900 : 0);
    nextHttpPing_ = 0;
    httpPings_ = 0;
  }

  void disconnect() override {
    if (state == NET_IN_MATCH && !roomId.empty()) {
      g_rejoin_code = roomId;
      g_rejoin_until = net_now_ms() + 60000;
    }
    js_ws_close();
    sock_ = false;
    queue_.clear();
    state = NET_OFFLINE;
    rooms.clear();
    resetRoom();
    clearRoom();
    ui(0);
  }

  void update(int64_t now) override {
    pollPage(now);
    if (state == NET_OFFLINE) return;
    if (!wantJoin_.empty() && state == NET_ONLINE && now >= nextTry_) {
      std::string c = wantJoin_;
      if (--wantTries_ <= 0) wantJoin_.clear();
      open(c);
    }
    pollSocket(now);
    if (state == NET_OFFLINE) return;

    if (sock_ && inRoom()) {
      if (now >= nextPing_) {
        js_ws_send("ping");
        pingSentAt_ = now;
        nextPing_ = now + 2000;
      }
      if (now >= nextHello_) sendHello();
    } else if (state == NET_ONLINE && now >= nextHttpPing_) {
      js_http_ping((httpBase_ + "/").c_str());
      nextHttpPing_ = now + (httpPings_ == 0 ? 700 : 4000);
    }
    // probe public rooms only once we know whether the server has regions: a room is placed
    // where it is first opened, a probe of "UPUB1" on an old server would pin it near us
    if (state == NET_ONLINE && regions_ >= 0 && !probeBusy_ && now >= nextProbe_) {
      std::string c = nextProbeCode();
      js_probe((wsBase_ + "/room/" + c).c_str(), c.c_str(), PROBE_WAIT_MS);
      probeBusy_ = true;
      probeStarted_ = now;
    }
    if (probeBusy_ && now - probeStarted_ > 8000) probeBusy_ = false;   // lost result

    if (state == NET_STARTING) {
      if (started_ && now >= countdownEndMs) state = NET_IN_MATCH;
      else if (!started_ && now - goAt_ > START_TIMEOUT_MS) {
        // somebody did not confirm (left at the wrong moment): open the room again, so the
        // server forgets the "ready" flags, and let the host press START once more
        std::string c = roomId;
        lastError = "the match did not start - try again";
        leaveRoom();
        wantJoin_ = c;
        wantTries_ = 3;
        nextTry_ = now + 500;
      }
    }
    flush(now);
    ui(state == NET_IN_ROOM || state == NET_STARTING ? 2 : state == NET_IN_MATCH ? 0 : 1);
  }

  void refreshRooms() override {
    loadRecent();
    buildRooms();
    nextHttpPing_ = 0;
    if (regions_ >= 0) startSweep(false);
  }

  void createRoom(const std::string &, int) override {
    if (state != NET_ONLINE) return;
    std::string c(1, SERVERS[regions_ == 1 ? cur_ : 0].letter);   // first letter = server (region)
    for (int i = 0; i < 4; i++) c += CODE_CHARS[rng_() % strlen(CODE_CHARS)];
    open(c);
  }

  void joinRoom(const std::string &id) override {
    if (state != NET_ONLINE) return;
    if (id == "?") { js_net_ask_code(); return; }
    std::string c = clean_code(id);
    if (!c.empty()) open(c);
    else lastError = "a room code has 4-8 letters or digits";
  }

  void leaveRoom() override {
    if (!inRoom() && state != NET_CONNECTING) return;
    js_ws_close();
    sock_ = false;
    queue_.clear();
    resetRoom();
    clearRoom();
    state = NET_ONLINE;
    buildRooms();
  }

  void setReady(bool r) override {
    for (auto &m : members) if (m.me) m.ready = r;
    sendHello();
  }

  void startMatch() override {
    if (!canStart()) return;
    sendNow("{\"k\":\"go\",\"m\":" + std::to_string(matchMap) + ",\"l\":" + std::to_string(matchLives) + "}");
    beginGo(net_now_ms());
  }

  void sendGame(const std::string &d) override {
    if (sock_ && inRoom()) queue_.push_back(d);
  }

  void setHero(int hero) override {
    NetBackend::setHero(hero);
    g_last_hero = hero;
    if (sock_ && state == NET_IN_ROOM) sendHello();
  }
  void setMap(int map) override {
    matchMap = map;
    sendRoom();
  }
  void setLives(int lives) override {
    matchLives = lives;
    sendRoom();
  }
  // server choice in the game (native builds; the web page has its own picker)
  int serverCount() const override { return regions_ == 1 ? SERVER_N : regions_ == 0 ? 1 : 0; }
  std::string serverName(int i) const override {
    if (regions_ != 1) return "Server 1";
    return i >= 0 && i < SERVER_N ? SERVERS[i].longName : "";
  }
  int serverPing(int i) const override { return i >= 0 && i < SERVER_N ? srvPing_[i] : -1; }
  int serverOnline(int i) const override {
    if (i < 0 || i >= servers()) return -1;
    int on = -1;
    for (int k = 0; k < PUB_N; k++) if (pub_[i][k].players >= 0) on = (on < 0 ? 0 : on) + pub_[i][k].players;
    return on;
  }
  int currentServer() const override { return cur_; }
  void chooseServer(int i) override { if (state == NET_ONLINE) setServer(i); }
  void nameChanged() override {
    NetBackend::nameChanged();
    if (sock_ && inRoom()) sendHello();
  }

 private:
  std::mt19937 rng_;
  std::string httpBase_, wsBase_;
  bool sock_ = false;               // socket open (or opening) for roomId
  bool joining_ = false;            // waiting for "welcome"
  std::string myId_, hostId_;
  std::map<std::string, int> slotOf_;   // member id -> server slot (sort order of members)
  std::vector<std::string> queue_;  // game messages waiting for the next packet
  int64_t lastFlush_ = 0, lastTokens_ = 0, pingSentAt_ = 0, nextPing_ = 0, nextHello_ = 0;
  int64_t nextHttpPing_ = 0, nextTry_ = 0, goAt_ = 0;
  double tokens_ = TOKENS_MAX;
  bool started_ = false, readySent_ = false;
  std::string wantJoin_;
  int wantTries_ = 0;
  std::vector<std::pair<std::string, std::string>> recent_;   // code, host name
  int uiMode_ = -1;
  int httpPings_ = 0;
  // sockets in my room that did not say "hi" yet (players still loading, or probes): not shown
  std::map<std::string, int> pending_;
  struct PubRoom { int players = -1, ready = 0, ping = -1; bool open = true; std::string host; };
  PubRoom pub_[SERVER_N][PUB_N];
  int regions_ = -1;                  // -1 checking, 0 old server (one region), 1 four servers
  int cur_ = 0;                       // server shown in the lobby
  bool chosen_ = false, autoPicked_ = false;
  int srvPing_[SERVER_N] = {-1, -1, -1, -1};
  std::vector<std::string> sweepList_;   // probe these first (lobby opened, refresh, server changed)
  int probeK_ = 0, otherK_ = 0;
  bool probeBusy_ = false;
  int64_t nextProbe_ = 0, probeStarted_ = 0;
  bool wsPinged_ = false;             // a probe measured the ping: better than the HTTP one
  std::string uiCode_, uiName_, uiServers_;
  int uiCur_ = -1;

  // ---------------------------------------------------------------- rooms / page
  void loadRecent() {
    recent_.clear();
    int f = bb_readfile("online_rooms.txt");
    if (!f) return;
    while (!bb_eof(f) && recent_.size() < 5) {
      auto p = split(bb_readline(f), '\t');
      std::string c = clean_code(p[0]);
      if (!c.empty()) recent_.push_back({c, p.size() > 1 ? p[1] : std::string()});
    }
    bb_closefile(f);
  }
  void saveRecent(const std::string &code, const std::string &host) {
    if (isPublic(code)) return;                   // always in the list anyway
    loadRecent();
    recent_.erase(std::remove_if(recent_.begin(), recent_.end(),
                                 [&](const std::pair<std::string, std::string> &r) { return r.first == code; }),
                  recent_.end());
    recent_.insert(recent_.begin(), {code, host});
    if (recent_.size() > 5) recent_.resize(5);
    int f = bb_writefile("online_rooms.txt");
    if (!f) return;
    for (auto &r : recent_) bb_writeline(f, r.first + "\t" + r.second);
    bb_closefile(f);
  }
  void buildRooms() {
    rooms.clear();
    int online = -1;
    for (int i = 0; i < PUB_N; i++) {
      const PubRoom &pr = pub_[cur_][i];
      NetRoom n;
      n.id = publicCode(cur_, i);
      n.name = "public room " + std::to_string(i + 1);
      n.maxPlayers = pr.players >= 0 ? 4 : 0;   // 0 = not known yet (not shown)
      n.players = pr.players < 0 ? 0 : pr.players;
      n.open = pr.open && n.players < 4 && regions_ >= 0;
      n.host = pr.players < 0 ? "checking..." : !pr.open ? (pr.players < 4 ? "playing" : pr.host)
               : pr.players == 0 ? "empty" : pr.host;
      n.ping = pr.ping >= 0 ? pr.ping : srvPing_[cur_];
      rooms.push_back(n);
    }
    for (int s2 = 0; s2 < servers(); s2++)
      for (int i = 0; i < PUB_N; i++)
        if (pub_[s2][i].players >= 0) online = (online < 0 ? 0 : online) + pub_[s2][i].players;
    if (!inRoom()) onlineCount = online;
    NetRoom ask;
    ask.id = "?";
    ask.name = "enter room code";
    ask.host = "press join";
    ask.maxPlayers = 0;              // 0 = player count unknown (not shown)
    rooms.push_back(ask);
    for (auto &r : recent_) {
      NetRoom n;
      n.id = r.first;
      n.name = "room " + r.first;
      n.host = r.second.empty() ? "recent" : r.second;
      n.maxPlayers = 0;
      rooms.push_back(n);
    }
  }
  void ui(int mode) {
    std::string code = mode == 2 ? roomId : std::string();
    std::string srv;                  // "name<TAB>ping<TAB>players" per server, one per line
    if (regions_ == 1)
      for (int i = 0; i < SERVER_N; i++) {
        int on = -1;
        for (int k = 0; k < PUB_N; k++) if (pub_[i][k].players >= 0) on = (on < 0 ? 0 : on) + pub_[i][k].players;
        srv += std::string(SERVERS[i].longName) + "\t" + std::to_string(srvPing_[i]) + "\t" + std::to_string(on) + "\n";
      }
    if (mode == uiMode_ && code == uiCode_ && playerName == uiName_ && srv == uiServers_ && cur_ == uiCur_) return;
    uiMode_ = mode; uiCode_ = code; uiName_ = playerName; uiServers_ = srv; uiCur_ = cur_;
    js_net_ui(mode, code.c_str(), playerName.c_str(), srv.c_str(), cur_);
  }

  // ---------------------------------------------------------------- servers / public rooms
  int servers() const { return regions_ == 1 ? SERVER_N : 1; }
  std::string publicCode(int srv, int i) const {
    if (regions_ != 1) return "PUBLIC" + std::to_string(i + 1);     // old server: as in 1.7.0
    return std::string(1, SERVERS[srv].letter) + "PUB" + std::to_string(i + 1);
  }
  bool publicIndex(const std::string &code, int &srv, int &i) const {
    for (srv = 0; srv < servers(); srv++)
      for (i = 0; i < PUB_N; i++) if (code == publicCode(srv, i)) return true;
    return false;
  }
  bool isPublic(const std::string &code) const { int a, b; return publicIndex(code, a, b); }
  void loadServerChoice() {
    int f = bb_readfile("online_server.txt");
    if (!f) return;
    std::string l = bb_readline(f);
    bb_closefile(f);
    int s = server_of_code(l);
    if (s >= 0) { cur_ = s; chosen_ = true; }
  }
  void saveServerChoice() {
    int f = bb_writefile("online_server.txt");
    if (!f) return;
    bb_writeline(f, std::string(1, SERVERS[cur_].letter));
    bb_closefile(f);
  }
  // look into the rooms of the shown server first (all servers when the lobby opens)
  void startSweep(bool all) {
    sweepList_.clear();
    for (int i = 0; i < PUB_N; i++) sweepList_.push_back(publicCode(cur_, i));
    if (all)
      for (int s2 = 0; s2 < servers(); s2++)
        if (s2 != cur_) for (int i = 0; i < PUB_N; i++) sweepList_.push_back(publicCode(s2, i));
    nextProbe_ = 0;
  }
  // after the sweep: 3 probes of the shown server, then 1 of another server, and so on
  std::string nextProbeCode() {
    if (!sweepList_.empty()) {
      std::string c = sweepList_.front();
      sweepList_.erase(sweepList_.begin());
      return c;
    }
    int k = probeK_++ % 4;
    if (k < 3 || servers() == 1) return publicCode(cur_, k % PUB_N);
    int others = (servers() - 1) * PUB_N;
    int o = otherK_++ % others;
    int s2 = o / PUB_N;
    if (s2 >= cur_) s2++;
    return publicCode(s2, o % PUB_N);
  }
  void setServer(int s) {
    if (regions_ != 1 || s < 0 || s >= SERVER_N || s == cur_) return;
    cur_ = s;
    chosen_ = true;
    saveServerChoice();
    if (srvPing_[cur_] >= 0) ping = srvPing_[cur_];
    startSweep(false);
    buildRooms();
  }
  void pollPage(int64_t now) {
    char buf[256];
    int n;
    while ((n = js_net_poll(1, buf, sizeof buf)) >= 0) {
      auto f = split(std::string(buf, n > 0 ? n : 0), '\t');
      if (f[0] == "K" && f.size() > 1) {           // room code typed on the page
        std::string c = clean_code(f[1]);
        if (c.empty()) lastError = "a room code has 4-8 letters or digits";
        else if (state == NET_ONLINE) open(c);
      } else if (f[0] == "N" && f.size() > 1) {    // new player name
        std::string nm;
        for (char ch : f[1]) if ((unsigned char)ch >= 32) nm += ch;
        if (nm.size() > 16) nm.resize(16);
        if (!nm.empty()) {
          playerName = nm;
          for (auto &m : members) if (m.me) m.name = nm;
          int fh = bb_writefile("online_name.txt");
          if (fh) { bb_writeline(fh, nm); bb_closefile(fh); }
          if (sock_ && inRoom()) sendHello();
        }
      } else if (f[0] == "Q" && f.size() > 2) {    // public room probe
        probeBusy_ = false;
        nextProbe_ = now + (!sweepList_.empty() ? 300 : PROBE_EVERY_MS);
        int sv, i;
        if (publicIndex(f[1], sv, i)) {
          PubRoom &pr = pub_[sv][i];
          if (f[2] == "ok") {
            pr.players = f.size() > 3 ? atoi(f[3].c_str()) : 0;
            pr.host = f.size() > 4 ? f[4] : std::string();
            pr.ready = f.size() > 5 ? atoi(f[5].c_str()) : 0;
            pr.open = true;
            int rt = f.size() > 6 ? atoi(f[6].c_str()) : -1;
            if (rt >= 0) {                       // WebSocket ping to that room = to that server
              pr.ping = rt;
              srvPing_[sv] = rt;
              if (sv == cur_) { ping = rt; wsPinged_ = true; }
            }
          } else if (f[2] == "fail") {
            pr.open = false;                     // full or a match is running (count kept)
            if (pr.players < 0) pr.players = 0;
          }
          autoPick();
        }
        if (state == NET_ONLINE) buildRooms();
      } else if (f[0] == "Z" && f.size() > 1) {    // server with / without regions
        regions_ = f[1] == "1" ? 1 : 0;
        if (regions_ == 0) cur_ = 0;
        startSweep(true);
        if (state == NET_ONLINE) buildRooms();
      } else if (f[0] == "V" && f.size() > 1) {    // server picked on the page
        if (state == NET_ONLINE) setServer(atoi(f[1].c_str()));
      } else if (f[0] == "T" && f.size() > 1) {    // HTTP round trip
        int p = atoi(f[1].c_str());
        // the first request also opens the TLS connection: it is much slower, skip it
        if (p >= 0 && !inRoom() && httpPings_++ > 0 && !wsPinged_) ping = p;
      }
    }
  }

  // ---------------------------------------------------------------- socket
  void open(const std::string &code) {
    resetRoom();
    roomId = code;
    int sv, pi;
    roomName = publicIndex(code, sv, pi) ? "public room " + std::to_string(pi + 1) : "room " + code;
    js_ws_open((wsBase_ + "/room/" + code).c_str());
    sock_ = true;
    joining_ = true;
    state = NET_CONNECTING;
  }
  void resetRoom() {
    myId_.clear(); hostId_.clear(); slotOf_.clear(); pending_.clear();
    started_ = false; readySent_ = false; joining_ = false;
    matchMap = 0; matchLives = 3; matchSeed = 0;
    tokens_ = TOKENS_MAX;
    queue_.clear();
  }

  void pollSocket(int64_t now) {
    static char buf[2048];
    int n;
    while ((n = js_net_poll(0, buf, sizeof buf)) >= 0) {
      if (n == 0) continue;
      handle(split(std::string(buf, n), '\t'), now);
      if (state == NET_OFFLINE) return;
    }
  }

  NetMember *member(const std::string &id) {
    for (auto &m : members) if (m.id == id) return &m;
    return nullptr;
  }
  int indexOf(const std::string &id) const {
    for (size_t i = 0; i < members.size(); i++) if (members[i].id == id) return (int)i + 1;
    return 0;
  }
  void addMember(const std::string &id, int slot) {
    slotOf_[id] = slot;
    if (!member(id)) {
      NetMember m;
      m.id = id;
      m.me = id == myId_;
      m.name = m.me ? playerName : "...";
      if (m.me) { m.hero = myHero; m.ping = ping; }
      members.push_back(m);
    }
    sortMembers();
  }
  void sortMembers() {
    std::stable_sort(members.begin(), members.end(), [&](const NetMember &a, const NetMember &b) {
      return slotOf_[a.id] < slotOf_[b.id];
    });
    // host = the shown player with the lowest server slot. The server's own "host" is not used:
    // it only tells a newcomer (and counts probes), so two players could both think they host
    // after a probe took a free low slot. This rule gives the same host on every screen.
    bool wasHost = isHost();
    hostId_ = members.empty() ? std::string() : members.front().id;
    for (auto &m : members) m.host = m.id == hostId_;
    onlineCount = (int)members.size();
    if (!wasHost && isHost() && state == NET_IN_ROOM && sock_) sendRoom();   // I became the host
  }
  // first visit (no saved choice): show the server with the lowest ping once all were measured
  void autoPick() {
    if (chosen_ || autoPicked_ || regions_ != 1) return;
    int best = -1;
    for (int i = 0; i < SERVER_N; i++) {
      if (srvPing_[i] < 0) return;
      if (best < 0 || srvPing_[i] < srvPing_[best]) best = i;
    }
    autoPicked_ = true;
    if (best != cur_) { cur_ = best; ping = srvPing_[cur_]; startSweep(false); }
  }
  // ping shown for another player: round trip through the relay (their ping + mine), which
  // is also what NetPlayDelay() needs to choose the input delay
  int pathPing(int theirs) const {
    if (theirs < 0) return -1;
    return ping >= 0 ? theirs + ping : theirs;
  }

  void handle(const std::vector<std::string> &f, int64_t now) {
    const std::string &k = f[0];
    auto S = [&](size_t i) { return i < f.size() ? f[i] : std::string(); };
    auto I = [&](size_t i) { return i < f.size() ? atoi(f[i].c_str()) : 0; };
    if (k == "W") {                               // welcome: I am in the room
      myId_ = S(1);
      members.clear();
      slotOf_.clear();
      for (auto &p : split(S(4), ',')) {
        auto q = split(p, ':');
        if (q.size() < 2 || q[0].empty()) continue;
        if (q[0] == myId_) addMember(q[0], atoi(q[1].c_str()));
        else pending_[q[0]] = atoi(q[1].c_str());  // shown once they send "hi"
      }
      joining_ = false;
      wantJoin_.clear();
      state = NET_IN_ROOM;
      lastError.clear();
      nextPing_ = 0;
      sendHello();
      if (isHost()) sendRoom();
      saveRecent(roomId, isHost() ? playerName : std::string());
    } else if (k == "J") {
      pending_[S(1)] = I(2);
      sendHello();                                // so the new player learns my name
      if (isHost()) {
        sendRoom();
        if (state == NET_STARTING && !started_)   // joined while the match is starting
          sendNow("{\"k\":\"go\",\"m\":" + std::to_string(matchMap) + ",\"l\":" + std::to_string(matchLives) + "}");
      }
    } else if (k == "L") {
      std::string id = S(1);
      members.erase(std::remove_if(members.begin(), members.end(), [&](const NetMember &m) { return m.id == id; }),
                    members.end());
      slotOf_.erase(id);
      pending_.erase(id);
      sortMembers();
    } else if (k == "H") {                        // server's host: see sortMembers()
      sortMembers();
    } else if (k == "S") {                        // the server started the match
      matchSeed = (uint32_t)strtoul(S(1).c_str(), nullptr, 10);
      std::vector<NetMember> keep;
      std::map<std::string, int> order;
      for (auto &p : split(S(2), ',')) {
        auto q = split(p, ':');
        if (q.size() >= 2) order[q[0]] = atoi(q[1].c_str());
      }
      for (auto &m : members) if (order.count(m.id)) keep.push_back(m);
      for (auto &kv : order) slotOf_[kv.first] = kv.second;
      members = keep;
      sortMembers();
      started_ = true;
      if (state != NET_STARTING) beginGo(now);
      saveRecent(roomId, hostName());
    } else if (k == "I") {                        // somebody's name / character / ready / ping
      auto pend = pending_.find(S(1));
      if (pend != pending_.end() && state != NET_IN_MATCH) {
        addMember(pend->first, pend->second);
        pending_.erase(pend);
      }
      if (NetMember *m = member(S(1))) {
        if (!m->me) {
          m->name = S(2).empty() ? "player" : S(2);
          m->hero = I(3);
          m->ready = I(4) != 0;
          m->ping = pathPing(I(5));
        }
      }
    } else if (k == "R") {                        // stage / lives from the host
      if (S(1) == hostId_ && state == NET_IN_ROOM) { matchMap = I(2); matchLives = I(3) < 1 ? 3 : I(3); }
    } else if (k == "X") {                        // host pressed START
      if (S(1) == hostId_ && state == NET_IN_ROOM) {
        matchMap = I(2);
        matchLives = I(3) < 1 ? 3 : I(3);
        beginGo(now);
      }
    } else if (k == "F") {
      if (NetMember *m = member(S(1))) if (!m->me) m->hero = I(2);
    } else if (k == "G") {                        // lockstep message (net.cpp)
      if (inbox.size() < NET_INBOX_MAX) {
        NetGameMsg g;
        g.from = S(1);
        g.slot = indexOf(g.from);
        g.data = S(2);
        inbox.push_back(g);
      }
    } else if (k == "P") {
      ping = (int)(now - pingSentAt_);
      for (auto &m : members) if (m.me) m.ping = ping;
    } else if (k == "C") {                        // socket closed
      sock_ = false;
      int code = I(1);
      std::string why = S(2);
      bool wasJoining = joining_;
      if (state == NET_IN_MATCH) {
        members.erase(std::remove_if(members.begin(), members.end(), [](const NetMember &m) { return !m.me; }),
                      members.end());
        lastError = "connection to the room lost";
        return;
      }
      std::string c = roomId;
      resetRoom();
      clearRoom();
      state = NET_ONLINE;
      buildRooms();
      if (wasJoining) {
        lastError = wantJoin_.empty() ? "cannot join room " + c + " (full, playing or no internet)"
                                      : "room " + c + " is busy - trying again...";
        nextTry_ = now + 1500;
      } else {
        lastError = why == "rate limit" ? "too many messages - you left the room" : "connection to the room lost";
      }
      (void)code;
    }
  }

  std::string hostName() const {
    for (auto &m : members) if (m.host) return m.name;
    return std::string();
  }

  // host pressed START (or the server already started): confirm my character, then tell the
  // server I am ready; it starts the match (with a random seed) once everybody did
  void beginGo(int64_t now) {
    state = NET_STARTING;
    goAt_ = now;
    countdownEndMs = now + 3000;
    for (auto &m : members) if (m.me) m.ready = true;
    if (!readySent_) {
      sendNow("{\"k\":\"fin\",\"h\":" + std::to_string(myHero) + "}");
      sendNow("{\"t\":\"ready\"}");
      readySent_ = true;
    }
  }

  // ---------------------------------------------------------------- sending
  void sendHello() {
    const NetMember *m = me();
    bool r = m && m->ready;
    sendNow("{\"k\":\"hi\",\"n\":" + json_str(playerName) + ",\"h\":" + std::to_string(myHero) +
            ",\"r\":" + (r ? "1" : "0") + ",\"p\":" + std::to_string(ping) + "}");
    nextHello_ = net_now_ms() + 3000;
  }
  void sendRoom() {
    if (!sock_ || !isHost() || state != NET_IN_ROOM) return;
    sendNow("{\"k\":\"room\",\"m\":" + std::to_string(matchMap) + ",\"l\":" + std::to_string(matchLives) + "}");
  }
  void refill(int64_t now) {
    if (lastTokens_) tokens_ = std::min(TOKENS_MAX, tokens_ + (now - lastTokens_) * TOKENS_PER_MS);
    lastTokens_ = now;
  }
  // control messages go out at once (they are rare) but still use up a token
  void sendNow(const std::string &json) {
    if (!sock_) return;
    refill(net_now_ms());
    js_ws_send(json.c_str());
    tokens_ -= 1;
  }
  void flush(int64_t now) {
    if (queue_.empty() || !sock_ || now - lastFlush_ < SEND_EVERY_MS) return;
    refill(now);
    lastFlush_ = now;
    size_t i = 0;
    while (i < queue_.size() && tokens_ >= 1) {
      std::string p = "{\"k\":\"g\",\"a\":[";
      bool first = true;
      while (i < queue_.size()) {
        std::string item = json_str(queue_[i]);
        if (!first && p.size() + item.size() + 3 > PACKET_MAX) break;
        if (!first) p += ',';
        p += item;
        first = false;
        i++;
      }
      p += "]}";
      js_ws_send(p.c_str());
      tokens_ -= 1;
    }
    queue_.erase(queue_.begin(), queue_.begin() + i);
  }
};

}  // namespace

NetBackend *net_create_worker() { return new NetWorker(); }
