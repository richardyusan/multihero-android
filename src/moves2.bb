;----------------------------- pig's moves! -----------------------------------
Function DoPig(n)


End Function

;----------------------------- alien's moves! -----------------------------------
Function DoAlien(n)

zFace(n)=zBlowDir(n)
zBlowEffect(n)=0

	If zBlowStill(n)=1 Then
		zBlowStillSeq(n)=zBlowStillSeq(n)+1
		If zBlowStillSeq(n) > zBlowStillTime(n) Then zBlowStill(n)=0
		Goto noBlowSeq31
	EndIf

zBlowSeq(n)=zBlowSeq(n)+1:
.noBlowSeq31

zchunkType(n)=50
i=10 : ii=1: iii=7
Select zCurBlow(n)
Case 0:zCurBlow(n)=i:remImune(n):Goto noblowseq31
Case 2:zCurBlow(n)=i:remImune(n):Goto noblowseq31
Case 3:zCurBlow(n)=i:remImune(n):Goto noblowseq31
Case 4:zCurBlow(n)=iii:remImune(n):Goto noblowseq31
Case 5:zCurBlow(n)=i:remImune(n):Goto noblowseq31
Case 6:zCurBlow(n)=i:remImune(n):Goto noblowseq31
Case 8:zCurBlow(n)=i:remImune(n):Goto noblowseq31
Case 9:zCurBlow(n)=iii:remImune(n):Goto noblowseq31
Case 11:zCurBlow(n)=ii:remImune(n):Goto noblowseq31
Case 12:zCurBlow(n)=ii:remImune(n):Goto noblowseq31
Case 13:zCurBlow(n)=i:remImune(n):Goto noblowseq31
Case 14:zCurBlow(n)=ii:remImune(n):Goto noblowseq31
Case 15:zCurBlow(n)=ii:remImune(n):Goto noblowseq31
Case 16:zCurBlow(n)=ii:remImune(n):Goto noblowseq31

Case 7	;Alien acid spit
	a=15: b=a+8: c=b+15
	zNoMove(n)=1: zNoJump(n)=1
		
	If zBlowSeq(n) => 1 And zBlowSeq(n) < a Then zani(n)=1:zf(n)=2
	If zBlowSeq(n) => a And zBlowSeq(n) < b Then zani(n)=6:zf(n)=1
	If zBlowSeq(n) => b And zBlowSeq(n) < c Then zani(n)=1:zf(n)=2
	
	If zblowSeq(n)=a Then
		If gamesound Then PlaySound blowSnd
		getObj(n,8)
		upkey(n)=0 : downKey(n)=0
		;objThrow(nn)=0:objTaken(nn)=1
		If zface(n)=2 Then x=zx(n)+10 Else x=zx(n)-10
		throwObj(n,x,zy(n)-17,zface(n))
	EndIf
	
	If zblowseq(n) => c Then zBlowSeq(n)=0:zBlow(n)=0

Case 10 	;flying claw hit
	a=8: b=a+8: c=b+8: d=c+10
	zNoMove(n)=1: zNoJump(n)=1:zJumping(n)=0
	zjump(n)=0:zjump2(n)=0
	
	If zHitHead(n)=1 Then zBlowSeq(n)=b
	
	If zBlowSeq(n) => 1 And zBlowSeq(n) < a Then zani(n)=14:zf(n)=1:movex2(n,zBlowdir(n),2):movey(n,-4):zNoGrav(n)=1
	If zBlowSeq(n) => a And zBlowSeq(n) < b Then zani(n)=14:zf(n)=2:movex2(n,zBlowdir(n),2):zNoGrav(n)=1
	If zBlowSeq(n) => b And zBlowSeq(n) < c Then zani(n)=14:zf(n)=2:movex2(n,zBlowdir(n),2)
	If zBlowSeq(n) => c And zBlowSeq(n) <= d Then zani(n)=6:zf(n)=3
				
	If zani(n)=14 And zf(n)=2 Then
		zblowPamount(n)=2:nn=1
		xblow(n,nn)=0: yblow(n,nn)=35:wblow(n,nn)=50:hblow(n,nn)=1:nn=nn+1
		xblow(n,nn)=0: yblow(n,nn)=30:wblow(n,nn)=50:hblow(n,nn)=1:nn=nn+1
		zHitMode(n)=0:zBlowHold(n)=8
		zBlowDamage(n)=5:zBLowEffect(n)=1:zBlowImpact(n)=50:zBlowStillTime(n)=15:zBlowBlockTime(n)=15
		zBlowSound(n)=rashhitSnd
		zSuperBar(n)=0
	EndIf
	
	If zBlowSeq(n) = b And zongnd(n)=1 Then	zblowseq(n)=c
	
	If zblowseq(n) > d Then zBlowSeq(n)=0:zBlow(n)=0:zcurblow(n)=0


Case 1	;claw hit
	a=12: b=a+12: c=b+20: d=c+8: e=d+8: f=e+15
	zNoMove(n)=1: zNoJump(n)=1
	If zBlowSeq(n) => 1 And zBlowSeq(n) < a Then zani(n)=6:zf(n)=1
	If zBlowSeq(n) => a And zBlowSeq(n) < b Then zani(n)=6:zf(n)=2
	If zBlowSeq(n) => b And zBlowSeq(n) < c Then zani(n)=6:zf(n)=3
	
	If zani(n)=6 And zf(n)=2 Then
		zblowPamount(n)=2:nn=1
		xblow(n,nn)=0: yblow(n,nn)=35:wblow(n,nn)=53:hblow(n,nn)=1:nn=nn+1
		xblow(n,nn)=0: yblow(n,nn)=25:wblow(n,nn)=53:hblow(n,nn)=1:nn=nn+1
		zHitMode(n)=0:zBlowHold(n)=8
		zBlowDamage(n)=5:zBLowEffect(n)=1:zBlowImpact(n)=99:zBlowStillTime(n)=10:zBlowBlockTime(n)=15
		zBlowSound(n)=rashhitSnd
	EndIf
	
	If zBlowSeq(n) => c Then zBlowSeq(n)=0:zBlow(n)=0
	
End Select

End Function

;----------------------------- foot clan's moves! -----------------------------------
Function DoFootClan(n)

zFace(n)=zBlowDir(n)
zBlowEffect(n)=0

	If zBlowStill(n)=1 Then
		zBlowStillSeq(n)=zBlowStillSeq(n)+1
		If zBlowStillSeq(n) > zBlowStillTime(n) Then zBlowStill(n)=0
		Goto noBlowSeq32
	EndIf

zBlowSeq(n)=zBlowSeq(n)+1:
.noBlowSeq32

zchunkType(n)=50
i=10 : ii=1: 
Select zCurBlow(n)
Case 0:zCurBlow(n)=ii:remImune(n):Goto noblowseq32
Case 3:zCurBlow(n)=i:remImune(n):Goto noblowseq32
Case 5:zCurBlow(n)=4:remImune(n):Goto noblowseq32
Case 6:zCurBlow(n)=i:remImune(n):Goto noblowseq32
Case 7:zCurBlow(n)=4:remImune(n):Goto noblowseq32
Case 8:zCurBlow(n)=ii:remImune(n):Goto noblowseq32
Case 9:zCurBlow(n)=4:remImune(n):Goto noblowseq32
Case 11:zCurBlow(n)=i:remImune(n):Goto noblowseq32
Case 12:zCurBlow(n)=i:remImune(n):Goto noblowseq32
Case 13:zCurBlow(n)=i:remImune(n):Goto noblowseq32
Case 14:zCurBlow(n)=i:remImune(n):Goto noblowseq32
Case 15:zCurBlow(n)=i:remImune(n):Goto noblowseq32
Case 16:zCurBlow(n)=ii:remImune(n):Goto noblowseq32

