// SPDX-License-Identifier: MIT
// ws_client.cpp - WebSocket / HTTP client for the native builds, see ws_client.h.
//
// One thread per connection: connect (+ TLS handshake), then a loop that sends what the game
// queued and reads with a short timeout, so all TLS calls of a connection happen on one thread.
//
// Test hooks (environment, desktop only): MH_PROXY=host:port tunnels through an HTTP proxy
// (CONNECT), MH_CA_FILE=file.pem adds root certificates.
#ifndef __EMSCRIPTEN__
#include "ws_client.h"
#include "../platform.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <random>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET sock_t;
#define MH_BADSOCK INVALID_SOCKET
#define mh_closesock closesocket
#else
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>
typedef int sock_t;
#define MH_BADSOCK (-1)
#define mh_closesock ::close
#endif
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

#ifdef MH_WITH_TLS
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/error.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>
#include <psa/crypto.h>
#include "ca_roots.inc"     // static const char MH_CA_PEM[] (Mozilla roots, from certifi)
#endif

namespace mhws {
namespace {

int64_t now_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch()).count();
}

void log_msg(const std::string &s) { plat_log(("net: " + s).c_str()); }

struct Url {
  bool tls = false;
  std::string host, path;
  int port = 0;
};

bool parse_url(const std::string &u, Url &out) {
  size_t p = u.find("://");
  if (p == std::string::npos) return false;
  std::string scheme = u.substr(0, p);
  for (auto &c : scheme) c = (char)tolower((unsigned char)c);
  if (scheme == "wss" || scheme == "https") out.tls = true;
  else if (scheme == "ws" || scheme == "http") out.tls = false;
  else return false;
  std::string rest = u.substr(p + 3);
  size_t s = rest.find('/');
  std::string hp = s == std::string::npos ? rest : rest.substr(0, s);
  out.path = s == std::string::npos ? "/" : rest.substr(s);
  size_t c = hp.rfind(':');
  if (c != std::string::npos && hp.find(']') == std::string::npos) {
    out.host = hp.substr(0, c);
    out.port = atoi(hp.c_str() + c + 1);
  } else {
    out.host = hp;
    out.port = out.tls ? 443 : 80;
  }
  return !out.host.empty() && out.port > 0;
}

// ------------------------------------------------------------------ TCP
void net_startup() {
#ifdef _WIN32
  static std::once_flag once;
  std::call_once(once, [] { WSADATA d; WSAStartup(MAKEWORD(2, 2), &d); });
#endif
}

void set_nonblock(sock_t s, bool on) {
#ifdef _WIN32
  u_long v = on ? 1 : 0;
  ioctlsocket(s, FIONBIO, &v);
#else
  int f = fcntl(s, F_GETFL, 0);
  fcntl(s, F_SETFL, on ? (f | O_NONBLOCK) : (f & ~O_NONBLOCK));
#endif
}

// 1 readable, 0 timeout, -1 error
int wait_readable(sock_t s, int ms) {
  fd_set rf;
  FD_ZERO(&rf);
  FD_SET(s, &rf);
  timeval tv;
  tv.tv_sec = ms / 1000;
  tv.tv_usec = (ms % 1000) * 1000;
  int r = select((int)s + 1, &rf, nullptr, nullptr, &tv);
  return r > 0 ? 1 : r == 0 ? 0 : -1;
}

sock_t tcp_connect(const std::string &host, int port, int timeoutMs, std::string &err) {
  net_startup();
  addrinfo hints, *res = nullptr;
  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  std::string ps = std::to_string(port);
  if (getaddrinfo(host.c_str(), ps.c_str(), &hints, &res) != 0 || !res) {
    err = "no internet (cannot find " + host + ")";
    return MH_BADSOCK;
  }
  sock_t s = MH_BADSOCK;
  for (addrinfo *a = res; a; a = a->ai_next) {
    s = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
    if (s == MH_BADSOCK) continue;
    set_nonblock(s, true);
    int r = connect(s, a->ai_addr, (int)a->ai_addrlen);
    bool ok = r == 0;
    if (!ok) {
      fd_set wf, ef;
      FD_ZERO(&wf); FD_ZERO(&ef);
      FD_SET(s, &wf); FD_SET(s, &ef);
      timeval tv;
      tv.tv_sec = timeoutMs / 1000;
      tv.tv_usec = (timeoutMs % 1000) * 1000;
      if (select((int)s + 1, nullptr, &wf, &ef, &tv) > 0 && FD_ISSET(s, &wf)) {
        int e = 0;
        socklen_t l = sizeof e;
        getsockopt(s, SOL_SOCKET, SO_ERROR, (char *)&e, &l);
        ok = e == 0;
      }
    }
    if (ok) {
      set_nonblock(s, false);
      int one = 1;
      setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char *)&one, sizeof one);
#ifdef SO_NOSIGPIPE
      setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, (const char *)&one, sizeof one);
