#pragma once
#include "LuaMainInclude.h"
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#include "libyaml/include/yaml.h"

#define LUAYAML "LUAYAML"

struct LuaStream;

// All runtime state lives inside this struct, which is the Lua userdata block.
// The parser and emitter are initialised/destroyed per-call so they never
// hold stale state between encode/decode invocations.
// The output buffer and anti-recursion stack persist across calls and are freed
// only in __gc, keeping allocations to a minimum.
typedef struct LuaYaml {
    int pretty;  // 0 = flow style, 1 = block style

    // Encode output buffer (grows as needed, owned by kitsune allocator)
    char*  out;
    size_t outLen;
    size_t outCap;

    // Anti-recursion stack (table pointer addresses while encoding)
    uintptr_t* rec;
    size_t     recLen;
    size_t     recCap;
} LuaYaml;

LuaYaml* lua_pushyaml(lua_State* L);
LuaYaml* luaL_checkyaml(lua_State* L, int idx);

int Yaml_Dispose(lua_State* L);
int Yaml_ToString(lua_State* L);
int Yaml_New(lua_State* L);
int Yaml_Encode(lua_State* L);
int Yaml_Decode(lua_State* L);
