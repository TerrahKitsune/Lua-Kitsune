#pragma once
#include "LuaMainInclude.h"
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

LuaMsgPack* lua_pushmsgpack(lua_State* L);
LuaMsgPack* luaL_checkmsgpack(lua_State* L, int idx);

int MsgPack_Dispose(lua_State* L);
int MsgPack_ToString(lua_State* L);
int MsgPack_New(lua_State* L);
int MsgPack_Encode(lua_State* L);
int MsgPack_Decode(lua_State* L);
int MsgPack_EncodeIntoStream(lua_State* L);
int MsgPack_DecodeFromStream(lua_State* L);
