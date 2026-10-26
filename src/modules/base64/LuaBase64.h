#pragma once
#include "LuaMainInclude.h"

void build_decoding_table();
int lua_base64encode(lua_State *L);
int lua_base64decode(lua_State *L);
int lua_base64getencodingtable(lua_State *L);
int lua_base64setencodingtable(lua_State *L);
