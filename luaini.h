#pragma once
#include "lua_main_incl.h"
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#define LUAINI "LUAINI"

// All runtime state lives inside this struct, which is the Lua userdata block.
// The output buffer persists across calls (freed only in __gc). INI is pure C —
// no third-party library is needed for encode or decode.
//
// INI structure decoded to Lua:
//   { [section] = { [key] = value, ... }, ... }
// The special key "__global" holds keys that appear before any section header.
//
// INI structure encoded from Lua:
//   A flat two-level table is expected: { section = { key = value } }.
//   Pass the string "__global" as a section name for top-level keys.
typedef struct LuaIni {
    // Encode output buffer (grows as needed, owned by kitsune allocator)
    char*  out;
    size_t outLen;
    size_t outCap;
} LuaIni;

LuaIni* lua_ini_push(lua_State* L);
LuaIni* lua_ini_check(lua_State* L, int idx);

int lua_ini_gc(lua_State* L);
int lua_ini_tostring(lua_State* L);
int lua_ini_new(lua_State* L);
int lua_ini_encode(lua_State* L);
int lua_ini_decode(lua_State* L);
