#pragma once
#include "lua_main_incl.h"
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

LuaToml* lua_toml_push(lua_State* L);
LuaToml* lua_toml_check(lua_State* L, int idx);

int lua_toml_gc(lua_State* L);
int lua_toml_tostring(lua_State* L);
int lua_toml_new(lua_State* L);
int lua_toml_encode(lua_State* L);
int lua_toml_decode(lua_State* L);
