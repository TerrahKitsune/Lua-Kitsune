#pragma once
#include "Redis.h"

int redisstream_tostring(lua_State* L);
int redisstream_gc(lua_State* L);
int redisstream_call(lua_State* L);

typedef struct LuaRedisStream {
	LuaRedisKey key;
} LuaRedisStream;

static const struct luaL_Reg redisstreamfunctions[] = {
	{ NULL, NULL }
};

static const luaL_Reg redisstreammeta[] = {
	{ "__call",  redisstream_call },
	{ "__gc",  redisstream_gc },
	{ "__tostring",  redisstream_tostring },
	{ NULL, NULL }
};

static int internal_luaopen_redisstream(lua_State* L) {

	luaL_newlibtable(L, redisstreamfunctions);
	luaL_setfuncs(L, redisstreamfunctions, 0);

	luaL_newmetatable(L, REDISSTREAM);
	luaL_setfuncs(L, redisstreammeta, 0);

	lua_pushliteral(L, "__index");
	lua_pushvalue(L, -3);
	lua_rawset(L, -3);
	lua_pushliteral(L, "__metatable");
	lua_pushvalue(L, -3);
	lua_rawset(L, -3);

	return 1;
}