Case 4 	;flying kick (straight)
	a=8: b=a+8: c=b+8: d=c+10
	zNoMove(n)=1: zNoJump(n)=1:zJumping(n)=0
	zjump(n)=0:zjump2(n)=0
	
	If zHitHead(n)=1 Then zBlowSeq(n)=b
	
	If zBlowSeq(n) => 1 And zBlowSeq(n) < a Then zani(n)=8:zf(n)=1:movex2(n,zBlowdir(n),2):movey(n,-3):zNoGrav(n)=1
	If zBlowSeq(n) => a And zBlowSeq(n) < b Then zani(n)=8:zf(n)=2:movex2(n,zBlowdir(n),2):zNoGrav(n)=1
	If zBlowSeq(n) => b And zBlowSeq(n) < c Then zani(n)=8:zf(n)=1:movex2(n,zBlowdir(n),2)
	If zBlowSeq(n) => c And zBlowSeq(n) <= d Then zani(n)=1:zf(n)=0
				
	If zani(n)=8 And zf(n)=2 Then
		zblowPamount(n)=2:nn=1
		xblow(n,nn)=0: yblow(n,nn)=20:wblow(n,nn)=33:hblow(n,nn)=1:nn=nn+1
		xblow(n,nn)=0: yblow(n,nn)=13:wblow(n,nn)=30:hblow(n,nn)=1:nn=nn+1
		zHitMode(n)=0:zBlowHold(n)=8
		zBlowDamage(n)=5:zBLowEffect(n)=1:zBlowImpact(n)=50:zBlowStillTime(n)=15:zBlowBlockTime(n)=15
		zBlowSound(n)=rashHitSnd
		zSuperBar(n)=0
	EndIf
	
	If zBlowSeq(n) = b And zongnd(n)=1 Then	zblowseq(n)=c
	
	If zblowseq(n) > d Then zBlowSeq(n)=0:zBlow(n)=0:zcurblow(n)=0


Case 1	;punch
	a=12: b=a+8: c=b+8: d=c+12: e=d+8: f=e+15
	zNoMove(n)=1: zNoJump(n)=1
	If zBlowSeq(n) => 1 And zBlowSeq(n) < a Then zani(n)=6:zf(n)=1
	If zBlowSeq(n) => a And zBlowSeq(n) < b Then zani(n)=6:zf(n)=2
	If zBlowSeq(n) => b And zBlowSeq(n) < c Then zani(n)=6:zf(n)=3
	If zBlowSeq(n) => c And zBlowSeq(n) < d Then zani(n)=6:zf(n)=4
	
	If zani(n)=6 And zf(n)=2 Then
		zblowPamount(n)=2:nn=1
		xblow(n,nn)=0: yblow(n,nn)=43:wblow(n,nn)=30:hblow(n,nn)=1:nn=nn+1
		xblow(n,nn)=0: yblow(n,nn)=35:wblow(n,nn)=30:hblow(n,nn)=1:nn=nn+1
		zHitMode(n)=0:zBlowHold(n)=8
		zBlowDamage(n)=5:zBLowEffect(n)=1:zBlowImpact(n)=99:zBlowStillTime(n)=20:zBlowBlockTime(n)=20
		zBlowSound(n)=rashHitSnd
	EndIf
	
	If zBlowSeq(n) => d Then zBlowSeq(n)=0:zBlow(n)=0
	
Case 10	;round kick
	a=8: b=a+8: c=b+8: d=c+12: e=d+8: f=e+15
	zNoMove(n)=1: zNoJump(n)=1
	If zBlowSeq(n) => 1 And zBlowSeq(n) < a Then zani(n)=14:zf(n)=1
	If zBlowSeq(n) => a And zBlowSeq(n) < b Then zani(n)=14:zf(n)=2
	If zBlowSeq(n) => b And zBlowSeq(n) < c Then zani(n)=14:zf(n)=3
	If zBlowSeq(n) => c And zBlowSeq(n) < d Then zani(n)=14:zf(n)=4
	
	If zani(n)=14 And zf(n)=3 Then
		zblowPamount(n)=3:nn=1
		xblow(n,nn)=0: yblow(n,nn)=52:wblow(n,nn)=22:hblow(n,nn)=1:nn=nn+1
		xblow(n,nn)=0: yblow(n,nn)=42:wblow(n,nn)=22:hblow(n,nn)=1:nn=nn+1
		xblow(n,nn)=0: yblow(n,nn)=32:wblow(n,nn)=18:hblow(n,nn)=1:nn=nn+1
		zHitMode(n)=0:zBlowHold(n)=8
		zBlowDamage(n)=5:zBLowEffect(n)=1:zBlowImpact(n)=99:zBlowStillTime(n)=20:zBlowBlockTime(n)=20
		zBlowSound(n)=rashHitSnd
	EndIf
	
	If zBlowSeq(n) => d Then zBlowSeq(n)=0:zBlow(n)=0

Case 2	;Flying Kick
	a=7:b=35
	zNoJump(n)=0:ZJUMPING(N)=1
	If zBlowSeq(n) => 1 And zBlowSeq(n) =< a Then zani(n)=8:zf(n)=1:zblowAlert(n)=1
	If zBlowSeq(n) =a Then If gameSound Then PlaySound blowsnd
	If zBlowSeq(n) > a And zBlowSeq(n) =< b Then
		zblowPamount(n)=2:nn=1
		xblow(n,nn)=0: yblow(n,nn)=20:wblow(n,nn)=33:hblow(n,nn)=1:nn=nn+1
		xblow(n,nn)=0: yblow(n,nn)=13:wblow(n,nn)=30:hblow(n,nn)=1:nn=nn+1
		zHitMode(n)=0:zBlowHold(n)=8
		zBlowDamage(n)=5:zBLowEffect(n)=1:zBlowImpact(n)=99:zBlowStillTime(n)=15:zBlowBlockTime(n)=25
		zBlowSound(n)=rashHitsnd
		zani(n)=8:zf(n)=2
	EndIf
	If zBlowSeq(n) > b Then zBlowSeq(n)=0:zBlow(n)=0:zBlowStill(n)=0
	If zongnd(n)=1 And zBlowStill(n)=0 Then zBlow(n)=0:zblowseq(n)=0
	
End Select

End Function

;----------------------------- shredder's moves! -----------------------------------
Function DoShredder(n)

End Function

;----------------------------- make thug's moves! -----------------------------------
Function DoThug(n)


End Function

;----------------------------- red Horns's moves! -----------------------------------
Function DoRedHorns(n)

zFace(n)=zBlowDir(n)
zBlowEffect(n)=0

	If zBlowStill(n)=1 Then
		zBlowStillSeq(n)=zBlowStillSeq(n)+1
		If zBlowStillSeq(n) > zBlowStillTime(n) Then zBlowStill(n)=0
		Goto noBlowSeq35
	EndIf

zBlowSeq(n)=zBlowSeq(n)+1:
.noBlowSeq35

zchunkType(n)=50
i=10 : ii=1
Select zCurBlow(n)

