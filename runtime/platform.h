// SPDX-License-Identifier: MIT
// platform.h - interface between the portable runtime and an OS backend.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

// ---- implemented by the backend (android_main.cpp / linux_main.cpp)
void plat_present(const uint32_t *pix, int w, int h);   // show a composited frame (0x00RRGGBB)
void plat_pump();                                        // process OS events, may block while paused
int64_t plat_ms();
void plat_sleep(int ms);
bool plat_asset_read(const std::string &path, std::vector<uint8_t> &out);  // bundled read-only data
std::string plat_data_dir();                             // writable directory, ends with '/'
void plat_log(const char *msg);
[[noreturn]] void plat_exit(int code);
float plat_screen_aspect();                              // physical width / height
bool plat_touch_overlay();                               // draw on-screen controls?

// ---- implemented by the runtime, called by the backend
void rt_init();
void rt_key(int scancode, bool down);                   // DirectInput scancodes
void rt_touch(int id, float nx, float ny, int action);  // nx,ny in 0..1 of the screen; 0=down 1=move 2=up
void rt_touch_cancel_all();
void rt_vbutton(int vb, bool down);                    // VB_* from a physical pad
void rt_audio_mix(int16_t *out, int frames);             // stereo, 44100 Hz
void rt_joy_axis(int port, float x, float y);
void rt_joy_button(int port, int button, bool down);    // button 1..n

// (VB_* enum below)
// ---- implemented by the game glue (maps virtual buttons to the player's configured keys)
enum { VB_UP, VB_DOWN, VB_LEFT, VB_RIGHT, VB_SHOT, VB_SPECIAL, VB_JUMP, VB_BLOCK, VB_GRAB, VB_COUNT };
int glue_player_code(int vb);     // key scancode (or joystick button) configured for player 1
int glue_player_uses_joy();       // 1 if player 1 uses a joystick, port in glue_player_port()
int glue_player_port();
