// SPDX-License-Identifier: MIT
// ws_client.h - small WebSocket (RFC 6455) and HTTP GET client for the native builds
// (Android, Windows, Linux). The web build uses the browser's WebSocket instead.
//
// Every connection runs on its own thread; the game thread only queues text to send and
// polls events, so nothing here blocks a frame. wss:// and https:// use mbedTLS (built in
// when MH_WITH_TLS is defined) with the Mozilla root certificates compiled into the program,
// so no system certificate store is needed.
#pragma once
#ifndef __EMSCRIPTEN__
#include <string>

namespace mhws {

enum EventType { EV_OPEN = 1, EV_TEXT = 2, EV_CLOSE = 3 };
struct Event {
  int type = 0;
  std::string text;   // EV_TEXT: the message, EV_CLOSE: the reason
  int code = 0;       // EV_CLOSE: close code (1006 = connection failed / lost)
};

// open a WebSocket (ws://host[:port]/path or wss://...); returns a handle > 0
int ws_open(const std::string &url);
// queue a text message; false when the socket is not open (yet)
bool ws_send(int h, const std::string &text);
// next event of the socket, false when none is waiting
bool ws_poll(int h, Event &ev);
// close the socket and forget the handle (no more events)
void ws_close(int h);

// GET request in the background; returns a handle > 0
int http_get(const std::string &url);
// true once finished: status (-1 = failed), body, time in ms; the handle is freed then
bool http_result(int h, int &status, std::string &body, int &ms);

}  // namespace mhws
#endif