Default 	;throw spinning fire
	a=7: b=a+85: c=b+8: d=c+8: e=d+8: f=e+15
	zNoMove(n)=1
	zNoJump(n)=1
	zjump(n)=0
	;If zongnd(n)=0 Then zy(n)=zy(n)-2
	If zBlowSeq(n) = a And gameSound=1 Then PlaySound shurikenSnd
	If zBlowSeq(n) > 1 And zBlowSeq(n) =< a Then zani(n)=6:zf(n)=1
	If zBlowSeq(n) > a And zBlowSeq(n) =< b Then zani(n)=6:zf(n)=1
	If zBlowSeq(n) = a+1 Then 
		dir=zface(n):y=zy(n)-zheight(n)+15
		If zface(n)=2 Then x=zx(n)+15
		If zface(n)=4 Then x=zx(n)-15
		zSuperBar(n)=0
		makeshot(n,17,x,y,dir)
	EndIf
	
	If zBlowSeq(n) > b Then zBlowSeq(n)=0:zBlow(n)=0	
	
End Select

End Function

;----------------------------- Gargola's moves! -----------------------------------
Function DoGargola(n)

zFace(n)=zBlowDir(n)
zBlowEffect(n)=0

	If zBlowStill(n)=1 Then
		zBlowStillSeq(n)=zBlowStillSeq(n)+1
		If zBlowStillSeq(n) > zBlowStillTime(n) Then zBlowStill(n)=0
		Goto noBlowSeq36
	EndIf

zBlowSeq(n)=zBlowSeq(n)+1:
.noBlowSeq36

zchunkType(n)=5
i=10 : ii=1
Select zCurBlow(n)

Default 	;jump + pink blob spit
	a=40: b=a+8: c=b+8: d=c+8: e=d+8: f=e+15: g=f+8: h=g+15: i=h+15
	zNoMove(n)=1
	zNoJump(n)=1
	If zblowseq(n) > a And zblowseq(n) =< d Then zNoGrav(n)=1
	
	If zBlowSeq(n) > 1 And zBlowSeq(n) =< a Then zani(n)=1:zf(n)=0
	If zBlowSeq(n) > a And zBlowSeq(n) =< b Then zani(n)=1:zf(n)=1:movex2(n,zblowdir(n),2):movey(n,-3)
	If zBlowSeq(n) > b And zBlowSeq(n) =< c Then zani(n)=1:zf(n)=1:movex2(n,zblowdir(n),2):movey(n,-2)
	If zBlowSeq(n) > c And zBlowSeq(n) =< d Then zani(n)=1:zf(n)=1:movex2(n,zblowdir(n),2):movey(n,-1)
	If zBlowSeq(n) > d And zBlowSeq(n) =< e Then zani(n)=1:zf(n)=1:movex2(n,zblowdir(n),2):movey(n,-2)
	If zBlowSeq(n) > e Then movex2(n,zblowdir(n),2) 
	
	If zBlowSeq(n) = a Then 
		dir=zface(n):y=zy(n)-zheight(n)+15
		If zface(n)=2 Then x=zx(n)+18
		If zface(n)=4 Then x=zx(n)-18
		zSuperBar(n)=0
		makeshot(n,18,x,y,dir)
	EndIf
	
	If zf(n)=1 And zBlowSeq(n) > e Then
		zblowpamount(n)=5:nn=1
		xblow(n,nn)=-6: yblow(n,nn)=68:wblow(n,nn)=26:hblow(n,nn)=1:nn=nn+1
		xblow(n,nn)=-6: yblow(n,nn)=56:wblow(n,nn)=30:hblow(n,nn)=1:nn=nn+1
		xblow(n,nn)=-6: yblow(n,nn)=44:wblow(n,nn)=30:hblow(n,nn)=1:nn=nn+1
		xblow(n,nn)=-6: yblow(n,nn)=32:wblow(n,nn)=30:hblow(n,nn)=1:nn=nn+1
		xblow(n,nn)=-6: yblow(n,nn)=20:wblow(n,nn)=20:hblow(n,nn)=1:nn=nn+1
		zHitMode(n)=0:zBlowHold(n)=12
		zBlowDamage(n)=8:zBLowEffect(n)=1:zBlowImpact(n)=30:zBlowStillTime(n)=12:zBlowBlockTime(n)=30
		zBlowSound(n)=highpunchSnd
	EndIf
	
	If zhitHead(n)=1 Then zBlowseq(n)=d+1		
	If zBlowSeq(n) > d And zongnd(n)=1 Then zBlowSeq(n)=0:zBlow(n)=0:
	
End Select

End Function
;----------------------------------- Red plant --------------------------------
Function DoRedPlant(n)

zFace(n)=zBlowDir(n)
zBlowEffect(n)=0

	If zBlowStill(n)=1 Then
		zBlowStillSeq(n)=zBlowStillSeq(n)+1
		If zBlowStillSeq(n) > zBlowStillTime(n) Then zBlowStill(n)=0
		Goto noBlowSeq37
	EndIf

zBlowSeq(n)=zBlowSeq(n)+1:
.noBlowSeq37

zchunkType(n)=5
Select zCurBlow(n)

Default 	;spit red ball
	a=5: b=a+30: c=b+25
	zNoMove(n)=1
	zNoJump(n)=1
	If zBlowSeq(n) > 1 And zBlowSeq(n) =< a Then zani(n)=1:zf(n)=0
	If zBlowSeq(n) > a And zBlowSeq(n) =< b Then zani(n)=1:zf(n)=1
	If zBlowSeq(n) > b And zBlowSeq(n) =< c Then zani(n)=1:zf(n)=0
		
	If zBlowSeq(n) = a Then 
		dir=zface(n):y=zy(n)-zheight(n)+20
		If zface(n)=2 Then x=zx(n)+10
		If zface(n)=4 Then x=zx(n)-10
		zSuperBar(n)=0
		da = makeshot(n,7,x,y,dir)
		Select zVar1(n)
		 Case 1: shotHitMode(da)=0   ;Strong fire ball
		  	
		End Select
	EndIf
			
	If zBlowSeq(n) > c And zongnd(n)=1 Then zBlowSeq(n)=0:zBlow(n)=0:
	
End Select

End Function

;----------------------------------- Bowser --------------------------------
Function DoBowser(n)

zFace(n)=zBlowDir(n)
zBlowEffect(n)=0

	If zBlowStill(n)=1 Then
		zBlowStillSeq(n)=zBlowStillSeq(n)+1
		If zBlowStillSeq(n) > zBlowStillTime(n) Then zBlowStill(n)=0
		Goto noBlowSeq38
	EndIf

zBlowSeq(n)=zBlowSeq(n)+1:
.noBlowSeq38

zchunkType(n)=5

i=7:ii=5
Select zCurBlow(n)
Case 0:zCurBlow(n)=i:remImune(n):Goto noblowseq38
Case 1:zCurBlow(n)=ii:remImune(n):Goto noblowseq38
Case 2:zCurBlow(n)=i:remImune(n):Goto noblowseq38
Case 3:zCurBlow(n)=i:remImune(n):Goto noblowseq38
Case 4:zCurBlow(n)=i:remImune(n):Goto noblowseq38
Case 6:zCurBlow(n)=i:remImune(n):Goto noblowseq38
Case 8:zCurBlow(n)=ii:remImune(n):Goto noblowseq38
Case 9:zCurBlow(n)=ii:remImune(n):Goto noblowseq38
Case 10:zCurBlow(n)=ii:remImune(n):Goto noblowseq38
Case 11:zCurBlow(n)=i:remImune(n):Goto noblowseq38
Case 12:zCurBlow(n)=i:remImune(n):Goto noblowseq38
Case 13:zCurBlow(n)=i:remImune(n):Goto noblowseq38
Case 14:zCurBlow(n)=i:remImune(n):Goto noblowseq38
Case 15:zCurBlow(n)=i:remImune(n):Goto noblowseq38
Case 16:zCurBlow(n)=i:remImune(n):Goto noblowseq38

