;=============================================================================
; port.bb - input helpers added by the Android port
;
; Touch controls and game pads give their buttons to the game directly as actions of a player
; (PortActDown / PortActHit, runtime/bbrt.cpp), not as keyboard keys. So they keep working when the
; keys are remapped in Options > Controls (even badly), and every pad controls its own player:
; the first pad used is player 1, the second player 2, and so on.
;
; Action numbers: 0 up, 1 down, 2 left, 3 right, 4 attack, 5 special, 6 jump, 7 block, 8 throw
;=============================================================================

;adds touch / pad actions of player n to the keys read from the keyboard (called in Getinput)
Function portActions(n)
If PortActDown(n,0) Then upKey(n)=1:hitKey(n)=1
If PortActDown(n,1) Then downKey(n)=1:hitKey(n)=1
If PortActDown(n,2) Then leftKey(n)=1:hitKey(n)=1
If PortActDown(n,3) Then rightKey(n)=1:hitKey(n)=1
If PortActHit(n,2) Then leftKeyhit(n)=1:hitKey(n)=1
If PortActHit(n,3) Then rightKeyhit(n)=1:hitKey(n)=1
If PortActHit(n,5) Then specialkey(n)=1:hitKey(n)=1
If PortActHit(n,4) Then shotKey(n)=1:hitKey(n)=1
If PortActHit(n,6) Then jumpKey(n)=1:hitKey(n)=1
If PortActDown(n,6) Then jumpKeyDown(n)=1:hitKey(n)=1
If PortActHit(n,8) Then grabKey(n)=1:hitKey(n)=1
If PortActDown(n,7) Then blockKey(n)=1:hitKey(n)=1
If shotkey(n)=1 And blockkey(n)=1 Then grabKey(n)=1
If blockKey(n)=1 And specialKey(n)=1 Then superKey(n)=1
End Function

;menus: players 2-4 move their pointer with their pad (player 1 uses the touch / mouse pointer)
Function portPointerActions(n)
If PortActDown(n,0) Then upKey(n)=1
If PortActDown(n,1) Then downKey(n)=1
If PortActDown(n,2) Then leftKey(n)=1
If PortActDown(n,3) Then rightKey(n)=1
If PortActHit(n,4) Then shotKey(n)=1
End Function

;any attack / special press of any player ("press any key" screens)
Function portAnyPress()
r=0
For n=1 To 4
	If PortActHit(n,4) Or PortActHit(n,5) Then r=1
Next
Return r
End Function

;once per frame of a match: tells the touch overlay a match runs, applies the adventure cheats
;chosen on the main menu (infinite health / infinite jutsu, never in VS or online)
Function portFrame()
PortScreen(3)
If vsMode=0 Then
	For n=1 To zzamount
		If zon(n)=1 And zAI(n)=0 Then
			If PortCheat(1) Then zlife(n)=100
			If PortCheat(2) Then zSuperBar(n)=100
		EndIf
	Next
EndIf
End Function
