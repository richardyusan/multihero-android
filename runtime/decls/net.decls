; net.decls - online multiplayer API used by src/online.bb (implemented in runtime/net/net.cpp)
; Indices are 1-based. Strings are returned as typed by players; use Lower() before pri().
.lib " "

; ---- connection
NetInit%()                 ; read online.cfg, create the backend and connect; 1 = ok
NetShutdown()              ; disconnect (called when leaving the online menu)
NetUpdate()                ; process network traffic - call once per frame
NetBackendName$()          ; "demo", "vps relay", "epic p2p"
NetStatus$()               ; "offline", "connecting", "online", "in room", "starting", "in match", "error"
NetError$()                ; last error message, "" if none
NetPlayerName$()           ; local player name
NetPing%()                 ; round trip to the server/host in ms, -1 if unknown
NetOnlineCount%()          ; players connected to the service

; ---- room list
NetRefresh()               ; ask for a fresh room list
NetRoomCount%()
NetRoomName$(i%)
NetRoomHost$(i%)
NetRoomPlayers%(i%)
NetRoomMax%(i%)
NetRoomPing%(i%)           ; estimated ping to that room's host in ms
NetRoomOpen%(i%)           ; 1 = can be joined (not full, not playing)

; ---- inside a room
NetCreateRoom(name$, maxPlayers%)
NetJoinRoom(i%)
NetLeaveRoom()
NetInRoom%()
NetRoomTitle$()
NetIsHost%()
NetMemberCount%()
NetMemberName$(i%)
NetMemberReady%(i%)
NetMemberPing%(i%)
NetMemberIsHost%(i%)
NetMemberIsMe%(i%)
NetSetReady(ready%)
NetIsReady%()
NetCanStart%()             ; host only: everybody ready and at least 2 players
NetStart()                 ; host only: start the match for the whole room
NetCountdown%()            ; seconds left before the match starts, -1 if not starting
NetMatchStarted%()         ; 1 once the countdown finished
NetMatchSeed%()            ; shared random seed for the match (same on every client)
NetLocalSlot%()            ; this player's slot (1..4) in the match

; ---- game data channel (for future netplay: inputs, state, chat)
NetSendGame(data$)         ; send to everybody else in the room
NetRecvGame$()             ; next received message, "" if none
NetRecvSender%()           ; slot of the sender of the last NetRecvGame$() message

; ---- character / stage choice in the room (Epic and demo backends)
NetHeroSupported%()        ; 1 = players can pick their character in the room
NetSetHero(hero%)          ; my character (1..10), shared with the room
NetMyHero%()
NetMemberHero%(i%)         ; character of member i, 0 = not chosen
NetSetMap(map%)            ; host: stage for the match, 0 = random
NetMatchMap%()             ; stage chosen by the host, 0 = random
NetSetLives(lives%)        ; host: lives per player for the match (1..9)
NetMatchLives%()           ; lives chosen by the host (3 if not set)

; ---- real-time netplay (lockstep): see runtime/net/net.cpp and src/online.bb
NetPlayPrepare()           ; freeze the player list when the match starts (before NetMemberHero etc.)
NetPlayDelay%()            ; input delay in frames suggested by the pings in the room
NetPlayBegin%(delay%)      ; start exchanging inputs; returns this phone's slot
NetPlayPlayers%()          ; players in the match (slots 1..n)
NetPlayHuman%(slot%)       ; 1 = a person plays this slot, 0 = CPU (demo bots)
NetPlaySend(frame%, mask%) ; my buttons for a future frame
NetPlayReady%(frame%)      ; 1 when the buttons of every player for that frame have arrived
NetPlayMask%(slot%, frame%); buttons of a player for a frame
NetPlaySetCpu(slot%)       ; stop waiting for a player (left the match, CPU takes over)
NetPlayCheck(frame%, sum%) ; exchange a game state checksum
NetPlayDesync%()           ; first frame where the checksums differed, -1 = in sync
NetPlaySilentMs%()         ; ms since the last packet from another player
NetPlayPeerGone%()         ; 1 = a player we wait for has left the room
NetPlayEnd()
NetPlayLeftAt%(slot%)      ; frame after which the CPU plays a player who left (-1 = still playing)
NetPlayKicked%()           ; 1 = the others decided that this phone left (offline too long)
NetPlaySlotState%(slot%)   ; 0 none, 1 connected, 2 no data for a while, 3 left (CPU now), 4 CPU bot
NetPlaySlotPing%(slot%)    ; ping shown for that player in ms, -1 = unknown / host
NetPlaySlotName$(slot%)    ; player name of that slot (frozen when the match started)
