#pragma once
#include "LuaMainInclude.h"
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#include "tomlc99/toml.h"

#define LUATOML "LUATOML"

// All runtime state lives inside this struct, which is the Lua userdata block.
// The output buffer persists across calls (freed only in __gc) to minimize
// allocations. The anti-recursion stack guards against circular table refs
// during encode. tomlc99 is used only for decode; the encoder is hand-written.
typedef struct LuaToml {
    int pretty;  // 0 = compact, 1 = indented (2 spaces per level)

    // Encode output buffer (grows as needed, owned by kitsune allocator)
    char*  out;
    size_t outLen;
    size_t outCap;

    // Anti-recursion stack (table pointer addresses while encoding)
    uintptr_t* rec;
    size_t     recLen;
    size_t     recCap;
} LuaToml;

LuaToml* lua_pushtoml(lua_State* L);
LuaToml* luaL_checktoml(lua_State* L, int idx);

int Toml_Dispose(lua_State* L);
int Toml_ToString(lua_State* L);
int Toml_New(lua_State* L);
int Toml_Encode(lua_State* L);
int Toml_Decode(lua_State* L);
