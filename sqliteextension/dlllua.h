#pragma once
#include "../lua_main_incl.h"

int query(lua_State* L);
lua_State* OpenLuaState(lua_Alloc memoryAllocator);