Case 5	;Smashing jump
	a=0: b=a+10: c=b+10: d=c+10: e=d+10: f=e+900: g=f+30: h=g+15: i=h+15
	zNoMove(n)=1
	zNoJump(n)=1
	If zblowseq(n) > a And zblowseq(n) =< d Then zNoGrav(n)=1
	
	If zBlowSeq(n) > 1 And zBlowSeq(n) =< a Then zani(n)=4:zf(n)=1
	If zBlowSeq(n) > a And zBlowSeq(n) =< b Then zani(n)=4:zf(n)=1:movex2(n,zblowdir(n),2):movey(n,-4)
	If zBlowSeq(n) > b And zBlowSeq(n) =< c Then zani(n)=4:zf(n)=1:movex2(n,zblowdir(n),2):movey(n,-3)
	If zBlowSeq(n) > c And zBlowSeq(n) =< d Then zani(n)=4:zf(n)=1:movex2(n,zblowdir(n),2):movey(n,-1)
	If zBlowSeq(n) > d And zBlowSeq(n) =< e Then zani(n)=4:zf(n)=1:movex2(n,zblowdir(n),2):movey(n,-2)
	If zBlowSeq(n) > e And zongnd(n)=0 Then movex2(n,zblowdir(n),2):moveY(n,1) 
	
	If zf(n)=1 And zBlowSeq(n) > c Then 
		zblowpamount(n)=4:nn=1
		xblow(n,nn)=0: yblow(n,nn)=42:wblow(n,nn)=45:hblow(n,nn)=1:nn=nn+1
		xblow(n,nn)=0: yblow(n,nn)=32:wblow(n,nn)=36:hblow(n,nn)=1:nn=nn+1
		xblow(n,nn)=0: yblow(n,nn)=22:wblow(n,nn)=25:hblow(n,nn)=1:nn=nn+1
		xblow(n,nn)=0: yblow(n,nn)=12:wblow(n,nn)=10:hblow(n,nn)=1:nn=nn+1
		zHitMode(n)=0:zBlowHold(n)=12
		zBlowDamage(n)=8:zBLowEffect(n)=1:zBlowImpact(n)=30:zBlowStillTime(n)=12:zBlowBlockTime(n)=30
		zBlowSound(n)=marioFierceSnd
	EndIf
	
	If zhitHead(n)=1 Then zBlowseq(n)=d+1		
	
	If zBlowSeq(n) > d And zBlowSeq(n) < f And zongnd(n)=1 Then 
		makechunk(n,zx(n)-15,zy(n),zface(n),16)
		makechunk(n,zx(n)+15,zy(n),zface(n),16)
		If gamesound Then PlaySound zHitWallSnd:PlaySound zHitWallSnd
		zblowseq(n)=f
		quake=1:quakeSeq=0
	EndIf
	If zBlowSeq(n) > f And zBlowSeq(n) =< g Then zani(n)=1:zf(n)=0
	If zBlowSeq(n) > g Then zBlowSeq(n)=0:zBlow(n)=0:
	
Case 7 	;spit fire ball
	a=5: b=a+20: c=b+10
	zNoMove(n)=1
	zNoJump(n)=1
	If zBlowSeq(n) > 1 And zBlowSeq(n) =< a Then zani(n)=1:zf(n)=0
	If zBlowSeq(n) > a And zBlowSeq(n) =< b Then zani(n)=10:zf(n)=1
	If zBlowSeq(n) > b And zBlowSeq(n) =< c Then zani(n)=1:zf(n)=0
		
	If zBlowSeq(n) = a Then 
		dir=zface(n):y=zy(n)-zheight(n)+30
		If zface(n)=2 Then x=zx(n)+28
		If zface(n)=4 Then x=zx(n)-28
		zSuperBar(n)=0
		makeshot(n,19,x,y,dir)
	EndIf
			
	If zBlowSeq(n) > c And zongnd(n)=1 Then zBlowSeq(n)=0:zBlow(n)=0:
	
End Select

End Function

;------------------------- Thief with gun ---------------------------------
Function DoThief(n)

zFace(n)=zBlowDir(n)
zBlowEffect(n)=0

	If zBlowStill(n)=1 Then
		zBlowStillSeq(n)=zBlowStillSeq(n)+1
		If zBlowStillSeq(n) > zBlowStillTime(n) Then zBlowStill(n)=0
		Goto noBlowSeq39
	EndIf

zBlowSeq(n)=zBlowSeq(n)+1:
.noBlowSeq39

zchunkType(n)=5
Select zCurBlow(n)

Default 	;shoot
	a=10: b=a+10: c=b+10: d=c+30: e=d+20: f=e+5
	zNoMove(n)=1
	zNoJump(n)=1
	If zBlowSeq(n) > 1 And zBlowSeq(n) =< a Then zani(n)=6:zf(n)=1
	If zBlowSeq(n) > a And zBlowSeq(n) =< b Then zani(n)=6:zf(n)=2
	If zBlowSeq(n) > b And zBlowSeq(n) =< c Then zani(n)=6:zf(n)=3
	If zBlowSeq(n) > c And zBlowSeq(n) =< d Then zani(n)=6:zf(n)=4
	If zBlowSeq(n) > d And zBlowSeq(n) =< e Then zani(n)=6:zf(n)=3
		
	If zBlowSeq(n) = c Then 
		dir=zface(n):y=zy(n)-zheight(n)+5
		If zface(n)=2 Then x=zx(n)+18
		If zface(n)=4 Then x=zx(n)-18
		zSuperBar(n)=0
		makeshot(n,10,x,y,dir)
		If gameSound Then PlaySound shotSnd
		If zface(n)=2 Then x=zx(n)+25
		If zface(n)=4 Then x=zx(n)-25
		makechunk(n,x,zy(n)-38,dir,50)
	EndIf
			
	If zBlowSeq(n) > e Then zBlowSeq(n)=0:zBlow(n)=0:
	
End Select

End Function
;------------------------- Turtle ---------------------------------
Function DoTurtle(n)

zFace(n)=zBlowDir(n)
zBlowEffect(n)=0

	If zBlowStill(n)=1 Then
		zBlowStillSeq(n)=zBlowStillSeq(n)+1
		If zBlowStillSeq(n) > zBlowStillTime(n) Then zBlowStill(n)=0
		Goto noBlowSeq40
	EndIf

zBlowSeq(n)=zBlowSeq(n)+1:
.noBlowSeq40

zchunkType(n)=5
Select zCurBlow(n)

