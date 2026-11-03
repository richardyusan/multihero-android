// SPDX-License-Identifier: MIT
// net_relay.cpp - "vps relay" backend: TCP client for server/lobby_server.py.
//
// Protocol (version 1): one message per line, fields separated by TAB, UTF-8.
//   client -> server                      server -> client
//   HELLO  name proto                     WELCOME id online
//   LIST                                  ROOMS n   (followed by n ROOM lines)
//   CREATE name max                       ROOM id name host players max open hostping
//   JOIN   roomid                         JOINED roomid roomname
//   LEAVE                                 MEMBERS n (followed by n MEMBER lines)
//   READY  0|1                            MEMBER id name ready ping host
//   START                                 LEFT
//   PING   clientms lastrtt               START seed
//   MSG    hexdata                        PONG clientms online
//                                         MSG fromid hexdata
//                                         ERROR text
// Game data sent with MSG is relayed by the server to everybody else in the room. This is
// the hook for real netplay (inputs per frame, state sync), which is not implemented yet.
#include "net.h"

#if defined(__EMSCRIPTEN__) || defined(_WIN32)
// Browsers cannot open plain TCP sockets (and the web build has no threads): the relay needs a
// WebSocket front end there. The Windows build uses Epic online; a Winsock port of this client
// is still to do. net.cpp falls back to the demo backend.
NetBackend *net_create_relay() { return nullptr; }
#else

#include <atomic>
#include <cerrno>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>

#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

namespace {

const int PROTO = 1;

std::vector<std::string> split_tab(const std::string &s) {
  std::vector<std::string> out;
  size_t a = 0;
  for (;;) {
    size_t b = s.find('\t', a);
    out.push_back(s.substr(a, b == std::string::npos ? std::string::npos : b - a));
    if (b == std::string::npos) break;
    a = b + 1;
  }
  return out;
}
std::string field_safe(std::string s) {
  for (auto &c : s) if (c == '\t' || c == '\n' || c == '\r') c = ' ';
  return s;
}
std::string to_hex(const std::string &d) {
  static const char *H = "0123456789abcdef";
  std::string o;
  for (unsigned char c : d) { o += H[c >> 4]; o += H[c & 15]; }
  return o;
}
std::string from_hex(const std::string &h) {
  auto v = [](char c) { return c >= 'a' ? c - 'a' + 10 : (c >= 'A' ? c - 'A' + 10 : c - '0'); };
  std::string o;
  for (size_t i = 0; i + 1 < h.size(); i += 2) o += (char)(v(h[i]) << 4 | v(h[i + 1]));
  return o;
}

// result of the background connect, handed over to the game thread
struct ConnectJob {
  std::atomic<int> done{0};   // 0 running, 1 finished
  int fd = -1;
  std::string error;
};

class NetRelay : public NetBackend {
 public:
  ~NetRelay() override { disconnect(); }
  const char *name() const override { return "vps relay"; }

  void connect() override {
    host_ = cfg.get("server");
    port_ = cfg.get("port", "7777");
    if (host_.empty()) {
      state = NET_ERROR;
      lastError = "no server set in online.cfg";
      return;
    }
    startConnect();
  }

  void disconnect() override {
    if (fd_ >= 0) {
      sendLine("LEAVE");
      flush();
      ::close(fd_);
      fd_ = -1;
    }
    if (job_) {
      if (thread_.joinable()) thread_.detach();   // a slow DNS lookup must not block the UI;
      job_.reset();                               // the thread keeps its own reference
    } else if (thread_.joinable()) {
      thread_.join();
    }
    state = NET_OFFLINE;
    rooms.clear();
    clearRoom();
  }

