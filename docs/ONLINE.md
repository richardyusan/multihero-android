# Multiplayer online

Main menu → **MULTIPLAYER ONLINE** opens a lobby: room list with ping and player count, create /
join room, READY, START (host), countdown.

> **Status (v1.4.0):** lobby **and real-time online matches** through **Epic Online Services**
> (P2P, no own server) or your own VPS relay. See [Real-time online matches](#real-time-online-matches-netplay).

## Architecture

```
src/online.bb            lobby screen (Blitz, uses the game's font/buttons/pointers)
      │  Net* functions, declared in runtime/decls/net.decls
      ▼
runtime/net/net.cpp      reads online.cfg, picks a backend, implements the Net* functions
      │  class NetBackend (runtime/net/net.h)
      ├── net_demo.cpp   "demo (offline)": fake rooms and bots, no network - UI testing
      ├── net_relay.cpp  "vps relay": TCP client for server/lobby_server.py   (working)
      └── net_eos.cpp    "epic online": Epic Online Services Lobby + P2P      (working)
```

The Blitz code never knows which service is used, so adding a backend only touches `runtime/net/`.

### Choosing the backend: `config/online.cfg`

The file is copied into the APK assets. A copy in the app's data folder overrides it.
`config/online.local.cfg` (not in git, for secrets) is read afterwards and overrides single keys.

| Key | Meaning |
|---|---|
| `backend` | `auto` (relay if `server` is set, otherwise demo), `demo`, `relay`, `eos` |
| `server`, `port` | lobby server address for `relay` (default port 7777) |
| `name` | player name; empty = random `player1234`, remembered on the device |
| `eos_*` | Epic Online Services credentials for `eos` |

With the default file (no server), the lobby runs in **demo** mode and says so in the info bar.

## Option A – own server on an Oracle Cloud free VPS (ready to use)

`server/lobby_server.py` is a single-file Python 3 server with no dependencies. It keeps the room
list, ready flags and pings, starts matches with a shared seed, and relays game messages between
the players of a room. One small VM handles hundreds of players in the lobby.

### 1. Create the VM

1. Sign up at cloud.oracle.com ("Always Free" resources need no payment, but a card is asked for verification).
2. **Compute → Instances → Create instance**: image *Canonical Ubuntu 22.04/24.04*, shape
   *VM.Standard.E2.1.Micro* (AMD, always free) or *VM.Standard.A1.Flex* (Ampere, always free).
   Download the SSH key.
3. Note the **public IP**.

### 2. Open the port (two places)

1. **Networking → Virtual cloud networks → your VCN → Security lists → Default → Add ingress rule**:
   source `0.0.0.0/0`, IP protocol TCP, destination port `7777`.
2. Oracle's Ubuntu images also block ports in the VM's own firewall:
   ```bash
   sudo iptables -I INPUT 6 -m state --state NEW -p tcp --dport 7777 -j ACCEPT
   sudo netfilter-persistent save
   ```

### 3. Install and run the server

```bash
ssh -i oracle.key ubuntu@<public-ip>
sudo mkdir -p /opt/multihero && sudo chown ubuntu /opt/multihero
# copy server/lobby_server.py to /opt/multihero (scp, or paste it)
python3 /opt/multihero/lobby_server.py --port 7777        # quick test, Ctrl+C to stop
```

Run it permanently with systemd (file included as `server/multihero-lobby.service`):

```bash
sudo cp multihero-lobby.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now multihero-lobby
journalctl -u multihero-lobby -f          # live log
```

### 4. Point the game at it

Edit `config/online.cfg` before building:

```
backend = relay
server = <public-ip or domain>
port = 7777
```

Rebuild the APK. Test with one phone by running the test player on your PC:

```bash
python3 server/test_bot.py --server <public-ip> --create     # bot creates a room, the phone joins it
python3 server/test_bot.py --server <public-ip>              # bot joins the phone's room and gets ready
```

### Protocol (version 1)

Plain text over TCP, one message per line, fields separated by TAB.

| Client → server | Server → client |
|---|---|
| `HELLO name proto` | `WELCOME id online` |
| `LIST` | `ROOMS n` + n × `ROOM id name host players max open hostping` |
| `CREATE name max` | `JOINED roomid roomname` |
| `JOIN roomid` | `MEMBERS n` + n × `MEMBER id name ready ping host` (sent on every change) |
| `LEAVE` | `LEFT` |
| `READY 0/1` | `START seed` (host started, everybody ready, 2+ players) |
| `START` | `PONG clientms online` |
| `PING clientms lastrtt` | `MSG fromid hexdata` (relayed game data) |
| `MSG hexdata` | `ERROR text` |

Ping shown in the room list = your ping to the server + the host's ping to the server (the path
game data takes through the relay).

