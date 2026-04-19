#pragma once
#include "lua_main_incl.h"
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#include "msgpack/include/msgpack.h"

#define LUAMSGPACK "LUAMSGPACK"

struct LuaStream;

typedef struct LuaMsgPack {
    msgpack_sbuffer sbuf;

    // Anti-recursion stack (table pointer addresses while encoding)
    uintptr_t* rec;
    size_t     recLen;
    size_t     recCap;
} LuaMsgPack;

LuaMsgPack* lua_msgpack_push(lua_State* L);
LuaMsgPack* lua_msgpack_check(lua_State* L, int idx);

int lua_msgpack_gc(lua_State* L);
int lua_msgpack_tostring(lua_State* L);
int lua_msgpack_new(lua_State* L);
int lua_msgpack_encode(lua_State* L);
int lua_msgpack_decode(lua_State* L);
int lua_msgpack_encode_into_stream(lua_State* L);
int lua_msgpack_decode_from_stream(lua_State* L);
