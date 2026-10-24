// SPDX-License-Identifier: MIT
// mh_glue.cpp - MultiHero specific glue: tells the on-screen controls which
// keys player 1 has configured (keys.cfg is loaded into these arrays by the game).
#include "bbrt.h"
#include "platform.h"

extern BArray<int> a_zcontroller, a_controllerport;
extern BArray<int> a_upk, a_downk, a_leftk, a_rightk, a_shotk, a_specialk, a_jumpk, a_blockk, a_grabk;

int glue_player_uses_joy() { return a_zcontroller.nd ? (a_zcontroller(1) == 1) : 0; }
int glue_player_port() { return a_controllerport.nd ? a_controllerport(1) : 0; }

int glue_player_code(int vb) {
  // defaults = MultiHero's default keyboard layout for player one
  static const int defaults[VB_COUNT] = {17, 31, 30, 32, 49, 35, 50, 57, 36};
  BArray<int> *arr = nullptr;
  switch (vb) {
    case VB_UP: arr = &a_upk; break;
    case VB_DOWN: arr = &a_downk; break;
    case VB_LEFT: arr = &a_leftk; break;
    case VB_RIGHT: arr = &a_rightk; break;
    case VB_SHOT: arr = &a_shotk; break;
    case VB_SPECIAL: arr = &a_specialk; break;
    case VB_JUMP: arr = &a_jumpk; break;
    case VB_BLOCK: arr = &a_blockk; break;
    case VB_GRAB: arr = &a_grabk; break;
    default: return 0;
  }
  if (arr->nd) {
    int v = (*arr)(1);
    if (v > 0) return v;
  }
  return defaults[vb];
}