Security notes: there are no accounts or passwords; names are free text (max 16 chars) and rooms
are public. Messages are not encrypted. Fine for playing with friends; put it behind TLS
(e.g. stunnel) and add authentication before a public launch.

## Option B – Epic Online Services P2P (working, Android arm64 / x86_64)

EOS is free and provides lobbies plus peer-to-peer connections with NAT traversal and Epic's relay
servers, so no own server is needed. Players don't need an Epic account: the game logs in with the
**Connect** interface and an anonymous **device id** (created on first start, kept on the phone).

### What `runtime/net/net_eos.cpp` does

| Lobby feature | EOS |
|---|---|
| login | `EOS_Connect_CreateDeviceId` → `EOS_Connect_Login` (device id token); first time `EOS_Connect_CreateUser` |
| room list | `EOS_LobbySearch` on bucket `multihero:1`, refreshed every 4 s; room name / host / started from lobby attributes |
| create / join / leave | `EOS_Lobby_CreateLobby` (public, max 2–4, host migration on), `JoinLobby`, `LeaveLobby` |
| member list, READY | member attributes `NAME`, `READY`, `PING`; lobby attributes `ROOMNAME`, `HOSTNAME`, `STARTED`, `SEED` |
| ping | P2P socket `MHGAME` channel 0: every 2 s each member sends `P<time>` to the host and gets `Q<time>` back; the result is published as the `PING` member attribute |
| START | host sets `SEED` and `STARTED=1`; every member sees the lobby update and starts the 3-second countdown with the same seed |
| game data | `NetSendGame` → P2P channel 1, reliable ordered; received into `NetRecvGame$` |

Lobby updates are sent one at a time and each carries all of the player's member attributes, so a late
update can never undo a newer READY state; failed updates are retried. EOS is initialised once per
process, ticked every frame (also during matches, `net_background_tick`) and shut down at exit.

### Credentials (kept out of git)

1. dev.epicgames.com/portal → your product → **Product Settings**: product id, sandbox id, deployment id.
2. **Clients**: a client with a policy that allows P2P and lobbies (e.g. *Peer2Peer*) → client id + secret.
3. Copy `config/online.local.cfg.example` to `config/online.local.cfg` and fill it in:
   ```
   backend = eos
   eos_product_id = ...
   eos_sandbox_id = ...
   eos_deployment_id = ...
   eos_client_id = ...
   eos_client_secret = ...
   ```
   `online.local.cfg` is in `.gitignore` (the client secret must not be published). It is packed into
   the APK next to `online.cfg` and overrides it key by key. Note: anybody can unpack an APK, so the
   secret only keeps honest people out; Epic's P2P client policy limits what it can be used for.

### SDK setup (once)

The EOS SDK is free but may not be redistributed, so it is not in this repository.

1. Download the **EOS SDK for Android** (and optionally the C SDK for Linux tests) from the
   developer portal → *SDK Download*. Tested with **v1.19.2.1**.
2. Unpack the parts the build needs and fetch the Android libraries the EOS Java code uses
   (androidx.browser, androidx.security-crypto, androidx.core 1.6.0, Tink …):
   ```bash
   python3 tools/setup_eos.py EOS-SDK-Android-58105819-Release-v1.19.2.1.zip EOS-SDK-58105819-Release-v1.19.2.1.zip
   ```
   → `third_party/eos/SDK/` and `third_party/android-deps/` (both ignored by git).
3. Android build-tools **35** or newer (`sdkmanager "build-tools;35.0.0"`): `d8` from build-tools 34
   crashes on the EOS classes.
4. `./build_android.sh` finds the SDK automatically (`EOS_SDK=…` to use another folder, `NO_EOS=1` to
   skip it). It then:
   - compiles the network code with `-DMH_WITH_EOS` and links `libEOSSDK.so` for **arm64-v8a and
     x86_64** (Epic ships no 32-bit ARM library: `armeabi-v7a` phones get the demo lobby with the note
     "epic sdk not built in"),
   - dexes the EOS classes and androidx libraries, compiles their resources and generates their `R`
     classes,
   - adds EOS's manifest entries (`EOSAuthHandlerActivity`, browser queries, `ACCESS_NETWORK_STATE`,
     `ACCESS_WIFI_STATE`) and the required `eos_login_protocol_scheme` (= `eos.` + client id in lower
     case) and `eos_customTabToolbarColor` resources.

`MHActivity` loads `libEOSSDK.so` from Java (its `JNI_OnLoad` stores the Java VM) and calls
`EOSSDK.init(activity)`; both are wrapped so builds without EOS still start. On Android
`EOS_Initialize` gets `EOS_Android_InitializeOptions` with the app's data folder.