#endif
      break;
    }
    mh_closesock(s);
    s = MH_BADSOCK;
  }
  freeaddrinfo(res);
  if (s == MH_BADSOCK) err = "cannot connect to " + host;
  return s;
}

// ------------------------------------------------------------------ TLS
#ifdef MH_WITH_TLS
std::mutex g_ca_mx;
mbedtls_x509_crt g_ca;
bool g_ca_ready = false;

bool ca_init() {
  std::lock_guard<std::mutex> lk(g_ca_mx);
  if (g_ca_ready) return true;
  if (psa_crypto_init() != PSA_SUCCESS) return false;
  mbedtls_x509_crt_init(&g_ca);
  int r = mbedtls_x509_crt_parse(&g_ca, (const unsigned char *)MH_CA_PEM, sizeof MH_CA_PEM);
  if (r < 0) { log_msg("root certificates could not be read"); return false; }
  if (const char *extra = getenv("MH_CA_FILE")) mbedtls_x509_crt_parse_file(&g_ca, extra);
  g_ca_ready = true;
  return true;
}
#endif

// ------------------------------------------------------------------ stream (TCP or TLS)
class Stream {
 public:
  ~Stream() { close(); }

  bool open(const Url &u, std::string &err, int timeoutMs = 8000) {
    std::string chost = u.host;
    int cport = u.port;
    const char *proxy = getenv("MH_PROXY");
    if (proxy && *proxy) {
      std::string p = proxy;
      size_t c = p.rfind(':');
      chost = p.substr(0, c);
      cport = c == std::string::npos ? 8080 : atoi(p.c_str() + c + 1);
    }
    s_ = tcp_connect(chost, cport, timeoutMs, err);
    if (s_ == MH_BADSOCK) return false;
    if (proxy && *proxy) {
      std::string hp = u.host + ":" + std::to_string(u.port);
      std::string req = "CONNECT " + hp + " HTTP/1.1\r\nHost: " + hp + "\r\n\r\n";
      ::send(s_, req.data(), (int)req.size(), MSG_NOSIGNAL);
      std::string resp;
      char ch;
      int64_t t0 = now_ms();
      while (resp.find("\r\n\r\n") == std::string::npos && now_ms() - t0 < timeoutMs) {
        if (wait_readable(s_, 200) <= 0) continue;
        if (::recv(s_, &ch, 1, 0) != 1) break;
        resp += ch;
      }
      if (resp.compare(0, 9, "HTTP/1.1 ") != 0 || resp.compare(9, 3, "200") != 0) {
        err = "proxy refused";
        return false;
      }
    }
    if (!u.tls) return true;
#ifdef MH_WITH_TLS
    if (!ca_init()) { err = "tls setup failed"; return false; }
    tls_ = true;
    mbedtls_ssl_init(&ssl_);
    mbedtls_ssl_config_init(&conf_);
    mbedtls_ctr_drbg_init(&drbg_);
    mbedtls_entropy_init(&ent_);
    if (mbedtls_ctr_drbg_seed(&drbg_, mbedtls_entropy_func, &ent_, (const unsigned char *)"mh", 2) != 0 ||
        mbedtls_ssl_config_defaults(&conf_, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM,
                                    MBEDTLS_SSL_PRESET_DEFAULT) != 0) {
      err = "tls setup failed";
      return false;
    }
    mbedtls_ssl_conf_authmode(&conf_, MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_ca_chain(&conf_, &g_ca, nullptr);
    mbedtls_ssl_conf_rng(&conf_, mbedtls_ctr_drbg_random, &drbg_);
    mbedtls_ssl_conf_read_timeout(&conf_, (uint32_t)timeoutMs);
    if (mbedtls_ssl_setup(&ssl_, &conf_) != 0 || mbedtls_ssl_set_hostname(&ssl_, u.host.c_str()) != 0) {
      err = "tls setup failed";
      return false;
    }
    mbedtls_ssl_set_bio(&ssl_, this, bio_send, nullptr, bio_recv_timeout);
    int r;
    while ((r = mbedtls_ssl_handshake(&ssl_)) != 0) {
      if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
      char eb[128];
      mbedtls_strerror(r, eb, sizeof eb);
      uint32_t flags = mbedtls_ssl_get_verify_result(&ssl_);
      err = flags && flags != (uint32_t)-1 ? "secure connection refused (certificate)" : "secure connection failed";
      log_msg(err + ": " + eb);
      return false;
    }
    return true;
#else
    err = "this build has no tls (wss://) support";
    return false;
#endif
  }

  // >0 bytes read, 0 nothing within timeoutMs, -1 closed / error
  int read(char *buf, int n, int timeoutMs) {
    if (s_ == MH_BADSOCK) return -1;
#ifdef MH_WITH_TLS
    if (tls_) {
      mbedtls_ssl_conf_read_timeout(&conf_, (uint32_t)(timeoutMs > 0 ? timeoutMs : 1));
      int r = mbedtls_ssl_read(&ssl_, (unsigned char *)buf, (size_t)n);
      if (r > 0) return r;
      if (r == MBEDTLS_ERR_SSL_TIMEOUT || r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE)
        return 0;
#if defined(MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET)
      if (r == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET) return 0;   // TLS 1.3, not an error
#endif
      return -1;
    }
#endif
    int w = wait_readable(s_, timeoutMs);
    if (w == 0) return 0;
    if (w < 0) return -1;
    int r = (int)::recv(s_, buf, n, 0);
    return r > 0 ? r : -1;
  }

  bool write(const char *p, size_t n) {
    if (s_ == MH_BADSOCK) return false;
    while (n > 0) {
      int r;
#ifdef MH_WITH_TLS
      if (tls_) {
        r = mbedtls_ssl_write(&ssl_, (const unsigned char *)p, n);
        if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
      } else
#endif
        r = (int)::send(s_, p, (int)n, MSG_NOSIGNAL);
      if (r <= 0) return false;
      p += r;
      n -= (size_t)r;
    }
    return true;
  }
  bool write(const std::string &s) { return write(s.data(), s.size()); }

  void close() {
#ifdef MH_WITH_TLS
    if (tls_) {
      mbedtls_ssl_free(&ssl_);
      mbedtls_ssl_config_free(&conf_);
      mbedtls_ctr_drbg_free(&drbg_);
      mbedtls_entropy_free(&ent_);
      tls_ = false;
    }
#endif
    if (s_ != MH_BADSOCK) { mh_closesock(s_); s_ = MH_BADSOCK; }
  }

 private:
  sock_t s_ = MH_BADSOCK;
#ifdef MH_WITH_TLS
  bool tls_ = false;
  mbedtls_ssl_context ssl_;
  mbedtls_ssl_config conf_;
  mbedtls_ctr_drbg_context drbg_;
  mbedtls_entropy_context ent_;

  static int bio_send(void *ctx, const unsigned char *buf, size_t len) {
    Stream *st = (Stream *)ctx;
    int r = (int)::send(st->s_, (const char *)buf, (int)len, MSG_NOSIGNAL);
    return r >= 0 ? r : MBEDTLS_ERR_SSL_INTERNAL_ERROR;
  }
  static int bio_recv_timeout(void *ctx, unsigned char *buf, size_t len, uint32_t timeout) {
    Stream *st = (Stream *)ctx;
    int w = wait_readable(st->s_, (int)timeout);
    if (w == 0) return MBEDTLS_ERR_SSL_TIMEOUT;
    if (w < 0) return MBEDTLS_ERR_SSL_INTERNAL_ERROR;
    int r = (int)::recv(st->s_, (char *)buf, (int)len, 0);
    return r >= 0 ? r : MBEDTLS_ERR_SSL_INTERNAL_ERROR;   // 0 = closed by the server
  }
#endif
};

// read an HTTP response head (status line + headers); extra bytes after it go to rest
bool read_head(Stream &st, std::string &head, std::string &rest, int timeoutMs) {
  int64_t t0 = now_ms();
  char buf[2048];
  std::string acc;
  while (now_ms() - t0 < timeoutMs) {
    int r = st.read(buf, sizeof buf, 100);
    if (r < 0) return false;
    if (r == 0) continue;
    acc.append(buf, (size_t)r);
    size_t e = acc.find("\r\n\r\n");
    if (e != std::string::npos) {
      head = acc.substr(0, e + 2);
      rest = acc.substr(e + 4);
      return true;
    }
    if (acc.size() > 32768) return false;
  }
  return false;
}

int status_of(const std::string &head) {
  // "HTTP/1.1 101 Switching Protocols"
  size_t sp = head.find(' ');
  return sp == std::string::npos ? -1 : atoi(head.c_str() + sp + 1);
}

std::string header_value(const std::string &head, const char *name) {
  std::string lower = head;
  for (auto &c : lower) c = (char)tolower((unsigned char)c);
  std::string key = "\r\n" + std::string(name) + ":";
  for (auto &c : key) c = (char)tolower((unsigned char)c);
  size_t p = lower.find(key);
  if (p == std::string::npos) return std::string();
  p += key.size();
  size_t e = head.find("\r\n", p);
  std::string v = head.substr(p, e == std::string::npos ? std::string::npos : e - p);
  while (!v.empty() && v[0] == ' ') v.erase(0, 1);
  return v;
}

std::string base64(const unsigned char *d, size_t n) {
  static const char *T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string o;
  for (size_t i = 0; i < n; i += 3) {
    uint32_t v = (uint32_t)d[i] << 16 | (i + 1 < n ? (uint32_t)d[i + 1] << 8 : 0) | (i + 2 < n ? d[i + 2] : 0);
    o += T[(v >> 18) & 63];
    o += T[(v >> 12) & 63];
    o += i + 1 < n ? T[(v >> 6) & 63] : '=';
    o += i + 2 < n ? T[v & 63] : '=';
  }
  return o;
}

// ------------------------------------------------------------------ WebSocket connection
struct Conn {
  std::mutex mx;
  std::deque<Event> events;
  std::deque<std::string> out;
  std::atomic<bool> stop{false};
  std::atomic<bool> open{false};
  std::string url;

