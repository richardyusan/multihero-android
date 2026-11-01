// SPDX-License-Identifier: MIT
// net_demo.cpp - offline "demo" backend. Simulates a lobby service with a few rooms and bot
// players so the lobby screen can be used and tested without any server. Nothing is sent
// over the network. The backend name shown in the lobby is "demo (offline)".
#include "net.h"
#include <algorithm>
#include <random>

namespace {

const char *BOT_NAMES[] = {"ryu_fan", "toadmaster", "webslinger", "plumber64", "shellshock",
                           "ninjagaiden", "darkknight", "hunter", "saiyan", "vampirekiller"};

class NetDemo : public NetBackend {
 public:
  const char *name() const override { return "demo (offline)"; }

  void connect() override {
    state = NET_CONNECTING;
    connectAt_ = net_now_ms() + 700;    // pretend to log in
    rng_.seed((uint32_t)net_now_ms());
  }
  void disconnect() override {
    state = NET_OFFLINE;
    rooms.clear();
    clearRoom();
  }

  void update(int64_t now) override {
    if (state == NET_CONNECTING && now >= connectAt_) {
      state = NET_ONLINE;
      makeRooms();
      nextTick_ = now;
    }
    if (state == NET_OFFLINE || state == NET_CONNECTING) return;
    tickCountdown(now);
    if (now < nextTick_) return;
    nextTick_ = now + 1000;

    // jittering pings and a slowly changing player count
    ping = 35 + (int)(rng_() % 25);
    onlineCount = std::max(3, onlineCount + (int)(rng_() % 3) - 1);
    for (auto &r : rooms) r.ping = std::max(15, r.ping + (int)(rng_() % 11) - 5);
    for (auto &m : members) m.ping = m.me ? ping : std::max(15, m.ping + (int)(rng_() % 9) - 4);

    if (state == NET_IN_ROOM) simulateRoom(now);
  }

  void refreshRooms() override {
    if (state == NET_ONLINE) makeRooms();
  }

  void createRoom(const std::string &roomName, int maxPlayers) override {
    if (state != NET_ONLINE) return;
    roomId = "demo-mine";
    this->roomName = roomName;
    maxPlayers_ = maxPlayers;
    members.clear();
    NetMember me;
    me.id = "me"; me.name = playerName; me.host = true; me.me = true; me.ping = ping; me.hero = myHero;
    members.push_back(me);
    state = NET_IN_ROOM;
    nextBotJoin_ = net_now_ms() + 1500;
  }

  void joinRoom(const std::string &id) override {
    if (state != NET_ONLINE) return;
    for (auto &r : rooms) {
      if (r.id != id) continue;
      if (!r.open) { lastError = "room is full or playing"; return; }
      roomId = r.id;
      roomName = r.name;
      maxPlayers_ = r.maxPlayers;
      members.clear();
      NetMember host;
      host.id = "bot0"; host.name = r.host; host.host = true; host.ping = r.ping; host.bot = true;
      host.hero = 1 + (int)(rng_() % 10);
      members.push_back(host);
      for (int i = 1; i < r.players; i++) members.push_back(bot(i));
      NetMember me;
      me.id = "me"; me.name = playerName; me.me = true; me.ping = ping; me.hero = myHero;
      members.push_back(me);
      state = NET_IN_ROOM;
      lastError.clear();
      return;
    }
  }

  void leaveRoom() override {
    clearRoom();
    if (state == NET_IN_MATCH || state == NET_STARTING) state = NET_ONLINE;
    makeRooms();
  }

  void setReady(bool r) override {
    for (auto &m : members) if (m.me) m.ready = r;
  }

  void startMatch() override {
    beginCountdown((uint32_t)rng_(), net_now_ms());
  }

  // bots are CPU players in a match, nobody receives game data
  void sendGame(const std::string &) override {}
  bool supportsHeroes() const override { return true; }

 private:
  std::mt19937 rng_;
  int64_t connectAt_ = 0, nextTick_ = 0, nextBotJoin_ = 0;
  int maxPlayers_ = 4;
  int botCounter_ = 0;

  NetMember bot(int i) {
    NetMember b;
    b.id = "bot" + std::to_string(++botCounter_);
    b.name = BOT_NAMES[(botCounter_ + i) % 10];
    b.ping = 30 + (int)(rng_() % 90);
    b.bot = true;                    // played by the CPU when the match starts
    b.hero = 1 + (int)(rng_() % 10);
    return b;
  }

  void makeRooms() {
    rooms.clear();
    const char *names[] = {"lava arena", "rooftop brawl", "castle duel", "ctf training", "anything goes"};
    int count = 3 + (int)(rng_() % 3);
    for (int i = 0; i < count; i++) {
      NetRoom r;
      r.id = "demo-" + std::to_string(i);
      r.name = names[i];
      r.host = BOT_NAMES[(i * 3) % 10];
      r.maxPlayers = (i == 3) ? 2 : 4;
      r.players = 1 + (int)(rng_() % r.maxPlayers);
      r.ping = 25 + (int)(rng_() % 150);
      r.open = r.players < r.maxPlayers && i != 4;
      rooms.push_back(r);
    }
    onlineCount = 0;
    for (auto &r : rooms) onlineCount += r.players;
    onlineCount += 2 + (int)(rng_() % 6);
  }

  // bots join our room and get ready; a bot host starts the match once we are ready
  void simulateRoom(int64_t now) {
    bool iAmHost = isHost();
    if (iAmHost && (int)members.size() < maxPlayers_ && now >= nextBotJoin_ && members.size() < 3) {
      members.push_back(bot((int)members.size()));
      nextBotJoin_ = now + 2500 + (int)(rng_() % 3000);
    }
    for (auto &m : members)
      if (!m.me && !m.ready && rng_() % 3 == 0) m.ready = true;
    if (!iAmHost && state == NET_IN_ROOM) {
      bool all = true;
      for (auto &m : members) if (!m.host && !m.ready) all = false;
      if (all) {
        for (auto &m : members) if (m.host) m.ready = true;
        beginCountdown((uint32_t)rng_(), now);
      }
    }
  }
};

}  // namespace

NetBackend *net_create_demo() { return new NetDemo(); }
