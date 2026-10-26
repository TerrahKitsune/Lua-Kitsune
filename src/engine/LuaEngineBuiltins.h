#pragma once
#include "LuaMainInclude.h"

// Lua C functions registered by KitsuneInit into the global environment.

int Global_GetMemory(lua_State* L);
int Global_ShellExecute(lua_State* L);
