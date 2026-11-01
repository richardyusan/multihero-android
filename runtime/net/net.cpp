// SPDX-License-Identifier: MIT
// net.cpp - Net* functions for the Blitz code (see runtime/decls/net.decls), config loading
// and backend selection.
//
// online.cfg (bundled with the assets, can be overridden by a copy in the app's data folder):
//   backend = auto | demo | relay | eos     (auto: relay when "server" is set, otherwise demo)
//   server  = my-vps.example.com            (relay backend)
//   port    = 7777
//   name    = player name (default: random "player1234", remembered in online_name.txt)
//   max_players = 4
//   eos_*   = Epic Online Services settings (see docs/ONLINE.md)
// online.local.cfg (not in git) is read afterwards and overrides single keys.
#include "net.h"
#include "../bbrt.h"
#include "../platform.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <map>
#include <memory>
#include <random>

NetBackend *net_create_demo();
NetBackend *net_create_relay();
NetBackend *net_create_eos();   // nullptr when built without MH_WITH_EOS

static std::unique_ptr<NetBackend> g_net;
static std::string g_init_note;          // shown as "error" text when we fell back to another backend
static int g_last_sender = 0;
void bb_netplaysend(int frame, int mask);

int64_t net_now_ms() { return plat_ms(); }

// Services that live for the whole process (EOS) register a tick here; it runs every frame
// (bb_flip) while no backend is active, so pending requests such as "leave lobby" still finish.
void (*g_net_background_tick)() = nullptr;
void net_background_tick() {
  if (!g_net && g_net_background_tick) g_net_background_tick();
}