Default 	;spinning shell
	a=10: b=a+10: c=b+5: d=c+5: e=d+5: f=e+10: g=f+10
	Nspin=4
	zNoJump(n)=1
	If zBlowSeq(n)=a Then zBlowSeq2(n)=0
	
	If zBlowSeq(n) > 1 And zBlowSeq(n) =< a Then zani(n)=6:zf(n)=1:zNoMove(n)=1
	If zBlowSeq(n) > a And zBlowSeq(n) =< b Then zani(n)=6:zf(n)=2:zNoMove(n)=1
	If zBlowSeq(n) > b And zBlowSeq(n) =< c Then zani(n)=6:zf(n)=3
	If zBlowSeq(n) > c And zBlowSeq(n) =< d Then zani(n)=6:zf(n)=4
	If zBlowSeq(n) > d And zBlowSeq(n) =< e Then zani(n)=6:zf(n)=5
	If zBlowSeq(n) > e And zBlowSeq(n) =< f Then zani(n)=6:zf(n)=2:zNoMove(n)=1
	If zBlowSeq(n) > f And zBlowSeq(n) =< g Then zani(n)=6:zf(n)=1:zNoMove(n)=1
		
	If zf(n)=3 Or zf(n)=4 Or zf(n)=5 Then 
		zblowpamount(n)=3:nn=1
		xblow(n,nn)=-2: yblow(n,nn)=15:wblow(n,nn)=4:hblow(n,nn)=1:nn=nn+1
		xblow(n,nn)=-7: yblow(n,nn)=10:wblow(n,nn)=14:hblow(n,nn)=1:nn=nn+1
		xblow(n,nn)=-9: yblow(n,nn)=4:wblow(n,nn)=18:hblow(n,nn)=1:nn=nn+1
		zHitMode(n)=0:zBlowHold(n)=12
		zBlowDamage(n)=8:zBLowEffect(n)=1:zBlowImpact(n)=30:zBlowStillTime(n)=12:zBlowBlockTime(n)=30
		zBlowSound(n)=tucupSnd
		zblowback(n)=1
	EndIf	
	
	If zBlowSeq(n) = e Then
		zBlowseq2(n)=zBlowSeq2(n)+1:zblowSeq(n)=b		
		If zBlowseq2(n) > Nspin Then zBlowseq(n)=e+2
	EndIf
	If zBlowSeq(n) > g Then zBlowSeq(n)=0:zBlow(n)=0:
	
End Select

End Function
;----------------------------------- Turtle on cloud --------------------------------
Function DoTurtleCloud(n)

zFace(n)=zBlowDir(n)
zBlowEffect(n)=0

	If zBlowStill(n)=1 Then
		zBlowStillSeq(n)=zBlowStillSeq(n)+1
		If zBlowStillSeq(n) > zBlowStillTime(n) Then zBlowStill(n)=0
		Goto noBlowSeq41
	EndIf

zBlowSeq(n)=zBlowSeq(n)+1:
.noBlowSeq41

zchunkType(n)=5
Select zCurBlow(n)

Default 	;throw hammers
	a=45: b=a+25: c=b+5 
	zNoJump(n)=1
		
	If zBlowSeq(n) => 1 And zBlowSeq(n) < a Then zani(n)=6:zf(n)=1
	
	If zblowSeq(n)=1 Then
		originalObj=zGotObj(n)
		da = getObj(n,13)
		upkey(n)=0 : downKey(n)=0
		objXspeed(da)=.3 : objYspeed(da)=-2
		If zface(n)=2 Then x=zx(n)+10 Else x=zx(n)-10
		throwObj(n,x,zy(n)-17,zface(n))
		zGotObj(n)=originalObj
	EndIf
	
	If zBlowSeq(n) => a And zBlowSeq(n) < b Then zani(n)=4:zf(n)=1
	If zblowseq(n) => b Then zBlowSeq(n)=0:zBlow(n)=0
	
End Select

End Function
;----------------------------- Joker's moves! -----------------------------------
Function DoJoker(n)

zFace(n)=zBlowDir(n)
zBlowEffect(n)=0

	If zBlowStill(n)=1 Then
		zBlowStillSeq(n)=zBlowStillSeq(n)+1
		If zBlowStillSeq(n) > zBlowStillTime(n) Then zBlowStill(n)=0
		Goto noBlowSeq42
	EndIf

zBlowSeq(n)=zBlowSeq(n)+1:
.noBlowSeq42

zchunkType(n)=50
i=7 : ii=7: iii=7
Select zCurBlow(n)
Case 2:zCurBlow(n)=i:remImune(n):nextBlow(n)=zCurBlow(n):Goto noblowseq42
Case 3:zCurBlow(n)=i:remImune(n):nextBlow(n)=zCurBlow(n):Goto noblowseq42
Case 4:zCurBlow(n)=1:remImune(n):nextBlow(n)=zCurBlow(n):Goto noblowseq42
Case 5:zCurBlow(n)=1:remImune(n):nextBlow(n)=zCurBlow(n):Goto noblowseq42
Case 6:zCurBlow(n)=i:remImune(n):nextBlow(n)=zCurBlow(n):Goto noblowseq42
Case 8:zCurBlow(n)=i:remImune(n):nextBlow(n)=zCurBlow(n):Goto noblowseq42
Case 9:zCurBlow(n)=iii:remImune(n):nextBlow(n)=zCurBlow(n):Goto noblowseq42
Case 11:zCurBlow(n)=ii:remImune(n):nextBlow(n)=zCurBlow(n):Goto noblowseq42
Case 12:zCurBlow(n)=ii:remImune(n):nextBlow(n)=zCurBlow(n):Goto noblowseq42
Case 13:zCurBlow(n)=i:remImune(n):nextBlow(n)=zCurBlow(n):Goto noblowseq42
Case 14:zCurBlow(n)=ii:remImune(n):nextBlow(n)=zCurBlow(n):Goto noblowseq42
Case 15:zCurBlow(n)=ii:remImune(n):nextBlow(n)=zCurBlow(n):Goto noblowseq42
Case 16:zCurBlow(n)=ii:remImune(n):nextBlow(n)=zCurBlow(n):Goto noblowseq42

Case 0	;Blocking
	zNoMove(n)=1:zNoJump(n)=1
	zBlock(n)=1:zani(n)=13:zf(n)=1
	If blockKey(n)=0 And zBLocked(n)=0 Then zBlowSeq(n)=0:zBlow(n)=0;:zBlock(n)=0

Case 1	;High Punch
	a=5: b=a+5: c=b+5: d=c+5: e=d+5: f=e+5: g=f+5: h=g+5
	zNoMove(n)=1: zNoJump(n)=1
	If zBlowSeq(n) => 1 And zBlowSeq(n) < a Then zani(n)=6:zf(n)=1
	If zBlowSeq(n) => a And zBlowSeq(n) < b Then zani(n)=6:zf(n)=2
	If zBlowSeq(n) => b And zBlowSeq(n) < c Then zani(n)=6:zf(n)=3
	If zBlowSeq(n) => c And zBlowSeq(n) < d Then
		zblowpamount(n)=3:nn=1
		xblow(n,nn)=0: yblow(n,nn)=39:wblow(n,nn)=26:hblow(n,nn)=1:nn=nn+1
		xblow(n,nn)=0: yblow(n,nn)=36:wblow(n,nn)=33:hblow(n,nn)=1:nn=nn+1
		xblow(n,nn)=0: yblow(n,nn)=30:wblow(n,nn)=33:hblow(n,nn)=1:nn=nn+1
		zHitMode(n)=0:zBlowHold(n)=8
		zBlowDamage(n)=15:zBLowEffect(n)=1:zBlowImpact(n)=99:zBlowStillTime(n)=20:zBlowBlockTime(n)=25
		zBlowSound(n)=bHitSnd
		zani(n)=6:zf(n)=4
		zSuperBar(n)=0
	EndIf
	If zBlowSeq(n) > d And zBlowSeq(n) =< e Then zani(n)=6:zf(n)=5
	If zBlowSeq(n) > e And zBlowSeq(n) =< f Then zani(n)=6:zf(n)=6
	If zBlowSeq(n) > f Then zBlowSeq(n)=0:zBlow(n)=0