  void push(Event e) {
    std::lock_guard<std::mutex> lk(mx);
    events.push_back(std::move(e));
  }
  void fail(int code, const std::string &why) {
    Event e;
    e.type = EV_CLOSE;
    e.code = code;
    e.text = why;
    open = false;
    push(e);
  }
};

void send_frame(Stream &st, std::mt19937 &rng, int opcode, const std::string &payload, bool &ok) {
  std::string f;
  f += (char)(0x80 | opcode);
  size_t n = payload.size();
  if (n < 126) f += (char)(0x80 | n);
  else if (n < 65536) { f += (char)(0x80 | 126); f += (char)(n >> 8); f += (char)(n & 255); }
  else {
    f += (char)(0x80 | 127);
    for (int i = 7; i >= 0; i--) f += (char)((uint64_t)n >> (i * 8) & 255);
  }
  uint32_t m = rng();
  unsigned char mk[4] = {(unsigned char)(m >> 24), (unsigned char)(m >> 16), (unsigned char)(m >> 8), (unsigned char)m};
  f.append((const char *)mk, 4);
  for (size_t i = 0; i < n; i++) f += (char)(payload[i] ^ mk[i & 3]);
  if (ok && !st.write(f)) ok = false;
}

void ws_thread(std::shared_ptr<Conn> c) {
  Url u;
  if (!parse_url(c->url, u)) { c->fail(1006, "bad server address"); return; }
  Stream st;
  std::string err;
  if (!st.open(u, err)) { c->fail(1006, err); return; }
  if (c->stop) return;

  std::mt19937 rng((uint32_t)std::random_device{}() ^ (uint32_t)now_ms());
  unsigned char key[16];
  for (auto &k : key) k = (unsigned char)(rng() & 255);
  std::string req = "GET " + u.path + " HTTP/1.1\r\nHost: " + u.host +
                    ((u.tls && u.port == 443) || (!u.tls && u.port == 80) ? "" : ":" + std::to_string(u.port)) +
                    "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: " + base64(key, 16) +
                    "\r\nSec-WebSocket-Version: 13\r\nUser-Agent: MultiHero\r\n\r\n";
  std::string head, rx;
  if (!st.write(req) || !read_head(st, head, rx, 10000)) { c->fail(1006, "no answer from the server"); return; }
  int status = status_of(head);
  if (status != 101) {
    // the server refuses a room that is full (403) or playing (409)
    c->fail(1006, "http " + std::to_string(status));
    return;
  }
  if (c->stop) return;
  c->open = true;
  { Event e; e.type = EV_OPEN; c->push(e); }

  bool ok = true;
  std::string msg;            // text message being assembled from fragments
  int64_t lastData = now_ms();
  char buf[8192];
  while (ok && !c->stop) {
    // send what the game queued
    std::deque<std::string> outq;
    {
      std::lock_guard<std::mutex> lk(c->mx);
      outq.swap(c->out);
    }
    for (auto &t : outq) send_frame(st, rng, 1, t, ok);
    if (!ok) break;

    // parse complete frames
    for (;;) {
      if (rx.size() < 2) break;
      const unsigned char *b = (const unsigned char *)rx.data();
      bool fin = (b[0] & 0x80) != 0;
      int op = b[0] & 15;
      bool masked = (b[1] & 0x80) != 0;
      uint64_t len = b[1] & 127;
      size_t pos = 2;
      if (len == 126) {
        if (rx.size() < 4) break;
        len = (uint64_t)b[2] << 8 | b[3];
        pos = 4;
      } else if (len == 127) {
        if (rx.size() < 10) break;
        len = 0;
        for (int i = 0; i < 8; i++) len = len << 8 | b[2 + i];
        pos = 10;
      }
      if (len > (1u << 20)) { ok = false; break; }
      unsigned char mk[4] = {0, 0, 0, 0};
      if (masked) {
        if (rx.size() < pos + 4) break;
        memcpy(mk, b + pos, 4);
        pos += 4;
      }
      if (rx.size() < pos + len) break;
      std::string pl = rx.substr(pos, (size_t)len);
      if (masked) for (size_t i = 0; i < pl.size(); i++) pl[i] = (char)(pl[i] ^ mk[i & 3]);
      rx.erase(0, pos + (size_t)len);

      if (op == 8) {                                  // close from the server
        int code = pl.size() >= 2 ? ((unsigned char)pl[0] << 8 | (unsigned char)pl[1]) : 1005;
        std::string why = pl.size() > 2 ? pl.substr(2) : std::string();
        bool o2 = true;
        send_frame(st, rng, 8, pl.substr(0, pl.size() >= 2 ? 2 : 0), o2);
        c->fail(code, why);
        return;
      } else if (op == 9) {                           // ping -> pong
        send_frame(st, rng, 10, pl, ok);
      } else if (op == 1 || op == 0) {
        if (op == 1) msg = pl; else msg += pl;
        if (fin) {
          Event e;
          e.type = EV_TEXT;
          e.text.swap(msg);
          c->push(std::move(e));
          msg.clear();
        }
      }
      // binary (2) and pong (10): ignored
    }
    if (!ok) break;

    int r = st.read(buf, sizeof buf, 4);
    if (r < 0) { ok = false; break; }
    if (r > 0) { rx.append(buf, (size_t)r); lastData = now_ms(); }
    else if (now_ms() - lastData > 30000) { ok = false; break; }   // nothing for 30 s: dead
  }
  if (c->stop) {                                       // closed by us: say goodbye
    bool o2 = true;
    std::string p;
    p += (char)(1000 >> 8);
    p += (char)(1000 & 255);
    send_frame(st, rng, 8, p + "bye", o2);
    return;
  }
  c->fail(1006, "connection lost");
}

// ------------------------------------------------------------------ HTTP GET
struct Req {
  std::atomic<bool> done{false};
  int status = -1, ms = -1;
  std::string url, body;
};

void http_thread(std::shared_ptr<Req> q) {
  Url u;
  Stream st;
  std::string err, head, rest;
  if (parse_url(q->url, u) && st.open(u, err)) {
    std::string req = "GET " + u.path + " HTTP/1.1\r\nHost: " + u.host +
                      "\r\nUser-Agent: MultiHero\r\nAccept: */*\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n";
    int64_t t0 = now_ms();   // after connecting: the time of one request, like a ping
    if (st.write(req) && read_head(st, head, rest, 10000)) {
      q->ms = (int)(now_ms() - t0);
      q->status = status_of(head);
      std::string cl = header_value(head, "content-length");
      bool chunked = header_value(head, "transfer-encoding").find("chunked") != std::string::npos;
      size_t want = cl.empty() ? (size_t)-1 : (size_t)atol(cl.c_str());
      char buf[4096];
      int64_t t1 = now_ms();
      while (rest.size() < want && rest.size() < 262144 && now_ms() - t1 < 10000) {
        if (chunked && rest.find("\r\n0\r\n") != std::string::npos) break;
        int r = st.read(buf, sizeof buf, 100);
        if (r < 0) break;
        if (r > 0) rest.append(buf, (size_t)r);
      }
      if (chunked) {                                 // "<hex>\r\n<data>\r\n" ... "0\r\n\r\n"
        std::string body;
        size_t p = 0;
        for (;;) {
          size_t e = rest.find("\r\n", p);
          if (e == std::string::npos) break;
          size_t n = strtoul(rest.c_str() + p, nullptr, 16);
          if (n == 0 || e + 2 + n > rest.size()) break;
          body += rest.substr(e + 2, n);
          p = e + 2 + n + 2;
        }
        q->body = body;
      } else {
        q->body = rest.substr(0, want == (size_t)-1 ? std::string::npos : want);
      }
    }
  }
  q->done = true;
}

std::mutex g_mx;
std::map<int, std::shared_ptr<Conn>> g_conns;
std::map<int, std::shared_ptr<Req>> g_reqs;
int g_next = 1;

}  // namespace

