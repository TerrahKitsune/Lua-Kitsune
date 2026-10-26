#pragma once
#include "LuaMainInclude.h"

void BuildDecodingTable();
int Base64_Encode(lua_State *L);
int Base64_Decode(lua_State *L);
int Base64_GetEncodeTable(lua_State *L);
int Base64_SetEncodeTable(lua_State *L);