Case 10	;Joker bomb throw (above)
	a=5: b=a+5: c=b+5: d=c+5: e=d+5: f=e+5: g=f+5: h=g+5: i=h+10: j=i+5: k=j+5: l=k+5
	zNoMove(n)=1: zNoJump(n)=1
		
	If zBlowSeq(n) => 1 And zBlowSeq(n) < a Then zani(n)=10:zf(n)=1
	If zBlowSeq(n) => a And zBlowSeq(n) < b Then zani(n)=10:zf(n)=2
	If zBlowSeq(n) => b And zBlowSeq(n) < c Then zani(n)=10:zf(n)=3
	If zBlowSeq(n) => c And zBlowSeq(n) < d Then zani(n)=10:zf(n)=2
	If zBlowSeq(n) => d And zBlowSeq(n) < e Then zani(n)=10:zf(n)=4
	If zBlowSeq(n) => e And zBlowSeq(n) < f Then zani(n)=10:zf(n)=5
	If zBlowSeq(n) => f And zBlowSeq(n) < g Then zani(n)=10:zf(n)=6
	If zBlowSeq(n) => g And zBlowSeq(n) < h Then zani(n)=10:zf(n)=7
	If zBlowSeq(n) => h And zBlowSeq(n) < i Then zani(n)=10:zf(n)=8
	
	If zblowSeq(n)=h Then
		da = getObj(n,9)
		objDamage(da)=15
		upkey(n)=0 : downKey(n)=0
		objXspeed(da)=1 : objYspeed(da)=-5
		If zface(n)=2 Then x=zx(n)+15 Else x=zx(n)-15
		throwObj(n,x,zy(n)-27,zface(n))
	EndIf
	
	If zblowseq(n) => i Then zBlowSeq(n)=0:zBlow(n)=0

Default	;Joker bomb throw
	a=5: b=a+5: c=b+5: d=c+5: e=d+5: f=e+5: g=f+5: h=g+5: i=h+5: j=i+5: k=j+5: l=k+5
	zNoMove(n)=1: zNoJump(n)=1
		
	If zBlowSeq(n) => 1 And zBlowSeq(n) < a Then zani(n)=10:zf(n)=1
	If zBlowSeq(n) => a And zBlowSeq(n) < b Then zani(n)=10:zf(n)=2
	If zBlowSeq(n) => b And zBlowSeq(n) < c Then zani(n)=10:zf(n)=3
	If zBlowSeq(n) => c And zBlowSeq(n) < d Then zani(n)=10:zf(n)=2
	If zBlowSeq(n) => d And zBlowSeq(n) < e Then zani(n)=10:zf(n)=4
	If zBlowSeq(n) => e And zBlowSeq(n) < f Then zani(n)=10:zf(n)=5
	If zBlowSeq(n) => f And zBlowSeq(n) < g Then zani(n)=10:zf(n)=6
	If zBlowSeq(n) => g And zBlowSeq(n) < h Then zani(n)=10:zf(n)=7
	If zBlowSeq(n) => h And zBlowSeq(n) < i Then zani(n)=10:zf(n)=8
	
	If zblowSeq(n)=h Then
		
		If Rand(1,5) > 3 And gameSound Then PlaySound jokerSnd
		da = getObj(n,9)
		objDamage(da)=15
		upkey(n)=0 : downKey(n)=0
		If zface(n)=2 Then x=zx(n)+15 Else x=zx(n)-15
		throwObj(n,x,zy(n)-17,zface(n))
	EndIf
	
	If zBlowSeq(n) => i Then zBlowSeq(n)=0:zBlow(n)=0

End Select

End Function
;----------------------------------- laser shot helper --------------------------------
Function DoLaserHelper(n)

zFace(n)=zBlowDir(n)
zBlowEffect(n)=0

	If zBlowStill(n)=1 Then
		zBlowStillSeq(n)=zBlowStillSeq(n)+1
		If zBlowStillSeq(n) > zBlowStillTime(n) Then zBlowStill(n)=0
		Goto noBlowSeq43
	EndIf

zBlowSeq(n)=zBlowSeq(n)+1:
.noBlowSeq43

zchunkType(n)=5
Select zCurBlow(n)

Default 	;shoots laser
	a=20: b=a+20: c=b+20
	zNoMove(n)=1
	zNoJump(n)=1
	;If zBlowSeq(n) > 1 And zBlowSeq(n) =< a Then zani(n)=1:zf(n)=0
			
	If zBlowSeq(n) = a Then 
		If gameSound Then PlaySound laserSnd
		zBlowSeq2(n)=zBlowSeq2(n)+1
		dir=zface(n):y=zy(n)-11
		If zface(n)=2 Then x=zx(n)+18 :x2=x+10
		If zface(n)=4 Then x=zx(n)-18 :x2=x-10
		makeshot(n,24,x,y,dir)
		makeChunk(n,x2,y+4,dir,28)
	EndIf
	
	If zBlowseq(n) = c Then
		aiGetTarget(n)
		If zx(aiTarget(n)) < zx(n) Then zblowDir(n)=4 Else zblowDir(n)=2
		zBlowSeq(n)=0
	EndIf
			
	If zBlowSeq(n) = b And aiLevel(n)=1 And zBlowSeq2(n) > 10 Then	;If temporary helper then die after 10 shots
		zBlowSeq(n)=0:zBlow(n)=0
		zon(n)=0
		makeChunk(n,zx(n),zy(n)-15,2,zDeathChunk(n))
		If gamesound Then PlaySound mikeKickSnd
	EndIf
	
End Select

End Function
;----------------------------------- Venom ----------------------------------------
Function DoVenom(n)

End Function


;---------------------------------- bombing ship helper --------------------------------
Function DoBombingShip(n)

zFace(n)=zBlowDir(n)
zBlowEffect(n)=0

	If zBlowStill(n)=1 Then
		zBlowStillSeq(n)=zBlowStillSeq(n)+1
		If zBlowStillSeq(n) > zBlowStillTime(n) Then zBlowStill(n)=0
		Goto noBlowSeq45
	EndIf

zBlowSeq(n)=zBlowSeq(n)+1:
.noBlowSeq45

zchunkType(n)=5
Select zCurBlow(n)

Default 	;drops bombs
	a=3: b=a+3: c=b+3
	
	If zBlowSeq(n) > 1 And zBlowSeq(n) =< a Then zani(n)=1:zf(n)=0
			
	If zBlowSeq(n) = a Then 
		zBlowSeq2(n) = zBlowSeq2(n) + 1
		If gamesound Then PlaySound shotSnd
		da = getObj(n,9)
		upkey(n)=0 : downKey(n)=0
		objDamage(da)=zvar1(n)
		objHitMode(da)=0
		objXspeed(da)=1 : objYspeed(da)=0
		throwObj(n,zx(n),zy(n),zface(n))
	EndIf
			
	If zBlowseq(n) = b Then
		zBlow(n)=0 : zBlowSeq(n)=0
	EndIf
	;If zBlowseq(n) = b And zBlowSeq2(n) < 10 Then
	;	zBlowSeq(n)=0
	;EndIf
	
	If zx(n) < xScr-500 Or zx(n) > xScr+1050 Then
		zon(n)=0
	EndIf
	