int ws_open(const std::string &url) {
  auto c = std::make_shared<Conn>();
  c->url = url;
  int h;
  {
    std::lock_guard<std::mutex> lk(g_mx);
    h = g_next++;
    g_conns[h] = c;
  }
  std::thread(ws_thread, c).detach();
  return h;
}

bool ws_send(int h, const std::string &text) {
  std::shared_ptr<Conn> c;
  {
    std::lock_guard<std::mutex> lk(g_mx);
    auto it = g_conns.find(h);
    if (it == g_conns.end()) return false;
    c = it->second;
  }
  if (!c->open) return false;
  std::lock_guard<std::mutex> lk(c->mx);
  c->out.push_back(text);
  return true;
}

bool ws_poll(int h, Event &ev) {
  std::shared_ptr<Conn> c;
  {
    std::lock_guard<std::mutex> lk(g_mx);
    auto it = g_conns.find(h);
    if (it == g_conns.end()) return false;
    c = it->second;
  }
  std::lock_guard<std::mutex> lk(c->mx);
  if (c->events.empty()) return false;
  ev = std::move(c->events.front());
  c->events.pop_front();
  return true;
}

void ws_close(int h) {
  std::lock_guard<std::mutex> lk(g_mx);
  auto it = g_conns.find(h);
  if (it == g_conns.end()) return;
  it->second->stop = true;
  g_conns.erase(it);
}

int http_get(const std::string &url) {
  auto q = std::make_shared<Req>();
  q->url = url;
  int h;
  {
    std::lock_guard<std::mutex> lk(g_mx);
    h = g_next++;
    g_reqs[h] = q;
  }
  std::thread(http_thread, q).detach();
  return h;
}

bool http_result(int h, int &status, std::string &body, int &ms) {
  std::lock_guard<std::mutex> lk(g_mx);
  auto it = g_reqs.find(h);
  if (it == g_reqs.end()) { status = -1; ms = -1; body.clear(); return true; }
  if (!it->second->done) return false;
  status = it->second->status;
  body = it->second->body;
  ms = it->second->ms;
  g_reqs.erase(it);
  return true;
}

}  // namespace mhws
#endif
