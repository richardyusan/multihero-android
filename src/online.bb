;=============================================================================
; online.bb - MULTIPLAYER ONLINE lobby (added by the Android port)
;
; Shown when menuOption = 6. All network work is done by the Net* functions
; (runtime/decls/net.decls, implemented in runtime/net/). Which service is used
; (offline demo, own VPS relay server, Epic Online Services P2P) is chosen in
; online.cfg, so this file does not need to change when a backend is added.
;
; Button ids used here: 1-3 bottom row, 4 back, 5 retry, 6 stage, 7 lives, 10-15 room rows,
; 20 my character, 30-39 character picker, 40 close picker.
;
; Real-time play (np* functions at the end of this file): when the host presses START every
; phone loads the same stage with the same characters and random seed, then only the buttons
; of each player travel over the network (lockstep, see runtime/net/net.cpp). Each phone runs
; the whole game itself, so all players see the same fight.
;=============================================================================

Global olActive		;1 while the lobby is open (backend connected)
Global olSel		;selected room in the list (1..n), 0 = none
Global olInfo$		;short message shown above the buttons
Global olInfoTime	;MilliSecs() when olInfo$ was set
Global olWasInRoom	;to detect being kicked out of a room
Global olLastErr$	;last error already shown

;----------- real-time online match state --------------------------------------
Global netPlay		;1 while an online match runs
Global npFrame		;logic frame of the match (same number on every phone)
Global npDelay		;input delay in frames
Global npSlot		;my player number in the match
Global npPlayers	;players in the match
Global npSeed, npMap, npSavedMod
Global npQuitAsk	;MilliSecs() of the first ESC (press again to leave)
Global npLeaving	;I left the match
Global npLost		;connection lost
Global npMsg$		;message for the lobby after the match
Global npLastSync	;MilliSecs() of the last frame that had everybody's input
Dim npActive(4)		;1 = a person still plays this slot (we wait for their input)
Dim olHeroName$(10)
Dim olHeroPic(10)	;standing sprite of each fighter (loaded when the lobby opens)
Global olPicker		;1 while the character picker is open
Global olStagePicker	;1 while the stage picker is open
Dim olMapPic(15)	;stage thumbnails (100x75) and small versions (40x30) for the room
Dim olMapMini(15)
Global npNoticeText$, npNoticeTime	;banner during an online match ("x left the room")
olHeroName$(1)="ryu" : olHeroName$(2)="rash" : olHeroName$(3)="spider-man" : olHeroName$(4)="mario"
olHeroName$(5)="mike" : olHeroName$(6)="gaiden" : olHeroName$(7)="batman" : olHeroName$(8)="predator"
olHeroName$(9)="goku" : olHeroName$(10)="richter"

;----------- entry point, called every frame from the menu loop -------------
Function onlineMenu()

