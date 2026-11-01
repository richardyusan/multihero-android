# MultiHero lobby server

| File | Purpose |
|---|---|
| `lobby_server.py` | Lobby + relay server (Python 3.8+, no dependencies). `python3 lobby_server.py --port 7777` |
| `multihero-lobby.service` | systemd unit to keep it running on a Linux VPS |
| `test_bot.py` | A fake second player for testing with one phone (`--create` to host a room) |

Deployment on an Oracle Cloud "Always Free" VM, the protocol and how to point the game at the
server are described in [../docs/ONLINE.md](../docs/ONLINE.md).

Quick local test (PC):

```bash
python3 lobby_server.py --port 7777 &
python3 test_bot.py --create          # creates "testbot's room"
```
