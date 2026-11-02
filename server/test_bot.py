#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""
Test player for the lobby server: connects, joins the first open room (or creates one),
gets ready and prints everything the server sends. Useful to test the game's lobby with
only one phone.

    python3 test_bot.py --server my-vps.example.com --port 7777 [--name bot] [--create]
"""
import argparse
import socket
import threading
import time


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--server", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=7777)
    ap.add_argument("--name", default="testbot")
    ap.add_argument("--create", action="store_true", help="create a room instead of joining one")
    ap.add_argument("--seconds", type=int, default=0, help="quit after N seconds (0 = never)")
    a = ap.parse_args()

    s = socket.create_connection((a.server, a.port), timeout=10)
    s.settimeout(None)
    f = s.makefile("r", encoding="utf-8", newline="\n")
    lock = threading.Lock()

    def send(*fields):
        with lock:
            s.sendall(("\t".join(str(x) for x in fields) + "\n").encode())

    def pinger():
        while True:
            send("PING", int(time.monotonic() * 1000), 20)
            time.sleep(2)

    send("HELLO", a.name, 1)
    threading.Thread(target=pinger, daemon=True).start()
    if a.seconds:
        threading.Timer(a.seconds, lambda: s.close()).start()
    in_room = False
    rooms_left = 0
    first_open = None
    try:
        for line in f:
            p = line.rstrip("\n").split("\t")
            if p[0] != "PONG":
                print("<", " | ".join(p), flush=True)
            if p[0] == "WELCOME":
                if a.create:
                    send("CREATE", a.name + "'s room", 4)
                else:
                    send("LIST")
            elif p[0] == "ROOMS":
                rooms_left, first_open = int(p[1]), None
                if rooms_left == 0 and not in_room:
                    time.sleep(1)
                    send("LIST")
            elif p[0] == "ROOM":
                rooms_left -= 1
                if p[6] == "1" and first_open is None:
                    first_open = p[1]
                if rooms_left == 0 and not in_room:
                    if first_open:
                        send("JOIN", first_open)
                    else:
                        time.sleep(1)
                        send("LIST")
            elif p[0] == "JOINED":
                in_room = True
                time.sleep(1)
                send("READY", 1)
            elif p[0] == "LEFT":
                in_room = False
                send("LIST")
            elif p[0] == "START":
                send("MSG", "68656c6c6f")      # "hello" in hex, relayed to the others
    except OSError:
        pass


if __name__ == "__main__":
    main()
