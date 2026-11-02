#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""
MultiHero lobby + relay server (protocol version 1).

Runs anywhere with Python 3.8+, no extra packages. Made for a small free VPS such as
Oracle Cloud "Always Free". See server/README.md for deployment.

    python3 lobby_server.py --port 7777

Protocol: one message per line, fields separated by TAB (see runtime/net/net_relay.cpp).
The server keeps the room list, ready flags and pings, starts matches, and relays game
messages (MSG) between the players of a room.
"""
import argparse
import asyncio
import logging
import random
import time

PROTO = 1
MAX_NAME = 16
MAX_LINE = 64 * 1024
IDLE_TIMEOUT = 30          # seconds without any message -> disconnect
MAX_ROOMS = 200

log = logging.getLogger("mh-lobby")


def clean(s, limit=MAX_NAME):
    s = "".join(ch for ch in s if ch >= " " and ch != "\x7f")
    return s[:limit].strip() or "player"


class Client:
    def __init__(self, cid, writer):
        self.id = str(cid)
        self.writer = writer
        self.name = "player"
        self.ping = -1
        self.ready = False
        self.room = None
        self.hello = False
        self.last_seen = time.monotonic()

    def send(self, *fields):
        line = "\t".join(str(f) for f in fields) + "\n"
        try:
            self.writer.write(line.encode("utf-8", "replace"))
        except Exception:
            pass


class Room:
    def __init__(self, rid, name, host, max_players):
        self.id = str(rid)
        self.name = name
        self.host = host
        self.max = max_players
        self.members = [host]
        self.playing = False

    @property
    def open(self):
        return not self.playing and len(self.members) < self.max


class Lobby:
    def __init__(self):
        self.clients = {}
        self.rooms = {}
        self.next_id = 1

    def new_id(self):
        self.next_id += 1
        return self.next_id

    # ------------------------------------------------------------- helpers
    def send_rooms(self, c):
        rooms = [r for r in self.rooms.values()]
        c.send("ROOMS", len(rooms))
        for r in rooms:
            c.send("ROOM", r.id, r.name, r.host.name, len(r.members), r.max,
                   1 if r.open else 0, r.host.ping)

    def send_members(self, room):
        for c in room.members:
            c.send("MEMBERS", len(room.members))
            for m in room.members:
                c.send("MEMBER", m.id, m.name, 1 if m.ready else 0, m.ping,
                       1 if m is room.host else 0)

    def leave_room(self, c, notify=True):
        room = c.room
        if not room:
            return
        room.members.remove(c)
        c.room = None
        c.ready = False
        if notify:
            c.send("LEFT")
        if not room.members:
            del self.rooms[room.id]
            log.info("room %s closed", room.name)
            return
        if room.host is c:
            room.host = room.members[0]
            room.host.ready = False
        if room.playing and len(room.members) < 2:
            room.playing = False
        self.send_members(room)

    # ------------------------------------------------------------- commands
    def handle(self, c, f):
        cmd = f[0]
        arg = lambda i, d="": f[i] if i < len(f) else d
        if cmd == "HELLO":
            c.name = clean(arg(1))
            if arg(2) != str(PROTO):
                c.send("ERROR", "server speaks protocol %d, please update the game" % PROTO)
                return
            c.hello = True
            c.send("WELCOME", c.id, len(self.clients))
            log.info("hello %s (%s)", c.name, c.id)
            return
        if not c.hello:
            c.send("ERROR", "send HELLO first")
            return
        if cmd == "PING":
            if arg(2).lstrip("-").isdigit():
                c.ping = int(arg(2))
            c.send("PONG", arg(1), len(self.clients))
        elif cmd == "LIST":
            self.send_rooms(c)
        elif cmd == "CREATE":
            if c.room:
                self.leave_room(c, notify=False)
            if len(self.rooms) >= MAX_ROOMS:
                c.send("ERROR", "too many rooms on this server")
                return
            mx = int(arg(2, "4")) if arg(2, "4").isdigit() else 4
            room = Room(self.new_id(), clean(arg(1), 24), c, max(2, min(4, mx)))
            self.rooms[room.id] = room
            c.room, c.ready = room, False
            c.send("JOINED", room.id, room.name)
            self.send_members(room)
            log.info("%s created room %s", c.name, room.name)
        elif cmd == "JOIN":
            room = self.rooms.get(arg(1))
            if not room:
                c.send("ERROR", "room no longer exists")
                self.send_rooms(c)
                return
            if not room.open:
                c.send("ERROR", "room is full or already playing")
                return
            if c.room:
                self.leave_room(c, notify=False)
            room.members.append(c)
            c.room, c.ready = room, False
            c.send("JOINED", room.id, room.name)
            self.send_members(room)
        elif cmd == "LEAVE":
            self.leave_room(c)
        elif cmd == "READY":
            if c.room and not c.room.playing:
                c.ready = arg(1) == "1"
                self.send_members(c.room)
        elif cmd == "START":
            room = c.room
            if not room or room.host is not c:
                c.send("ERROR", "only the host can start")
            elif len(room.members) < 2 or not all(m.ready for m in room.members):
                c.send("ERROR", "everybody must be ready")
            else:
                room.playing = True
                seed = random.randint(1, 2**31 - 1)
                for m in room.members:
                    m.send("START", seed)
                log.info("room %s started (%d players)", room.name, len(room.members))
        elif cmd == "MSG":
            if c.room:
                for m in c.room.members:
                    if m is not c:
                        m.send("MSG", c.id, arg(1))
        else:
            c.send("ERROR", "unknown command " + cmd[:20])

    # ------------------------------------------------------------- connections
    async def serve_client(self, reader, writer):
        c = Client(self.new_id(), writer)
        self.clients[c.id] = c
        peer = writer.get_extra_info("peername")
        log.info("connect %s from %s", c.id, peer)
        try:
            while True:
                try:
                    raw = await asyncio.wait_for(reader.readline(), IDLE_TIMEOUT)
                except asyncio.TimeoutError:
                    break
                if not raw or len(raw) > MAX_LINE:
                    break
                c.last_seen = time.monotonic()
                line = raw.decode("utf-8", "replace").rstrip("\r\n")
                if line:
                    self.handle(c, line.split("\t"))
                await writer.drain()
        except (ConnectionError, asyncio.IncompleteReadError):
            pass
        finally:
            self.leave_room(c, notify=False)
            self.clients.pop(c.id, None)
            try:
                writer.close()
            except Exception:
                pass
            log.info("disconnect %s (%s)", c.id, c.name)


async def main():
    ap = argparse.ArgumentParser(description="MultiHero lobby/relay server")
    ap.add_argument("--host", default="0.0.0.0")
    ap.add_argument("--port", type=int, default=7777)
    ap.add_argument("--verbose", action="store_true")
    a = ap.parse_args()
    logging.basicConfig(level=logging.DEBUG if a.verbose else logging.INFO,
                        format="%(asctime)s %(message)s")
    lobby = Lobby()
    server = await asyncio.start_server(lobby.serve_client, a.host, a.port)
    log.info("MultiHero lobby server (protocol %d) listening on %s:%d", PROTO, a.host, a.port)
    async with server:
        await server.serve_forever()


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        pass