End Select

End Function
;----------------------- ray balls shooter --------------------------------
Function DoRayBalls(n)

zFace(n)=zBlowDir(n)
zBlowEffect(n)=0

	If zBlowStill(n)=1 Then
		zBlowStillSeq(n)=zBlowStillSeq(n)+1
		If zBlowStillSeq(n) > zBlowStillTime(n) Then zBlowStill(n)=0
		Goto noBlowSeq46
	EndIf

zBlowSeq(n)=zBlowSeq(n)+1:
.noBlowSeq46

zchunkType(n)=5
Select zCurBlow(n)

Default 	;shoots ray balls
	a=8: b=a+8: c=b+8: d=c+8: e=d+8
	zNoMove(n)=1
	zNoJump(n)=1
	;If zBlowSeq(n) > 1 And zBlowSeq(n) =< a Then zani(n)=1:zf(n)=0
	zani(n)=1:zf(n)=0		
	If zBlowSeq(n) = a Or zBlowSeq(n) = b Or zBlowSeq(n) = c Or zBlowSeq(n) = d Then 
		If gameSound Then PlaySound raySnd
		da = getObj(n,12)
		objDamage(da)=6
		upkey(n)=0 : downKey(n)=0
		If zBlowSeq(n)=a Then x=zx(n)-9 :y=zy(n)-7: zFace(n)=4: objXspeed(da)=3 : objYspeed(da)=-1
		If zBlowSeq(n)=b Then x=zx(n)+9 :y=zy(n)-7: zFace(n)=2: objXspeed(da)=3 : objYspeed(da)=-1
		If zBlowSeq(n)=c Then x=zx(n)-6 :y=zy(n)-20: zFace(n)=4: objXspeed(da)=1 : objYspeed(da)=-4
		If zBlowSeq(n)=d Then x=zx(n)+6 :y=zy(n)-20: zFace(n)=2: objXspeed(da)=1 : objYspeed(da)=-4	: zBlowSeq2(n)=zBlowSeq2(n)+1	
		throwObj(n,x,y,zface(n))
	EndIf
	
	If zBlowseq(n) = e And zBlowSeq2(n) < 10 Then zBlowSeq(n)=0
			
	If zBlowSeq(n) > e Then
		zBlowSeq(n)=0:zBlow(n)=0
		zon(n)=0
		makeChunk(n,zx(n),zy(n)-15,2,zDeathChunk(n))
		If gamesound Then PlaySound mikeKickSnd
	EndIf
	
End Select

End Function
;---------------------------- Soldier -----------------------------
Function DoSoldier(n)

zFace(n)=zBlowDir(n)
zBlowEffect(n)=0

	If zBlowStill(n)=1 Then
		zBlowStillSeq(n)=zBlowStillSeq(n)+1
		If zBlowStillSeq(n) > zBlowStillTime(n) Then zBlowStill(n)=0
		Goto noBlowSeq47
	EndIf

zBlowSeq(n)=zBlowSeq(n)+1:
.noBlowSeq47

zchunkType(n)=5
Select zCurBlow(n)

Default 	;shoot
	a=5: b=a+5: c=b+8: d=c+8: e=d+5: f=e+5
	zNoMove(n)=1
	zNoJump(n)=1
	If zBlowSeq(n) > 1 And zBlowSeq(n) =< a Then zani(n)=6:zf(n)=1
	If zBlowSeq(n) > a And zBlowSeq(n) =< b Then zani(n)=6:zf(n)=2
	If zBlowSeq(n) > b And zBlowSeq(n) =< c Then zani(n)=6:zf(n)=3
	If zBlowSeq(n) > c And zBlowSeq(n) =< d Then zani(n)=6:zf(n)=4
	If zBlowSeq(n) > d And zBlowSeq(n) =< e Then zani(n)=6:zf(n)=3
		
	If zBlowSeq(n) = c Then 
		dir=zface(n):y=zy(n)-zheight(n)+5
		If zface(n)=2 Then x=zx(n)+18
		If zface(n)=4 Then x=zx(n)-18
		zSuperBar(n)=0
		dam = makeshot(n,24,x,y,dir)
		If zVar2(n) > 0 Then shotDamage(dam)=zVar2(n)	;If var2 > 0 then shotdamage is custom
		If gameSound Then PlaySound laserSnd
		If zface(n)=2 Then x=zx(n)+25
		If zface(n)=4 Then x=zx(n)-25
		makechunk(n,x,zy(n)-38,dir,50)
	EndIf
			
	If zBlowSeq(n) > e Then zBlowSeq(n)=0:zBlow(n)=0:
	
End Select

End Function
;----------------- cylinder ----------------------------------
Function DoCylinder(n)

zFace(n)=zBlowDir(n)
zBlowEffect(n)=0

	If zBlowStill(n)=1 Then
		zBlowStillSeq(n)=zBlowStillSeq(n)+1
		If zBlowStillSeq(n) > zBlowStillTime(n) Then zBlowStill(n)=0
		Goto noBlowSeq48
	EndIf

zBlowSeq(n)=zBlowSeq(n)+1:
.noBlowSeq48

zchunkType(n)=5
Select zCurBlow(n)

Default 	;shoot laser
	a=3: b=a+3: c=b+3: d=c+3: e=d+3: f=e+3: g=f+20: h=g+3: i=h+3: j=i+3: k=j+3: l=k+3: m=l+3
	zNoMove(n)=1
	
	If zBlowSeq(n) > 1 And zBlowSeq(n) =< a Then zani(n)=6:zf(n)=1
	If zBlowSeq(n) > a And zBlowSeq(n) =< b Then zani(n)=6:zf(n)=2
	If zBlowSeq(n) > b And zBlowSeq(n) =< c Then zani(n)=6:zf(n)=3
	If zBlowSeq(n) > c And zBlowSeq(n) =< d Then zani(n)=6:zf(n)=4
	If zBlowSeq(n) > d And zBlowSeq(n) =< e Then zani(n)=6:zf(n)=5
	If zBlowSeq(n) > e And zBlowSeq(n) =< f Then zani(n)=6:zf(n)=6
	If zBlowSeq(n) > f And zBlowSeq(n) =< g Then zani(n)=6:zf(n)=7
	If zBlowSeq(n) > g And zBlowSeq(n) =< h Then zani(n)=6:zf(n)=6
	If zBlowSeq(n) > h And zBlowSeq(n) =< i Then zani(n)=6:zf(n)=5
	If zBlowSeq(n) > i And zBlowSeq(n) =< j Then zani(n)=6:zf(n)=4
	If zBlowSeq(n) > j And zBlowSeq(n) =< k Then zani(n)=6:zf(n)=3
	If zBlowSeq(n) > k And zBlowSeq(n) =< l Then zani(n)=6:zf(n)=2
	If zBlowSeq(n) > l And zBlowSeq(n) =< m Then zani(n)=6:zf(n)=1
		
	If zBlowSeq(n) = f+2 Then 
		dir=zface(n):y=zy(n)-8
		If zface(n)=2 Then x=zx(n)+10
		If zface(n)=4 Then x=zx(n)-10
		zSuperBar(n)=0
		Select zVar1(n)
		 Case 1: makeshot(n,21,x,y,dir): If gameSound Then PlaySound PredatorRaySnd   ;green ray
		 Case 2: makeshot(n,10,x,y,dir): If gameSound Then PlaySound shotSnd   ;simple bullet 
		 Case 3: makeshot(n,23,x,y,dir)
				If gameSound Then PlaySound bazookaSnd	;missile
				makeChunk(n,x,y,dir,16)
		 Case 4: makeshot(n,25,x,y,dir): If gameSound Then PlaySound raySnd   ;ray ball
		 Default: makeshot(n,24,x,y,dir): If gameSound Then PlaySound laserSnd  ;big laser
		
		End Select
		makeChunk(n,x,y,dir,28)
	EndIf
			
	If zBlowSeq(n) > m Then zBlowSeq(n)=0:zBlow(n)=0:
	