The APK grows by about 20 MB (Epic's library is ~25–30 MB per ABI before compression).

### Testing on a PC

The same backend builds on Linux with the C SDK (`libEOSSDK-Linux-Shipping.so`). Two copies with a
different `HOME` get different device ids, so one PC can play host and guest against Epic's real
servers:

```bash
g++ -std=c++17 -O2 -fwrapv -w -DMH_WITH_EOS -Iruntime -Ithird_party/eos/SDK/Include build/mh.cpp \
    runtime/bbrt.cpp runtime/mh_glue.cpp runtime/linux_main.cpp runtime/net/*.cpp -o build/mh_eos \
    -lpthread -Lthird_party/eos/SDK/Bin -l:libEOSSDK-Linux-Shipping.so -Wl,-rpath,third_party/eos/SDK/Bin
```

Verified this way (v1.3.0): login, room list, create / join, READY both ways, ping (~16 ms), START
countdown on both sides with the same seed, P2P game messages in both directions, clean exit.

### Logs on the phone

```bash
adb logcat -s MultiHero
```
EOS warnings and errors appear as `EOS[<category>] …`; lobby errors are also shown in the lobby's
info bar (e.g. `login: EOS_InvalidAuth` = wrong client id / secret or policy).

## Adding another backend

1. Create `runtime/net/net_mything.cpp` with a class derived from `NetBackend` (`net.h`) and
   implement: `connect`, `disconnect`, `update` (called every frame), `refreshRooms`,
   `createRoom`, `joinRoom`, `leaveRoom`, `setReady`, `startMatch`, `sendGame`.
2. Keep the shared fields up to date: `state`, `ping`, `onlineCount`, `rooms`, `members`
   (with `me`/`host`/`ready`/`ping`), `roomName`, `lastError`; call `beginCountdown(seed, now)`
   when the match starts and push received game data to `inbox`.
3. Register it in `net_create_backend()` in `net.cpp` and pick it in `online.cfg`.

`build_android.sh` and `scripts/test_linux.sh` compile every `runtime/net/*.cpp` automatically.

## Real-time online matches (netplay)

Since v1.4.0 the match after START is played **online in real time**: every player controls their
own fighter on their own phone and sees the others move.

**In the room**: tap **[change]** under your name to open the fighter grid and pick one of the 10
characters; the host taps **LIVES** (1–9) and **STAGE** (one of the 15 VS stages or *random*). When the
host presses START (everybody READY) all phones load the same stage. Rules online: death match,
items on, no team attack. During the match the bottom centre shows your slot and the ping (bars +
ms). ESC (the ESC button) twice leaves the match; the CPU then plays on for you.

### How it works: deterministic lockstep

The game logic of MultiHero is deterministic: with the same random seed and the same buttons it
always produces the same fight (the random generator is `mt19937`, the logic never reads the clock,
the native code is compiled with `-ffp-contract=off` so float maths is identical on every CPU).
So, like many fighting games, the phones only exchange **buttons**, not positions:

1. `onlineStartMatch()` (`src/online.bb`) freezes the player list (`NetPlayPrepare`), sets the
   same players, characters, stage and rules on every phone and skips the menus.
2. Before the first frame `npBegin()` seeds the random generator with the room's seed and resets
   AI / hit state left from earlier local games.
3. Every frame, inside `Getinput`, `npSync()` packs the buttons this phone read for player 1
   (touch, keyboard or pad) into a 15-bit mask and sends it for frame *now + delay*. It then waits
   until the masks of all players for the current frame are there (`NetPlayReady`) and writes
   them into each player's key arrays, so every phone simulates exactly the same frame.
4. The delay (3–12 frames, ~55–220 ms) is chosen by each phone from the pings in the room
   (`NetPlayDelay`), so a normal connection never has to wait.
5. Once per second the phones compare a checksum of positions, life and the random generator
   (`NetPlayCheck`); a difference shows **out of sync!** in the corner (should never happen).

Messages (`I<seed>:<frame>,<mask>`, `C<seed>:<frame>,<sum>`) go through `NetSendGame`, i.e. Epic
P2P (reliable, ordered) or the VPS relay – the netplay layer (`runtime/net/net.cpp`) works with any
backend. Demo bots are CPU fighters.

| Situation | What happens |
|---|---|
| A player presses ESC twice | their slot gets a "left" flag at an agreed frame; from that frame the CPU plays it on every phone |
| Connection slow | the game waits and shows *waiting for players… n*; it continues when the data arrives |
| A player disappears (app closed, no network) | after 4 s (player left the room) or 20 s without data: back to the lobby, "connection lost" |
| Match over | normal results screen, then back to the online lobby |

Tested with two clients against Epic's live servers: identical checksums for whole matches with
random button mashing by both players, ESC/leave and lost connection.

Limits: up to 4 players, VS death match only, no rejoin after leaving, the app must stay in the
foreground during a match (in the background the others wait and then stop).