If olActive=0 Then
	NetInit()
	olActive=1 : olSel=0 : olWasInRoom=0
	olSetInfo("")
	If npMsg$ <> "" Then olSetInfo(npMsg$) : npMsg$ = ""
	If NetMyHero()=0 Then NetSetHero(Rand(1,10))
	olPicker=0 : olStagePicker=0
	For i=1 To 15
		If olMapPic(i)=0 Then
			olMapPic(i)=LoadImage("Maps\Original\map_"+i+".jpg")
			If olMapPic(i) <> 0 Then olMapMini(i)=PortResizeImage(olMapPic(i),40,30)
		EndIf
	Next
	For i=1 To 10
		If olHeroPic(i)=0 Then olHeroPic(i)=LoadImage("gfx\"+i+"\zwalk0.bmp")
	Next
EndIf

NetUpdate()
PortScreen(2)	;hide the touch fighting controls, they would cover the lobby buttons
buttonAmount = 70	;lobby buttons 1-7, rows 10-15, my character 20, fighter picker 30-40, stage picker 50-66
pointers

If NetMatchStarted() Then
	onlineStartMatch()
	Return
EndIf

If olWasInRoom=1 And NetInRoom()=0 And NetStatus$()="online" Then olSetInfo("you left the room")
olWasInRoom = NetInRoom()

If NetInRoom() Then
	olRoomScreen()
Else
	olListScreen()
EndIf

DrawImage pointerPic(1),xpointer(1),ypointer(1)	;touch / mouse pointer only

End Function

;----------- ESC / back -------------------------------------------------------
Function onlineBack()
If NetInRoom() Then
	NetLeaveRoom()
Else
	NetShutdown()
	olActive=0
	menuOption=2
EndIf
End Function

;----------- the match was started by the host -------------------------------
; Sets up a VS deathmatch that is identical on every phone (players, characters, stage,
; rules, random seed) and leaves the menu loop; the match itself is synchronised by npSync().
Function onlineStartMatch()
NetPlayPrepare()
npPlayers = NetMemberCount()
If npPlayers > 4 Then npPlayers = 4
npSeed = NetMatchSeed()
npMap = NetMatchMap()
If npMap < 1 Or npMap > 15 Then npMap = 1 + (Abs(npSeed) Mod 15)
npSavedMod = curModId : curModId = 1		;stages of the original game, present on every phone

For i=1 To 4
	zon(i)=0 : zAI(i)=0 : ButSeq(59+i)=0 : zteam(i)=0 : aiLevel(i)=3 : npActive(i)=0
Next
For i=1 To npPlayers
	zon(i)=1 : ButSeq(59+i)=1
	h = NetMemberHero(i)
	If h < 1 Or h > 10 Then h = 1 + ((i-1) Mod 10)
	curGuy(i) = h
Next
zzamount=4
vsMode=1 : gameMode=1 : flagAmount=0 : gameLives=NetMatchLives() : teamAttack=0 : noItems=0 : ifiniteLives=0

netPlay=1
olActive=0
gameStart=1		;leave the menu loop; vsSelectMap() skips the stage screen
End Function

;----------- called before the first frame of the match -------------------------
Function npBegin()
SeedRnd npSeed
npFrame=0 : npLeaving=0 : npLost=0 : npQuitAsk=0
rendert=0 : quake=0 : quakeSeq=0
;forget AI / hit state left over from earlier local games (must be equal on every phone)
For n=1 To 30
	aiTarget(n)=0 : aiCurLevel(n)=3 : NextBlow(n)=0 : aiWalk(n)=0 : aiJumpedRand(n)=0
	For k=1 To 30
		zImune(n,k)=0 : zImuneSeq(n,k)=0 : zImuneTime(n,k)=0 : zImuneTo(n,k)=0
	Next
Next
npDelay = NetPlayDelay()
npSlot = NetPlayBegin(npDelay)
For i=1 To npPlayers
	npActive(i) = NetPlayHuman(i)
	If npActive(i)=0 Then zAI(i)=1		;demo bots
Next
npLastSync = MilliSecs()
End Function

;----------- once per frame, inside Getinput -------------------------------------
; My buttons were read into player 1's keys (touch / keyboard / pad of this phone). They are
; sent for frame npFrame+npDelay; then every human slot gets the buttons sent for npFrame.
Function npSync()
If npLeaving Or npLost Then Return
If NetPlayKicked() Then
	npLost=1 : npMsg$ = "connection too slow - you left the match" : gameDone=1 : Return
EndIf
mask = npPack(1)
If npQuitAsk = -1 Then mask = mask Or 16384 : npLeaving = 1
NetPlaySend(npFrame+npDelay, mask)
If npLeaving Then gameDone = 1 : Return

If NetPlayReady(npFrame)=0 Then
	t0 = MilliSecs()
	Repeat
		w = MilliSecs()-t0
		If w > 300 Then npWaitScreen(w)
		;a player who disappears is handed to the CPU by NetPlayReady (after ~15 s without data);
		;give up only if nothing works for a long time, or if the others decided that we left
		If NetPlayKicked() Then npLost=1 : npMsg$ = "connection too slow - you left the match"
		If w > 40000 Then npLost=1 : npMsg$ = "connection lost - the match was stopped"
		If npLost Then
			gameDone = 1
			Return
		EndIf
		Delay 2
	Until NetPlayReady(npFrame)
EndIf
npLastSync = MilliSecs()

For s=1 To npPlayers
	If npActive(s) Then
		c = NetPlayLeftAt(s)
		If c >= 0 And npFrame > c Then	;disappeared (same frame on every phone): the CPU plays on
			npActive(s)=0 : zAI(s)=1 : aiLevel(s)=3 : NetPlaySetCpu(s)
			npNotice(olText$(NetPlaySlotName$(s))+" left the room - cpu plays p"+s)
		Else
			m = NetPlayMask(s, npFrame)
			npUnpack(s, m)
			If m And 16384 Then		;this player left with ESC: the CPU plays on for them
				npActive(s)=0 : zAI(s)=1 : aiLevel(s)=3 : NetPlaySetCpu(s)
				npNotice(olText$(NetPlaySlotName$(s))+" left the match - cpu plays p"+s)
			EndIf
		EndIf
	EndIf
Next
If npFrame Mod 54 = 0 Then NetPlayCheck(npFrame, npChecksum())
npFrame = npFrame+1
End Function

Function npPack(n)
m=0
If upKey(n) Then m = m Or 1
If downKey(n) Then m = m Or 2
If leftKey(n) Then m = m Or 4
If rightKey(n) Then m = m Or 8
If leftKeyHit(n) Then m = m Or 16
If rightKeyHit(n) Then m = m Or 32
If specialKey(n) Then m = m Or 64
If shotKey(n) Then m = m Or 128
If jumpKey(n) Then m = m Or 256
If jumpKeyDown(n) Then m = m Or 512
If grabKey(n) Then m = m Or 1024
If blockKey(n) Then m = m Or 2048
If superKey(n) Then m = m Or 4096
If hitKey(n) Then m = m Or 8192
Return m
End Function

Function npUnpack(n,m)
upKey(n)=(m And 1)>0 : downKey(n)=(m And 2)>0 : leftKey(n)=(m And 4)>0 : rightKey(n)=(m And 8)>0
leftKeyHit(n)=(m And 16)>0 : rightKeyHit(n)=(m And 32)>0 : specialKey(n)=(m And 64)>0
shotKey(n)=(m And 128)>0 : jumpKey(n)=(m And 256)>0 : jumpKeyDown(n)=(m And 512)>0
grabKey(n)=(m And 1024)>0 : blockKey(n)=(m And 2048)>0 : superKey(n)=(m And 4096)>0
hitKey(n)=(m And 8192)>0
End Function

;state summary compared between phones every second (detects a desync)
Function npChecksum()
c = PortRngPeek()
For n=1 To zzamount
	c = c*31 + Int(zx(n)*8) : c = c*31 + Int(zy(n)*8)
	c = c*31 + zlife(n) : c = c*31 + zlives(n) : c = c*31 + zon(n)
	c = c*31 + zblow(n)*16 + zcurblow(n)
Next
Return c
End Function

;ESC during an online match: first press asks, second press leaves
Function npEscape()
If npQuitAsk > 0 And MilliSecs()-npQuitAsk < 3000 Then
	npQuitAsk = -1
Else
	npQuitAsk = MilliSecs()
EndIf
End Function

;drawn over the game every frame
Function npNotice(t$)
npNoticeText$ = t$
npNoticeTime = MilliSecs()
End Function

Function npOverlay()
;bottom centre (the top row is the players' life bars, the corners are under the touch controls):
;every player with a connection light (green ok, yellow slow, red left, grey CPU) and the ping
w = 104
x0 = 320 - npPlayers*w/2
Color 0,0,0 : Rect x0-4,458,npPlayers*w+8,20,1
For s=1 To npPlayers
	x = x0 + (s-1)*w
	st = NetPlaySlotState(s)
	Select st
		Case 1: Color 60,210,90
		Case 2: Color 230,200,60
		Case 3: Color 230,70,60
		Default: Color 120,120,130
	End Select
	Oval x+2,463,9,9,1
	If s = npSlot Then Color 120,220,255 Else Color 230,230,240
	Text x+14,460,"P"+s
	If st = 3 Then
		Color 230,70,60 : Text x+38,460,"left"
	ElseIf st = 4 Then
		Color 150,150,160 : Text x+38,460,"cpu"
	Else
		p = NetPlaySlotPing(s)
		If p < 0 Then
			Color 200,200,215
			If s = 1 Then Text x+38,460,"host" Else Text x+38,460,"-"
		Else
			If p < 80 Then
				Color 60,210,90
			ElseIf p < 160 Then
				Color 230,200,60
			Else
				Color 230,70,60
			EndIf
			Text x+38,460,p+"ms"
		EndIf
	EndIf
Next
If npNoticeText$ <> "" And MilliSecs()-npNoticeTime < 4000 Then
	t$ = npNoticeText$
	Color 0,0,0 : Rect 316-Len(t$)*4,62,Len(t$)*8+8,20,1
	Color 255,210,90 : Rect 316-Len(t$)*4,62,Len(t$)*8+8,20,0
	Text 320-Len(t$)*4,64,t$
EndIf
If npQuitAsk > 0 And MilliSecs()-npQuitAsk < 3000 Then
	Color 0,0,0 : Rect 160,40,320,20,1
	Color 255,210,90 : Text 320-4*Len("press ESC again to leave the match"),42,"press ESC again to leave the match"
EndIf
If NetPlayDesync() >= 0 Then
	Color 0,0,0 : Rect 268,436,104,18,1
	Color 255,90,80 : Text 272,437,"out of sync!"
EndIf
End Function

;round trip to the other players in ms (-1 = unknown): for the host the slowest member,
;for the others their own ping to the host
Function npPingNow()
If NetIsHost()=0 Then Return NetPing()
p = -1
For i=1 To NetMemberCount()
	If NetMemberIsMe(i)=0 And NetMemberPing(i) > p Then p = NetMemberPing(i)
Next
Return p
End Function

Function npWaitScreen(w)
Color 20,20,40 : Rect 170,200,300,60,1
Color 150,150,175 : Rect 170,200,300,60,0
t$ = "waiting for players... "+(w/1000)
Color 255,210,90 : Text 320-4*Len(t$),214,t$
t$ = "connection is slow"
Color 200,200,215 : Text 320-4*Len(t$),236,t$
Flip
End Function

;----------- after the match: back to the lobby ----------------------------------
Function npFinish()
NetPlayEnd()
;keep the connection a moment so the last packets reach the others
For i=1 To 40
	NetUpdate() : Delay 20
Next
NetShutdown()
netPlay=0 : olActive=0
curModId = npSavedMod
If npLeaving Then npMsg$ = "you left the match"
End Function

;----------- room list --------------------------------------------------------
Function olListScreen()

status$ = NetStatus$()

;--- clicks (button rectangles are from the previous frame)
For b=10 To 15
	If clickedBut(b) Then
		r = b-9
		If r <= NetRoomCount() Then
			If olSel = r And NetRoomOpen(r) Then
				NetJoinRoom(r) : olSetInfo("joining...")
			Else
				olSel = r
			EndIf
			If gamesound Then PlaySound clickSnd
		EndIf
	EndIf
Next
If clickedBut(1) And status$="online" Then NetRefresh() : olSetInfo("refreshing...") : If gamesound Then PlaySound clickSnd
If clickedBut(2) And status$="online" Then
	NetCreateRoom(NetPlayerName$()+"'s room", 4)
	olSetInfo("creating room...")
	If gamesound Then PlaySound ddhitsnd
EndIf
If clickedBut(3) And olSel > 0 And olSel <= NetRoomCount() Then
	If NetRoomOpen(olSel) Then
		NetJoinRoom(olSel) : olSetInfo("joining...")
		If gamesound Then PlaySound ddhitsnd
	Else
		olSetInfo("that room is full or playing")
	EndIf
EndIf
If clickedBut(4) Then
	If gamesound Then PlaySound clickSnd
	onlineBack()
	Return
EndIf
If clickedBut(5) Then
	NetShutdown() : NetInit() : olSetInfo("retrying...")
EndIf
If olSel > NetRoomCount() Then olSel = 0

;--- header
fontType=1
pri priW("multiplayer online"),98,"multiplayer online"
olInfoBar()

;--- room list panel
olPanel(30,148,580,194,16,16,28)
Color 34,36,56 : Rect 31,149,578,20,1
olSmall(44,151,"ROOM",150,155,190)
olSmall(300,151,"HOST",150,155,190)
olSmall(440,151,"PLAYERS",150,155,190)
olSmall(528,151,"PING",150,155,190)

For b=10 To 15 : butOn(b)=0 : Next

If status$="connecting" Then
	fontType=1
	pri priW("connecting..."),225,"connecting..."
	olSmallC(320,252,"connecting to "+NetBackendName$(),150,155,190)
ElseIf status$="error" Or status$="offline" Then
	fontType=1
	pri priW("cannot connect"),200,"cannot connect"
	olSmallC(320,226,NetError$(),255,120,100)
	olButton(5,250,256,140,30,"retry",1)
ElseIf NetRoomCount()=0 Then
	fontType=1
	pri priW("no rooms yet"),212,"no rooms yet"
	olSmallC(320,240,"Press CREATE ROOM and invite your friends",150,155,190)
Else
	For r=1 To NetRoomCount()
		If r > 6 Then Exit
		b = 9+r
		y = 172+(r-1)*28
		xBut(b)=34 : yBut(b)=y : wBut(b)=572 : hBut(b)=26 : butOn(b)=1
		If olSel = r Then
			Color 52,62,140 : Rect 34,y,572,26,1
			Color 130,150,255 : Rect 34,y,572,26,0
		ElseIf olHover(b) Then
			Color 36,38,64 : Rect 34,y,572,26,1
		EndIf
		fontType=1
		pri 44,y+5,olCut$(olText$(NetRoomName$(r)),15)
		olSmall(300,y+5,olCut$(NetRoomHost$(r),11),200,200,215)
		pri 452,y+5,NetRoomPlayers(r)+"/"+NetRoomMax(r)
		olPing(528,y+7,NetRoomPing(r))
		If NetRoomOpen(r)=0 Then olBadge(392,y+5,40,16,140,40,40,"FULL")
	Next
EndIf

;--- buttons
on = (status$="online")
olButton(1,30,354,150,32,"refresh",on)
olButton(2,194,354,230,32,"create room",on)
joinOk = 0
If on And olSel > 0 Then joinOk = NetRoomOpen(olSel)
olButton(3,438,354,172,32,"join room",joinOk)
olButton(4,30,424,110,30,"back",1)

olSmall(610-Len("You: "+NetPlayerName$())*8,431,"You: "+NetPlayerName$(),200,200,215)
olShowInfo()

End Function

;----------- inside a room ----------------------------------------------------
Function olRoomScreen()

host = NetIsHost()
status$ = NetStatus$()
canEdit = (status$="in room")
If canEdit=0 Then olPicker = 0 : olStagePicker = 0
If host=0 Then olStagePicker = 0
pickerWasOpen = olPicker Or olStagePicker

;--- stage picker (host): 50 = random, 51-65 = stage 1-15, 66 = close
If olStagePicker Then
	For b=50 To 65
		If clickedBut(b) Then
			NetSetMap(b-50) : olStagePicker = 0
			If gamesound Then PlaySound ddhitsnd
		EndIf
	Next
	If clickedBut(66) Then olStagePicker = 0 : If gamesound Then PlaySound clickSnd
EndIf

;--- character picker (opened from the button next to my name)
If olPicker Then
	For b=30 To 39
		If clickedBut(b) Then
			NetSetHero(b-29) : olPicker = 0
			If gamesound Then PlaySound ddhitsnd
		EndIf
	Next
	If clickedBut(40) Then olPicker = 0 : If gamesound Then PlaySound clickSnd
EndIf

;--- clicks
If pickerWasOpen=0 Then
If clickedBut(1) And canEdit Then
	If NetIsReady() Then NetSetReady(0) Else NetSetReady(1)
	If gamesound Then PlaySound clickSnd
EndIf
If clickedBut(2) And NetCanStart() Then
	NetStart()
	If gamesound Then PlaySound energySnd
EndIf
If clickedBut(20) And canEdit Then
	olPicker = 1
	If gamesound Then PlaySound clickSnd
EndIf
If clickedBut(6) And host Then
	olStagePicker = 1
	If gamesound Then PlaySound clickSnd
EndIf
If clickedBut(7) And host Then
	l = NetMatchLives()+1 : If l > 9 Then l = 1
	NetSetLives(l)
	If gamesound Then PlaySound clickSnd
EndIf
If clickedBut(3) Or clickedBut(4) Then
	NetLeaveRoom()
	olSetInfo("you left the room")
	If gamesound Then PlaySound clickSnd
	Return
EndIf
EndIf

;--- header
fontType=1
t$ = olCut$(olText$(NetRoomTitle$()),22)
pri priW(t$),98,t$
olInfoBar()

For b=1 To 7 : butOn(b)=0 : Next
For b=10 To 66 : butOn(b)=0 : Next

;--- players panel
olPanel(30,148,580,194,16,16,28)
For s=1 To 4
	y = 156+(s-1)*46
	Color 28,30,48 : Rect 38,y,564,40,1
	fontType=1
	pri 50,y+12,"p"+s
	If s <= NetMemberCount() Then
		If NetMemberIsMe(s) Then
			Color 100,120,220 : Rect 38,y,564,40,0
		EndIf
		;character portrait + name
		h = olHeroOf(s)
		If olHeroPic(h) <> 0 Then
			DrawImage olHeroPic(h),116-ImageWidth(olHeroPic(h))/2,y+38-ImageHeight(olHeroPic(h))
		EndIf
		pri 150,y+3,olCut$(olText$(NetMemberName$(s)),12)
		If NetMemberIsMe(s) And NetHeroSupported() And canEdit Then
			xBut(20)=150 : yBut(20)=y+22 : wBut(20)=150 : hBut(20)=16 : butOn(20)=1
			If olHover(20) Then Color 72,72,135 Else Color 42,42,86
			Rect 150,y+22,150,16,1
			Color 255,200,60 : Rect 150,y+37,150,1,1
			olSmall(154,y+22,olHeroName$(h)+"  [change]",255,255,255)
		Else
			olSmall(154,y+22,olHeroName$(h),170,190,255)
		EndIf
		tx = 330
		If NetMemberIsHost(s) Then olBadge(tx,y+12,48,16,170,120,20,"HOST") : tx = tx + 54
		If NetMemberIsMe(s) Then olBadge(tx,y+12,40,16,60,80,170,"YOU")
		olPing(436,y+14,NetMemberPing(s))
		If NetMemberReady(s) Then
			olBadge(500,y+8,94,24,40,150,70,"READY")
		Else
			olBadge(500,y+8,94,24,70,70,85,"NOT READY")
		EndIf
	Else
		olSmall(96,y+12,"waiting for player...",110,115,140)
	EndIf
Next

;--- countdown
c = NetCountdown()
If c >= 0 Then
	olPicker = 0 : olStagePicker = 0
	olPanel(160,200,320,80,40,40,90)
	fontType=1
	pri priW("match starts in "+c),232,"match starts in "+c
EndIf

;--- buttons
If NetIsReady() Then rt$="not ready" Else rt$="ready"
olButton(1,30,354,180,32,rt$,canEdit)
If host Then
	olButton(2,224,354,180,32,"start",NetCanStart())
Else
	olButton(2,224,354,180,32,"waiting...",0)
EndIf
olButton(3,418,354,192,32,"leave room",1)
olButton(4,30,424,110,30,"back",1)

m = NetMatchMap()
If m < 1 Or m > 15 Then st$ = "random" Else st$ = "stage "+m
lv$ = "lives: "+NetMatchLives()
If host And NetHeroSupported() Then
	olButton(7,234,424,170,30,lv$,canEdit)
	olButton(6,418,424,192,30,"   "+st$,canEdit)
	olStageMini(424,424,m)
Else
	olSmall(570-Len(lv$+"   "+st$)*8,431,lv$+"   "+st$,200,200,215)
	olStageMini(570,424,m)
EndIf

;hint line (when no other message is shown)
If olInfo$ = "" Or MilliSecs() - olInfoTime >= 3000 Then
	If host Then
		If NetCanStart() Then
			olSmallC(320,394,"Everybody is ready - press START!",120,230,140)
		ElseIf canEdit Then
			olSmallC(320,394,"START needs 2+ players, all READY",200,200,215)
		EndIf
	Else
		olSmallC(320,394,"The host starts the match",200,200,215)
	EndIf
EndIf
olShowInfo()

If olPicker Or olStagePicker Then
	;only the picker reacts while it is open
	For b=1 To 7 : butOn(b)=0 : Next
	butOn(20)=0
	If olPicker Then olHeroPicker() Else olStageGrid()
EndIf

End Function

;small picture of stage m (0 = random) with a frame, 40x30 at x,y
Function olStageMini(x,y,m)
Color 150,150,175 : Rect x-1,y-1,42,32,0
If m >= 1 And m <= 15 And olMapMini(m) <> 0 Then
	DrawImage olMapMini(m),x,y
Else
	Color 40,42,80 : Rect x,y,40,30,1
	olSmallC(x+20,y+7,"?",255,210,90)
EndIf
End Function

;grid of the 15 VS stages + random, buttons 50-65, close = 66
Function olStageGrid()
Color 0,0,0 : Rect 24,92,592,380,1
olPanel(24,92,592,380,18,18,34)
fontType=1
pri priW("choose the stage"),98,"choose the stage"
cur = NetMatchMap()
For i=0 To 15
	b = 50+i
	x = 34 + (i Mod 4)*146
	y = 124 + (i/4)*80
	xBut(b)=x : yBut(b)=y : wBut(b)=136 : hBut(b)=76 : butOn(b)=1
	If i = cur Then
		Color 52,62,140 : Rect x,y,136,76,1
		Color 255,200,60 : Rect x,y,136,76,0
	ElseIf olHover(b) Then
		Color 40,42,80 : Rect x,y,136,76,1
		Color 130,150,255 : Rect x,y,136,76,0
	Else
		Color 28,30,48 : Rect x,y,136,76,1
		Color 70,72,100 : Rect x,y,136,76,0
	EndIf
	If i >= 1 And olMapPic(i) <> 0 Then
		DrawImage olMapPic(i),x+3,y+(76-ImageHeight(olMapPic(i)))/2
		olSmall(x+106,y+30,i,255,255,255)
	Else
		olSmallC(x+68,y+22,"?",255,210,90)
		olSmallC(x+68,y+42,"random",255,255,255)
	EndIf
Next
olButton(66,250,442,140,26,"close",1)
End Function

;character of member s (default by slot when the backend has no character choice)
Function olHeroOf(s)
h = NetMemberHero(s)
If NetHeroSupported()=0 Or h < 1 Or h > 10 Then h = 1 + ((s-1) Mod 10)
Return h
End Function

;grid of the 10 fighters, buttons 30-39, close = 40
Function olHeroPicker()
Color 0,0,0 : Rect 30,96,580,300,1
olPanel(30,96,580,300,18,18,34)
fontType=1
pri priW("choose your fighter"),104,"choose your fighter"
mine = NetMyHero()
For i=1 To 10
	b = 29+i
	x = 44 + ((i-1) Mod 5)*112
	y = 132 + ((i-1)/5)*118
	xBut(b)=x : yBut(b)=y : wBut(b)=104 : hBut(b)=110 : butOn(b)=1
	If i = mine Then
		Color 52,62,140 : Rect x,y,104,110,1
		Color 255,200,60 : Rect x,y,104,110,0
	ElseIf olHover(b) Then
		Color 40,42,80 : Rect x,y,104,110,1
		Color 130,150,255 : Rect x,y,104,110,0
	Else
		Color 28,30,48 : Rect x,y,104,110,1
		Color 70,72,100 : Rect x,y,104,110,0
	EndIf
	If olHeroPic(i) <> 0 Then
		DrawImage olHeroPic(i),x+52-ImageWidth(olHeroPic(i))/2,y+86-ImageHeight(olHeroPic(i))
	EndIf
	olSmallC(x+52,y+90,olHeroName$(i),255,255,255)
Next
olButton(40,250,360,140,28,"close",1)
End Function

;----------- widgets ----------------------------------------------------------
Function olInfoBar()
olPanel(30,120,580,24,22,24,40)
p = NetPing()
If p >= 0 Then ps$ = p+" ms" Else ps$ = "-"
olSmall(40,124,"Server: "+NetBackendName$(),200,200,215)
olSmall(262,124,"Status: "+NetStatus$(),200,200,215)
olSmall(408,124,"Ping: "+ps$,200,200,215)
olSmall(510,124,"Online: "+NetOnlineCount(),200,200,215)
End Function

Function olPanel(x,y,w,h,r,g,b)
Color r,g,b : Rect x,y,w,h,1
Color 150,150,175 : Rect x,y,w,h,0
Color 60,60,85 : Rect x+1,y+1,w-2,h-2,0
End Function

Function olButton(b,x,y,w,h,txt$,enabled)
xBut(b)=x : yBut(b)=y : wBut(b)=w : hBut(b)=h : butOn(b)=enabled
If enabled Then
	If olHover(b) Then
		olPanel(x,y,w,h,72,72,135)
	Else
		olPanel(x,y,w,h,42,42,86)
	EndIf
	Color 255,200,60 : Rect x+3,y+h-4,w-6,2,1
Else
	olPanel(x,y,w,h,24,24,32)
EndIf
fontType=1
pri x+(w-olW(txt$))/2,y+(h-17)/2,txt$
End Function

;small coloured badge with text in the runtime font (8x16)
Function olBadge(x,y,w,h,r,g,b,txt$)
Color r,g,b : Rect x,y,w,h,1
Color r+50,g+50,b+50 : Rect x,y,w,h,0
olSmallC(x+w/2,y+(h-16)/2,txt$,255,255,255)
End Function

;small text (runtime font, 8 pixels per character) in any colour
Function olSmall(x,y,s$,r,g,b)
Color r,g,b
Text x,y,s$
End Function

Function olSmallC(cx,y,s$,r,g,b)
olSmall(cx-Len(s$)*4,y,s$,r,g,b)
End Function

;width in pixels of a string in the current bitmap font (priW returns the centred x)
Function olW(s$)
Return 640-priW(s$)*2
End Function

Function olHover(b)
If xpointer(1) >= xBut(b) And xpointer(1) <= xBut(b)+wBut(b) Then
	If ypointer(1) >= yBut(b) And ypointer(1) <= yBut(b)+hBut(b) Then Return 1
EndIf
Return 0
End Function

;signal bars + "42ms" (green / yellow / red)
Function olPing(x,y,p)
cr=90 : cg=90 : cb=100
If p >= 0 Then cr=60 : cg=210 : cb=90
If p >= 80 Then cr=230 : cg=200 : cb=60
If p >= 160 Then cr=230 : cg=70 : cb=60
bars = 3
If p >= 80 Then bars = 2
If p >= 160 Then bars = 1
If p < 0 Then bars = 0
For i=1 To 3
	If i > bars Then Color 60,60,72 Else Color cr,cg,cb
	Rect x+(i-1)*6,y+12-i*4,4,i*4,1
Next
If p >= 0 Then olSmall(x+22,y-2,p+"ms",cr,cg,cb) Else olSmall(x+22,y-2,"-",cr,cg,cb)
End Function

Function olSetInfo(s$)
olInfo$ = s$
olInfoTime = MilliSecs()
End Function

Function olShowInfo()
e$ = NetError$()
If e$ <> olLastErr$ Then
	olLastErr$ = e$
	If e$ <> "" And NetStatus$() <> "error" Then olSetInfo(e$)
EndIf
If olInfo$ <> "" And MilliSecs() - olInfoTime < 3000 Then
	olSmallC(320,394,olInfo$,255,210,90)
EndIf
End Function

;text for pri(): lower case, only characters the bitmap font has
Function olText$(s$)
s$ = Lower(s$)
out$ = ""
For i=1 To Len(s$)
	c$ = Mid$(s$,i,1)
	If Instr("0123456789abcdefghijklmnopqrstuvwxyz.,-()/ +*&=:!?'",c$) Then
		out$ = out$ + c$
	ElseIf c$ = "_" Then
		out$ = out$ + "-"
	EndIf
Next
Return out$
End Function

Function olCut$(s$,n)
If Len(s$) > n Then Return Left$(s$,n-2)+".."
Return s$
End Function
