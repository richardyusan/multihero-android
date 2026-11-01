; port.decls - helpers added by the Android port (implemented in runtime/bbrt.cpp)
.lib " "

PortScreen(id%)            ; tell the touch overlay which screen is shown: 1 = main menu (UNLOCK ALL button),
                           ; 2 = online lobby (fighting controls hidden); call every frame
PortRngPeek%()             ; next value of the random generator without using it (netplay checksums)