End Select

End Function
;----------------- Dragon ----------------------------------
Function DoDragon(n)

;zFace(n)=zBlowDir(n)
zBlowEffect(n)=0

	If zBlowStill(n)=1 Then
		zBlowStillSeq(n)=zBlowStillSeq(n)+1
		If zBlowStillSeq(n) > zBlowStillTime(n) Then zBlowStill(n)=0
		Goto noBlowSeq49
	EndIf

zBlowSeq(n)=zBlowSeq(n)+1:
.noBlowSeq49

zchunkType(n)=5
Select zCurBlow(n)

Case 1 	;fly
	zani(n)=zFlyAni(n) : zf(n)=zfa(n)
	
Case 2 	;spit fire
	a=3: b=a+3: c=b+60: d=c+3
	zNoMove(n)=1
	zani(n)=zFlyAni(n) : zf(n)=zfa(n)
	
	If zBlowSeq(n) > 1 And zBlowSeq(n) =< a Then zNoMove(n)=1
	If zBlowSeq(n) > a And zBlowSeq(n) =< b Then zNoMove(n)=1
	If zBlowSeq(n) > b And zBlowSeq(n) =< c Then zNoMove(n)=1
		
	If zBlowSeq(n) = a Then 
		If gameSound And rendert=1 Then PlaySound dragonRoarSnd
		oo = getObj(n,14)
		If zBlowSeq2(n) = 99 Then objYspeed(oo)=2
		upkey(n)=0 : downKey(n)=0
		If zface(n)=2 Then x=zx(n)+15 Else x=zx(n)-15
		throwObj(n,x,zy(n)-50,zface(n))
		makeChunk(n,x,zy(n)-50,zFace(n),28)
	EndIf
		
	If zBlowSeq(n) > c Then zBlowSeq(n)=0:zCurBlow(n)=1

Case 3 	;spit acid
	a=3: b=a+3: c=b+15: d=c+3
	zNoMove(n)=1
	zani(n)=zFlyAni(n) : zf(n)=zfa(n)
	
	If zBlowSeq(n) > 1 And zBlowSeq(n) =< a Then zNoMove(n)=1
	If zBlowSeq(n) > a And zBlowSeq(n) =< b Then zNoMove(n)=1
	If zBlowSeq(n) > b And zBlowSeq(n) =< c Then zNoMove(n)=1
		
	If zBlowSeq(n) = a Then 
		If gameSound And rendert=1 Then PlaySound dragonRoarSnd
		If zface(n)=2 Then x=zx(n)+15 Else x=zx(n)-15
		oo = getObj(n,8)
		objXspeed(oo)=.5:objYspeed(oo)=3
		throwObj(n,x,zy(n)-45,zface(n))
		
		oo = getObj(n,8)
		objXspeed(oo)=1
		throwObj(n,x,zy(n)-50,zface(n))
		
		makeChunk(n,x,zy(n)-50,zFace(n),28)
	EndIf
		
	If zBlowSeq(n) > c Then zBlowSeq(n)=0:zCurBlow(n)=1
	
End Select

End Function
;------------------------- Laser Beam ---------------------------------------
Function DoLaserBeam(n)

zFace(n)=zBlowDir(n)
zBlowEffect(n)=0

	If zBlowStill(n)=1 Then
		zBlowStillSeq(n)=zBlowStillSeq(n)+1
		If zBlowStillSeq(n) > zBlowStillTime(n) Then zBlowStill(n)=0
		Goto noBlowSeq50
	EndIf

zBlowSeq(n)=zBlowSeq(n)+1:
.noBlowSeq50

zchunkType(n)=5
Select zCurBlow(n)

Default 	;laser beam
	a=5: b=a+5: c=b+8: d=c+8: e=d+5: f=e+5
	zNoMove(n)=1
	zNoJump(n)=1
		
	;If zBlowSeq(n) => b And zBlowSeq(n) =< c Then
		checkDist(n,zx(n),zy(n)-8,zFace(n))
		For x=4 To (xDist(n)) Step 10
			If zFace(n)=2 Then
				makeChunk(n,Int((zx(n)) + x) , zy(n)-3,2,32+Rand(0,1))
			Else
				makeChunk(n,Int((zx(n))- x), zy(n)-3,2,32+Rand(0,1))
			EndIf
		Next
		hm=2 ;hit mode
		;makeRectHit(n, x, y, w, h, dir, hitMode, xHit, yHit, damage, hitHold, chunk, HitSOund)
		
		If zface(n)=2 Then
		 makeRectHit(n,zx(n)+5           ,zy(n)-16,xDist(n),15,zFace(n),hm,2,.5,zVar2(n),6,17,highpunchSnd)
		 makeChunk(n,zx(n) + xDist(n), zy(n)+8,2,21+Rand(0,1))
		 ;makeChunk(n,zx(n) + 0, zy(n)-0,2,21+Rand(0,1))
		Else
		 makeRectHit(n,zx(n)-(xDist(n)+5),zy(n)-16,xDist(n),15,zFace(n),hm,2,.5,zVar2(n),6,17,highpunchSnd)
		 makeChunk(n,zx(n) - xDist(n) , zy(n)+8,4,21+Rand(0,1))
		 ;makeChunk(n,zx(n) - 0, zy(n)-0,4,21+Rand(0,1))
		EndIf
		zani(n)=10:zf(n)=3
	;EndIf	
			
	If zBlowSeq(n) > 0 Then zBlowSeq(n)=0:zBlow(n)=0
	;zAliveSeq(n)=zAliveSeq(n)+1
	
	If zLives(n)=1 And zy(n) < zVar1(n) Then makeChunk(n,zx(n),zy(n),2,1) : killz(n):zon(n)=0
	If zLives(n)=2 And zx(n) > zVar1(n) Then makeChunk(n,zx(n),zy(n),2,1) :killz(n):zon(n)=0
	If zLives(n)=3 And zy(n) > zVar1(n) Then makeChunk(n,zx(n),zy(n),2,1) :killz(n):zon(n)=0
	If zLives(n)=4 And zx(n) < zVar1(n) Then makeChunk(n,zx(n),zy(n),2,1) :killz(n):zon(n)=0
	
End Select

End Function

;------------------------- Punching bag ---------------------------------------
Function DoBag(n)

zFace(n)=zBlowDir(n)
zBlowEffect(n)=0

	If zBlowStill(n)=1 Then
		zBlowStillSeq(n)=zBlowStillSeq(n)+1
		If zBlowStillSeq(n) > zBlowStillTime(n) Then zBlowStill(n)=0
		Goto noBlowSeq52
	EndIf

zBlowSeq(n)=zBlowSeq(n)+1:
.noBlowSeq52

zchunkType(n)=5
Select zCurBlow(n)

Default 	
	zblow(n)=0
		
End Select

End Function