#pragma once
#include "lua_main_incl.h"

// Lua C functions registered by KitsuneInit into the global environment.

int L_kbhit(lua_State* L);
int L_getch(lua_State* L);
int L_GetTextColor(lua_State* L);
int L_SetTextColor(lua_State* L);
int L_cls(lua_State* L);
int L_put(lua_State* L);
int L_GetMemory(lua_State* L);
int L_ShellExecute(lua_State* L);
int L_GetReg(lua_State* L);
int L_ToggleConsole(lua_State* L);
int L_SetTitle(lua_State* L);
int luaopen_session(lua_State* L);
