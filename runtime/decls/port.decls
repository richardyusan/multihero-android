; port.decls - helpers added by the Android port (implemented in runtime/bbrt.cpp)
.lib " "

PortScreen(id%)            ; tell the touch overlay which screen is shown: 1 = main menu (UNLOCK ALL button),
                           ; 2 = online lobby (fighting controls hidden); call every frame
PortRngPeek%()             ; next value of the random generator without using it (netplay checksums)
PortActDown%(player%, action%) ; 1 while touch / game pad holds the action (0 up 1 down 2 left 3 right
                           ; 4 attack 5 special 6 jump 7 block 8 throw) for player 1..4
PortActHit%(player%, action%)  ; presses of that action since the last call
PortCheat%(id%)            ; adventure cheats from the main menu: 1 = infinite health, 2 = infinite jutsu
