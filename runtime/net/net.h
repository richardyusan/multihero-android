// SPDX-License-Identifier: MIT
// net.h - online multiplayer abstraction.
//
// The lobby screen (src/online.bb) talks to the Net* functions in net.cpp, which forward to
// one NetBackend. To add a new service, derive from NetBackend, implement the virtual
// functions and register it in net_create_backend() (net.cpp).
//
// Backends shipped:
//   NetDemo   (net_demo.cpp)  - offline simulation with fake rooms and bots, for UI testing
//   NetRelay  (net_relay.cpp) - TCP client for server/lobby_server.py (e.g. an Oracle Cloud free VPS)
//   NetEos    (net_eos.cpp)   - Epic Online Services (Lobby + P2P) skeleton, needs the EOS SDK
#pragma once
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <map>
#include <string>
#include <vector>

enum NetState {
  NET_OFFLINE,      // not connected
  NET_CONNECTING,   // resolving / connecting / logging in
  NET_ONLINE,       // connected, browsing rooms
  NET_IN_ROOM,      // inside a room, waiting for players / ready
  NET_STARTING,     // host pressed start, countdown running
  NET_IN_MATCH,     // countdown finished, match running
  NET_ERROR         // failed; error text in lastError (a backend may retry by itself)
};

struct NetRoom {
  std::string id;          // backend specific room / lobby id
  std::string name;
  std::string host;        // host player name
  int players = 0;
  int maxPlayers = 4;
  int ping = -1;           // estimated ms to the host, -1 = unknown
  bool open = true;        // joinable (not full, not playing)
};

struct NetMember {
  std::string id;          // backend specific user id
  std::string name;
  bool ready = false;
  bool host = false;
  bool me = false;
  bool bot = false;        // simulated player (demo backend): played by the CPU in a match
  int ping = -1;
  int hero = 0;            // chosen character (1..10), 0 = not chosen / not supported
};

struct NetConfig {
  std::map<std::string, std::string> values;   // everything from online.cfg
  std::string get(const std::string &k, const std::string &def = "") const {
    auto it = values.find(k);
    return it == values.end() || it->second.empty() ? def : it->second;
  }
  int getInt(const std::string &k, int def) const {
    std::string v = get(k);
    return v.empty() ? def : atoi(v.c_str());
  }
};

struct NetGameMsg {
  int slot;                // sender slot (1..n), 0 = unknown
  std::string data;
  std::string from;        // sender member id
};

// Received game messages are kept until the game reads them; a match can queue a few
// thousand input packets while a slow phone is still loading the stage.
const size_t NET_INBOX_MAX = 50000;

class NetBackend {
 public:
  virtual ~NetBackend() {}

  // ---- implemented by each backend
  virtual const char *name() const = 0;
  virtual void connect() = 0;                              // start connecting (non-blocking)
  virtual void disconnect() = 0;
  virtual void update(int64_t nowMs) = 0;                  // called every frame
  virtual void refreshRooms() = 0;
  virtual void createRoom(const std::string &roomName, int maxPlayers) = 0;
  virtual void joinRoom(const std::string &roomId) = 0;
  virtual void leaveRoom() = 0;
  virtual void setReady(bool ready) = 0;
  virtual void startMatch() = 0;                           // host only
  virtual void sendGame(const std::string &data) = 0;      // to everybody else in the room
  // optional: character choice (member attribute) and stage choice (host, room attribute)
  virtual bool supportsHeroes() const { return false; }
  virtual void setHero(int hero) { myHero = hero; for (auto &m : members) if (m.me) m.hero = hero; }
  virtual void setMap(int map) { matchMap = map; }
  virtual void setLives(int lives) { matchLives = lives; }

  // ---- shared state, read by the Net* functions
  NetConfig cfg;
  std::string playerName;
  NetState state = NET_OFFLINE;
  std::string lastError;
  int ping = -1;
  int onlineCount = 0;
  std::vector<NetRoom> rooms;
  std::string roomId, roomName;
  std::vector<NetMember> members;
  uint32_t matchSeed = 0;
  int matchMap = 0;                                        // 0 = random (derived from the seed)
  int matchLives = 3;                                      // lives per player, chosen by the host
  int myHero = 0;
  int64_t countdownEndMs = 0;                              // when NET_STARTING ends
  std::deque<NetGameMsg> inbox;

  bool inRoom() const { return state == NET_IN_ROOM || state == NET_STARTING || state == NET_IN_MATCH; }
  const NetMember *me() const {
    for (auto &m : members) if (m.me) return &m;
    return nullptr;
  }
  bool isHost() const { const NetMember *m = me(); return m && m->host; }
  bool canStart() const {
    if (!isHost() || state != NET_IN_ROOM || members.size() < 2) return false;
    for (auto &m : members) if (!m.ready) return false;
    return true;
  }
  int localSlot() const {
    for (size_t i = 0; i < members.size(); i++) if (members[i].me) return (int)i + 1;
    return 0;
  }

 protected:
  // helpers for backends
  void beginCountdown(uint32_t seed, int64_t nowMs, int seconds = 3) {
    matchSeed = seed;
    state = NET_STARTING;
    countdownEndMs = nowMs + seconds * 1000;
  }
  void tickCountdown(int64_t nowMs) {
    if (state == NET_STARTING && nowMs >= countdownEndMs) state = NET_IN_MATCH;
  }
  void clearRoom() {
    roomId.clear();
    roomName.clear();
    members.clear();
    if (inRoom()) state = NET_ONLINE;
  }
};

// net.cpp
NetBackend *net_create_backend(const std::string &kind, std::string &note);
int64_t net_now_ms();
