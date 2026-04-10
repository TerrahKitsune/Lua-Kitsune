#pragma once
#include "lua_main_incl.h"

// Lua C functions registered by KitsuneInit into the global environment.

int L_GetMemory(lua_State* L);
int L_ShellExecute(lua_State* L);