  void update(int64_t now) override {
    if (job_ && job_->done.load()) {
      if (thread_.joinable()) thread_.join();
      if (job_->fd >= 0 && state == NET_CONNECTING) {
        fd_ = job_->fd;
        sendLine("HELLO\t" + field_safe(playerName) + "\t" + std::to_string(PROTO));
        lastError.clear();
      } else {
        state = NET_ERROR;
        lastError = job_->error;
        retryAt_ = now + 5000;
      }
      job_.reset();
    }
    if (fd_ < 0) {
      if (state == NET_ERROR && retryAt_ && now >= retryAt_) startConnect();
      return;
    }
    readSocket(now);
    if (fd_ < 0) return;
    tickCountdown(now);
    if (now >= nextPing_) {
      sendLine("PING\t" + std::to_string(now) + "\t" + std::to_string(ping));
      nextPing_ = now + 2000;
    }
    if (state == NET_ONLINE && now >= nextList_) {
      sendLine("LIST");
      nextList_ = now + 3000;
    }
    flush();
  }

  void refreshRooms() override { if (state == NET_ONLINE) { sendLine("LIST"); nextList_ = net_now_ms() + 3000; } }
  void createRoom(const std::string &n, int maxP) override {
    if (state == NET_ONLINE) sendLine("CREATE\t" + field_safe(n) + "\t" + std::to_string(maxP));
  }
  void joinRoom(const std::string &id) override { if (state == NET_ONLINE) sendLine("JOIN\t" + id); }
  void leaveRoom() override {
    if (!inRoom()) return;
    sendLine("LEAVE");
    clearRoom();
    state = NET_ONLINE;
    nextList_ = 0;
  }
  void setReady(bool r) override { sendLine(std::string("READY\t") + (r ? "1" : "0")); }
  void startMatch() override { sendLine("START"); }
  void sendGame(const std::string &d) override { sendLine("MSG\t" + to_hex(d)); }

 private:
  std::string host_, port_;
  int fd_ = -1;
  std::shared_ptr<ConnectJob> job_;   // shared with the connect thread
  std::thread thread_;
  std::string inbuf_, outbuf_;
  std::string myId_;
  int64_t nextPing_ = 0, nextList_ = 0, retryAt_ = 0;
  int pendingRooms_ = 0, pendingMembers_ = 0;
  std::vector<NetRoom> newRooms_;
  std::vector<NetMember> newMembers_;

  void startConnect() {
    state = NET_CONNECTING;
    retryAt_ = 0;
    job_ = std::make_shared<ConnectJob>();
    std::shared_ptr<ConnectJob> job = job_;
    std::string host = host_, port = port_;
    // getaddrinfo() blocks, so resolve + connect on a helper thread
    thread_ = std::thread([job, host, port]() {
      addrinfo hints{}, *res = nullptr;
      hints.ai_family = AF_UNSPEC;
      hints.ai_socktype = SOCK_STREAM;
      int rc = getaddrinfo(host.c_str(), port.c_str(), &hints, &res);
      if (rc != 0 || !res) {
        job->error = "cannot resolve " + host;
        job->done = 1;
        return;
      }
      int fd = -1;
      for (addrinfo *a = res; a; a = a->ai_next) {
        fd = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (fd < 0) continue;
        fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
        int r = ::connect(fd, a->ai_addr, a->ai_addrlen);
        if (r < 0 && errno == EINPROGRESS) {
          pollfd p{fd, POLLOUT, 0};
          if (poll(&p, 1, 6000) == 1) {
            int err = 0;
            socklen_t len = sizeof err;
            getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len);
            r = err ? -1 : 0;
          } else r = -1;
        }
        if (r == 0) break;
        ::close(fd);
        fd = -1;
      }
      freeaddrinfo(res);
      if (fd < 0) job->error = "cannot connect to " + host + ":" + port;
      else {
        int one = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
      }
      job->fd = fd;
      job->done = 1;
    });
  }

  void sendLine(const std::string &l) {
    if (fd_ < 0) return;
    outbuf_ += l;
    outbuf_ += '\n';
  }

  void flush() {
    while (fd_ >= 0 && !outbuf_.empty()) {
      ssize_t n = ::send(fd_, outbuf_.data(), outbuf_.size(), MSG_NOSIGNAL);
      if (n > 0) outbuf_.erase(0, n);
      else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
      else { lost("connection lost"); break; }
    }
  }