static std::string trim(const std::string &s) {
  size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
  return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

static void load_config_file(NetConfig &c, const char *file) {
  int f = bb_readfile(file);
  if (!f) return;
  while (!bb_eof(f)) {
    std::string line = bb_readline(f);
    size_t h = line.find('#');
    if (h != std::string::npos) line = line.substr(0, h);
    size_t eq = line.find('=');
    if (eq == std::string::npos) continue;
    std::string k = bb_lower(trim(line.substr(0, eq))), v = trim(line.substr(eq + 1));
    if (!k.empty()) c.values[k] = v;
  }
  bb_closefile(f);
}

// online.cfg is public (in git); online.local.cfg holds private values such as the EOS client
// secret, is not committed (.gitignore) and overrides online.cfg key by key.
static NetConfig load_config() {
  NetConfig c;
  load_config_file(c, "online.cfg");
  load_config_file(c, "online.local.cfg");
  return c;
}

static std::string clean_name(std::string n, size_t limit = 16) {
  std::string out;
  for (char ch : n) {
    unsigned char c = (unsigned char)ch;
    if (c < 32 || c == 127) continue;
    out += (char)c;
  }
  if (out.size() > limit) out.resize(limit);
  return out;
}

static std::string load_player_name(const NetConfig &c) {
  std::string n = clean_name(c.get("name"));
  if (!n.empty()) return n;
  int f = bb_readfile("online_name.txt");
  if (f) {
    n = clean_name(trim(bb_readline(f)));
    bb_closefile(f);
    if (!n.empty()) return n;
  }
  std::mt19937 r((uint32_t)std::chrono::steady_clock::now().time_since_epoch().count());
  n = "player" + std::to_string(1000 + r() % 9000);
  f = bb_writefile("online_name.txt");
  bb_writeline(f, n);
  bb_closefile(f);
  return n;
}

NetBackend *net_create_backend(const std::string &kindIn, std::string &note) {
  std::string kind = kindIn;
  if (kind == "eos") {
    if (NetBackend *b = net_create_eos()) return b;
    note = "epic sdk not built in - using demo";
    kind = "demo";
  }
  if (kind == "relay") {
    if (NetBackend *b = net_create_relay()) return b;
    note = "vps relay not available in the browser - using demo";
  }
  return net_create_demo();
}

// ------------------------------------------------------------------ Net* builtins

static NetBackend *net() { return g_net.get(); }
static bool idx_ok(int i, size_t n) { return i >= 1 && (size_t)i <= n; }

int bb_netinit() {
  if (g_net) return 1;
  NetConfig cfg = load_config();
  std::string kind = bb_lower(cfg.get("backend", "auto"));
  if (kind == "auto") kind = cfg.get("server").empty() ? "demo" : "relay";
  g_init_note.clear();
  g_net.reset(net_create_backend(kind, g_init_note));
  g_net->cfg = cfg;
  g_net->playerName = load_player_name(cfg);
  g_net->connect();
  return 1;
}
void bb_netshutdown() {
  if (!g_net) return;
  g_net->disconnect();
  g_net.reset();
}
void bb_netupdate() {
  if (g_net) g_net->update(net_now_ms());
}
BStr bb_netbackendname() { return net() ? net()->name() : "none"; }
BStr bb_netstatus() {
  if (!net()) return "offline";
  switch (net()->state) {
    case NET_OFFLINE: return "offline";
    case NET_CONNECTING: return "connecting";
    case NET_ONLINE: return "online";
    case NET_IN_ROOM: return "in room";
    case NET_STARTING: return "starting";
    case NET_IN_MATCH: return "in match";
    case NET_ERROR: return "error";
  }
  return "offline";
}
BStr bb_neterror() {
  if (!net()) return "";
  return net()->lastError.empty() ? g_init_note : net()->lastError;
}
BStr bb_netplayername() { return net() ? net()->playerName : ""; }
int bb_netping() { return net() ? net()->ping : -1; }
int bb_netonlinecount() { return net() ? net()->onlineCount : 0; }

void bb_netrefresh() { if (net()) net()->refreshRooms(); }
int bb_netroomcount() { return net() ? (int)net()->rooms.size() : 0; }
static const NetRoom *room(int i) { return net() && idx_ok(i, net()->rooms.size()) ? &net()->rooms[i - 1] : nullptr; }
BStr bb_netroomname(int i) { auto r = room(i); return r ? r->name : ""; }
BStr bb_netroomhost(int i) { auto r = room(i); return r ? r->host : ""; }
int bb_netroomplayers(int i) { auto r = room(i); return r ? r->players : 0; }
int bb_netroommax(int i) { auto r = room(i); return r ? r->maxPlayers : 0; }
int bb_netroomping(int i) { auto r = room(i); return r ? r->ping : -1; }
int bb_netroomopen(int i) { auto r = room(i); return r && r->open ? 1 : 0; }

void bb_netcreateroom(const BStr &name, int maxPlayers) {
  if (!net()) return;
  if (maxPlayers < 2) maxPlayers = 2;
  if (maxPlayers > 4) maxPlayers = 4;
  net()->createRoom(clean_name(name.empty() ? net()->playerName : name, 24), maxPlayers);
}
void bb_netjoinroom(int i) { if (auto r = room(i)) net()->joinRoom(r->id); }
void bb_netleaveroom() { if (net()) net()->leaveRoom(); }
int bb_netinroom() { return net() && net()->inRoom() ? 1 : 0; }
BStr bb_netroomtitle() { return net() ? net()->roomName : ""; }
int bb_netishost() { return net() && net()->isHost() ? 1 : 0; }
int bb_netmembercount() { return net() ? (int)net()->members.size() : 0; }
static const NetMember *mem(int i) { return net() && idx_ok(i, net()->members.size()) ? &net()->members[i - 1] : nullptr; }
BStr bb_netmembername(int i) { auto m = mem(i); return m ? m->name : ""; }
int bb_netmemberready(int i) { auto m = mem(i); return m && m->ready ? 1 : 0; }
int bb_netmemberping(int i) { auto m = mem(i); return m ? m->ping : -1; }
int bb_netmemberishost(int i) { auto m = mem(i); return m && m->host ? 1 : 0; }
int bb_netmemberisme(int i) { auto m = mem(i); return m && m->me ? 1 : 0; }
void bb_netsetready(int r) { if (net() && net()->state == NET_IN_ROOM) net()->setReady(r != 0); }
int bb_netisready() { auto m = net() ? net()->me() : nullptr; return m && m->ready ? 1 : 0; }
int bb_netcanstart() { return net() && net()->canStart() ? 1 : 0; }
void bb_netstart() { if (net() && net()->canStart()) net()->startMatch(); }
int bb_netcountdown() {
  if (!net() || net()->state != NET_STARTING) return -1;
  int64_t left = net()->countdownEndMs - net_now_ms();
  return left <= 0 ? 0 : (int)((left + 999) / 1000);
}
int bb_netmatchstarted() { return net() && net()->state == NET_IN_MATCH ? 1 : 0; }
int bb_netmatchseed() { return net() ? (int)net()->matchSeed : 0; }
int bb_netlocalslot() { return net() ? net()->localSlot() : 0; }

void bb_netsendgame(const BStr &data) { if (net() && net()->inRoom()) net()->sendGame(data); }
BStr bb_netrecvgame() {
  if (!net() || net()->inbox.empty()) return "";
  NetGameMsg m = net()->inbox.front();
  net()->inbox.pop_front();
  g_last_sender = m.slot;
  return m.data;
}
int bb_netrecvsender() { return g_last_sender; }

// ------------------------------------------------------------------ hero / stage choice
int bb_netherosupported() { return net() && net()->supportsHeroes() ? 1 : 0; }
void bb_netsethero(int hero) { if (net()) net()->setHero(hero); }
int bb_netmyhero() { return net() ? net()->myHero : 0; }
int bb_netmemberhero(int i) { auto m = mem(i); return m ? m->hero : 0; }
void bb_netsetmap(int map) { if (net() && net()->isHost()) net()->setMap(map); }
int bb_netmatchmap() { return net() ? net()->matchMap : 0; }
void bb_netsetlives(int lives) { if (net() && net()->isHost()) net()->setLives(lives < 1 ? 1 : lives > 9 ? 9 : lives); }
int bb_netmatchlives() { return net() && net()->matchLives >= 1 ? net()->matchLives : 3; }

// ------------------------------------------------------------------ lockstep netplay
// Every phone runs the same deterministic simulation (same seed, same stage, same characters)
// and only the players' buttons travel over the network: each frame a player sends the input
// it will use "delay" frames later, and nobody simulates frame F before it has the input of
// every human player for F. Messages ("I<token>:<frame>,<mask>" and "C<token>:<frame>,<sum>")
// go through NetBackend::sendGame, so it works with every backend (Epic P2P, VPS relay).
namespace {
struct NetPlay {
  bool on = false;
  std::string token;                          // match id: messages of older matches are ignored
  int players = 0, local = 0, delay = 4;
  std::vector<std::string> ids;               // slot -> member id, fixed for the whole match
  std::vector<bool> human, active;            // active = we still wait for this slot's input
  std::vector<std::map<int, int>> masks;      // slot -> frame -> mask
  std::map<int, uint32_t> myCheck;            // frame -> local checksum
  std::map<std::pair<int, int>, uint32_t> peerCheck;  // (slot, frame) -> checksum
  int desyncFrame = -1;
  int64_t lastPacketMs = 0;
} g_np;
std::vector<NetMember> g_np_members;          // players frozen when the match started

int np_slot_of(const std::string &id) {
  for (int s = 1; s <= g_np.players; s++) if (g_np.ids[s] == id) return s;
  return 0;
}

void np_compare(int slot, int frame) {
  auto a = g_np.myCheck.find(frame);
  auto b = g_np.peerCheck.find({slot, frame});
  if (a == g_np.myCheck.end() || b == g_np.peerCheck.end()) return;
  if (a->second != b->second && (g_np.desyncFrame < 0 || frame < g_np.desyncFrame)) {
    g_np.desyncFrame = frame;
    char buf[160];
    snprintf(buf, sizeof buf, "netplay: OUT OF SYNC with slot %d at frame %d (%08x vs %08x)", slot, frame,
             a->second, b->second);
    plat_log(buf);
  }
  g_np.peerCheck.erase(b);
}

void np_poll() {
  if (!net()) return;
  net()->update(net_now_ms());
  auto &in = net()->inbox;
  while (!in.empty()) {
    NetGameMsg m = in.front();
    in.pop_front();
    if (!g_np.on || m.data.size() < 3) continue;
    char kind = m.data[0];
    size_t colon = m.data.find(':'), comma = m.data.find(',');
    if (colon == std::string::npos || comma == std::string::npos || comma < colon) continue;
    if (m.data.compare(1, colon - 1, g_np.token) != 0) continue;
    int slot = np_slot_of(m.from);
    if (slot == 0 || slot == g_np.local) continue;
    int frame = atoi(m.data.c_str() + colon + 1);
    long long v = atoll(m.data.c_str() + comma + 1);
    g_np.lastPacketMs = net_now_ms();
    if (kind == 'I') g_np.masks[slot][frame] = (int)v;
    else if (kind == 'C') { g_np.peerCheck[{slot, frame}] = (uint32_t)v; np_compare(slot, frame); }
  }
}

void np_send(char kind, int frame, long long v) {
  if (!net()) return;
  net()->sendGame(std::string(1, kind) + g_np.token + ":" + std::to_string(frame) + "," + std::to_string(v));
}
}  // namespace

// input delay in frames for this phone, from the worst ping in the room (one way + margin)
int bb_netplaydelay() {
  int worst = net() ? net()->ping : -1;
  if (net()) for (auto &m : net()->members) if (m.ping > worst) worst = m.ping;
  if (worst < 0) worst = 60;
  int d = (int)((worst / 2 + 40) / 18.5) + 1;     // the game runs at 54 frames per second
  return d < 3 ? 3 : d > 12 ? 12 : d;
}

// freeze the player list (slot order) at the moment the match starts
void bb_netplayprepare() { g_np_members = net() ? net()->members : std::vector<NetMember>(); }

int bb_netplaybegin(int delay) {
  g_np = NetPlay();
  if (!net()) return 1;
  if (g_np_members.empty()) bb_netplayprepare();
  g_np.on = true;
  g_np.token = std::to_string(net()->matchSeed);
  g_np.delay = delay;
  g_np.players = (int)std::min<size_t>(g_np_members.size(), 4);
  g_np.ids.assign(g_np.players + 1, std::string());
  g_np.human.assign(g_np.players + 1, false);
  g_np.active.assign(g_np.players + 1, false);
  g_np.masks.assign(g_np.players + 1, std::map<int, int>());
  for (int s = 1; s <= g_np.players; s++) {
    const NetMember &m = g_np_members[s - 1];
    g_np.ids[s] = m.id;
    g_np.human[s] = !m.bot;
    g_np.active[s] = !m.bot;
    if (m.me) g_np.local = s;
  }
  g_np.lastPacketMs = net_now_ms();
  // the first frames have no input yet: send "nothing pressed" for them
  for (int f = 0; f < delay; f++) bb_netplaysend(f, 0);
  char buf[96];
  snprintf(buf, sizeof buf, "netplay: %d players, local slot %d, delay %d frames", g_np.players, g_np.local, delay);
  plat_log(buf);
  return g_np.local;
}
int bb_netplayplayers() { return g_np.players; }
int bb_netplayhuman(int slot) { return slot >= 1 && slot <= g_np.players && g_np.human[slot] ? 1 : 0; }
void bb_netplaysend(int frame, int mask) {
  if (!g_np.on || g_np.local == 0) return;
  g_np.masks[g_np.local][frame] = mask;
  np_send('I', frame, mask);
}
int bb_netplayready(int frame) {
  if (!g_np.on) return 1;
  np_poll();
  for (int s = 1; s <= g_np.players; s++)
    if (g_np.active[s] && !g_np.masks[s].count(frame)) return 0;
  return 1;
}
int bb_netplaymask(int slot, int frame) {
  if (!g_np.on || slot < 1 || slot > g_np.players) return 0;
  auto &mm = g_np.masks[slot];
  auto it = mm.find(frame);
  int v = it == mm.end() ? 0 : it->second;
  // forget old frames
  while (!mm.empty() && mm.begin()->first < frame - 300) mm.erase(mm.begin());
  return v;
}
void bb_netplaysetcpu(int slot) { if (slot >= 1 && slot <= g_np.players) g_np.active[slot] = false; }
void bb_netplaycheck(int frame, int sum) {
  if (!g_np.on) return;
  g_np.myCheck[frame] = (uint32_t)sum;
  while (g_np.myCheck.size() > 64) g_np.myCheck.erase(g_np.myCheck.begin());
  np_send('C', frame, (uint32_t)sum);
  static bool log = getenv("MH_NPLOG") != nullptr;
  if (log) {
    char buf[80];
    snprintf(buf, sizeof buf, "netplay check frame %d = %08x", frame, (uint32_t)sum);
    plat_log(buf);
  }
  for (int s = 1; s <= g_np.players; s++) np_compare(s, frame);
}
int bb_netplaydesync() { return g_np.desyncFrame; }
// ms since the last packet from any player (to show "waiting" / give up)
int bb_netplaysilentms() { return g_np.on ? (int)(net_now_ms() - g_np.lastPacketMs) : 0; }
// 1 when a player we still wait for is no longer in the room (left or lost the connection)
int bb_netplaypeergone() {
  if (!g_np.on || !net()) return 0;
  for (int s = 1; s <= g_np.players; s++) {
    if (!g_np.active[s] || s == g_np.local) continue;
    bool found = false;
    for (auto &m : net()->members) if (m.id == g_np.ids[s]) found = true;
    if (!found) return 1;
  }
  return 0;
}
void bb_netplayend() { g_np = NetPlay(); g_np_members.clear(); }