  void lost(const std::string &why) {
    if (fd_ >= 0) ::close(fd_);
    fd_ = -1;
    inbuf_.clear();
    outbuf_.clear();
    rooms.clear();
    clearRoom();
    state = NET_ERROR;
    lastError = why;
    retryAt_ = net_now_ms() + 5000;
  }

  void readSocket(int64_t now) {
    char buf[4096];
    for (;;) {
      ssize_t n = ::recv(fd_, buf, sizeof buf, 0);
      if (n > 0) { inbuf_.append(buf, n); continue; }
      if (n == 0) { lost("server closed the connection"); return; }
      if (errno == EAGAIN || errno == EWOULDBLOCK) break;
      lost("connection lost");
      return;
    }
    size_t nl;
    while ((nl = inbuf_.find('\n')) != std::string::npos) {
      std::string line = inbuf_.substr(0, nl);
      inbuf_.erase(0, nl + 1);
      if (!line.empty() && line.back() == '\r') line.pop_back();
      handle(split_tab(line), now);
      if (fd_ < 0) return;
    }
  }

  void handle(const std::vector<std::string> &f, int64_t now) {
    const std::string &cmd = f[0];
    auto I = [&](size_t i) { return i < f.size() ? atoi(f[i].c_str()) : 0; };
    auto S = [&](size_t i) { return i < f.size() ? f[i] : std::string(); };
    if (cmd == "WELCOME") {
      myId_ = S(1);
      onlineCount = I(2);
      state = NET_ONLINE;
      nextList_ = 0;
      nextPing_ = 0;
    } else if (cmd == "PONG") {
      ping = (int)(now - atoll(S(1).c_str()));
      onlineCount = I(2);
      for (auto &m : members) if (m.me) m.ping = ping;
    } else if (cmd == "ROOMS") {
      pendingRooms_ = I(1);
      newRooms_.clear();
      if (!pendingRooms_) rooms.clear();
    } else if (cmd == "ROOM" && pendingRooms_ > 0) {
      NetRoom r;
      r.id = S(1); r.name = S(2); r.host = S(3);
      r.players = I(4); r.maxPlayers = I(5); r.open = I(6) != 0;
      int hostPing = I(7);
      // our path to the host goes through the relay: our ping + the host's ping
      r.ping = (ping >= 0 && hostPing >= 0) ? ping + hostPing : -1;
      newRooms_.push_back(r);
      if (--pendingRooms_ == 0) rooms = newRooms_;
    } else if (cmd == "JOINED") {
      roomId = S(1);
      roomName = S(2);
      state = NET_IN_ROOM;
      lastError.clear();
    } else if (cmd == "MEMBERS") {
      pendingMembers_ = I(1);
      newMembers_.clear();
      if (!pendingMembers_) members.clear();
    } else if (cmd == "MEMBER" && pendingMembers_ > 0) {
      NetMember m;
      m.id = S(1); m.name = S(2); m.ready = I(3) != 0; m.ping = I(4); m.host = I(5) != 0;
      m.me = m.id == myId_;
      newMembers_.push_back(m);
      if (--pendingMembers_ == 0) members = newMembers_;
    } else if (cmd == "LEFT") {
      clearRoom();
      state = NET_ONLINE;
      nextList_ = 0;
    } else if (cmd == "START") {
      beginCountdown((uint32_t)atoll(S(1).c_str()), now);
    } else if (cmd == "MSG") {
      int slot = 0;
      for (size_t i = 0; i < members.size(); i++) if (members[i].id == S(1)) slot = (int)i + 1;
      inbox.push_back({slot, from_hex(S(2)), S(1)});
      if (inbox.size() > NET_INBOX_MAX) inbox.pop_front();
    } else if (cmd == "ERROR") {
      lastError = S(1);
    }
  }
};

}  // namespace

NetBackend *net_create_relay() { return new NetRelay(); }

#endif  // __EMSCRIPTEN__
